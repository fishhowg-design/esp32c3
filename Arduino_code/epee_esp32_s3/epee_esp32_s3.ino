#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "led_controller.h"
#include "ScoreManager.h"
#include "ScoreDisplay.h"
#include "FencingTimer.h" // [TIMER_MOD] 引入计时器头文件

// =====================【参数与引脚】=====================
const int PIN_RED_LED = 4;
const int PIN_GRN_LED = 5;
const int PIN_BUZZER = 3;
const int BTN_NEXT = 7;    // [TIMER_MOD] 复用：下一分/起动暂停
const int BTN_RESET = 6;   // [TIMER_MOD] 复用：比分重置/时间重置
const int BTN_PHASE = 15;  // [TIMER_MOD] 新增：阶段切换(休息/比赛)
const int BTN_MODE = 16;   // [TIMER_MOD] 新增：3m/5m切换
const int LED_BOARD = 8;

// 新增手动加减分引脚定义
const int BTN_RED_ADD = 14;    // 红方+1分
const int BTN_RED_SUB = 9;     // 红方-1分
const int BTN_GREEN_ADD = 17;  // 绿方+1分
const int BTN_GREEN_SUB = 18;  // 绿方-1分

// [TIMER_MOD] 计时器显示引脚 (请确保这两个引脚未被占用)
#define TIMER_CLK_PIN 1 
#define TIMER_DIO_PIN 2

const unsigned long LIGHT_DURATION = 3000;
const unsigned long BEEP_DURATION = 800;
const int MAX_CONNECT_RETRY = 5;
const unsigned long CONNECTION_CHECK_INTERVAL = 2000;

static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// --- 串口互斥锁 ---
SemaphoreHandle_t serialMutex;
// --- 全局比分管理实例（替换原有redScore/greenScore全局变量）---
ScoreManager scoreManager;
//控制TM1637 
ScoreDisplay scoreDisplay;
FencingTimer fencingTimer; // [TIMER_MOD] 实例化计时器

// =====================【串口锁定打印函数】=====================
void lockedPrintf(const char* format, ...) {
  if (serialMutex == NULL) return;
  if (xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.print(buffer);
    xSemaphoreGive(serialMutex);
  }
}

void lockedPrintln(String msg) {
  if (serialMutex == NULL) return;
  if (xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) {
    Serial.println(msg);
    xSemaphoreGive(serialMutex);
  }
}

// =====================【分数变化回调函数】=====================
void onScoreChanged(int redScore, int greenScore, bool isReset) {
  if (isReset) {
    lockedPrintf("[比分回调] 分数重置 | 红%d - 绿%d\n", redScore, greenScore);
    scoreDisplay.begin();
    scoreDisplay.setScore(redScore, greenScore);
    fencingTimer.resetTimer(); // [TIMER_MOD] 分数重置时同步重置时间
  } else {
    lockedPrintf("[比分回调] 分数更新 | 红%d - 绿%d\n", redScore, greenScore);
    scoreDisplay.setScore(redScore, greenScore);
  }
}

// =====================【跨核同步变量】=====================
volatile bool redHitRaw = false;
volatile bool greenHitRaw = false;
volatile uint32_t redHitTimestamp = 0;
volatile uint32_t greenHitTimestamp = 0;
volatile bool redConnected = false;
volatile bool greenConnected = false;

BLEClient* redClient = nullptr;
BLEClient* greenClient = nullptr;
unsigned long lastConnectionCheck = 0;

// =====================【逻辑变量】=====================
unsigned long firstHitTime = 0;
bool isLocked = false;
bool redHitReceived = false;
bool greenHitReceived = false;
bool effectActive = false;
unsigned long hitEffectStartTime = 0;

static boolean doConnectRed = false;
static boolean doConnectGreen = false;
static BLEAdvertisedDevice* redDevice;
static BLEAdvertisedDevice* greenDevice;
int redRetryCount = 0;
int greenRetryCount = 0;

// =====================【前置函数声明区】=====================
void resetMatch(bool total);
void evaluateHit();
void handleHitEffects();
void checkButtons();
void updateBLEStatusLed();
void checkBLEConnectionStatus();

// =====================【核心功能函数】=====================
void resetMatch(bool total) {
  scoreManager.reset(total);
  
  isLocked = false;
  redHitReceived = false;
  greenHitReceived = false;
  firstHitTime = 0;
  redHitRaw = false;
  greenHitRaw = false;
  digitalWrite(PIN_RED_LED, LOW);
  digitalWrite(PIN_GRN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  
  int red = scoreManager.getRedScore();
  int green = scoreManager.getGreenScore();
  lockedPrintf("[系统] %s | 比分: 红%d - 绿%d\n", total ? "全部重置" : "下一分开始", red, green);
}

void evaluateHit() {
  isLocked = true;
  hitEffectStartTime = millis();
  effectActive = true;
  digitalWrite(PIN_BUZZER, HIGH);

  // [TIMER_MOD] 击中判定瞬间自动暂停计时，方便裁判查看
  if (fencingTimer.isTimerRunning()) {
    fencingTimer.toggleStartPause();
  }

  if (redHitReceived && greenHitReceived) {
    scoreManager.addBothScores();
    digitalWrite(PIN_RED_LED, HIGH);
    digitalWrite(PIN_GRN_LED, HIGH);
    lockedPrintf("[裁判] 双方同时击中! (时间差: %d 毫秒)\n", abs((int)(redHitTimestamp - greenHitTimestamp)));
  } else if (redHitReceived) {
    scoreManager.addRedScore();
    digitalWrite(PIN_RED_LED, HIGH);
    lockedPrintln("[裁判] red得分");
  } else if (greenHitReceived) {
    scoreManager.addGreenScore();
    digitalWrite(PIN_GRN_LED, HIGH);
    lockedPrintln("[裁判] green得分");
  }
  
  // 通过比分类获取最新分数
  int red = scoreManager.getRedScore();
  int green = scoreManager.getGreenScore();
  lockedPrintf("[比分] red %d : %d green\n", red, green);
}

void handleHitEffects() {
  if (!effectActive) return;
  unsigned long elapsed = millis() - hitEffectStartTime;
  if (elapsed > BEEP_DURATION) digitalWrite(PIN_BUZZER, LOW);
  if (elapsed > LIGHT_DURATION) {
    digitalWrite(PIN_RED_LED, LOW);
    digitalWrite(PIN_GRN_LED, LOW);
    effectActive = false;
    lockedPrintln("[系统] 声光效果结束，等待重置");
  }
}

// =====================【按键逻辑融合 - TIMER_MOD】=====================
void checkButtons() {
  // --- BTN_NEXT (7): 灭灯 或 启动/暂停 ---
  static bool lastNext = HIGH;
  bool currNext = digitalRead(BTN_NEXT);
  if (lastNext == HIGH && currNext == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (digitalRead(BTN_NEXT) == LOW) {
      if (isLocked) {
        lockedPrintln("[按键] 下一分准备 (灭灯)");
        resetMatch(false);
        // ========== 新增：恢复计时器运行 ==========
        if (!fencingTimer.isTimerRunning()) {
          fencingTimer.toggleStartPause();
          lockedPrintln("[计时] 恢复比赛计时");
        }
      // =========================================
      } else {
        fencingTimer.toggleStartPause();
        lockedPrintf("[计时] %s\n", fencingTimer.isTimerRunning() ? "开始" : "暂停");
      }
    }
  }
  lastNext = currNext;

  // --- BTN_RESET (6): 彻底重置 ---
  static bool lastReset = HIGH;
  bool currReset = digitalRead(BTN_RESET);
  if (lastReset == HIGH && currReset == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (digitalRead(BTN_RESET) == LOW) {
      lockedPrintln("[按键] 全局重置 (分数+时间)");
      resetMatch(true);
      fencingTimer.resetTimer();
    }
  }
  lastReset = currReset;

  // --- BTN_PHASE (15): 进入/退出休息 ---
  static bool lastPhase = HIGH;
  bool currPhase = digitalRead(BTN_PHASE);
  if (lastPhase == HIGH && currPhase == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (digitalRead(BTN_PHASE) == LOW) {
      fencingTimer.nextPhase();
      lockedPrintln(fencingTimer.isResting() ? "[计时] 进入休息模式" : "[计时] 重回比赛模式");
    }
  }
  lastPhase = currPhase;

  // --- BTN_MODE (16): 切换 3min/5min ---
  static bool lastMode = HIGH;
  bool currMode = digitalRead(BTN_MODE);
  if (lastMode == HIGH && currMode == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (digitalRead(BTN_MODE) == LOW) {
      fencingTimer.toggleDurationMode();
      lockedPrintf("[计时] 切换至 %d 分钟赛制\n", fencingTimer.getCurrentDurationMode());
    }
  }
  lastMode = currMode;

  // ===================== 新增手动加减分按键逻辑 =====================
  // --- BTN_RED_ADD (14): 红方+1分 ---
  static bool lastRedAdd = HIGH;
  bool currRedAdd = digitalRead(BTN_RED_ADD);
  if (lastRedAdd == HIGH && currRedAdd == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50)); // 消抖
    if (digitalRead(BTN_RED_ADD) == LOW) {
      lockedPrintln("[按键] 手动红方+1分");
      scoreManager.addRedScore(); // 调用ScoreManager加分函数
    }
  }
  lastRedAdd = currRedAdd;

  // --- BTN_RED_SUB (9): 红方-1分 ---
  static bool lastRedSub = HIGH;
  bool currRedSub = digitalRead(BTN_RED_SUB);
  if (lastRedSub == HIGH && currRedSub == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50)); // 消抖
    if (digitalRead(BTN_RED_SUB) == LOW) {
      lockedPrintln("[按键] 手动红方-1分");
      scoreManager.subtractRedScore(); // 调用ScoreManager减分函数
    }
  }
  lastRedSub = currRedSub;

  // --- BTN_GREEN_ADD (17): 绿方+1分 ---
  static bool lastGreenAdd = HIGH;
  bool currGreenAdd = digitalRead(BTN_GREEN_ADD);
  if (lastGreenAdd == HIGH && currGreenAdd == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50)); // 消抖
    if (digitalRead(BTN_GREEN_ADD) == LOW) {
      lockedPrintln("[按键] 手动绿方+1分");
      scoreManager.addGreenScore(); // 调用ScoreManager加分函数
    }
  }
  lastGreenAdd = currGreenAdd;

  // --- BTN_GREEN_SUB (18): 绿方-1分 ---
  static bool lastGreenSub = HIGH;
  bool currGreenSub = digitalRead(BTN_GREEN_SUB);
  if (lastGreenSub == HIGH && currGreenSub == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50)); // 消抖
    if (digitalRead(BTN_GREEN_SUB) == LOW) {
      lockedPrintln("[按键] 手动绿方-1分");
      scoreManager.subtractGreenScore(); // 调用ScoreManager减分函数
    }
  }
  lastGreenSub = currGreenSub;
}

// =====================【蓝牙部分 (无修改)】=====================
void updateBLEStatusLed() {
  if (redConnected && greenConnected) {
    led_connected_both();
  } else if(redConnected){
    led_connected_red();
  } else if(greenConnected){
    led_connected_green();
  }else{
    led_on_boot();
  }
}

void checkBLEConnectionStatus() {
  if (millis() - lastConnectionCheck < CONNECTION_CHECK_INTERVAL) return;
  lastConnectionCheck = millis();

  if (redConnected && redClient != nullptr) {
    if (!redClient->isConnected()) {
      lockedPrintln("[蓝牙] red设备已掉线!");
      redConnected = false;
      redClient->disconnect();
      delete redClient;
      redClient = nullptr;
    }
  }

  if (greenConnected && greenClient != nullptr) {
    if (!greenClient->isConnected()) {
      lockedPrintln("[蓝牙] green设备已掉线!");
      greenConnected = false;
      greenClient->disconnect();
      delete greenClient;
      greenClient = nullptr;
    }
  }
}

bool connectToDevice(BLEAdvertisedDevice* target, void (*cb)(BLERemoteCharacteristic*, uint8_t*, size_t, bool), String side) {
  if (target == nullptr) return false;
  lockedPrintf("[蓝牙] 开始连接%s设备...\n", side.c_str());

  BLEClient* pClient = BLEDevice::createClient();
  if (!pClient->connect(target)) {
    lockedPrintf("[蓝牙] %s设备连接失败\n", side.c_str());
    delete pClient;
    return false;
  }

  BLERemoteService* pSvc = pClient->getService(serviceUUID);
  if (pSvc == nullptr) {
    lockedPrintf("[蓝牙] %s设备未找到指定服务\n", side.c_str());
    pClient->disconnect();
    delete pClient;
    return false;
  }

  BLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID);
  if (pChar == nullptr) {
    lockedPrintf("[蓝牙] %s设备未找到指定特征值\n", side.c_str());
    pClient->disconnect();
    delete pClient;
    return false;
  }

  if (pChar->canNotify()) {
    pChar->registerForNotify(cb);
    lockedPrintf("[蓝牙] %s设备通知已注册成功\n", side.c_str());
  }

  if (side == "red") {
    redClient = pClient;
  } else if (side == "green") {
    greenClient = pClient;
  }

  return true;
}

// =====================【任务回调与类】=====================
// 完全保留原有实现，无任何修改
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  lockedPrintln("[信号] red原始击中信号!");
  led_hit_red();
  if (!isLocked) {
    redHitRaw = true;
    redHitTimestamp = millis();
    lockedPrintf("[信号] red击中信号触发 时间戳: %u\n", redHitTimestamp);
  }
}

static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  lockedPrintln("[信号] green原始击中信号!");
  led_hit_green(); 
  if (!isLocked) {
    greenHitRaw = true;
    greenHitTimestamp = millis();
    lockedPrintf("[信号] green击中信号触发 时间戳: %u\n", greenHitTimestamp);
  }
}

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String name = advertisedDevice.getName().c_str();
    if (name == "epee_red" && !redConnected && !doConnectRed) {
      lockedPrintln("[扫描] 发现red重剑设备!");
      redDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnectRed = true;
    } else if (name == "epee_green" && !greenConnected && !doConnectGreen) {
      lockedPrintln("[扫描] 发现green重剑设备!");
      greenDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnectGreen = true;
    }
  }
};

// =====================【多核任务函数】=====================
void TaskLogic(void* pvParameters) {
  lockedPrintln("[核心1] 逻辑任务已启动");
  
  // [TIMER_MOD] 显式初始化计时器显示屏
  fencingTimer.begin(); 

  for (;;) {
    // [TIMER_MOD] 驱动计时器跳动
    fencingTimer.update();

    if (!isLocked) {
      // 只有在计时器运行时才处理击中判定（训练模式如需常开可修改此条）
      if (fencingTimer.isTimerRunning()) {
          if (redHitRaw) {
            if (firstHitTime == 0) firstHitTime = redHitTimestamp;
            if (redHitTimestamp - firstHitTime <= 40) redHitReceived = true;
            redHitRaw = false;
          }
          if (greenHitRaw) {
            if (firstHitTime == 0) firstHitTime = greenHitTimestamp;
            if (greenHitTimestamp - firstHitTime <= 40) greenHitReceived = true;
            greenHitRaw = false;
          }
          if (firstHitTime > 0 && (millis() - firstHitTime > 45)) {
            evaluateHit();
          }
      } else {
          // 如果计时器没开却收到信号，直接清掉
          redHitRaw = false;
          greenHitRaw = false;
      }
    }
    handleHitEffects();
    checkButtons();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void TaskBLE(void* pvParameters) {
  lockedPrintln("[核心0] 蓝牙任务已启动");
  for (;;) {
    checkBLEConnectionStatus();
    updateBLEStatusLed();
    
    if (doConnectRed && !redConnected && redRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(redDevice, redNotifyCallback, "red")) {
        redConnected = true;
        redRetryCount = 0;
      } else {
        redRetryCount++;
      }
      doConnectRed = false;
    }
    if (doConnectGreen && !greenConnected && greenRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(greenDevice, greenNotifyCallback, "green")) {
        greenConnected = true;
        greenRetryCount = 0;
      } else {
        greenRetryCount++;
      }
      doConnectGreen = false;
    }

    if (((!redConnected && redRetryCount < MAX_CONNECT_RETRY) || (!greenConnected && greenRetryCount < MAX_CONNECT_RETRY)) && (!doConnectRed && !doConnectGreen)) {
      BLEDevice::getScan()->start(1, false);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// =====================【Arduino 标准入口】=====================
void setup() {
  Serial.begin(115200);
  serialMutex = xSemaphoreCreateMutex();
  
  led_init();
  led_on_boot();
  // 初始化比分显示
  scoreDisplay.begin();

  delay(1000);
  lockedPrintln("\n==============================");
  lockedPrintln("    重剑计分系统 S3 (带计时) 启动...");
  lockedPrintln("==============================");

  // 原有引脚初始化完全保留
  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_GRN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(BTN_PHASE, INPUT_PULLUP); // [TIMER_MOD]
  pinMode(BTN_MODE, INPUT_PULLUP);  // [TIMER_MOD]
  pinMode(LED_BOARD, OUTPUT);

  // 初始化新增的手动加减分引脚（INPUT_PULLUP模式）
  pinMode(BTN_RED_ADD, INPUT_PULLUP);
  pinMode(BTN_RED_SUB, INPUT_PULLUP);
  pinMode(BTN_GREEN_ADD, INPUT_PULLUP);
  pinMode(BTN_GREEN_SUB, INPUT_PULLUP);

  BLEDevice::init("epee_master_s3");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  // 初始化比分回调函数（核心新增，仅这一行）
  scoreManager.setScoreChangeCallback(onScoreChanged);

  xTaskCreatePinnedToCore(TaskLogic, "Logic", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskBLE, "BLE", 8192, NULL, 1, NULL, 0);

  lockedPrintln("[系统] 计分与计时任务已就绪");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}