#include "BluetoothManager.h"

// 本文件将原 .ino 中的蓝牙逻辑封装为单例类

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    BluetoothManager* mgr = BluetoothManager::getInstance();
    if (!mgr) return;
    String name = advertisedDevice.getName().c_str();
    if (name == "epee_red" && !mgr->redConnected && !mgr->doConnectRed) {
      LogUtils::println("[扫描] 发现red重剑设备!");
      mgr->redDevice = new BLEAdvertisedDevice(advertisedDevice);
      mgr->doConnectRed = true;
    } else if (name == "epee_green" && !mgr->greenConnected && !mgr->doConnectGreen) {
      LogUtils::println("[扫描] 发现green重剑设备!");
      mgr->greenDevice = new BLEAdvertisedDevice(advertisedDevice);
      mgr->doConnectGreen = true;
    } else if (name == "epee_control" && !mgr->remoteConnected && !mgr->doConnectRemote) {
      LogUtils::println("[扫描] 发现遥控设备epee_control!");
      mgr->remoteDevice = new BLEAdvertisedDevice(advertisedDevice);
      mgr->doConnectRemote = true;
    }
  }
};

static BluetoothManager* instance = nullptr;

BluetoothManager* BluetoothManager::getInstance() {
  if (!instance) instance = new BluetoothManager();
  return instance;
}

BluetoothManager::BluetoothManager()
  : serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b"),
    charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8"),
    remoteServiceUUID("5fafc202-1fb5-459e-8fcc-c5c9c331914c"),
    remoteCharUUID("ceb5483f-36e1-4688-b7f5-ea07361b26a9"),
    redConnected(false), greenConnected(false), remoteConnected(false),
    redClient(nullptr), greenClient(nullptr), remoteClient(nullptr),
    lastConnectionCheck(0), doConnectRed(false), doConnectGreen(false), doConnectRemote(false),
    redDevice(nullptr), greenDevice(nullptr), remoteDevice(nullptr),
    redRetryCount(0), greenRetryCount(0), remoteRetryCount(0) {}

BluetoothManager::~BluetoothManager() {}

void BluetoothManager::init() {
  BLEDevice::init("epee_master_s3");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void BluetoothManager::start() {
  xTaskCreatePinnedToCore(TaskBLE, "BLE", 8192, NULL, 1, NULL, 0);
}

void BluetoothManager::TaskBLE(void* pvParameters) {
  BluetoothManager* mgr = BluetoothManager::getInstance();
  if (mgr) mgr->taskLoop();
}

void BluetoothManager::taskLoop() {
  LogUtils::println("[核心0] 蓝牙任务已启动");
  for (;;) {
    checkBLEConnectionStatus();
    updateBLEStatusLed();

    if (doConnectRed && !redConnected && redRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(redDevice, [](BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify){
          LogUtils::println("[信号] red原始击中信号!");
          led_hit_red();
          FencingCore::getInstance()->setRedHit();
        }, "red", serviceUUID, charUUID)) {
        redConnected = true;
        redRetryCount = 0;
      } else {
        redRetryCount++;
      }
      doConnectRed = false;
    }

    if (doConnectGreen && !greenConnected && greenRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(greenDevice, [](BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify){
          LogUtils::println("[信号] green原始击中信号!");
          led_hit_green();
          FencingCore::getInstance()->setGreenHit();
        }, "green", serviceUUID, charUUID)) {
        greenConnected = true;
        greenRetryCount = 0;
      } else {
        greenRetryCount++;
      }
      doConnectGreen = false;
    }

    if (doConnectRemote && !remoteConnected && remoteRetryCount < MAX_CONNECT_RETRY) {
      if (connectToDevice(remoteDevice, [](BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify){
          if (length < 1) { LogUtils::println("[遥控] 无效数据（长度为0）"); return; }
          FencingCore* core = FencingCore::getInstance();
          if (!core) { LogUtils::println("[遥控] FencingCore实例获取失败"); return; }
          uint8_t cmd = pData[0];
          switch (cmd) {
            case 0x01:
              LogUtils::println("[遥控] 触发[NEXT]按钮");
              if (core->isLocked()) { core->resetMatch(false); if (!core->isTimerRunning()) core->toggleTimerStartPause(); } else { core->toggleTimerStartPause(); }
              break;
            case 0x02: LogUtils::println("[遥控] 触发[RESET]按钮"); core->resetMatch(true); core->resetTimer(); break;
            case 0x03: LogUtils::println("[遥控] 触发[PHASE]按钮"); core->nextPhase(); break;
            case 0x04: LogUtils::println("[遥控] 触发[MODE]按钮"); core->toggleDurationMode(); break;
            case 0x05: LogUtils::println("[遥控] 触发[红方+1]按钮"); core->addRedScore(); break;
            case 0x06: LogUtils::println("[遥控] 触发[红方-1]按钮"); core->subtractRedScore(); break;
            case 0x07: LogUtils::println("[遥控] 触发[绿方+1]按钮"); core->addGreenScore(); break;
            case 0x08: LogUtils::println("[遥控] 触发[绿方-1]按钮"); core->subtractGreenScore(); break;
            default: LogUtils::printf("[遥控] 未知指令码: 0x%02X\n", cmd); break;
          }
        }, "remote", remoteServiceUUID, remoteCharUUID)) {
        remoteConnected = true;
        remoteRetryCount = 0;
      } else {
        remoteRetryCount++;
      }
      doConnectRemote = false;
    }

    if (((!redConnected && redRetryCount < MAX_CONNECT_RETRY) || (!greenConnected && greenRetryCount < MAX_CONNECT_RETRY) || (!remoteConnected && remoteRetryCount < MAX_CONNECT_RETRY)) && (!doConnectRed && !doConnectGreen && !doConnectRemote)) {
      BLEDevice::getScan()->start(1, false);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void BluetoothManager::updateBLEStatusLed() {
  if (redConnected && greenConnected && remoteConnected) {
    led_connected_all();
  } else if (redConnected && greenConnected) {
    led_connected_both();
  } else if (redConnected) {
    led_connected_red();
  } else if (greenConnected) {
    led_connected_green();
  } else if (remoteConnected) {
    led_connected_remote();
  } else {
    led_on_boot();
  }
}

void BluetoothManager::checkBLEConnectionStatus() {
  if (millis() - lastConnectionCheck < CONNECTION_CHECK_INTERVAL) return;
  lastConnectionCheck = millis();

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

bool BluetoothManager::connectToDevice(BLEAdvertisedDevice* target, void (*cb)(BLERemoteCharacteristic*, uint8_t*, size_t, bool), String side, BLEUUID svcUUID, BLEUUID chrUUID) {
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

  if (side == "red") {
    redClient = pClient;
  } else if (side == "green") {
    greenClient = pClient;
  } else if (side == "remote") {
    remoteClient = pClient;
  }

  return true;
}
