#ifndef FENCINGCORE_H
#define FENCINGCORE_H

#include <Arduino.h>
#include "ScoreManager.h"
#include "ScoreDisplay.h"
#include "FencingTimer.h"

class FencingCore {
public:
    // ===================== 常量定义（不变）=====================
    static const int PIN_RED_LED;
    static const int PIN_GRN_LED;
    static const int PIN_BUZZER;
    static const int BTN_NEXT;
    static const int BTN_RESET;
    static const int BTN_PHASE;
    static const int BTN_MODE;
    static const int BTN_RED_ADD;
    static const int BTN_RED_SUB;
    static const int BTN_GREEN_ADD;
    static const int BTN_GREEN_SUB;
    static const int TIMER_CLK_PIN;
    static const int TIMER_DIO_PIN;

    static const unsigned long LIGHT_DURATION;
    static const unsigned long BEEP_DURATION;
    static const int HIT_TIME_WINDOW;
    static const int HIT_EVAL_DELAY;

    // ===================== 单例接口（不变）=====================
    static FencingCore* getInstance();

    // ===================== 核心公有接口（不变）=====================
    void init();
    void updateTimer();
    void processHitDetection();
    void handleHitEffects();
    void checkButtons();
    void setRedHit();
    void setGreenHit();
    void resetMatch(bool total);
    bool isLocked() const { return m_isLocked; }
    bool isTimerRunning() const { return m_fencingTimer.isTimerRunning(); } // const 匹配

private:
    // ===================== 私有成员（不变）=====================
    FencingCore();
    ~FencingCore() = default;
    FencingCore(const FencingCore&) = delete;
    FencingCore& operator=(const FencingCore&) = delete;

    static FencingCore* s_instance;

    ScoreManager m_scoreManager;
    ScoreDisplay m_scoreDisplay;
    FencingTimer m_fencingTimer;

    volatile bool m_redHitRaw;
    volatile bool m_greenHitRaw;
    volatile uint32_t m_redHitTimestamp;
    volatile uint32_t m_greenHitTimestamp;
    unsigned long m_firstHitTime;
    bool m_isLocked;
    bool m_redHitReceived;
    bool m_greenHitReceived;
    bool m_effectActive;
    unsigned long m_hitEffectStartTime;

    // ===================== 内部方法（新增静态回调）=====================
    void onScoreChanged(int redScore, int greenScore, bool isReset);
    void evaluateHit();
    // 静态回调函数（适配ScoreManager的普通函数指针）
    static void staticScoreChangeCallback(int red, int green, bool isReset);
};

#endif // FENCINGCORE_H