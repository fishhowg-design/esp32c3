#ifndef SCORE_DISPLAY_H
#define SCORE_DISPLAY_H

#include <Arduino.h>

class ScoreDisplay {
public:
    // 构造函数：指定该组模块的引脚
    ScoreDisplay(int dataPin, int clockPin, int latchPin);

    // 初始化硬件
    void begin();

    // 分数操作
    void addScore();      // 加1分
    void subScore();      // 减1分
    void setScore(int s); // 直接设分
    void reset();        // 清零
    int getScore();      // 获取当前分数

private:
    int _dataPin, _clockPin, _latchPin;
    int _currentScore;

    static const uint8_t digitMap[]; // 段码表
    void updateHardware();           // 核心刷新逻辑
};

#endif