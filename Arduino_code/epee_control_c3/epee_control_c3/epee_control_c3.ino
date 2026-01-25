#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ===================== 硬件配置 =====================
#define BTN_NEXT      20   
#define BTN_RESET     21  
#define BTN_PHASE     3   
#define BTN_MODE      4   
#define BTN_RED_ADD   5   
#define BTN_RED_SUB   6   
#define BTN_GREEN_ADD 1   
#define BTN_GREEN_SUB 2   

#define DEBOUNCE_DELAY 50 // 防抖滤除时间 (ms)

// ===================== BLE 配置 =====================
#define DEVICE_NAME "epee_control"
static BLEUUID serviceUUID("5fafc202-1fb5-459e-8fcc-c5c9c331914c");
static BLEUUID charUUID("ceb5483f-36e1-4688-b7f5-ea07361b26a9");

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

// ===================== 增强型按钮结构体 =====================
struct Button {
  uint8_t pin;
  uint8_t cmd;
  const char* name;
  
  bool lastReading;        // 上次物理采样电平
  bool stableState;        // 经过防抖处理后的稳定电平
  unsigned long lastDebounceTime; // 最后一次电平跳变的时间戳
};

Button buttons[] = {
  {BTN_NEXT,      0x01, "NEXT",      HIGH, HIGH, 0},
  {BTN_RESET,     0x02, "RESET",     HIGH, HIGH, 0},
  {BTN_PHASE,     0x03, "PHASE",     HIGH, HIGH, 0},
  {BTN_MODE,      0x04, "MODE",      HIGH, HIGH, 0},
  {BTN_RED_ADD,   0x05, "RED_ADD",   HIGH, HIGH, 0},
  {BTN_RED_SUB,   0x06, "RED_SUB",   HIGH, HIGH, 0},
  {BTN_GREEN_ADD, 0x07, "GREEN_ADD", HIGH, HIGH, 0},
  {BTN_GREEN_SUB, 0x08, "GREEN_SUB", HIGH, HIGH, 0}
};
const int btnCount = sizeof(buttons) / sizeof(Button);

// ===================== BLE 回调 =====================
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("[BLE] 连接成功");
  }
  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("[BLE] 连接断开，重新广播...");
    pServer->getAdvertising()->start();
  }
};

// ===================== 检测逻辑 =====================
void checkButtons() {
  unsigned long now = millis();

  for (int i = 0; i < btnCount; i++) {
    bool reading = digitalRead(buttons[i].pin);

    // 【新增调试日志】只要物理电平发生变化，就立即打印（不受防抖限制）
    if (reading != buttons[i].lastReading) {
      Serial.printf("[物理信号] 引脚 PIN %d (%s) 变为: %s\n", 
                    buttons[i].pin, buttons[i].name, reading == LOW ? "LOW (按下)" : "HIGH (松开)");
      
      buttons[i].lastDebounceTime = now;
      buttons[i].lastReading = reading;
    }

    // 只有稳定超过 DEBOUNCE_DELAY 毫秒，才更新稳定状态
    if ((now - buttons[i].lastDebounceTime) > DEBOUNCE_DELAY) {
      if (reading != buttons[i].stableState) {
        buttons[i].stableState = reading;

        // 只有在稳定状态变为按下时才触发逻辑
        if (buttons[i].stableState == LOW) {
          handleButtonPress(i);
        }
      }
    }
  }
}
void handleButtonPress(int index) {
  Serial.printf("[触发] %s | 指令:0x%02X\n", buttons[index].name, buttons[index].cmd);
  
  if (deviceConnected && pCharacteristic != nullptr) {
    pCharacteristic->setValue(&buttons[index].cmd, 1);
    pCharacteristic->notify();
    Serial.println(" -> BLE 发送成功");
  } else {
    Serial.println(" -> BLE 未连接，发送取消");
  }
}

// ===================== 主程序 =====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("===== epee_control (Native BLE) 启动 =====");

  // 初始化引脚
  for (int i = 0; i < btnCount; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    // 初始化电平记录，避免开机瞬间误触
    bool current = digitalRead(buttons[i].pin);
    buttons[i].lastReading = current;
    buttons[i].stableState = current;
  }

  // 初始化 BLE
  BLEDevice::init(DEVICE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(serviceUUID);
  pCharacteristic = pService->createCharacteristic(
                      charUUID,
                      BLECharacteristic::PROPERTY_READ | 
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(serviceUUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  
  Serial.println("[BLE] 广播已开启");
}

void loop() {
  checkButtons();
  
  // 极短延迟维持系统稳定性，且不影响响应速度
  delay(1); 
}