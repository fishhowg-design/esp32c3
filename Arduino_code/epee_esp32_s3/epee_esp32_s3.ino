/* * ESP32-S3-N8R2 双核多任务测试
 * 硬件：ESP32-S3
 */

// 任务句柄
TaskHandle_t Task0;
TaskHandle_t Task1;

void setup() {
  Serial.begin(115200);
  delay(1000); // 等待串口稳定

  Serial.println("--- 系统启动，准备创建多核任务 ---");

  // 在 Core 0 上创建任务
  xTaskCreatePinnedToCore(
    Task0Code,   /* 任务函数名 */
    "Task_Zero", /* 任务名称 */
    10000,       /* 堆栈大小 (Stack size in words) */
    NULL,        /* 任务输入参数 */
    1,           /* 任务优先级 */
    &Task0,      /* 任务句柄 */
    0);          /* 核心 ID: 0 */

  delay(500); // 稍微错开启动时间

  // 在 Core 1 上创建任务
  xTaskCreatePinnedToCore(
    Task1Code,   /* 任务函数名 */
    "Task_One",  /* 任务名称 */
    10000,       /* 堆栈大小 */
    NULL,        /* 任务输入参数 */
    1,           /* 任务优先级 */
    &Task1,      /* 任务句柄 */
    1);          /* 核心 ID: 1 */
}

// Core 0 的任务代码
void Task0Code(void * pvParameters) {
  Serial.print("任务 0 运行在核心: ");
  Serial.println(xPortGetCoreID());

  for (;;) {
    Serial.printf("[CORE 0] 正在处理底层协议或传感器数据... 运行时间: %lu ms\n", millis());
    delay(2000); // 延时 2 秒
  }
}

// Core 1 的任务代码
void Task1Code(void * pvParameters) {
  Serial.print("任务 1 运行在核心: ");
  Serial.println(xPortGetCoreID());

  for (;;) {
    Serial.printf("[CORE 1] 正在处理用户逻辑或UI显示... 运行时间: %lu ms\n", millis());
    delay(1000); // 延时 1 秒
  }
}

void loop() {
  // 这里的 loop() 默认运行在 Core 1
  // 我们已经在上面手动创建了任务，所以这里可以留空
  vTaskDelete(NULL); // 销毁当前的 loop 任务以节省资源（可选）
}