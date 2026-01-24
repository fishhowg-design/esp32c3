#include "FencingCore.h"
#include "led_controller.h"
#include <FreeRTOS.h>
#include <task.h>

// ===================== 常量初始化（不变）=====================
const int FencingCore::PIN_RED_LED = 4;
const int FencingCore::PIN_GRN_LED = 5;
const int FencingCore::PIN_BUZZER = 3;
const int FencingCore::BTN_NEXT = 7;
const int FencingCore::BTN_RESET = 6;
const int FencingCore::BTN_PHASE = 15;
const int FencingCore::BTN_MODE = 16;
const int FencingCore::BTN_RED_ADD = 14;
const int FencingCore::BTN_RED_SUB = 9;
const int FencingCore::BTN_GREEN_ADD = 17;
const int FencingCore::BTN_GREEN_SUB = 18;
const int FencingCore::TIMER_CLK_PIN = 1;
const int FencingCore::TIMER_DIO_PIN = 2;

const unsigned long FencingCore::LIGHT_DURATION = 3000;
const unsigned long FencingCore::BEEP_DURATION = 800;
const int FencingCore::HIT_TIME_WINDOW = 40;
const int FencingCore::HIT_EVAL_DELAY = 45;

// ===================== 单例实例初始化（不变）=====================
FencingCore* FencingCore::s_instance = nullptr;
FencingCore* FencingCore::getInstance() {
    if (s_instance == nullptr) {
        s_instance = new FencingCore();
    }
    return s_instance;
}

// ===================== 构造函数（修复回调注册）=====================
FencingCore::FencingCore()
    : m_redHitRaw(false)
    , m_greenHitRaw(false)
    , m_redHitTimestamp(0)
    , m_greenHitTimestamp(0)
    , m_firstHitTime(0)
    , m_isLocked(false)
    , m_redHitReceived(false)
    , m_greenHitReceived(false)
    , m_effectActive(false)
    , m_hitEffectStartTime(0) {
    // 修复：注册静态回调函数（适配普通函数指针）
    m_scoreManager.setScoreChangeCallback(staticScoreChangeCallback);
}

// ===================== 静态回调函数（核心修复）=====================
void FencingCore::staticScoreChangeCallback(int red, int green, bool isReset) {
    // 静态函数通过单例访问成员方法
    FencingCore::getInstance()->onScoreChanged(red, green, isReset);
}

// ===================== init方法（修复begin参数）=====================
void FencingCore::init() {
    // 初始化引脚（不变）
    pinMode(PIN_RED_LED, OUTPUT);
    pinMode(PIN_GRN_LED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(BTN_NEXT, INPUT_PULLUP);
    pinMode(BTN_RESET, INPUT_PULLUP);
    pinMode(BTN_PHASE, INPUT_PULLUP);
    pinMode(BTN_MODE, INPUT_PULLUP);
    pinMode(BTN_RED_ADD, INPUT_PULLUP);
    pinMode(BTN_RED_SUB, INPUT_PULLUP);
    pinMode(BTN_GREEN_ADD, INPUT_PULLUP);
    pinMode(BTN_GREEN_SUB, INPUT_PULLUP);

    // 初始化显示和计时器（修复：移除多余的引脚参数）
    m_scoreDisplay.begin();
    m_fencingTimer.begin(); // 原FencingTimer::begin()无参，匹配接口

    // 全局重置（不变）
    resetMatch(true);
    Serial.println("[FencingCore] 比分+计时+击中判定系统初始化完成");
}

// ===================== 其他方法（完全不变，无需修改）=====================
void FencingCore::updateTimer() {
    m_fencingTimer.update();
}

void FencingCore::processHitDetection() {
    if (m_isLocked) return;
    if (!m_fencingTimer.isTimerRunning()) {
        m_redHitRaw = false;
        m_greenHitRaw = false;
        return;
    }

    if (m_redHitRaw) {
        if (m_firstHitTime == 0) m_firstHitTime = m_redHitTimestamp;
        if (m_redHitTimestamp - m_firstHitTime <= HIT_TIME_WINDOW) {
            m_redHitReceived = true;
        }
        m_redHitRaw = false;
    }

    if (m_greenHitRaw) {
        if (m_firstHitTime == 0) m_firstHitTime = m_greenHitTimestamp;
        if (m_greenHitTimestamp - m_firstHitTime <= HIT_TIME_WINDOW) {
            m_greenHitReceived = true;
        }
        m_greenHitRaw = false;
    }

    if (m_firstHitTime > 0 && (millis() - m_firstHitTime > HIT_EVAL_DELAY)) {
        evaluateHit();
    }
}

void FencingCore::handleHitEffects() {
    if (!m_effectActive) return;
    unsigned long elapsed = millis() - m_hitEffectStartTime;
    if (elapsed > BEEP_DURATION) digitalWrite(PIN_BUZZER, LOW);
    if (elapsed > LIGHT_DURATION) {
        digitalWrite(PIN_RED_LED, LOW);
        digitalWrite(PIN_GRN_LED, LOW);
        m_effectActive = false;
        Serial.println("[系统] 声光效果结束，等待重置");
    }
}

void FencingCore::checkButtons() {
    static bool lastNext = HIGH, lastReset = HIGH, lastPhase = HIGH, lastMode = HIGH;
    static bool lastRedAdd = HIGH, lastRedSub = HIGH, lastGreenAdd = HIGH, lastGreenSub = HIGH;

    // BTN_NEXT
    bool currNext = digitalRead(BTN_NEXT);
    if (lastNext == HIGH && currNext == LOW) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (digitalRead(BTN_NEXT) == LOW) {
            if (m_isLocked) {
                Serial.println("[按键] 下一分准备 (灭灯)");
                resetMatch(false);
                if (!m_fencingTimer.isTimerRunning()) {
                    m_fencingTimer.toggleStartPause();
                    Serial.println("[计时] 恢复比赛计时");
                }
            } else {
                m_fencingTimer.toggleStartPause();
                Serial.printf("[计时] %s\n", m_fencingTimer.isTimerRunning() ? "开始" : "暂停");
            }
        }
    }
    lastNext = currNext;

    // BTN_RESET
    bool currReset = digitalRead(BTN_RESET);
    if (lastReset == HIGH && currReset == LOW) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (digitalRead(BTN_RESET) == LOW) {
            Serial.println("[按键] 全局重置 (分数+时间)");
            resetMatch(true);
            m_fencingTimer.resetTimer();
        }
    }
    lastReset = currReset;

    // BTN_PHASE
    bool currPhase = digitalRead(BTN_PHASE);
    if (lastPhase == HIGH && currPhase == LOW) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (digitalRead(BTN_PHASE) == LOW) {
            m_fencingTimer.nextPhase();
            Serial.println(m_fencingTimer.isResting() ? "[计时] 进入休息模式" : "[计时] 重回比赛模式");
        }
    }
    lastPhase = currPhase;

    // BTN_MODE
    bool currMode = digitalRead(BTN_MODE);
    if (lastMode == HIGH && currMode == LOW) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (digitalRead(BTN_MODE) == LOW) {
            m_fencingTimer.toggleDurationMode();
            Serial.printf("[计时] 切换至 %d 分钟赛制\n", m_fencingTimer.getCurrentDurationMode());
        }
    }
    lastMode = currMode;

    // 手动加减分
    bool currRedAdd = digitalRead(BTN_RED_ADD);
    bool currRedSub = digitalRead(BTN_RED_SUB);
    bool currGreenAdd = digitalRead(BTN_GREEN_ADD);
    bool currGreenSub = digitalRead(BTN_GREEN_SUB);

    if (lastRedAdd == HIGH && currRedAdd == LOW) {vTaskDelay(50);if(digitalRead(BTN_RED_ADD)==LOW){Serial.println("[按键] 手动红方+1分");m_scoreManager.addRedScore();}}
    if (lastRedSub == HIGH && currRedSub == LOW) {vTaskDelay(50);if(digitalRead(BTN_RED_SUB)==LOW){Serial.println("[按键] 手动红方-1分");m_scoreManager.subtractRedScore();}}
    if (lastGreenAdd == HIGH && currGreenAdd == LOW) {vTaskDelay(50);if(digitalRead(BTN_GREEN_ADD)==LOW){Serial.println("[按键] 手动绿方+1分");m_scoreManager.addGreenScore();}}
    if (lastGreenSub == HIGH && currGreenSub == LOW) {vTaskDelay(50);if(digitalRead(BTN_GREEN_SUB)==LOW){Serial.println("[按键] 手动绿方-1分");m_scoreManager.subtractGreenScore();}}

    lastRedAdd = currRedAdd;
    lastRedSub = currRedSub;
    lastGreenAdd = currGreenAdd;
    lastGreenSub = currGreenSub;
}

void FencingCore::setRedHit() {
    if (!m_isLocked) {
        m_redHitRaw = true;
        m_redHitTimestamp = millis();
        Serial.printf("[信号] red击中信号触发 时间戳: %u\n", m_redHitTimestamp);
    }
}

void FencingCore::setGreenHit() {
    if (!m_isLocked) {
        m_greenHitRaw = true;
        m_greenHitTimestamp = millis();
        Serial.printf("[信号] green击中信号触发 时间戳: %u\n", m_greenHitTimestamp);
    }
}

void FencingCore::resetMatch(bool total) {
    m_scoreManager.reset(total);
    m_isLocked = false;
    m_redHitReceived = false;
    m_greenHitReceived = false;
    m_firstHitTime = 0;
    m_redHitRaw = false;
    m_greenHitRaw = false;
    digitalWrite(PIN_RED_LED, LOW);
    digitalWrite(PIN_GRN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    m_effectActive = false;

    int red = m_scoreManager.getRedScore();
    int green = m_scoreManager.getGreenScore();
    Serial.printf("[系统] %s | 比分: 红%d - 绿%d\n", total ? "全部重置" : "下一分开始", red, green);
}

void FencingCore::onScoreChanged(int redScore, int greenScore, bool isReset) {
    if (isReset) {
        Serial.printf("[比分回调] 分数重置 | 红%d - 绿%d\n", redScore, greenScore);
        m_scoreDisplay.begin();
        m_scoreDisplay.setScore(redScore, greenScore);
        m_fencingTimer.resetTimer();
    } else {
        Serial.printf("[比分回调] 分数更新 | 红%d - 绿%d\n", redScore, greenScore);
        m_scoreDisplay.setScore(redScore, greenScore);
    }
}

void FencingCore::evaluateHit() {
    m_isLocked = true;
    m_hitEffectStartTime = millis();
    m_effectActive = true;
    digitalWrite(PIN_BUZZER, HIGH);

    if (m_fencingTimer.isTimerRunning()) {
        m_fencingTimer.toggleStartPause();
    }

    if (m_redHitReceived && m_greenHitReceived) {
        m_scoreManager.addBothScores();
        digitalWrite(PIN_RED_LED, HIGH);
        digitalWrite(PIN_GRN_LED, HIGH);
        Serial.printf("[裁判] 双方同时击中! (时间差: %d 毫秒)\n", abs((int)(m_redHitTimestamp - m_greenHitTimestamp)));
    } else if (m_redHitReceived) {
        m_scoreManager.addRedScore();
        digitalWrite(PIN_RED_LED, HIGH);
        Serial.println("[裁判] red得分");
    } else if (m_greenHitReceived) {
        m_scoreManager.addGreenScore();
        digitalWrite(PIN_GRN_LED, HIGH);
        Serial.println("[裁判] green得分");
    }
    
    int red = m_scoreManager.getRedScore();
    int green = m_scoreManager.getGreenScore();
    Serial.printf("[比分] red %d : %d green\n", red, green);
}