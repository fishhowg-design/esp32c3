#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "led_controller.h"
#include "FencingCore.h" // 仅引入封装类，无其他依赖

// =====================【蓝牙相关常量（完全保留，未改动）】=====================
const int LED_BOARD = 8;
const int MAX_CONNECT_RETRY = 5;
const unsigned long CONNECTION_CHECK_INTERVAL = 2000;
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// --- 串口互斥锁（保留）---
SemaphoreHandle_t serialMutex;

// =====================【蓝牙相关变量（完全保留，未改动）】=====================
volatile bool redConnected = false;
volatile bool greenConnected = false;
BLEClient* redClient = nullptr;
BLEClient* greenClient = nullptr;
unsigned long lastConnectionCheck = 0;
static boolean doConnectRed = false;
static boolean doConnectGreen = false;
static BLEAdvertisedDevice* redDevice;
static BLEAdvertisedDevice* greenDevice;
int redRetryCount = 0;
int greenRetryCount = 0;

// =====================【前置函数声明（蓝牙相关，保留）】=====================
void updateBLEStatusLed();
void checkBLEConnectionStatus();
bool connectToDevice(BLEAdvertisedDevice* target, void (*cb)(BLERemoteCharacteristic*, uint8_t*, size_t, bool), String side);

// =====================【串口锁定打印（完全保留，未改动）】=====================
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

// =====================【蓝牙回调（仅调用FencingCore的setHit，无其他改动）】=====================
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  lockedPrintln("[信号] red原始击中信号!");
  led_hit_red();
  FencingCore::getInstance()->setRedHit(); // 仅这一行，调用封装类
}

static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  lockedPrintln("[信号] green原始击中信号!");
  led_hit_green();
  FencingCore::getInstance()->setGreenHit(); // 仅这一行，调用封装类
}

// =====================【蓝牙扫描回调（完全保留，未改动）】=====================
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

// =====================【蓝牙相关函数（完全保留，未改动）】=====================
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

// =====================【多核任务函数（仅简化TaskLogic，蓝牙Task完全不动）】=====================
void TaskLogic(void* pvParameters) {
  lockedPrintln("[核心1] 逻辑任务已启动");
  FencingCore* core = FencingCore::getInstance(); // 获取封装类实例

  for (;;) {
    // 仅调用4个封装方法，无任何业务逻辑！
    core->updateTimer();          // 更新计时器显示
    core->processHitDetection();  // 处理击中判定（核心，全部封装）
    core->handleHitEffects();     // 处理声光效果
    core->checkButtons();         // 检测比分/时间按键

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 蓝牙任务（完全保留，一行未改）
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

// =====================【Arduino 标准入口（仅初始化FencingCore）】=====================
void setup() {
  Serial.begin(115200);
  serialMutex = xSemaphoreCreateMutex();
  
  // 初始化LED和蓝牙相关引脚
  led_init();
  led_on_boot();
  pinMode(LED_BOARD, OUTPUT);

  delay(1000);
  lockedPrintln("\n==============================");
  lockedPrintln("    重剑计分系统 S3 (带计时) 启动...");
  lockedPrintln("==============================");

  // 初始化封装的比分+计时+击中判定核心（仅这一行）
  FencingCore::getInstance()->init();

  // 蓝牙初始化（完全保留，未改动）
  BLEDevice::init("epee_master_s3");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  // 创建FreeRTOS任务（完全保留，未改动）
  xTaskCreatePinnedToCore(TaskLogic, "Logic", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskBLE, "BLE", 8192, NULL, 1, NULL, 0);

  lockedPrintln("[系统] 所有任务已就绪");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}