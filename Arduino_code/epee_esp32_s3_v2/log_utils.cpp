#include "log_utils.h"

// 静态成员初始化（类外定义，全局唯一）
SemaphoreHandle_t LogUtils::s_serial_mutex = NULL;
bool LogUtils::s_is_initialized = false;

/**
 * @brief 初始化串口和互斥锁（静态函数）
 */
bool LogUtils::init(uint32_t baud) {
  // 仅初始化一次，避免重复创建锁/重启串口
  if (s_is_initialized) {
    println("[LogUtils] 已初始化，无需重复调用");
    return true;
  }

  // 1. 初始化串口
  Serial.begin(baud);
  

  // 2. 创建全局互斥锁（FreeRTOS）
  if (s_serial_mutex == NULL) {
    s_serial_mutex = xSemaphoreCreateMutex();
  }

  // 3. 验证初始化结果
  if (s_serial_mutex == NULL) {
    Serial.println("[LogUtils] ❌ 互斥锁创建失败！");
    return false;
  }

  s_is_initialized = true;
  println("[LogUtils] ✅ 串口+互斥锁初始化完成（波特率：" + String(baud) + "）");
  return true;
}

/**
 * @brief 加锁的格式化打印（静态核心实现）
 */
void LogUtils::printf(const char* format, ...) {
  // 空指针/未初始化防护
  if (format == NULL || !s_is_initialized || s_serial_mutex == NULL) {
    Serial.print("[LogUtils] ⚠️ 打印失败（参数无效/未初始化）：");
    Serial.print(format);
    return;
  }

  // 申请锁（超时10ms，避免死锁）
  if (xSemaphoreTake(s_serial_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // 处理可变参数（兼容printf）
    char buffer[256]; // 可根据需求调整缓冲区大小
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // 输出到串口
    Serial.print(buffer);

    // 释放锁（必须！否则其他线程无法打印）
    xSemaphoreGive(s_serial_mutex);
  } else {
    // 锁超时，降级打印（无锁，仅提示）
    Serial.print("[LogUtils] ⚠️ 打印锁超时：");
    Serial.print(format);
  }
}

/**
 * @brief 加锁的换行打印（静态核心实现）
 */
void LogUtils::println(const String& msg) {
  // 未初始化防护
  if (!s_is_initialized || s_serial_mutex == NULL) {
    Serial.println("[LogUtils] ⚠️ 打印失败（未初始化）：" + msg);
    return;
  }

  // 申请锁
  if (xSemaphoreTake(s_serial_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    Serial.println(msg);
    xSemaphoreGive(s_serial_mutex);
  } else {
    Serial.println("[LogUtils] ⚠️ 打印锁超时：" + msg);
  }
}