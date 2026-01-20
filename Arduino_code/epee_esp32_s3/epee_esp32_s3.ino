#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "led_controller.h"

// =====================【参数与引脚】=====================
const int PIN_RED_LED = 4;
const int PIN_GRN_LED = 5;
const int PIN_BUZZER = 3;
const int BTN_NEXT = 7;
const int BTN_RESET = 6;
const int LED_BOARD = 8;

const unsigned long LIGHT_DURATION = 3000;
const unsigned long BEEP_DURATION = 800;
const int MAX_CONNECT_RETRY = 5;
// 新增：连接状态检测间隔（秒）
const unsigned long CONNECTION_CHECK_INTERVAL = 2000;

static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// --- 串口互斥锁 ---
SemaphoreHandle_t serialMutex;

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

// =====================【跨核同步变量】=====================
volatile bool redHitRaw = false;
volatile bool greenHitRaw = false;
volatile uint32_t redHitTimestamp = 0;
volatile uint32_t greenHitTimestamp = 0;
volatile bool redConnected = false;
volatile bool greenConnected = false;

// 新增：保存BLE客户端实例，用于状态检测
BLEClient* redClient = nullptr;
BLEClient* greenClient = nullptr;

// 新增：连接检测时间戳
unsigned long lastConnectionCheck = 0;

// =====================【逻辑变量】=====================
int redScore = 0;
int greenScore = 0;
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
// 新增：连接状态检测函数
void checkBLEConnectionStatus();

// =====================【核心功能函数】=====================

void resetMatch(bool total) {
  if (total) {
    redScore = 0;
    greenScore = 0;
  }
  isLocked = false;
  redHitReceived = false;
  greenHitReceived = false;
  firstHitTime = 0;
  redHitRaw = false;
  greenHitRaw = false;
  digitalWrite(PIN_RED_LED, LOW);
  digitalWrite(PIN_GRN_LED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
  lockedPrintf("[系统] %s | 比分: 红%d - 绿%d\n", total ? "全部重置" : "下一分开始", redScore, greenScore);
}

void evaluateHit() {
  isLocked = true;
  hitEffectStartTime = millis();
  effectActive = true;
  digitalWrite(PIN_BUZZER, HIGH);

  if (redHitReceived && greenHitReceived) {
    redScore++;
    greenScore++;
    digitalWrite(PIN_RED_LED, HIGH);
    digitalWrite(PIN_GRN_LED, HIGH);
    lockedPrintf("[裁判] 双方同时击中! (时间差: %d 毫秒)\n", abs((int)(redHitTimestamp - greenHitTimestamp)));
  } else if (redHitReceived) {
    redScore++;
    digitalWrite(PIN_RED_LED, HIGH);
    lockedPrintln("[裁判] red得分");
  } else if (greenHitReceived) {
    greenScore++;
    digitalWrite(PIN_GRN_LED, HIGH);
    lockedPrintln("[裁判] green得分");
  }
  lockedPrintf("[比分] red %d : %d green\n", redScore, greenScore);
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
    // 模拟双连
    led_connected_both();
  } else if(redConnected){
   // 模拟连上red
  led_connected_red();
  } else if(greenConnected){
     // 模拟连上green
  led_connected_green();
  }else{
 // 开机亮白灯
  led_on_boot();
  }
}

// 新增：连接状态检测函数
void checkBLEConnectionStatus() {
  // 定期检测（每2秒）
  if (millis() - lastConnectionCheck < CONNECTION_CHECK_INTERVAL) return;
  lastConnectionCheck = millis();

  // 检查red设备连接状态
  if (redConnected && redClient != nullptr) {
    if (!redClient->isConnected()) {
      lockedPrintln("[蓝牙] red设备已掉线!");
      redConnected = false;
      redClient->disconnect();
      delete redClient;
      redClient = nullptr;
      //redRetryCount = 0; // 重置重试计数，允许重新连接
    }
  }

  // 检查green设备连接状态
  if (greenConnected && greenClient != nullptr) {
    if (!greenClient->isConnected()) {
      lockedPrintln("[蓝牙] green设备已掉线!");
      greenConnected = false;
      greenClient->disconnect();
      delete greenClient;
      greenClient = nullptr;
      //greenRetryCount = 0; // 重置重试计数，允许重新连接
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

  // 保存客户端实例，用于后续状态检测
  if (side == "red") {
    redClient = pClient;
  } else if (side == "green") {
    greenClient = pClient;
  }

  return true;
}

// =====================【任务回调与类】=====================
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  lockedPrintln("[信号] red原始击中信号!");
  if (!isLocked) {
    redHitRaw = true;
    redHitTimestamp = millis();
    lockedPrintf("[信号] red击中信号触发 时间戳: %u\n", redHitTimestamp);
  }
}

static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  lockedPrintln("[信号] green原始击中信号!");
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
    // 新增：检测蓝牙连接状态
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

    // 只有在没连全且没在尝试连接时才扫描
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
  // 初始化LED
  led_init();
  // 开机亮白灯
  led_on_boot();

  delay(1000);
  lockedPrintln("\n==============================");
  lockedPrintln("    重剑计分系统 S3 启动中...");
  lockedPrintln("==============================");

  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_GRN_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(LED_BOARD, OUTPUT);

  BLEDevice::init("epee_master_s3");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  xTaskCreatePinnedToCore(TaskLogic, "Logic", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskBLE, "BLE", 8192, NULL, 1, NULL, 0);

  lockedPrintln("[系统] 所有任务已绑定至对应核心");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}