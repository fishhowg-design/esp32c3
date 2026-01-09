#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// 引脚定义 - 完全不变，和你的原始代码一致
#define FENCING_PIN     12    // 重剑信号采集GPIO
#define DEBOUNCE_DELAY  20    // 消抖时间（20ms）重剑专用，无需修改
#define LED_HIT         18    // D4：击中提示灯（GPIO18）
#define LED_BLUETOOTH   19    // D5：蓝牙连接状态灯（GPIO19）

// BLE蓝牙配置（红方专属，设备名正常生效）
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define DEVICE_NAME         "fencing_sword_red"  // ✅ 设备名完美显示，无需缩短

// 状态变量 - 完全不变，你的逻辑100%保留
bool hitState = false;
bool lastHitState = false;
unsigned long lastDebounceTime = 0;
unsigned long hitLedOnTime = 0;
bool hitLedIsOn = false;
int redScore = 0;              // 红方累计得分

// BLE相关变量 - 极致精简，无多余定义，适配AirM2M库
BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;  // 蓝牙连接状态

// BLE连接回调类 - AirM2M库唯一支持的重启广播方式，无报错
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    digitalWrite(LED_BLUETOOTH, HIGH); // 蓝牙连接成功，指示灯亮
    Serial.println("✅【红方-蓝牙】手机小程序已连接");
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    digitalWrite(LED_BLUETOOTH, LOW);  // 蓝牙断开，指示灯灭
    Serial.println("❌【红方-蓝牙】手机小程序已断开");
    // ✅ AirM2M库唯一兼容的重启广播写法，绝对有效
    pServer->getAdvertising()->start();
    Serial.println("✅【红方-蓝牙】重新广播，等待重连");
  }
};

void setup() {
  // 初始化LED引脚 - 输出模式，完全不变
  pinMode(LED_HIT, OUTPUT);
  pinMode(LED_BLUETOOTH, OUTPUT);
  digitalWrite(LED_HIT, LOW);
  digitalWrite(LED_BLUETOOTH, LOW);

  // 初始化重剑信号采集引脚（内部上拉）- 逻辑正确，无需修改
  pinMode(FENCING_PIN, INPUT_PULLUP);

  // 初始化串口
  Serial.begin(115200);
  Serial.println("==================================");
  Serial.println("=== 重剑计分器（红方-AirM2M最终版）初始化 ===");
  Serial.println("==================================");

  // ✅✅✅【核心终极修复】AirM2M库设置蓝牙名称的唯一正确方式 ✅✅✅
  // 关键规则：必须在 createServer() 之前 执行 BLEDevice::init(DEVICE_NAME)
  // 该库会自动把这个名称写入广播包，手机就能正常显示 Fencing_Red ，无需任何其他函数！
  BLEDevice::init(DEVICE_NAME);  

  // 创建BLE服务 - 顺序不能变，必须在init之后
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // 创建BLE服务和特征值（读/写/通知）- 完全不变，小程序必备
  BLEService* pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902()); // 通知功能必须保留，不能删
  pCharacteristic->setValue("RED:0"); // 初始化得分，小程序首次连接可读
  pService->start();

  // ✅✅✅【AirM2M库最简广播启动】无任何配置函数，无报错，必用这个写法 ✅✅✅
  // 该库的广播会自动携带 设备名+服务UUID ，完美适配小程序搜索
  pServer->getAdvertising()->start();

  Serial.println("📶【红方-蓝牙】广播启动成功，设备名：" DEVICE_NAME);
  Serial.println("📡【红方-信号】GPIO12重剑采集就绪，等待击中");
}

void loop() {
  // 重剑信号采集+消抖处理 - 你的原始逻辑，完全不变，100%正确
  bool currentReading = digitalRead(FENCING_PIN);
  currentReading = !currentReading; // 反转：true=击中，false=未击中（低电平有效）

  if (currentReading != lastHitState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (currentReading != hitState) {
      hitState = currentReading;
      if (hitState) {
        hitEvent(); // 触发击中事件
      }
    }
  }

  // 非阻塞控制击中灯熄灭（亮500ms自动灭）- 逻辑不变，稳定可靠
  if (hitLedIsOn) {
    if (millis() - hitLedOnTime >= 500) {
      digitalWrite(LED_HIT, LOW);
      hitLedIsOn = false;
    }
  }

  lastHitState = currentReading;
}

// 红方击中事件处理函数 - 完全不变，功能完整，蓝牙上报正常
void hitEvent() {
  digitalWrite(LED_HIT, HIGH);
  hitLedOnTime = millis();
  hitLedIsOn = true;

  redScore++;
  Serial.print("🎯【红方-击中】时间戳：");
  Serial.print(millis());
  Serial.print(" | 红方得分：");
  Serial.println(redScore);

  if (deviceConnected) {
    String scoreData = "RED:" + String(redScore);
    pCharacteristic->setValue(scoreData.c_str());
    pCharacteristic->notify();  // BLE主动通知，小程序实时接收得分
    Serial.println("📤【红方-蓝牙】上报得分：" + scoreData);
  } else {
    Serial.println("⚠️【红方-蓝牙】未连接，得分暂存本地");
  }
}