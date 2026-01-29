#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <mutex>  // 添加互斥锁支持
#include <BLE2902.h>  // 添加BLE2902头文件

// ===================== 硬件配置 =====================
#define BTN_NEXT      9   
#define BTN_RESET     10  
#define BTN_PHASE     11   
#define BTN_MODE      12   
#define BTN_RED_ADD   17   
#define BTN_RED_SUB   18   
#define BTN_GREEN_ADD 7   
#define BTN_GREEN_SUB 8   

#define DEBOUNCE_DELAY 50 // 防抖滤出时间 (ms)

// ===================== BLE 配置 =====================
#define DEVICE_NAME "epee_control"
static BLEUUID serviceUUID("5fafc202-1fb5-459e-8fcc-c5c9c331914c");
static BLEUUID charUUID("ceb5483f-36e1-4688-b7f5-ea07361b26a9");

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

// 添加互斥锁保护共享变量
std::mutex bleMutex;

// ===================== 增强型按钮结构体 =====================
struct Button {
  uint8_t pin;
  uint8_t cmd;
  const char* name;
  
  bool lastReading;        // 上次物理采样电平
  bool stableState;        // 经过防抖处理后的稳定电平
  unsigned long lastDebounceTime; // 最后一次电平跳变的时间戳
  bool wasPressed;         // 记录是否曾经检测到按下事件，用于调试
};

Button buttons[] = {
  {BTN_NEXT,      0x01, "NEXT",      HIGH, HIGH, 0, false},
  {BTN_RESET,     0x02, "RESET",     HIGH, HIGH, 0, false},
  {BTN_PHASE,     0x03, "PHASE",     HIGH, HIGH, 0, false},
  {BTN_MODE,      0x04, "MODE",      HIGH, HIGH, 0, false},
  {BTN_RED_ADD,   0x05, "RED_ADD",   HIGH, HIGH, 0, false},
  {BTN_RED_SUB,   0x06, "RED_SUB",   HIGH, HIGH, 0, false},
  {BTN_GREEN_ADD, 0x07, "GREEN_ADD", HIGH, HIGH, 0, false},
  {BTN_GREEN_SUB, 0x08, "GREEN_SUB", HIGH, HIGH, 0, false}
};
const int btnCount = sizeof(buttons) / sizeof(Button);

// 全局队列存储待发送的命令
QueueHandle_t commandQueue;

// ===================== BLE 回调 =====================
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    std::lock_guard<std::mutex> lock(bleMutex);
    deviceConnected = true;
    Serial.println("[BLE] 连接成功");
  }
  void onDisconnect(BLEServer* pServer) {
    std::lock_guard<std::mutex> lock(bleMutex);
    deviceConnected = false;
    Serial.println("[BLE] 连接断开，重新广播...");
    pServer->getAdvertising()->start();
  }
};

// 按键处理函数
void handleButtonPress(int index) {
  buttons[index].wasPressed = true;  // 标记此按键曾被按下
  Serial.printf("[触发] %s | 指令:0x%02X | PIN:%d\n", buttons[index].name, buttons[index].cmd, buttons[index].pin);
  
  // 将命令放入队列，等待BLE任务发送
  xQueueSend(commandQueue, &buttons[index].cmd, portMAX_DELAY);
}

// BLE发送任务 - 在核心1运行
void bleTask(void *parameter) {
  while(1) {
    uint8_t cmd;
    
    // 从队列获取命令
    if(xQueueReceive(commandQueue, &cmd, portMAX_DELAY)) {
      std::lock_guard<std::mutex> lock(bleMutex);
      
      if(deviceConnected) {
        if(pCharacteristic != nullptr) {
          // 直接尝试通知，ESP32S3上的BLE库可能没有canNotify方法
          pCharacteristic->setValue(&cmd, 1);
          pCharacteristic->notify();
          Serial.printf(" -> BLE 发送命令: 0x%02X\n", cmd);
        } else {
          Serial.println(" -> BLE 特性指针为空");
        }
      } else {
        Serial.println(" -> BLE 未连接，发送取消");
      }
    }
    
    // 短暂延时
    delay(10);
  }
}

// 按键检测任务 - 在核心0运行
void buttonTask(void *parameter) {
  unsigned long now;
  
  while(1) {
    now = millis();

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
          } else if (buttons[i].stableState == HIGH && buttons[i].wasPressed) {
            // 记录按键释放事件，有助于判断按键操作是否完整
            Serial.printf("[释放] %s (PIN %d)\n", buttons[i].name, buttons[i].pin);
            buttons[i].wasPressed = false;
          }
        }
      }
    }
    
    // 极短延迟维持系统稳定性，且不影响响应速度
    delay(1);
  }
}

// ===================== 主程序 =====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("===== epee_control (Native BLE) 启动 =====");

  // 创建命令队列
  commandQueue = xQueueCreate(10, sizeof(uint8_t));
  
  // 初始化引脚
  for (int i = 0; i < btnCount; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    // 初始化电平记录，避免开机瞬间误触
    bool current = digitalRead(buttons[i].pin);
    buttons[i].lastReading = current;
    buttons[i].stableState = current;
    buttons[i].wasPressed = false;
    Serial.printf("[初始化] PIN %d (%s) 已设置为 INPUT_PULLUP, 初始电平: %s\n", 
                  buttons[i].pin, buttons[i].name, current == LOW ? "LOW" : "HIGH");
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
  // 添加描述符，兼容旧版库
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(serviceUUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  
  Serial.println("[BLE] 广播已开启");
  
  // 创建按键检测任务在核心0上运行
  xTaskCreatePinnedToCore(
    buttonTask,           // 任务函数
    "ButtonTask",         // 任务名称
    4096,                // 堆栈大小
    NULL,                // 参数
    1,                   // 优先级
    NULL,                // 任务句柄
    0                    // 核心号
  );
  
  // 创建BLE任务在核心1上运行
  xTaskCreatePinnedToCore(
    bleTask,              // 任务函数
    "BleTask",            // 任务名称
    4096,                // 堆栈大小
    NULL,                // 参数
    1,                   // 优先级
    NULL,                // 任务句柄
    1                    // 核心号
  );
  
  Serial.println("[任务] 双核心任务已创建");
}

void loop() {
  // 主循环不再执行任何操作，所有工作都在任务中完成
  delay(1000);
}