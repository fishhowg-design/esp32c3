#include <Arduino.h>

void setup() {
  // 1. 初始化串口
  Serial.begin(115200);

  // 2. 关键：等待 USB 串口稳定
  // S3 上电后需要一点时间建立 USB 握手
  for(int i = 0; i < 5; i++) {
    delay(1000);
  }
}

void loop() {
  // 打印当前运行时间，方便确认芯片没有死机
  Serial.print("[系统正常] 运行毫秒数: ");
  Serial.println(millis());

  // 让板载 LED 闪烁（如果是 S3 SuperMini，通常 GPIO 47 或 48 有灯）
  // 这里我们只看串口输出
  delay(1000); 
}