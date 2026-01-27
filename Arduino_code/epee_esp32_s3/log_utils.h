#ifndef LOG_UTILS_H
#define LOG_UTILS_H

#include <Arduino.h>
#include <FreeRTOS.h>
#include <semphr.h>

/**
 * @brief 线程安全的日志工具类（静态封装，全局唯一）
 * 无需实例化，直接通过 LogUtils::xxx() 调用
 */
class LogUtils {
public:
  /**
   * @brief 初始化串口和互斥锁（必须在setup()中最先调用，仅需调用一次）
   * @param baud 串口波特率，默认115200
   * @return true=初始化成功，false=失败
   */
  static bool init(uint32_t baud = 115200);

  /**
   * @brief 加锁的格式化打印（兼容printf语法，线程安全）
   * @param format 格式化字符串，如"[%s] 数值：%d"
   * @param ... 可变参数，如"蓝牙", 123
   */
  static void printf(const char* format, ...);

  /**
   * @brief 加锁的换行打印（线程安全）
   * @param msg 要打印的字符串
   */
  static void println(const String& msg);

  /**
   * @brief 检查互斥锁是否有效（调试用）
   * @return true=锁有效，false=锁无效
   */
  static bool isLockValid() { return s_serial_mutex != NULL; }

private:
  // 私有构造函数：禁止实例化（工具类无需对象）
  LogUtils() = delete;
  // 禁止拷贝/赋值
  LogUtils(const LogUtils&) = delete;
  LogUtils& operator=(const LogUtils&) = delete;

  // 静态成员：全局唯一的串口互斥锁（封装在类内，外部不可修改）
  static SemaphoreHandle_t s_serial_mutex;
  // 静态成员：标记是否已初始化（避免重复初始化）
  static bool s_is_initialized;
};

#endif // LOG_UTILS_H