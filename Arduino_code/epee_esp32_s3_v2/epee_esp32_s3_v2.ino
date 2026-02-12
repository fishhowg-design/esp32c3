#include "BluetoothManager.h"
#include "led_controller.h"
#include "FencingCore.h" // 仅引入封装类，无其他依赖
#include "log_utils.h"

// LED 与板载指示灯
const int LED_BOARD = 8;

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