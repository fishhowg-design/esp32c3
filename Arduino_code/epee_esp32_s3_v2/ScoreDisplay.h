#ifndef SCORE_DISPLAY_H
#define SCORE_DISPLAY_H

#include <TM1637Display.h>

// 引脚定义（适配你的接线：12=DIO，13=CLK）
#define TM1637_DIO_PIN 12
#define TM1637_CLK_PIN 13

// 比分最大值（4位数码管，左右各两位，00:00 ~ 99:99）
#define MAX_SCORE 99
#define MIN_SCORE 0

class ScoreDisplay {
public:
  // 构造函数：初始化TM1637和比分
  ScoreDisplay();

  // 初始化显示（需在setup中调用）
  void begin();

  // 红方（左）操作
  void addRedScore();   // 红方加分
  void subRedScore();   // 红方减分
  void resetRedScore(); // 红方分数归零

  // 绿方（右）操作
  void addGreenScore(); // 绿方加分
  void subGreenScore(); // 绿方减分
  void resetGreenScore();// 绿方分数归零

  // 全部归零
  void resetAllScore();

  // 手动设置比分（可选）
  void setScore(int red, int green);

private:
  TM1637Display display; // TM1637显示对象
  int redScore;          // 红方分数（0-99）
  int greenScore;        // 绿方分数（0-99）

  // 内部方法：更新显示（核心逻辑）
  void updateDisplay();
};

#endif // SCORE_DISPLAY_H