#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// 根据你的服务端设置修改这些 UUID（通常两个服务端可以使用相同的 UUID）
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// 标记是否发现设备
static boolean doConnectRed = false;
static boolean doConnectGreen = false;
static BLEAdvertisedDevice* redDevice;
static BLEAdvertisedDevice* greenDevice;

// 红色设备的回调函数
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("[日志 - epee_red]: ");
    for (int i = 0; i < length; i++) Serial.print((char)pData[i]);
    Serial.println();
}

// 绿色设备的回调函数
static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("[日志 - epee_green]: ");
    for (int i = 0; i < length; i++) Serial.print((char)pData[i]);
    Serial.println();
}

// 扫描回调：同时寻找红绿两个设备
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String name = advertisedDevice.getName().c_str();
        Serial.print("扫描中... 发现设备: ");
        Serial.println(name);

        if (name == "epee_red") {
            Serial.println(">>> [匹配成功] 找到 epee_red");
            redDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectRed = true;
        } else if (name == "epee_green") {
            Serial.println(">>> [匹配成功] 找到 epee_green");
            greenDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectGreen = true;
        }

        // 如果两个都找到了，可以考虑停止扫描
        if (doConnectRed && doConnectGreen) {
            BLEDevice::getScan()->stop();
            Serial.println("所有目标已找到，停止扫描。");
        }
    }
};

// 通用的连接函数
bool connectToDevice(BLEAdvertisedDevice* targetDevice, void (*callback)(BLERemoteCharacteristic*, uint8_t*, size_t, bool)) {
    Serial.print("尝试连接到: ");
    Serial.println(targetDevice->getName().c_str());

    BLEClient* pClient = BLEDevice::createClient();
    if (!pClient->connect(targetDevice)) return false;

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        pClient->disconnect();
        return false;
    }

    BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteCharacteristic == nullptr) {
        pClient->disconnect();
        return false;
    }

    if (pRemoteCharacteristic->canNotify()) {
        pRemoteCharacteristic->registerForNotify(callback);
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    // 针对 ESP32-C3 的串口等待
    while (!Serial && millis() < 5000);
    
    Serial.println("--- ESP32-C3 双路蓝牙监听启动 ---");

    BLEDevice::init("epee_supmin");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(15, false); // 扫描15秒
}

void loop() {
    // 处理红色设备连接
    if (doConnectRed) {
        if (connectToDevice(redDevice, redNotifyCallback)) {
            Serial.println("成功订阅 epee_red 日志");
        } else {
            Serial.println("连接 epee_red 失败");
        }
        doConnectRed = false;
    }

    // 处理绿色设备连接
    if (doConnectGreen) {
        if (connectToDevice(greenDevice, greenNotifyCallback)) {
            Serial.println("成功订阅 epee_green 日志");
        } else {
            Serial.println("连接 epee_green 失败");
        }
        doConnectGreen = false;
    }

    delay(1000);
}