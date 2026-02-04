#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>  // BLE2902头文件（兼容旧版库）

// ===================== 硬件配置 =====================
#define BTN_NEXT      21   
#define BTN_RESET     20  
#define BTN_PHASE     1   
#define BTN_MODE      2   
#define BTN_RED_ADD   5   
#define BTN_RED_SUB   6   
#define BTN_GREEN_ADD 4   
#define BTN_GREEN_SUB 3   

#define DEBOUNCE_DELAY 50 // 防抖滤出时间 (ms)

// ===================== BLE 配置 =====================
#define DEVICE_NAME "epee_control"
static BLEUUID serviceUUID("5fafc202-1fb5-459e-8fcc-c5c9c331914c");
static BLEUUID charUUID("ceb5483f-36e1-4688-b7f5-ea07361b26a9");

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

// 【修改1】替换std::mutex为FreeRTOS原生互斥锁（ESP32-C3兼容性更好）
SemaphoreHandle_t bleMutex;

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
    // 【修改2】替换std::lock_guard为FreeRTOS互斥锁加锁/解锁
    xSemaphoreTake(bleMutex, portMAX_DELAY); // 获取互斥锁（阻塞直到获取成功）
    deviceConnected = true;
    Serial.println("[BLE] 连接成功");
    xSemaphoreGive(bleMutex); // 释放互斥锁
  }
  void onDisconnect(BLEServer* pServer) {
    // 【修改2】替换std::lock_guard为FreeRTOS互斥锁加锁/解锁
    xSemaphoreTake(bleMutex, portMAX_DELAY);
    deviceConnected = false;
    Serial.println("[BLE] 连接断开，重新广播...");
    pServer->getAdvertising()->start();
    xSemaphoreGive(bleMutex);
  }
};

// 按键处理函数
void handleButtonPress(int index) {
  buttons[index].wasPressed = true;  // 标记此按键曾被按下
  Serial.printf("[触发] %s | 指令:0x%02X | PIN:%d\n", buttons[index].name, buttons[index].cmd, buttons[index].pin);
  
  // 将命令放入队列，等待BLE任务发送
  xQueueSend(commandQueue, &buttons[index].cmd, portMAX_DELAY);
}

// BLE发送任务 - 【修改3】移除核心1绑定，单核自动调度
void bleTask(void *parameter) {
  while(1) {
    uint8_t cmd;
    
    // 从队列获取命令
    if(xQueueReceive(commandQueue, &cmd, portMAX_DELAY)) {
      // 【修改2】替换std::lock_guard为FreeRTOS互斥锁加锁/解锁
      xSemaphoreTake(bleMutex, portMAX_DELAY);
      
      if(deviceConnected) {
        if(pCharacteristic != nullptr) {
          // 直接尝试通知，兼容ESP32-C3 BLE库
          pCharacteristic->setValue(&cmd, 1);
          pCharacteristic->notify();
          Serial.printf(" -> BLE 发送命令: 0x%02X\n", cmd);
        } else {
          Serial.println(" -> BLE 特性指针为空");
        }
      } else {
        Serial.println(" -> BLE 未连接，发送取消");
      }
      
      xSemaphoreGive(bleMutex); // 释放互斥锁
    }
    
    // 短暂延时
    delay(10);
  }
}

// 按键检测任务 - 【修改3】移除核心0绑定，单核自动调度
void buttonTask(void *parameter) {
  unsigned long now;
  
  while(1) {
    now = millis();

    for (int i = 0; i < btnCount; i++) {
      bool reading = digitalRead(buttons[i].pin);

      // 物理电平发生变化，立即打印调试信息
      if (reading != buttons[i].lastReading) {
        Serial.printf("[物理信号] 引脚 PIN %d (%s) 变为: %s\n", 
                      buttons[i].pin, buttons[i].name, reading == LOW ? "LOW (按下)" : "HIGH (松开)");
        
        buttons[i].lastDebounceTime = now;
        buttons[i].lastReading = reading;
      }

      // 稳定超过防抖时间，更新稳定状态
      if ((now - buttons[i].lastDebounceTime) > DEBOUNCE_DELAY) {
        if (reading != buttons[i].stableState) {
          buttons[i].stableState = reading;

          // 稳定状态变为按下时触发逻辑
          if (buttons[i].stableState == LOW) {
            handleButtonPress(i);
          } else if (buttons[i].stableState == HIGH && buttons[i].wasPressed) {
            Serial.printf("[释放] %s (PIN %d)\n", buttons[i].name, buttons[i].pin);
            buttons[i].wasPressed = false;
          }
        }
      }
    }
    
    // 极短延迟维持系统稳定性
    delay(1);
  }
}

// ===================== 主程序 =====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("===== epee_control (ESP32-C3 BLE) 启动 =====");

  // 创建命令队列
  commandQueue = xQueueCreate(10, sizeof(uint8_t));
  
  // 【修改4】创建FreeRTOS互斥锁（替代std::mutex）
  bleMutex = xSemaphoreCreateMutex();

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
  
  // 【修改5】移除xTaskCreatePinnedToCore，改用xTaskCreate（单核自动调度，无需指定核心）
  // 创建按键检测任务
  xTaskCreate(
    buttonTask,           // 任务函数
    "ButtonTask",         // 任务名称
    4096,                 // 堆栈大小
    NULL,                 // 参数
    1,                    // 优先级
    NULL                  // 任务句柄（无需保存）
  );
  
  // 创建BLE发送任务
  xTaskCreate(
    bleTask,              // 任务函数
    "BleTask",            // 任务名称
    4096,                 // 堆栈大小（BLE任务建议不小于4096）
    NULL,                 // 参数
    1,                    // 优先级
    NULL                  // 任务句柄（无需保存）
  );
  
  Serial.println("[任务] 单核任务已创建");
}

void loop() {
  // 主循环闲置，所有工作在任务中完成
  delay(1000);
}