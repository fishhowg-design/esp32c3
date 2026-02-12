#include "FencingCore.h"
#include "led_controller.h"
#include "log_utils.h"
#include <FreeRTOS.h>
#include <task.h>

// ===================== 引脚静态变量定义（默认0，setup 中会赋值）=====================
int FencingCore::PIN_LED_RED = 0;
int FencingCore::PIN_LED_GREEN = 0;
int FencingCore::PIN_BUZZER = 0;
int FencingCore::PIN_BTN_NEXT = 0;
int FencingCore::PIN_BTN_RESET = 0;
int FencingCore::PIN_BTN_PHASE = 0;
int FencingCore::PIN_BTN_MODE = 0;
int FencingCore::PIN_BTN_RED_ADD = 0;
int FencingCore::PIN_BTN_RED_SUB = 0;
int FencingCore::PIN_BTN_GREEN_ADD = 0;
int FencingCore::PIN_BTN_GREEN_SUB = 0;
int FencingCore::PIN_TIMER_CLK = 0;
int FencingCore::PIN_TIMER_DIO = 0;


const unsigned long FencingCore::LIGHT_DURATION = 3000;
const unsigned long FencingCore::BEEP_DURATION = 800;
const int FencingCore::HIT_TIME_WINDOW = 40;
const int FencingCore::HIT_EVAL_DELAY = 45;

extern void lockedPrintln(String msg);
extern void lockedPrintf(const char* format, ...);


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

      // 第一步：初始化日志工具（仅需调用一次，全局生效）
  if (!LogUtils::init(115200)) {
    while(1); // 初始化失败，卡死避免后续错误
  }
    // 初始化引脚（不变）
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    pinMode(PIN_BTN_RESET, INPUT_PULLUP);
    pinMode(PIN_BTN_PHASE, INPUT_PULLUP);
    pinMode(PIN_BTN_MODE, INPUT_PULLUP);
    pinMode(PIN_BTN_RED_ADD, INPUT_PULLUP);
    pinMode(PIN_BTN_RED_SUB, INPUT_PULLUP);
    pinMode(PIN_BTN_GREEN_ADD, INPUT_PULLUP);
    pinMode(PIN_BTN_GREEN_SUB, INPUT_PULLUP);

    // 初始化显示和计时器（修复：移除多余的引脚参数）
    m_scoreDisplay.begin();
    m_fencingTimer.begin(); // 原FencingTimer::begin()无参，匹配接口

    // 全局重置（不变）
    resetMatch(true);
    LogUtils::println("[FencingCore] 比分+计时+击中判定系统初始化完成");
}

// ===================== 新增：8个按钮独立处理函数实现 =====================
void FencingCore::handleBtnNext() {
    // 防抖（和物理按键逻辑一致）
    vTaskDelay(pdMS_TO_TICKS(50));
    if (m_isLocked) {
        LogUtils::println("[按键] 下一分准备 (灭灯)");
        resetMatch(false);
        if (!m_fencingTimer.isTimerRunning()) {
            m_fencingTimer.toggleStartPause();
            LogUtils::println("[计时] 恢复比赛计时");
        }
    } else {
        m_fencingTimer.toggleStartPause();
        LogUtils::printf("[计时] %s\n", m_fencingTimer.isTimerRunning() ? "开始" : "暂停");
    }
}

void FencingCore::handleBtnReset() {
    vTaskDelay(pdMS_TO_TICKS(50));
    LogUtils::println("[按键] 全局重置 (分数+时间)");
    resetMatch(true);
    m_fencingTimer.resetTimer();
}

void FencingCore::handleBtnPhase() {
    vTaskDelay(pdMS_TO_TICKS(50));
    m_fencingTimer.nextPhase();
    LogUtils::println(m_fencingTimer.isResting() ? "[计时] 进入休息模式" : "[计时] 重回比赛模式");
}

void FencingCore::handleBtnMode() {
    vTaskDelay(pdMS_TO_TICKS(50));
    m_fencingTimer.toggleDurationMode();
    LogUtils::printf("[计时] 切换至 %d 分钟赛制\n", m_fencingTimer.getCurrentDurationMode());
}

void FencingCore::handleBtnRedAdd() {
    vTaskDelay(pdMS_TO_TICKS(50));
    LogUtils::println("[按键] 手动红方+1分");
    m_scoreManager.addRedScore();
}

void FencingCore::handleBtnRedSub() {
    vTaskDelay(pdMS_TO_TICKS(50));
    LogUtils::println("[按键] 手动红方-1分");
    m_scoreManager.subtractRedScore();
}

void FencingCore::handleBtnGreenAdd() {
    vTaskDelay(pdMS_TO_TICKS(50));
    LogUtils::println("[按键] 手动绿方+1分");
    m_scoreManager.addGreenScore();
}

void FencingCore::handleBtnGreenSub() {
    vTaskDelay(pdMS_TO_TICKS(50));
    LogUtils::println("[按键] 手动绿方-1分");
    m_scoreManager.subtractGreenScore();
}

// ===================== 调整checkButtons函数（调用封装的函数）=====================
void FencingCore::checkButtons() {
    static bool lastNext = HIGH, lastReset = HIGH, lastPhase = HIGH, lastMode = HIGH;
    static bool lastRedAdd = HIGH, lastRedSub = HIGH, lastGreenAdd = HIGH, lastGreenSub = HIGH;

    // BTN_NEXT
    bool currNext = digitalRead(PIN_BTN_NEXT);
    if (lastNext == HIGH && currNext == LOW) {
        handleBtnNext(); // 调用封装的函数
    }
    lastNext = currNext;

    // BTN_RESET
    bool currReset = digitalRead(PIN_BTN_RESET);
    if (lastReset == HIGH && currReset == LOW) {
        handleBtnReset(); // 调用封装的函数
    }
    lastReset = currReset;

    // BTN_PHASE
    bool currPhase = digitalRead(PIN_BTN_PHASE);
    if (lastPhase == HIGH && currPhase == LOW) {
        handleBtnPhase(); // 调用封装的函数
    }
    lastPhase = currPhase;

    // BTN_MODE
    bool currMode = digitalRead(PIN_BTN_MODE);
    if (lastMode == HIGH && currMode == LOW) {
        handleBtnMode(); // 调用封装的函数
    }
    lastMode = currMode;

    // 手动加减分
    bool currRedAdd = digitalRead(PIN_BTN_RED_ADD);
    bool currRedSub = digitalRead(PIN_BTN_RED_SUB);
    bool currGreenAdd = digitalRead(PIN_BTN_GREEN_ADD);
    bool currGreenSub = digitalRead(PIN_BTN_GREEN_SUB);

    if (lastRedAdd == HIGH && currRedAdd == LOW) {
        handleBtnRedAdd(); // 调用封装的函数
    }
    if (lastRedSub == HIGH && currRedSub == LOW) {
        handleBtnRedSub(); // 调用封装的函数
    }
    if (lastGreenAdd == HIGH && currGreenAdd == LOW) {
        handleBtnGreenAdd(); // 调用封装的函数
    }
    if (lastGreenSub == HIGH && currGreenSub == LOW) {
        handleBtnGreenSub(); // 调用封装的函数
    }

    lastRedAdd = currRedAdd;
    lastRedSub = currRedSub;
    lastGreenAdd = currGreenAdd;
    lastGreenSub = currGreenSub;
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
        digitalWrite(PIN_LED_RED, LOW);
        digitalWrite(PIN_LED_GREEN, LOW);
        m_effectActive = false;
        LogUtils::println("[系统] 声光效果结束，等待重置");
    }
}

void FencingCore::setRedHit() {
    if (!m_isLocked) {
        m_redHitRaw = true;
        m_redHitTimestamp = millis();
        LogUtils::printf("[信号] red击中信号触发 时间戳: %u\n", m_redHitTimestamp);
    }
}

void FencingCore::setGreenHit() {
    if (!m_isLocked) {
        m_greenHitRaw = true;
        m_greenHitTimestamp = millis();
        LogUtils::printf("[信号] green击中信号触发 时间戳: %u\n", m_greenHitTimestamp);
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
    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    m_effectActive = false;

    int red = m_scoreManager.getRedScore();
    int green = m_scoreManager.getGreenScore();
    LogUtils::printf("[系统] %s | 比分: 红%d - 绿%d\n", total ? "全部重置" : "下一分开始", red, green);
}

void FencingCore::onScoreChanged(int redScore, int greenScore, bool isReset) {
    if (isReset) {
        LogUtils::printf("[比分回调] 分数重置 | 红%d - 绿%d\n", redScore, greenScore);
        m_scoreDisplay.begin();
        m_scoreDisplay.setScore(redScore, greenScore);
        m_fencingTimer.resetTimer();
    } else {
        LogUtils::printf("[比分回调] 分数更新 | 红%d - 绿%d\n", redScore, greenScore);
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
        digitalWrite(PIN_LED_RED, HIGH);
        digitalWrite(PIN_LED_GREEN, HIGH);
        LogUtils::printf("[裁判] 双方同时击中! (时间差: %d 毫秒)\n", abs((int)(m_redHitTimestamp - m_greenHitTimestamp)));
    } else if (m_redHitReceived) {
        m_scoreManager.addRedScore();
        digitalWrite(PIN_LED_RED, HIGH);
        LogUtils::println("[裁判] red得分");
    } else if (m_greenHitReceived) {
        m_scoreManager.addGreenScore();
        digitalWrite(PIN_LED_GREEN, HIGH);
        LogUtils::println("[裁判] green得分");
    }
    
    int red = m_scoreManager.getRedScore();
    int green = m_scoreManager.getGreenScore();
    LogUtils::printf("[比分] red %d : %d green\n", red, green);
}