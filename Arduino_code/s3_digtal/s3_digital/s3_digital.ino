#include "ScoreDisplay.h"

// 创建比分显示对象
ScoreDisplay scoreBoard;

void setup() {
  Serial.begin(115200);
  
  // 初始化比分显示
  scoreBoard.begin();
  Serial.println("比分系统初始化完成，初始比分 00:00");

  // 测试示例：模拟比分变化
  delay(2000);
  scoreBoard.addRedScore();  // 红方+1 → 01:00
  Serial.println("红方+1 → 01:00");
  delay(1000);
  scoreBoard.addGreenScore();// 绿方+1 → 01:01
  Serial.println("绿方+1 → 01:01");
  delay(1000);
  scoreBoard.addGreenScore();// 绿方+1 → 01:02
  Serial.println("绿方+1 → 01:02");
  delay(1000);
  scoreBoard.subRedScore();  // 红方-1 → 00:02
  Serial.println("红方-1 → 00:02");
  delay(2000);
  scoreBoard.resetAllScore();// 全部归零 → 00:00
  Serial.println("全部归零 → 00:00");
}

void loop() {
  // 实际使用时，你可以在这里接按键/串口/无线信号来控制比分：
  // 示例：串口控制（可根据需求扩展）
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    switch(cmd) {
      case 'r': scoreBoard.addRedScore(); break;    // 串口发'r' → 红方+1
      case 'R': scoreBoard.subRedScore(); break;    // 串口发'R' → 红方-1
      case 'g': scoreBoard.addGreenScore(); break;  // 串口发'g' → 绿方+1
      case 'G': scoreBoard.subGreenScore(); break;  // 串口发'G' → 绿方-1
      case '0': scoreBoard.resetAllScore(); break;  // 串口发'0' → 全部归零
    }
  }
  delay(100);
}