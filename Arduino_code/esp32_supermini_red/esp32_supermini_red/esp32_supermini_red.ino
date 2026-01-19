#include <NimBLEDevice.h>  // NimBLE唯一核心头文件，必写

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

// =====================【NimBLE相关变量】=====================
NimBLEServer* pServer = NULL;
NimBLECharacteristic* pCharacteristic = NULL;

/**
 * @brief NimBLE连接回调类 - 完美适配
 */
class MyServerCallbacks: public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) {
    deviceConnected = true;
    digitalWrite(LED_BLUETOOTH, HIGH);
    Serial.println("✅【红方-蓝牙】BLE计分主机 已成功连接！");
  };

  void onDisconnect(NimBLEServer* pServer) {
    deviceConnected = false;
    digitalWrite(LED_BLUETOOTH, LOW);
    Serial.println("❌【红方-蓝牙】与BLE主机断开连接！");
    NimBLEDevice::startAdvertising();
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
  pinMode(FENCING_PIN, INPUT_PULLUP); // 防浮空误触 保留原版最优配置

  Serial.begin(115200);
  Serial.println("==================================");
  Serial.println("=== 重剑计分器（红方-ESP32C3 NimBLE终极版） ===");
  Serial.println("==================================");

  // NimBLE初始化核心 - 极简无报错
  NimBLEDevice::init(DEVICE_NAME);
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  NimBLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      NIMBLE_PROPERTY::READ |
                      NIMBLE_PROPERTY::WRITE |
                      NIMBLE_PROPERTY::NOTIFY |
                      NIMBLE_PROPERTY::INDICATE
                    );
  
  // ✅ 关键修复：删掉 ble2902Desc 相关所有代码，NimBLE自动生成2902描述符，无需手动添加
  pCharacteristic->setValue("RED:0");
  pService->start();

  // ✅ 关键修复：NimBLE标准广播配置，删除所有报错的无效函数
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setName(DEVICE_NAME);
  NimBLEDevice::startAdvertising();

  Serial.println("📶【红方-蓝牙】广播启动成功，设备名：epee_red");
  Serial.println("🟥【红方-就绪】重剑采集就绪，等待击中信号！");
}

void loop() {
  // 重剑信号采集+消抖逻辑 完全原版不动，最优逻辑保留
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

  // ✅ 原版修复：指示灯+蜂鸣器自动关闭逻辑，必开，解决常亮常响问题
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

/**
 * @brief 击中事件处理函数 - 原版完美逻辑，无任何修改，计分精准无丢包
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

  // ✅ 原版最优连接判断，杜绝空包推送，适配所有BLE主机
  NimBLEServer *pServer = NimBLEDevice::getServer();
  if (pServer != NULL && pServer->getConnectedCount() > 0) {
    String scoreData = "time:" + timeStr + "|RED:" + String(redScore);
    pCharacteristic->setValue(scoreData.c_str());
    pCharacteristic->notify();
    Serial.println("📤【红方-上报】成功推送数据 → " + scoreData + "\n");
  } else {
    Serial.println("⚠️【红方-提示】无BLE主机连接，得分暂存本地\n");
  }
}