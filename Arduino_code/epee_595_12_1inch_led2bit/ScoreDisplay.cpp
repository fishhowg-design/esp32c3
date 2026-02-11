#include "ScoreDisplay.h"

// 共阴极标准段码 (0-9)
const uint8_t ScoreDisplay::digitMap[] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

ScoreDisplay::ScoreDisplay(int dataPin, int clockPin, int latchPin) {
    _dataPin = dataPin;
    _clockPin = clockPin;
    _latchPin = latchPin;
    _currentScore = 0;
}

void ScoreDisplay::begin() {
    pinMode(_dataPin, OUTPUT);
    pinMode(_clockPin, OUTPUT);
    pinMode(_latchPin, OUTPUT);
    updateHardware();
}

void ScoreDisplay::addScore() {
    if (_currentScore < 99) _currentScore++;
    updateHardware();
}

void ScoreDisplay::subScore() {
    if (_currentScore > 0) _currentScore--;
    updateHardware();
}

void ScoreDisplay::setScore(int s) {
    _currentScore = constrain(s, 0, 99);
    updateHardware();
}

void ScoreDisplay::reset() {
    _currentScore = 0;
    updateHardware();
}

int ScoreDisplay::getScore() {
    return _currentScore;
}

void ScoreDisplay::updateHardware() {
    int tens = _currentScore / 10;
    int ones = _currentScore % 10;

    digitalWrite(_latchPin, LOW);

    // 发送顺序：通常先发送的是离 SDI 引脚远的那个数字
    // 假设你的模块级联顺序是 [十位] -> [个位]
    // 如果数字显示反了，交换下面两行 shiftOut 的顺序即可
    
    // 1. 发送个位 (如果是级联两个 595，这会流向第二个芯片)
    shiftOut(_dataPin, _clockPin, MSBFIRST, ~digitMap[ones]); 
    
    // 2. 发送十位 (留在第一个芯片)
    // 击剑建议：如果是 0-9 分，十位可以显示 0，或者你可以修改成显示空白
    shiftOut(_dataPin, _clockPin, MSBFIRST, ~digitMap[tens]); 

    digitalWrite(_latchPin, HIGH);
}