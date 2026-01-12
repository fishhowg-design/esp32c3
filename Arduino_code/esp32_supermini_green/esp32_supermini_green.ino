#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 引脚定义 - 完美适配ESP32C3 Supermini 无任何冲突
#define FENCING_PIN     8    // 重剑信号采集GPIO（安全首选）
#define DEBOUNCE_DELAY  20    // 重剑专用消抖时间
#define LED_HIT         6    // 击中提示灯
#define LED_BLUETOOTH   10    // 蓝牙连接状态灯
#define BUZZER_PIN      7     // 蜂鸣器控制引脚（新增）

// BLE蓝牙配置（绿方专属，不变）
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define DEVICE_NAME         "epee_green"

// 状态变量 - 新增蜂鸣器状态
bool hitState = false;
bool lastHitState = false;
unsigned long lastDebounceTime = 0;
unsigned long hitLedOnTime = 0;
bool hitLedIsOn = false;
bool buzzerIsOn = false; // 蜂鸣器开启状态
int greenScore = 0;
bool deviceConnected = false;

// BLE相关变量
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;

// BLE连接回调类 不变
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    digitalWrite(LED_BLUETOOTH, HIGH);
    Serial.println("✅【绿方-蓝牙】手机小程序已连接");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    digitalWrite(LED_BLUETOOTH, LOW);
    Serial.println("❌【绿方-蓝牙】手机小程序已断开");
    pServer->getAdvertising()->start();
    Serial.println("✅【绿方-蓝牙】重新广播，等待重连");
  }
};

void setup() {
  // 初始化所有引脚
  pinMode(LED_HIT, OUTPUT);
  pinMode(LED_BLUETOOTH, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT); // 蜂鸣器初始化
  digitalWrite(LED_HIT, LOW);
  digitalWrite(LED_BLUETOOTH, LOW);
  digitalWrite(BUZZER_PIN, LOW); // 蜂鸣器默认关闭

  pinMode(FENCING_PIN, INPUT); // 重剑采集引脚

  Serial.begin(115200);
  Serial.println("==================================");
  Serial.println("=== 重剑计分器（绿方-ESP32C3完整版）初始化 ===");
  Serial.println("==================================");

  BLEDevice::init(DEVICE_NAME);
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
  pCharacteristic->setValue("GREEN:0");



  pService->start();

//添加部分看看能不能解决问题
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID); // 广播服务UUID
  pAdvertising->setScanResponse(true); // 开启扫描响应
  pAdvertising->setName(DEVICE_NAME); // 这里的名称会作为Short Local Name广播
/////////////////////

  

  pServer->getAdvertising()->start();

  Serial.println("📶【绿方-蓝牙】广播启动成功，设备名：" DEVICE_NAME);
  Serial.println("📡【绿方-信号】GPIO8重剑采集就绪，等待击中");
  Serial.println("🔔【绿方-提示】GPIO7蜂鸣器+GPIO6指示灯就绪");
}

void loop() {
  // 重剑信号采集+消抖 不变
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

  // 蜂鸣200ms停，灯亮500ms停，最优体验
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

  lastHitState = currentReading;
}

// 击中事件：亮灯+响蜂鸣+计分+蓝牙上报
void hitEvent() {
  digitalWrite(LED_HIT, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  hitLedOnTime = millis();
  hitLedIsOn = true;
  buzzerIsOn = true;

  greenScore++;
  Serial.print("🎯【绿方-击中】时间戳：");
  String time = String(millis());  
  Serial.print(time);
  Serial.print(" | 绿方得分：");
  Serial.println(greenScore);

  if (deviceConnected) {
    String scoreData = "time:"+ time +"|"+"GREEN:" + String(greenScore);
    pCharacteristic->setValue(scoreData.c_str());
    pCharacteristic->notify();
    Serial.println("📤【绿方-蓝牙】上报得分：" + scoreData);
  } else {
    Serial.println("⚠️【绿方-蓝牙】未连接，得分暂存本地");
  }
}