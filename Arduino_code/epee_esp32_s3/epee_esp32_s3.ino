#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "led_controller.h"
#include "ScoreManager.h"
#include "ScoreDisplay.h"

// =====================【参数与引脚】=====================
// 完全保留你原有定义，无任何修改
const int PIN_RED_LED = 4;
const int PIN_GRN_LED = 5;
const int PIN_BUZZER = 3;
const int BTN_NEXT = 7;
const int BTN_RESET = 6;
const int LED_BOARD = 8;

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

// =====================【串口锁定打印函数】=====================
// 完全保留原有实现
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
// 核心：你可以在这个函数里实现任意分数操作后的自定义逻辑
// 比如：刷新显示屏、发送蓝牙分数、记录日志、控制外设等
void onScoreChanged(int redScore, int greenScore, bool isReset) {
  if (isReset) {
    lockedPrintf("[比分回调] 分数重置 | 红%d - 绿%d\n", redScore, greenScore);
    // 重置后的自定义操作写这里，例：lcdShowScore(redScore, greenScore);
    // 初始化比分显示
    scoreDisplay.begin();
    scoreDisplay.setScore(redScore, greenScore);
  } else {
    lockedPrintf("[比分回调] 分数更新 | 红%d - 绿%d\n", redScore, greenScore);
    // 分数更新后的自定义操作写这里，例：bleSendScore(redScore, greenScore);
    scoreDisplay.setScore(redScore, greenScore);
  }
}

// =====================【跨核同步变量】=====================
// 完全保留原有定义，无任何修改
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
// 移除原有redScore/greenScore，其余完全保留
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
// 完全保留原有声明，无任何修改
void resetMatch(bool total);
void evaluateHit();
void handleHitEffects();
void checkButtons();
void updateBLEStatusLed();
void checkBLEConnectionStatus();

// =====================【核心功能函数】=====================
void resetMatch(bool total) {
  // 替换为比分类的重置方法
  scoreManager.reset(total);
  
  
  // 其余逻辑完全保留，无任何修改
  isLocked = false;
  redHitReceived = false;
  greenHitReceived = false;
  firstHitTime = 0;
  redHitRaw = false;
  greenHitRaw = false;
  digitalWrite(PIN_RED_LED, LOW);
  digitalWrite(PIN_GRN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  
  // 通过比分类获取最新分数
  int red = scoreManager.getRedScore();
  int green = scoreManager.getGreenScore();
  lockedPrintf("[系统] %s | 比分: 红%d - 绿%d\n", total ? "全部重置" : "下一分开始", red, green);
}

void evaluateHit() {
  // 原有声光、锁定逻辑完全保留
  isLocked = true;
  hitEffectStartTime = millis();
  effectActive = true;
  digitalWrite(PIN_BUZZER, HIGH);

  // 替换为比分类的加分方法
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

// 以下函数完全保留原有实现，无任何修改
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

void checkButtons() {
  static bool lastNext = HIGH;
  bool currNext = digitalRead(BTN_NEXT);
  if (lastNext == HIGH && currNext == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (digitalRead(BTN_NEXT) == LOW) {
      lockedPrintln("[按键] 下一分按键被按下");
      resetMatch(false);
    }
  }
  lastNext = currNext;

  static bool lastReset = HIGH;
  bool currReset = digitalRead(BTN_RESET);
  if (lastReset == HIGH && currReset == LOW) {
    vTaskDelay(pdMS_TO_TICKS(50));
    if (digitalRead(BTN_RESET) == LOW) {
      lockedPrintln("[按键] 重置按键被按下");
      resetMatch(true);
    }
  }
  lastReset = currReset;
}

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
// 完全保留原有实现，无任何修改
void TaskLogic(void* pvParameters) {
  lockedPrintln("[核心1] 逻辑任务已启动");
  for (;;) {
    if (!isLocked) {
      if (redHitRaw) {
        if (firstHitTime == 0) {
          firstHitTime = redHitTimestamp;
          lockedPrintf("[逻辑] red触发判定窗口 时间戳: %u\n", firstHitTime);
        }
        if (redHitTimestamp - firstHitTime <= 40) redHitReceived = true;
        redHitRaw = false;
      }
      if (greenHitRaw) {
        if (firstHitTime == 0) {
          firstHitTime = greenHitTimestamp;
          lockedPrintf("[逻辑] green触发判定窗口 时间戳: %u\n", firstHitTime);
        }
        if (greenHitTimestamp - firstHitTime <= 40) greenHitReceived = true;
        greenHitRaw = false;
      }
      if (firstHitTime > 0 && (millis() - firstHitTime > 45)) {
        evaluateHit();
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
  // 原有LED初始化完全保留
  led_init();
  led_on_boot();
  // 初始化比分显示
  scoreDisplay.begin();

  delay(1000);
  lockedPrintln("\n==============================");
  lockedPrintln("    重剑计分系统 S3 启动中...");
  lockedPrintln("==============================");

  // 原有引脚初始化完全保留
  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_GRN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(LED_BOARD, OUTPUT);

  // 原有BLE初始化完全保留
  BLEDevice::init("epee_master_s3");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  // 初始化比分回调函数（核心新增，仅这一行）
  scoreManager.setScoreChangeCallback(onScoreChanged);

  // 原有任务创建完全保留
  xTaskCreatePinnedToCore(TaskLogic, "Logic", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskBLE, "BLE", 8192, NULL, 1, NULL, 0);

  lockedPrintln("[系统] 所有任务已绑定至对应核心");
}

void loop() {
  // 完全保留原有实现
  vTaskDelay(pdMS_TO_TICKS(1000));
}