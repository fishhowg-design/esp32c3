#include <Arduino.h>
#include "led_controller.h"

void setup() {
  // 初始化LED
  led_init();
  
  // 开机亮白灯
  led_on_boot();
  delay(2000);
  
  // 模拟连上red
  led_connected_red();
  delay(2000);
  
  // 模拟连上green
  led_connected_green();
  delay(2000);
  
  // 模拟双连
  led_connected_both();
  delay(2000);
  
  // 模拟红色击中
  led_hit_red();
  delay(2000);
  
  // 模拟绿色击中
  led_hit_green();
  delay(2000);
}

void loop() {
  // 主循环
}