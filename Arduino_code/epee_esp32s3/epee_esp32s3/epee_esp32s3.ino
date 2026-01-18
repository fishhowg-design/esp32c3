// ESP32S3 非阻塞 日志打印（永不崩溃+日志稳定）最优版
unsigned long logTime = 0;  // 记录上次打印日志的时间
const int logInterval = 1000; // 日志打印间隔：1000ms=1秒

void setup() {
  disableCore0WDT();
  disableCore1WDT();
  Serial.begin(115200);
  while(!Serial);
  Serial.println("ESP32S3 非阻塞日志启动 ✅");
}

void loop() {
  // 非阻塞判断：到时间才打印日志，CPU全程不卡死
  if(millis() - logTime >= logInterval){
    logTime = millis(); // 更新时间戳
    Serial.println("【非阻塞日志】运行正常，时间：" + String(millis()) + "ms");
  }
}