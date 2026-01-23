#include "FencingTimer.h"

// 定义按键引脚
const int BTN_S_START  = 7;
const int BTN_N_PHASE  = 15;
const int BTN_R_RESET  = 6;
const int BTN_D_MODE   = 16;

FencingTimer fencingTimer;

// 防抖处理函数
bool isButtonPressed(int pin) {
    if (digitalRead(pin) == LOW) {
        delay(50); // 软件去抖
        if (digitalRead(pin) == LOW) {
            while (digitalRead(pin) == LOW); // 等待按键释放，防止连续触发
            return true;
        }
    }
    return false;
}

void setup() {
    Serial.begin(115200); // S3 建议使用 115200
    
    // 初始化按键引脚为上拉输入
    pinMode(BTN_S_START, INPUT_PULLUP);
    pinMode(BTN_N_PHASE, INPUT_PULLUP);
    pinMode(BTN_R_RESET, INPUT_PULLUP);
    pinMode(BTN_D_MODE,  INPUT_PULLUP);

    fencingTimer.begin();

    Serial.println(F("--- Fencing Timer Hardware Button Test ---"));
    Serial.println(F("Pin 7: Start/Pause | Pin 6: Next Phase"));
    Serial.println(F("Pin 15: Reset      | Pin 16: Mode (3/5m)"));
    Serial.println(F("------------------------------------------"));
}

void loop() {
    // 必须持续调用以驱动倒计时
    fencingTimer.update();

    // 检查按键 7: Start/Pause
    if (isButtonPressed(BTN_S_START)) {
        fencingTimer.toggleStartPause();
        Serial.print(F("[Btn 7] Timer: "));
        Serial.println(fencingTimer.isTimerRunning() ? F("RUNNING") : F("PAUSED"));
    }

    // 检查按键 6: Next Phase
    if (isButtonPressed(BTN_N_PHASE)) {
        fencingTimer.nextPhase();
        if (fencingTimer.isResting()) {
            Serial.println(F("[Btn 6] Phase: RESTING (1:00) - Auto Started"));
        } else {
            Serial.println(F("[Btn 6] Phase: BACK TO MATCH (Resuming)"));
        }
    }

    // 检查按键 15: Reset
    if (isButtonPressed(BTN_R_RESET)) {
        fencingTimer.resetTimer();
        Serial.println(F("[Btn 15] Status: RESET"));
    }

    // 检查按键 16: Toggle Mode
    if (isButtonPressed(BTN_D_MODE)) {
        fencingTimer.toggleDurationMode();
        Serial.print(F("[Btn 16] Mode Switched: "));
        Serial.print(fencingTimer.getCurrentDurationMode());
        Serial.println(F(" min"));
    }
}