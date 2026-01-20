#include "led_controller.h"

// 全局对象实例化
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
SemaphoreHandle_t led_mutex = NULL;

/**
 * @brief 初始化LED和互斥锁
 */
void led_init() {
  // 1. 创建互斥锁（FreeRTOS原生API）
  led_mutex = xSemaphoreCreateMutex();
  if (led_mutex == NULL) {
    Serial.println("LED互斥锁创建失败！");
    while(1); // 锁创建失败时卡死，避免后续错误
  }

  // 2. 初始化LED
  pixels.begin();
  pixels.setBrightness(LED_BRIGHTNESS);
  pixels.clear();
  pixels.show();
}

/**
 * @brief 核心：带锁保护的LED颜色设置
 * @param r/g/b 颜色值
 */
void led_set_color(uint8_t r, uint8_t g, uint8_t b) {
  // 申请锁（最多等待10ms，避免死锁）
  if (xSemaphoreTake(led_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // 临界区：只有拿到锁的核心能执行LED操作
    pixels.clear();
    pixels.setPixelColor(0, r, g, b);
    pixels.show();
    
    // 释放锁，让其他核心可以获取
    xSemaphoreGive(led_mutex);
  } else {
    // 锁获取失败（超时），打印错误日志
    Serial.println("LED锁获取超时，操作被跳过！");
  }
}

// 以下函数逻辑不变，因为核心的加锁逻辑已封装在led_set_color中
void led_on_boot() {
  led_set_color(LED_BRIGHTNESS, LED_BRIGHTNESS, LED_BRIGHTNESS);
}

void led_connected_red() {
  led_set_color(LED_BRIGHTNESS, LED_BRIGHTNESS/2, 0);
}

void led_connected_green() {
  led_set_color(0, LED_BRIGHTNESS, LED_BRIGHTNESS);
}

void led_connected_both() {
  led_set_color(0, 0, LED_BRIGHTNESS);
}

void led_hit_red() {
  led_set_color(LED_BRIGHTNESS, 0, 0);
}

void led_hit_green() {
  led_set_color(0, LED_BRIGHTNESS, 0);
}