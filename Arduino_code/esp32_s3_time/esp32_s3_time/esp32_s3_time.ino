#include "FencingTimer.h"

FencingTimer fencingTimer;

void setup() {
  Serial.begin(9600);
  fencingTimer.begin();

  Serial.println(F("--- Fencing Timer (Bout Logic) ---"));
  Serial.println(F("s: Start/Pause | n: Next Phase | r: Reset"));
}

void loop() {
  fencingTimer.update();

  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == '\n' || cmd == '\r') return;

    switch (cmd) {
      case 's': 
        fencingTimer.toggleStartPause();
        Serial.println(fencingTimer.isTimerRunning() ? F(">> RUNNING") : F("|| PAUSED"));
        break;

      case 'n': 
        fencingTimer.nextPhase();
        if (fencingTimer.isResting()) {
            Serial.println(F("Phase: RESTING (1:00)"));
        } else {
            Serial.println(F("Phase: BACK TO MATCH (Resuming time)"));
        }
        break;

      case 'r': 
        fencingTimer.resetTimer();
        Serial.println(F("Status: RESET"));
        break;

      case 'd': 
        fencingTimer.toggleDurationMode();
        Serial.print(F("Mode: "));
        Serial.print(fencingTimer.getCurrentDurationMode());
        Serial.println(F(" min"));
        break;
    }
  }
}