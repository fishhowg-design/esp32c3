#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "led_controller.h"
#include "FencingCore.h" // 仅引入封装类，无其他依赖
#include "log_utils.h"

// =====================【蓝牙相关常量（新增遥控设备配置）】=====================
const int LED_BOARD = 8;
const int MAX_CONNECT_RETRY = 5;
const unsigned long CONNECTION_CHECK_INTERVAL = 2000;
// 原有重剑设备UUID
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");
// 新增遥控设备配置
const char* REMOTE_DEVICE_NAME = "epee_control"; // 遥控设备蓝牙名称
static BLEUUID remoteServiceUUID("5fafc202-1fb5-459e-8fcc-c5c9c331914c"); // 遥控服务UUID
static BLEUUID remoteCharUUID("ceb5483f-36e1-4688-b7f5-ea07361b26a9");     // 遥控特征值UUID

// --- 串口互斥锁（保留）---
SemaphoreHandle_t serialMutex;

// =====================【蓝牙相关变量（新增遥控设备变量）】=====================
volatile bool redConnected = false;
volatile bool greenConnected = false;
volatile bool remoteConnected = false; // 新增：遥控设备连接状态
BLEClient* redClient = nullptr;
BLEClient* greenClient = nullptr;
BLEClient* remoteClient = nullptr;     // 新增：遥控设备客户端
unsigned long lastConnectionCheck = 0;
static boolean doConnectRed = false;
static boolean doConnectGreen = false;
static boolean doConnectRemote = false;// 新增：遥控设备连接标记
static BLEAdvertisedDevice* redDevice;
static BLEAdvertisedDevice* greenDevice;
static BLEAdvertisedDevice* remoteDevice; // 新增：遥控设备实例
int redRetryCount = 0;
int greenRetryCount = 0;
int remoteRetryCount = 0; // 新增：遥控设备重试次数

// =====================【前置函数声明（蓝牙相关，保留）】=====================
void updateBLEStatusLed();
void checkBLEConnectionStatus();
bool connectToDevice(BLEAdvertisedDevice* target, void (*cb)(BLERemoteCharacteristic*, uint8_t*, size_t, bool), String side, BLEUUID svcUUID, BLEUUID chrUUID); // 新增UUID参数


// =====================【蓝牙回调（原有+新增遥控回调）】=====================
// 原有红方击中回调（未改动）
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  LogUtils::println("[信号] red原始击中信号!");
  led_hit_red();
  FencingCore::getInstance()->setRedHit();
}

// 原有绿方击中回调（未改动）
static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  LogUtils::println("[信号] green原始击中信号!");
  led_hit_green();
  FencingCore::getInstance()->setGreenHit();
}

// 新增：遥控设备数据回调（解析8个按钮指令）
static void remoteNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
  if (length < 1) {
    LogUtils::println("[遥控] 无效数据（长度为0）");
    return;
  }

  FencingCore* core = FencingCore::getInstance();
  if (!core) {
    LogUtils::println("[遥控] FencingCore实例获取失败");
    return;
  }

  // 遥控设备发送1字节指令码，映射8个按钮（可根据实际需求调整指令码）
  uint8_t cmd = pData[0];
  switch (cmd) {
    case 0x01: // 对应BTN_NEXT（下一分/计时启停）
      LogUtils::println("[遥控] 触发[NEXT]按钮");
      if (core->isLocked()) {
        core->resetMatch(false);
        if (!core->isTimerRunning()) core->toggleTimerStartPause();
      } else {
        core->toggleTimerStartPause();
      }
      break;
    case 0x02: // 对应BTN_RESET（全局重置）
      LogUtils::println("[遥控] 触发[RESET]按钮");
      core->resetMatch(true);
      core->resetTimer();
      break;
    case 0x03: // 对应BTN_PHASE（休息/比赛模式）
      LogUtils::println("[遥控] 触发[PHASE]按钮");
      core->nextPhase();
      break;
    case 0x04: // 对应BTN_MODE（计时时长切换）
      LogUtils::println("[遥控] 触发[MODE]按钮");
      core->toggleDurationMode();
      break;
    case 0x05: // 对应BTN_RED_ADD（红方+1）
      LogUtils::println("[遥控] 触发[红方+1]按钮");
      core->addRedScore();
      break;
    case 0x06: // 对应BTN_RED_SUB（红方-1）
      LogUtils::println("[遥控] 触发[红方-1]按钮");
      core->subtractRedScore();
      break;
    case 0x07: // 对应BTN_GREEN_ADD（绿方+1）
      LogUtils::println("[遥控] 触发[绿方+1]按钮");
      core->addGreenScore();
      break;
    case 0x08: // 对应BTN_GREEN_SUB（绿方-1）
      LogUtils::println("[遥控] 触发[绿方-1]按钮");
      core->subtractGreenScore();
      break;
    default:
      LogUtils::printf("[遥控] 未知指令码: 0x%02X\n", cmd);
      break;
  }
}

// =====================【蓝牙扫描回调（新增识别遥控设备）】=====================
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    String name = advertisedDevice.getName().c_str();
    // 原有重剑设备扫描（未改动）
    if (name == "epee_red" && !redConnected && !doConnectRed) {
      LogUtils::println("[扫描] 发现red重剑设备!");
      redDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnectRed = true;
    } else if (name == "epee_green" && !greenConnected && !doConnectGreen) {
      LogUtils::println("[扫描] 发现green重剑设备!");
      greenDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnectGreen = true;
    }
    // 新增：扫描遥控设备
    else if (name == REMOTE_DEVICE_NAME && !remoteConnected && !doConnectRemote) {
      LogUtils::println("[扫描] 发现遥控设备epee_control!");
      remoteDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnectRemote = true;
    }
  }
};

// =====================【蓝牙相关函数（扩展支持遥控设备）】=====================
void updateBLEStatusLed() {
  if (redConnected && greenConnected && remoteConnected) {
    led_connected_all(); // 需在led_controller中实现「全部连接」LED逻辑（可选）
  } else if (redConnected && greenConnected) {
    led_connected_both();
  } else if(redConnected){
    led_connected_red();
  } else if(greenConnected){
    led_connected_green();
  } else if(remoteConnected) {
    led_connected_remote(); // 需在led_controller中实现「仅遥控连接」LED逻辑（可选）
  }else{
    led_on_boot();
  }
}

void checkBLEConnectionStatus() {
  if (millis() - lastConnectionCheck < CONNECTION_CHECK_INTERVAL) return;
  lastConnectionCheck = millis();

  // 原有红/绿设备状态检查（未改动）
  if (redConnected && redClient != nullptr) {
    if (!redClient->isConnected()) {
      LogUtils::println("[蓝牙] red设备已掉线!");
      redConnected = false;
      redClient->disconnect();
      delete redClient;
      redClient = nullptr;
    }
  }
  if (greenConnected && greenClient != nullptr) {
    if (!greenClient->isConnected()) {
      LogUtils::println("[蓝牙] green设备已掉线!");
      greenConnected = false;
      greenClient->disconnect();
      delete greenClient;
      greenClient = nullptr;
    }
  }
  // 新增：遥控设备状态检查
  if (remoteConnected && remoteClient != nullptr) {
    if (!remoteClient->isConnected()) {
      LogUtils::println("[蓝牙] 遥控设备已掉线!");
      remoteConnected = false;
      remoteClient->disconnect();
      delete remoteClient;
      remoteClient = nullptr;
    }
  }
}

// 扩展：支持自定义服务/特征UUID（适配遥控设备）
bool connectToDevice(BLEAdvertisedDevice* target, void (*cb)(BLERemoteCharacteristic*, uint8_t*, size_t, bool), String side, BLEUUID svcUUID, BLEUUID chrUUID) {
  if (target == nullptr) return false;
  LogUtils::printf("[蓝牙] 开始连接%s设备...\n", side.c_str());

  BLEClient* pClient = BLEDevice::createClient();
  if (!pClient->connect(target)) {
    LogUtils::printf("[蓝牙] %s设备连接失败\n", side.c_str());
    delete pClient;
    return false;
  }

  BLERemoteService* pSvc = pClient->getService(svcUUID);
  if (pSvc == nullptr) {
    LogUtils::printf("[蓝牙] %s设备未找到指定服务\n", side.c_str());
    pClient->disconnect();
    delete pClient;
    return false;
  }

  BLERemoteCharacteristic* pChar = pSvc->getCharacteristic(chrUUID);
  if (pChar == nullptr) {
    LogUtils::printf("[蓝牙] %s设备未找到指定特征值\n", side.c_str());
    pClient->disconnect();
    delete pClient;
    return false;
  }

  if (pChar->canNotify()) {
    pChar->registerForNotify(cb);
    LogUtils::printf("[蓝牙] %s设备通知已注册成功\n", side.c_str());
  }

  // 原有红/绿客户端赋值 + 新增遥控客户端赋值
  if (side == "red") {
    redClient = pClient;
  } else if (side == "green") {
    greenClient = pClient;
  } else if (side == "remote") {
    remoteClient = pClient;
  }

  return true;
}

// =====================【多核任务函数（原有逻辑未改动）】=====================
void TaskLogic(void* pvParameters) {
  LogUtils::println("[核心1] 逻辑任务已启动");
  FencingCore* core = FencingCore::getInstance();

  for (;;) {
    core->updateTimer();          
    core->processHitDetection();  
    core->handleHitEffects();     
    core->checkButtons();         

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 蓝牙任务（新增遥控设备连接逻辑）
void TaskBLE(void* pvParameters) {
  LogUtils::println("[核心0] 蓝牙任务已启动");
  for (;;) {
    checkBLEConnectionStatus();
    updateBLEStatusLed();
    
    // 原有红设备连接（未改动，仅适配新的connectToDevice参数）
    if (doConnectRed && !redConnected && redRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(redDevice, redNotifyCallback, "red", serviceUUID, charUUID)) {
        redConnected = true;
        redRetryCount = 0;
      } else {
        redRetryCount++;
      }
      doConnectRed = false;
    }
    // 原有绿设备连接（未改动，仅适配新的connectToDevice参数）
    if (doConnectGreen && !greenConnected && greenRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(greenDevice, greenNotifyCallback, "green", serviceUUID, charUUID)) {
        greenConnected = true;
        greenRetryCount = 0;
      } else {
        greenRetryCount++;
      }
      doConnectGreen = false;
    }
    // 新增：遥控设备连接逻辑
    if (doConnectRemote && !remoteConnected && remoteRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(remoteDevice, remoteNotifyCallback, "remote", remoteServiceUUID, remoteCharUUID)) {
        remoteConnected = true;
        remoteRetryCount = 0;
      } else {
        remoteRetryCount++;
      }
      doConnectRemote = false;
    }

    // 扩展扫描条件：包含遥控设备重试
    if (((!redConnected && redRetryCount < MAX_CONNECT_RETRY) || 
         (!greenConnected && greenRetryCount < MAX_CONNECT_RETRY) || 
         (!remoteConnected && remoteRetryCount < MAX_CONNECT_RETRY)) && 
        (!doConnectRed && !doConnectGreen && !doConnectRemote)) {
      BLEDevice::getScan()->start(1, false);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// =====================【Arduino 标准入口（未改动）】=====================
void setup() {
 
  // 第一步：初始化日志工具（仅需调用一次，全局生效）
  if (!LogUtils::init(115200)) {
    while(1); // 初始化失败，卡死避免后续错误
  }
  

  LogUtils::println("\n==============================");
  LogUtils::println("    重剑计分系统 S3 (带计时+遥控) 启动...");
  LogUtils::println("==============================");

  serialMutex = xSemaphoreCreateMutex();
  
  FencingCore::getInstance()->init();
  led_init();
  led_on_boot();
  pinMode(LED_BOARD, OUTPUT);

  


  BLEDevice::init("epee_master_s3");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  xTaskCreatePinnedToCore(TaskLogic, "Logic", 8192, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskBLE, "BLE", 8192, NULL, 1, NULL, 0);

  LogUtils::println("[系统] 所有任务已就绪");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}