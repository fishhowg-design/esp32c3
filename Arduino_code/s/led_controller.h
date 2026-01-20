#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
// 引入FreeRTOS头文件，用于互斥锁
#include "freertos/semphr.h"

// ESP32-S3板载RGB LED引脚（根据实际硬件调整，常见为GPIO48）
#define LED_PIN        48
#define LED_COUNT      1
#define LED_BRIGHTNESS 70

// NeoPixel对象
extern Adafruit_NeoPixel pixels;
// 互斥锁句柄，保护LED操作
extern SemaphoreHandle_t led_mutex;

// 初始化LED控制器（含锁初始化）
void led_init();

// 各状态控制函数（与之前一致）
void led_on_boot();
void led_connected_red();
void led_connected_green();
void led_connected_both();
void led_hit_red();
void led_hit_green();

// 辅助函数
void led_set_color(uint8_t r, uint8_t g, uint8_t b);

#endif // LED_CONTROLLER_H