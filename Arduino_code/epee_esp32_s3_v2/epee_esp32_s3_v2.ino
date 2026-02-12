#include "BluetoothManager.h"
#include "led_controller.h"
#include "FencingCore.h" // 仅引入封装类，无其他依赖
#include "log_utils.h"

// LED 与板载/外设引脚常量（规范化命名并集中在此）
const int LED_BOARD = 8;
// FencingCore 相关引脚（按组命名：PIN_LED_ / PIN_BTN_ / PIN_TIMER_）
const int PIN_LED_RED = 4;
const int PIN_LED_GREEN = 5;
const int PIN_BUZZER = 3;
const int PIN_BTN_NEXT = 7;
const int PIN_BTN_RESET = 6;
const int PIN_BTN_PHASE = 15;
const int PIN_BTN_MODE = 16;
const int PIN_BTN_RED_ADD = 14;
const int PIN_BTN_RED_SUB = 9;
const int PIN_BTN_GREEN_ADD = 17;
const int PIN_BTN_GREEN_SUB = 18;

// =====================【多核任务函数（原有逻辑未改动）】=====================
void TaskLogic(void* pvParameters) {
  LogUtils::println("[核心1] 逻辑任务已启动");
  FencingCore* core = FencingCore::getInstance();

  for (;;) {
    core->updateTimer();          
    core->processHitDetection();  
    core->handleHitEffects();     
    core->checkButtons();         

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// 蓝牙任务已封装到 BluetoothManager

// =====================【Arduino 标准入口（未改动）】=====================
void setup() {
 
  // 第一步：初始化日志工具（仅需调用一次，全局生效）
  if (!LogUtils::init(115200)) {
    while(1); // 初始化失败，卡死避免后续错误
  }
  

  LogUtils::println("\n==============================");
  LogUtils::println("    重剑计分系统 S3 (带计时+遥控) 启动...");
  LogUtils::println("==============================");

  LogUtils::println("[主程序]完成蓝牙初始化");
  BluetoothManager::getInstance()->init();
  
  //蓝牙处理线程启动
  BluetoothManager::getInstance()->start();

  // 在调用 FencingCore::init() 前赋值引脚（将类中的常量迁移到 .ino）
  FencingCore::PIN_LED_RED = PIN_LED_RED;
  FencingCore::PIN_LED_GREEN = PIN_LED_GREEN;
  FencingCore::PIN_BUZZER = PIN_BUZZER;
  FencingCore::PIN_BTN_NEXT = PIN_BTN_NEXT;
  FencingCore::PIN_BTN_RESET = PIN_BTN_RESET;
  FencingCore::PIN_BTN_PHASE = PIN_BTN_PHASE;
  FencingCore::PIN_BTN_MODE = PIN_BTN_MODE;
  FencingCore::PIN_BTN_RED_ADD = PIN_BTN_RED_ADD;
  FencingCore::PIN_BTN_RED_SUB = PIN_BTN_RED_SUB;
  FencingCore::PIN_BTN_GREEN_ADD = PIN_BTN_GREEN_ADD;
  FencingCore::PIN_BTN_GREEN_SUB = PIN_BTN_GREEN_SUB;

  LogUtils::println("[主程序]完成逻辑核心初始化");
  FencingCore::getInstance()->init();

  led_init();
  led_on_boot();

  pinMode(LED_BOARD, OUTPUT);

  
  

  //逻辑处理线程
  xTaskCreatePinnedToCore(TaskLogic, "Logic", 8192, NULL, 2, NULL, 1);

  LogUtils::println("[系统] 所有任务已就绪");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}