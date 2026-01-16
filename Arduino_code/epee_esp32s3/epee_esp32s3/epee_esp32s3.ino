// 针对此板的测试代码
#include <Adafruit_NeoPixel.h>

#define RGB_PIN 48 // 此板 WS2812 引脚通常为 48
#define NUMPIXELS 1 

Adafruit_NeoPixel pixels(NUMPIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  pixels.begin(); // 初始化 RGB 灯
  Serial.println("ESP32-S3 启动成功！");
}

void loop() {
  // 红色测试
  pixels.setPixelColor(0, pixels.Color(255, 0, 0));
  pixels.show();
  Serial.println("当前颜色: 红色");
  delay(1000);

  // 绿色测试
  pixels.setPixelColor(0, pixels.Color(0, 255, 0));
  pixels.show();
  Serial.println("当前颜色: 绿色");
  delay(1000);
}