#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>

#define DEVICE_NAME "Epee_Red" // 另一块改为 "Epee_Green"
#define SENSOR_PIN  2
#define HIT_LED     4
#define BUZZER      5
#define BT_LED      10

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        digitalWrite(BT_LED, HIGH);
    };
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(BT_LED, LOW);
        pServer->getAdvertising()->start();
    }
};

void setup() {
    pinMode(SENSOR_PIN, INPUT_PULLUP);
    pinMode(HIT_LED, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(BT_LED, OUTPUT);

    BLEDevice::init(DEVICE_NAME);
    BLEServer *pServer = BLEServer::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");

    pCharacteristic = pService->createCharacteristic(
                        "beb5483e-36e1-4688-b7f5-ea07361b26a8",
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
    pCharacteristic->addDescriptor(new BLE2902());
    pService->start();
    pServer->getAdvertising()->start();
}

void loop() {
    if (digitalRead(SENSOR_PIN) == LOW) {
        unsigned long hitTime = millis();
        
        // 1. 本地同步反馈
        digitalWrite(HIT_LED, HIGH);
        digitalWrite(BUZZER, HIGH);

        // 2. 发送信号给小程序
        if (deviceConnected) {
            String data = String(hitTime);
            pCharacteristic->setValue(data.c_str());
            pCharacteristic->notify();
        }

        delay(700); // 击中显示持续时间
        digitalWrite(HIT_LED, LOW);
        digitalWrite(BUZZER, LOW);
        delay(300); // 防止抖动
    }
}