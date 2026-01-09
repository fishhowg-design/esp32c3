#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 引脚定义
#define FENCING_PIN     12    // 重剑信号采集GPIO
#define DEBOUNCE_DELAY  20    // 消抖时间（20ms）
#define LED_HIT         18    // D4：击中提示灯（GPIO18）
#define LED_BLUETOOTH   19    // D5：蓝牙连接状态灯（GPIO19）

// BLE蓝牙配置（红方专属）
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"  // 服务UUID可共用
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" // 特征值UUID可共用
#define DEVICE_NAME         "Fencing_Red"                           // 红方蓝牙名

// 状态变量
bool hitState = false;
bool lastHitState = false;
unsigned long lastDebounceTime = 0;
unsigned long hitLedOnTime = 0;
bool hitLedIsOn = false;
int redScore = 0;              // 红方累计得分

// BLE相关变量
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;  // 蓝牙连接状态

// BLE连接回调类
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    digitalWrite(LED_BLUETOOTH, HIGH); // 连接成功，蓝牙灯常亮
    Serial.println("【红方-蓝牙】手机已连接");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    digitalWrite(LED_BLUETOOTH, LOW);  // 断开连接，蓝牙灯熄灭
    Serial.println("【红方-蓝牙】手机已断开");
    pServer->startAdvertising(); // 重启广告
  }
};

void setup() {
  // 初始化LED引脚
  pinMode(LED_HIT, OUTPUT);
  pinMode(LED_BLUETOOTH, OUTPUT);
  digitalWrite(LED_HIT, LOW);
  digitalWrite(LED_BLUETOOTH, LOW);

  // 初始化重剑信号采集引脚（内部上拉）
  pinMode(FENCING_PIN, INPUT_PULLUP);

  // 初始化串口
  Serial.begin(115200);
  Serial.println("=== 重剑记分器（红方）初始化 ===");

  // 初始化BLE蓝牙
  BLEDevice::init(DEVICE_NAME);  // 红方设备名
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // 创建BLE服务和特征值（支持通知）
  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  // 启动BLE广告
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.println("【红方-蓝牙】广告已启动，设备名：" DEVICE_NAME);
  Serial.println("【红方-信号】采集GPIO12已就绪");
}

void loop() {
  // 1. 读取并重剑信号（消抖处理）
  bool currentReading = digitalRead(FENCING_PIN);
  currentReading = !currentReading; // 反转：true=击中，false=未击中

  if (currentReading != lastHitState) {
    lastDebounceTime = millis();
  }

  // 2. 消抖后判定稳定状态
  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (currentReading != hitState) {
      hitState = currentReading;
      
      if (hitState) {
        // 击中事件触发
        hitEvent();
      }
    }
  }

  // 3. 非阻塞控制击中灯熄灭
  if (hitLedIsOn && (millis() - hitLedOnTime) > 500) {
    digitalWrite(LED_HIT, LOW);
    hitLedIsOn = false;
  }

  // 4. 更新上一次状态
  lastHitState = currentReading;
}

// 红方击中事件处理函数
void hitEvent() {
  // 1. 点亮击中提示灯
  digitalWrite(LED_HIT, HIGH);
  hitLedOnTime = millis();
  hitLedIsOn = true;

  // 2. 累计红方得分并打印
  redScore++;
  Serial.print("【红方-击中】时间戳：");
  Serial.print(millis());
  Serial.print(" | 红方得分：");
  Serial.println(redScore);

  // 3. 蓝牙发送红方得分给手机（仅连接时发送）
  if (deviceConnected) {
    String scoreData = "RED:" + String(redScore); // 红方数据格式：RED:分数
    pCharacteristic->setValue(scoreData.c_str());
    pCharacteristic->notify();  // 通知手机接收数据
    Serial.println("【红方-蓝牙】已发送得分：" + scoreData);
  } else {
    Serial.println("【红方-蓝牙】未连接，暂不发送数据");
  }
}