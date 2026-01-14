#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>

// =====================【引脚定义 - 和你的发送端完全一致 无需改接线】=====================
#define LED_HIT         6    // 收到击中数据 提示灯 GPIO6
#define LED_BLUETOOTH   10   // 蓝牙连接状态灯 GPIO10
#define BUZZER_PIN      7    // 收到击中数据 蜂鸣器 GPIO7
#define BAUD_RATE       115200 // 串口波特率 和发送端一致

// =====================【BLE蓝牙配置 - 从你发送端直接复制 绝对一致 不可修改】=====================
#define TARGET_DEVICE_NAME   "epee_red"   // 只连接你的红方重剑设备
#define SERVICE_UUID         "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// =====================【全局状态变量】=====================
bool deviceConnected = false;       // 蓝牙连接状态
bool isScanning = false;            // BLE扫描状态
unsigned long hitLedOnTime = 0;     // 击中灯点亮时间戳
bool hitLedIsOn = false;            // 击中灯状态
bool buzzerIsOn = false;            // 蜂鸣器状态
int recvRedScore = 0;               // 解析到的红方得分
unsigned long recvTotalCount = 0;   // 累计接收击中数据次数
String lastRecvData = "";           // 最后一次接收的原始数据

// BLE核心对象
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteChar = nullptr;

/**
 * @brief BLE特征值 通知回调函数 (核心：收到发送端数据的地方)
 * 你的发送端调用notify()推送数据，这里立刻触发，解析数据+日志打印+硬件反馈
 */
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  // 1. 接收原始数据并转字符串
  String recvData = String((char*)pData, length);
  lastRecvData = recvData;
  recvTotalCount++;
  unsigned long now = millis();

  // 2. 打印【数据接收】核心日志
  Serial.println("==================================");
  Serial.print("✅【蓝牙接收】第");Serial.print(recvTotalCount);Serial.println("次击中数据接收成功！");
  Serial.println("📥 原始接收数据：" + recvData);

  // 3. 完美解析你的发送端格式：time:时间戳|RED:得分
  int timeSplit = recvData.indexOf("time:");
  int redSplit = recvData.indexOf("|RED:");
  if(timeSplit != -1 && redSplit != -1){
    String timeStamp = recvData.substring(5, redSplit);
    recvRedScore = recvData.substring(redSplit+5).toInt();
    Serial.println("✅【数据解析】时间戳：" + timeStamp + " | 红方当前得分：" + String(recvRedScore));
  }else{
    Serial.println("⚠️【数据解析】数据格式异常，未解析到得分");
  }

  // 4. 硬件反馈：击中灯亮+蜂鸣器响 (和你的发送端时序完全一致)
  digitalWrite(LED_HIT, HIGH);
  digitalWrite(BUZZER_PIN, HIGH);
  hitLedIsOn = true;
  buzzerIsOn = true;
  hitLedOnTime = now;
  Serial.println("==================================\n");
}

/**
 * @brief 停止BLE客户端连接，复位状态
 */
void disconnectBLE() {
  if (pClient != nullptr && deviceConnected) {
    pClient->disconnect();
    deviceConnected = false;
  }
  digitalWrite(LED_BLUETOOTH, LOW);
  digitalWrite(LED_HIT, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  hitLedIsOn = false;
  buzzerIsOn = false;
  Serial.println("❌【蓝牙状态】与发送端断开连接！");
}

/**
 * @brief 扫描并连接指定的BLE设备 epee_red 【★★★全部错误修复在这里★★★】
 */
bool connectToBLEDevice() {
  Serial.println("🔍【蓝牙扫描】开始扫描目标设备：" + String(TARGET_DEVICE_NAME));
  isScanning = true;

  // ========== 修复BUG1：指针类型接收扫描结果 BLEScanResults* ==========
  BLEScanResults* foundDevices = BLEDevice::getScan()->start(3); // 扫描3秒超时 ✔修复完成
  bool foundTarget = false;
  if(foundDevices != nullptr && foundDevices->getCount() > 0){
    for (int i = 0; i < foundDevices->getCount(); i++) {
      BLEAdvertisedDevice advertisedDevice = foundDevices->getDevice(i);
      String devName = advertisedDevice.getName().c_str();
      
      // 只匹配你的发送端设备名 epee_red
      if (devName == TARGET_DEVICE_NAME) {
        foundTarget = true;
        Serial.println("✅【蓝牙扫描】发现目标设备：" + devName);
        Serial.println("📌 设备MAC地址：" + String(advertisedDevice.getAddress().toString().c_str()));
        Serial.println("📶 设备信号强度：" + String(advertisedDevice.getRSSI()) + " dBm");

        // 创建BLE客户端并连接
        pClient = BLEDevice::createClient();
        Serial.println("📞【蓝牙连接】正在连接目标设备...");
        if (pClient->connect(&advertisedDevice)) {
          deviceConnected = true;
          digitalWrite(LED_BLUETOOTH, HIGH);
          Serial.println("✅【蓝牙连接】连接成功！开始匹配服务UUID...");
          
          // 匹配发送端的服务UUID
          BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
          if (pRemoteService == nullptr) {
            Serial.println("❌【服务匹配】服务UUID匹配失败！");
            disconnectBLE();
            isScanning = false;
            BLEDevice::getScan()->stop(); // 停止扫描 ✔修复完成
            return false;
          }
          Serial.println("✅【服务匹配】服务UUID匹配成功！开始匹配特征值UUID...");

          // 匹配发送端的特征值UUID
          pRemoteChar = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID);
          if (pRemoteChar == nullptr) {
            Serial.println("❌【特征匹配】特征值UUID匹配失败！");
            disconnectBLE();
            isScanning = false;
            BLEDevice::getScan()->stop(); // 停止扫描 ✔修复完成
            return false;
          }
          Serial.println("✅【特征匹配】特征值UUID匹配成功！");

          // 开启通知：核心！订阅发送端的notify推送
          if(pRemoteChar->canNotify()){
            pRemoteChar->registerForNotify(notifyCallback);
            Serial.println("✅【通知订阅】成功开启数据通知！等待接收击中信号...\n");
          }else{
            Serial.println("❌【通知订阅】特征值不支持Notify！");
            disconnectBLE();
            isScanning = false;
            BLEDevice::getScan()->stop(); // 停止扫描 ✔修复完成
            return false;
          }
        } else {
          Serial.println("❌【蓝牙连接】连接失败！");
        }
        break;
      }
    }
  }

  // ========== 修复BUG2：扫描结束后必须手动停止扫描，释放资源 ==========
  BLEDevice::getScan()->stop(); // 关键修复：强制停止扫描 ✔修复完成
  BLEDevice::getScan()->clearResults(); // 清空扫描结果，避免内存占用 ✔新增优化

  // 扫描超时/未发现目标设备
  if(!foundTarget){
    Serial.println("⚠️【蓝牙扫描】扫描结束，未发现目标设备：" + String(TARGET_DEVICE_NAME));
  }
  isScanning = false;
  return foundTarget;
}

void setup() {
  // ===================== 步骤1：初始化引脚和串口 =====================
  pinMode(LED_HIT, OUTPUT);
  pinMode(LED_BLUETOOTH, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_HIT, LOW);
  digitalWrite(LED_BLUETOOTH, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(BAUD_RATE);
  Serial.println("==================================");
  Serial.println("=== 重剑计分器（BLE接收端-完整版） ===");
  Serial.println("=== 适配发送端：epee_red          ===");
  Serial.println("==================================");
  Serial.println("📌 串口波特率：" + String(BAUD_RATE));
  Serial.println("📌 等待初始化蓝牙...");

  // ===================== 步骤2：初始化BLE核心 =====================
  BLEDevice::init("EPEE_RECEIVER");  // 接收端自身的蓝牙名称
  BLEDevice::getScan()->setActiveScan(true); // 主动扫描，连接更快更稳定
  BLEDevice::getScan()->setInterval(1349);   // 扫描间隔优化
  BLEDevice::getScan()->setWindow(449);      // 扫描窗口优化
  Serial.println("✅【蓝牙初始化】BLE协议栈初始化完成！");

  // ===================== 步骤3：开始第一次扫描 =====================
  connectToBLEDevice();
}

void loop() {
  // ===================== 核心逻辑1：断开重连机制 =====================
  if (!deviceConnected && !isScanning) {
    Serial.println("\n🔄【蓝牙重连】无有效连接，3秒后重新扫描目标设备...");
    delay(3000);
    connectToBLEDevice(); // 重新扫描并连接
  }

  // ===================== 核心逻辑2：击中指示灯+蜂鸣器 时序控制 =====================
  // 和你的发送端完全一致：蜂鸣器响200ms，指示灯亮500ms
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

  // ===================== 核心逻辑3：心跳日志 方便调试 =====================
  static unsigned long lastHeartbeat = 0;
  if(millis() - lastHeartbeat >= 5000){
    lastHeartbeat = millis();
    if(deviceConnected){
      Serial.println("🟢【运行状态】蓝牙已连接 ✔ | 累计接收击中数据：" + String(recvTotalCount) + "次 | 红方当前得分：" + String(recvRedScore));
    }else{
      Serial.println("🟡【运行状态】蓝牙未连接 ⚠ | 等待连接发送端 epee_red");
    }
  }

  delay(10); // 轻微延时，降低CPU占用
}