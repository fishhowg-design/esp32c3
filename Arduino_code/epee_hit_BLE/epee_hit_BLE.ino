#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 引脚定义（新增连接状态LED引脚GPIO5）
#define HIT_SENSOR_PIN 4    // 击中信号输入引脚
#define HIT_LED_PIN 2       // 击中提示LED引脚（原LED）
#define CONN_LED_PIN 5      // 蓝牙连接状态LED引脚（新增）
#define BUZZER_PIN 3        // 蜂鸣器引脚

// 蓝牙相关定义（保持不变）
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// 全局变量
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;  // 蓝牙连接状态
bool hitDetected = false;      // 击中检测状态
unsigned long lastHitTime = 0; // 防重复触发计时
const unsigned long hitDebounceTime = 500; // 防抖时间500ms

// 蓝牙连接回调类（修改：连接/断开时控制连接状态LED）
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    digitalWrite(CONN_LED_PIN, LOW); // 连接成功：连接状态LED常亮（根据接线调整电平）
    Serial.println("蓝牙已连接，连接状态LED常亮");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    digitalWrite(CONN_LED_PIN, HIGH); // 断开连接：连接状态LED熄灭
    // 断开后重启蓝牙广播
    pServer->startAdvertising();
    Serial.println("蓝牙已断开，连接状态LED熄灭，重启广播");
  }
};

// 蜂鸣器发声函数（保持不变）
void buzzerBeep(int duration) {
  for (int i = 0; i < duration * 1000 / 2; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(500);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(500);
  }
}

void setup() {
  // 引脚初始化（新增连接状态LED）
  pinMode(HIT_SENSOR_PIN, INPUT_PULLUP);
  pinMode(HIT_LED_PIN, OUTPUT);
  pinMode(CONN_LED_PIN, OUTPUT); // 初始化新增LED引脚
  pinMode(BUZZER_PIN, OUTPUT);
  
  // 默认状态：击中LED灭、连接LED灭、蜂鸣器静音
  digitalWrite(HIT_LED_PIN, HIGH);
  digitalWrite(CONN_LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  // 串口初始化
  Serial.begin(115200);

  // 蓝牙初始化（保持不变）
  BLEDevice::init("Fencing_Sword_Red"); // 另一台设为Fencing_Sword_Green
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("蓝牙已启动，等待连接...");
}

void loop() {
  // 读取击中信号（保持不变）
  int hitState = digitalRead(HIT_SENSOR_PIN);
  unsigned long currentTime = millis();

  // 击中检测逻辑（保持不变）
  if (hitState == LOW && !hitDetected && (currentTime - lastHitTime) > hitDebounceTime) {
    hitDetected = true;
    lastHitTime = currentTime;

    // 击中提示：原有LED亮+蜂鸣器响
    digitalWrite(HIT_LED_PIN, LOW);
    buzzerBeep(500);
    digitalWrite(HIT_LED_PIN, HIGH);

    // 蓝牙发送击中信号（保持不变）
    if (deviceConnected) {
      pCharacteristic->setValue("HIT");
      pCharacteristic->notify();
      Serial.println("击中信号已发送：HIT");
    } else {
      Serial.println("蓝牙未连接，信号未发送");
    }
  }

  // 重置击中状态（保持不变）
  if (hitState == HIGH && hitDetected) {
    hitDetected = false;
  }

  delay(10);
}