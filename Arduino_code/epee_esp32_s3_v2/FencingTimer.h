#ifndef FENCING_TIMER_H
#define FENCING_TIMER_H

#include <TM1637Display.h>
#include <Arduino.h>

#define TM1637_DIO_PIN 10
#define TM1637_CLK_PIN 11

#define DURATION_FIE 180
#define DURATION_TRAINING 300
#define DURATION_REST 60

class FencingTimer {
public:
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