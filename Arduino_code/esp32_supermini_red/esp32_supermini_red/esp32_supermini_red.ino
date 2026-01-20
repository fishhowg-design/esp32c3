
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// =====================【引脚定义 - 完美适配ESP32C3 Supermini 无冲突】=====================
#define FENCING_PIN     8    // 重剑信号采集GPIO
#define DEBOUNCE_DELAY  20    // 重剑专用消抖时间 最优值
#define LED_HIT         6    // 击中提示灯 GPIO6
#define LED_BLUETOOTH   10    // 蓝牙连接状态灯 GPIO10
#define BUZZER_PIN      7     // 蜂鸣器控制引脚 GPIO7

// =====================【BLE蓝牙配置 - 与主机严格一致 不可修改】=====================
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define DEVICE_NAME         "epee_red"

// =====================【状态变量】=====================
bool hitState = false;
bool lastHitState = false;
unsigned long lastDebounceTime = 0;
unsigned long hitLedOnTime = 0;
bool hitLedIsOn = false;
bool buzzerIsOn = false;
int redScore = 0;
bool deviceConnected = false;
static BLE2902 ble2902Desc; // 解决内存泄漏 静态创建描述符

// =====================【BLE相关变量】=====================
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;

/**
 * @brief BLE连接回调类
 */
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    digitalWrite(LED_BLUETOOTH, HIGH);
    Serial.println("✅【红方-蓝牙】BLE计分主机 已成功连接！");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    digitalWrite(LED_BLUETOOTH, LOW);
    Serial.println("❌【红方-蓝牙】与BLE主机断开连接！");
    BLEDevice::startAdvertising();
    Serial.println("✅【红方-蓝牙】重新开启广播，等待主机重连...");
  }
};

void setup() {
  pinMode(LED_HIT, OUTPUT);
  pinMode(LED_BLUETOOTH, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_HIT, LOW);
  digitalWrite(LED_BLUETOOTH, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(FENCING_PIN, INPUT_PULLUP); // 防浮空误触

  Serial.begin(115200);
  Serial.println("==================================");
  Serial.println("=== 重剑计分器（红方-ESP32C3 完整版） ===");
  Serial.println("==================================");

  // BLE初始化核心 - 修复Notify权限 必加 INDICATE
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY |  // 原始保留
                      BLECharacteristic::PROPERTY_INDICATE  // ✅ 关键新增 缺一不可
                    );
  
  pCharacteristic->addDescriptor(&ble2902Desc);
  pCharacteristic->setValue("RED:0");
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setName(DEVICE_NAME);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();

  Serial.println("📶【红方-蓝牙】广播启动成功，设备名：epee_red");
  Serial.println("🟥【红方-就绪】重剑采集就绪，等待击中信号！");
}

void loop() {
  // 重剑信号采集+消抖逻辑 不变
  bool currentReading = digitalRead(FENCING_PIN);
  currentReading = !currentReading;

  if (currentReading != lastHitState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (currentReading != hitState) {
      hitState = currentReading;
      if (hitState) {
        hitEvent();
      }
    }
  }
/*
  // 击中指示灯+蜂鸣器时序控制
  if (hitLedIsOn || buzzerIsOn) {
    unsigned long now = millis();
    if (buzzerIsOn && (now - hitLedOnTime) >= 200) {
      digitalWrite(BUZZER_PIN, LOW);
      buzzerIsOn = false;
    }
    if (hitLedIsOn && (now - hitLedOnTime) >= 500) {
      digitalWrite(LED_HIT, LOW);
      hitLedIsOn = false;
    }
  }
*/
  lastHitState = currentReading;
}

/**
 * @brief 击中事件处理函数 - ✅ 修复连接状态判断 绝对准确
 */
void hitEvent() {
  digitalWrite(LED_HIT, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  hitLedOnTime = millis();
  hitLedIsOn = true;
  buzzerIsOn = true;

  if(redScore < 99) redScore++;
  String timeStr = String(millis());  
  Serial.print("🎯【红方-击中】时间戳：");
  Serial.print(timeStr);
  Serial.print(" | 红方得分：");
  Serial.println(redScore);

  // ✅ 关键修复：使用库原生连接判断，杜绝发空包，适配最新Arduino BLE库
  BLEServer *pServer = BLEDevice::getServer();
  if (pServer != NULL && pServer->getConnectedCount() > 0) {
    String scoreData = "time:" + timeStr + "|RED:" + String(redScore);
    pCharacteristic->setValue(scoreData.c_str());
    pCharacteristic->notify();
    Serial.println("📤【红方-上报】成功推送数据 → " + scoreData + "\n");
  } else {
    Serial.println("⚠️【红方-提示】无BLE主机连接，得分暂存本地\n");
  }
}