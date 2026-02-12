#ifndef FENCING_TIMER_H
#define FENCING_TIMER_H

#include <TM1637Display.h>
#include <Arduino.h>

// 这些将在 .ino 中定义并在运行时赋值到下面的静态变量
class FencingTimer {
public:
  // 使用不同的标识避免与宏冲突
  static int FT_TM1637_DIO_PIN;
  static int FT_TM1637_CLK_PIN;

  static int FT_DURATION_FIE;
  static int FT_DURATION_TRAINING;
  static int FT_DURATION_REST;

  FencingTimer();
  void begin();
  void update();
  void toggleStartPause();
  void resetTimer();
  void nextPhase(); // 核心逻辑修改
  void toggleDurationMode();

  bool isTimerRunning() const;
  int getCurrentDurationMode();
  bool isResting(); 

private:
  TM1637Display display;

  bool isRunning;
  bool isRestMode;
  unsigned long lastTick;
  int remainingSeconds;     // 当前倒计时显示的秒数
  int currentMaxDuration;   // 预设时长 (180/300)
  int savedMatchSeconds;    // 【新增】保存比赛断点时间

  void refreshDisplay();
};

#endif