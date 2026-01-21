#include "ScoreDisplay.h"

// 构造函数：初始化分数和TM1637对象
ScoreDisplay::ScoreDisplay() 
  : display(TM1637_CLK_PIN, TM1637_DIO_PIN),
    redScore(0),
    greenScore(0) {
}

// 初始化显示：设置亮度+清空+显示初始00:00
void ScoreDisplay::begin() {
  display.setBrightness(4); // 最大亮度
  display.clear();
  updateDisplay(); // 显示初始比分 00:00
}

// 红方加分（不超过99）
void ScoreDisplay::addRedScore() {
  if (redScore < MAX_SCORE) {
    redScore++;
    updateDisplay();
  }
}

// 红方减分（不低于0）
void ScoreDisplay::subRedScore() {
  if (redScore > MIN_SCORE) {
    redScore--;
    updateDisplay();
  }
}

// 红方分数归零
void ScoreDisplay::resetRedScore() {
  redScore = 0;
  updateDisplay();
}

// 绿方加分（不超过99）
void ScoreDisplay::addGreenScore() {
  if (greenScore < MAX_SCORE) {
    greenScore++;
    updateDisplay();
  }
}

// 绿方减分（不低于0）
void ScoreDisplay::subGreenScore() {
  if (greenScore > MIN_SCORE) {
    greenScore--;
    updateDisplay();
  }
}

// 绿方分数归零
void ScoreDisplay::resetGreenScore() {
  greenScore = 0;
  updateDisplay();
}

// 全部比分归零
void ScoreDisplay::resetAllScore() {
  redScore = 0;
  greenScore = 0;
  updateDisplay();
}

// 手动设置比分（用于特殊场景，如直接设置 10:05）
void ScoreDisplay::setScore(int red, int green) {
  // 边界保护：分数限制在0-99
  redScore = (red >= 0 && red <= 99) ? red : 0;
  greenScore = (green >= 0 && green <= 99) ? green : 0;
  updateDisplay();
}

// 核心：更新数码管显示（红:绿，中间冒号常亮）
void ScoreDisplay::updateDisplay() {
  // 组合比分：红方*100 + 绿方 → 例如红12，绿34 → 1234
  int totalScore = redScore * 100 + greenScore;
  // 显示格式：XX:XX（0x80/0x40 是冒号掩码，根据你的模块选一个）
  // 若冒号不亮，把 0x80 换成 0x40 试试
  display.showNumberDecEx(totalScore, 0x40, true, 4, 0);
}