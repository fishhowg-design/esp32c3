#ifndef BLUETOOTH_MANAGER_H
#define BLUETOOTH_MANAGER_H

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include "log_utils.h"
#include "FencingCore.h"
#include "led_controller.h"

// 公共常量（供 BluetoothManager 使用）
const int MAX_CONNECT_RETRY = 5;
const unsigned long CONNECTION_CHECK_INTERVAL = 2000;

class BluetoothManager {
public:
  static BluetoothManager* getInstance();
  void init();
  void start();

  // 单例析构（可选）
  ~BluetoothManager();

  // 任务入口（供 FreeRTOS 调用）
  static void TaskBLE(void* pvParameters);

private:
  BluetoothManager();
  void taskLoop();

  // UUIDs
  BLEUUID serviceUUID;
  BLEUUID charUUID;
  BLEUUID remoteServiceUUID;
  BLEUUID remoteCharUUID;

  // 连接状态与客户端
  volatile bool redConnected;
  volatile bool greenConnected;
  volatile bool remoteConnected;
  BLEClient* redClient;
  BLEClient* greenClient;
  BLEClient* remoteClient;

  unsigned long lastConnectionCheck;
  bool doConnectRed;
  bool doConnectGreen;
  bool doConnectRemote;
  BLEAdvertisedDevice* redDevice;
  BLEAdvertisedDevice* greenDevice;
  BLEAdvertisedDevice* remoteDevice;
  int redRetryCount;
  int greenRetryCount;
  int remoteRetryCount;

  void updateBLEStatusLed();
  void checkBLEConnectionStatus();
  bool connectToDevice(BLEAdvertisedDevice* target, void (*cb)(BLERemoteCharacteristic*, uint8_t*, size_t, bool), String side, BLEUUID svcUUID, BLEUUID chrUUID);

  // 广播回调需要访问实例
  friend class MyAdvertisedDeviceCallbacks;
};

#endif // BLUETOOTH_MANAGER_H
