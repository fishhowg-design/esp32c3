#include "FencingTimer.h"

FencingTimer::FencingTimer() 
  : display(TM1637_CLK_PIN, TM1637_DIO_PIN), 
    isRunning(false), 
    isRestMode(false), 
    lastTick(0),
    currentMaxDuration(DURATION_FIE) 
{
    remainingSeconds = currentMaxDuration;
    savedMatchSeconds = currentMaxDuration; // 初始化断点
}

void FencingTimer::begin() {
    display.setBrightness(0x0f);
    refreshDisplay();
}

void FencingTimer::update() {
    if (!isRunning || remainingSeconds <= 0) return;

    unsigned long currentMillis = millis();
    if (currentMillis - lastTick >= 1000) {
        lastTick = currentMillis;
        remainingSeconds--;
        refreshDisplay();

        if (remainingSeconds <= 0) {
            isRunning = false;
            remainingSeconds = 0;
            // 此处可添加响铃
        }
    }
}

void FencingTimer::toggleStartPause() {
    if (remainingSeconds <= 0 && !isRestMode) return; 
    isRunning = !isRunning;
    if (isRunning) lastTick = millis();
}

void FencingTimer::resetTimer() {
    isRunning = false;
    // 重置逻辑：如果是休息中重置，回到60秒；如果是比赛中重置，回到完整局时长
    if (isRestMode) {
        remainingSeconds = DURATION_REST;
    } else {
        remainingSeconds = currentMaxDuration;
        savedMatchSeconds = currentMaxDuration; 
    }
    refreshDisplay();
}

// 【逻辑更新】
void FencingTimer::nextPhase() {
    if (!isRestMode) {
        // --- 离开比赛，进入休息 ---
        savedMatchSeconds = remainingSeconds; // 核心：保存当前比赛还没跑完的时间
        
        isRestMode = true;
        remainingSeconds = DURATION_REST;
        isRunning = true; // 休息自动开始
        lastTick = millis();
    } else {
        // --- 离开休息，重回比赛 ---
        isRestMode = false;
        isRunning = false; // 比赛等待开始

        if (savedMatchSeconds > 0) {
            // 如果比赛时间没用完，恢复断点
            remainingSeconds = savedMatchSeconds;
        } else {
            // 如果时间用完了，加载全新的局时长
            remainingSeconds = currentMaxDuration;
            savedMatchSeconds = currentMaxDuration;
        }
    }
    refreshDisplay();
}

void FencingTimer::toggleDurationMode() {
    if (isRestMode) isRestMode = false;

    if (currentMaxDuration == DURATION_FIE) {
        currentMaxDuration = DURATION_TRAINING;
    } else {
        currentMaxDuration = DURATION_FIE;
    }
    
    // 切换模式意味着彻底重赛
    savedMatchSeconds = currentMaxDuration;
    resetTimer();
}

void FencingTimer::refreshDisplay() {
    int minutes = remainingSeconds / 60;
    int seconds = remainingSeconds % 60;
    int displayValue = (minutes * 100) + seconds;
    display.showNumberDecEx(displayValue, 0x40, true);
}

bool FencingTimer::isTimerRunning() { return isRunning; }
int FencingTimer::getCurrentDurationMode() { return currentMaxDuration / 60; }
bool FencingTimer::isResting() { return isRestMode; }