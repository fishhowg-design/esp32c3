// 引脚定义（匹配你的硬件）
#define PIN_BUTTON  9       // 按键引脚
#define LED_D4      18      // D4=GPIO12（高电平亮）
#define LED_D5      19      // D5=GPIO13（高电平亮）

// 配置参数
#define BLINK_INTERVAL 1000 // 闪烁间隔（毫秒）
#define DEBOUNCE_DELAY 50   // 消抖时间

// 状态变量
enum BlinkMode { STOP, BLINK_D4, BLINK_D5 }; // 闪烁模式
BlinkMode currentMode = STOP;

int buttonState = HIGH;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long lastBlinkTime = 0;
bool ledD4_State = LOW;  // 初始熄灭
bool ledD5_State = LOW;

void setup() {
  // 初始化引脚
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(LED_D4, OUTPUT);
  pinMode(LED_D5, OUTPUT);
  digitalWrite(LED_D4, ledD4_State);
  digitalWrite(LED_D5, ledD5_State);

  // 串口调试
  Serial.begin(115200);
  Serial.println("按键切换闪烁模式：");
  Serial.println("按1次→D4闪 | 按2次→D5闪 | 按3次→停止");
}

void loop() {
  // ========== 1. 按键消抖 + 模式切换 ==========
  int reading = digitalRead(PIN_BUTTON);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      // 检测按键按下（下降沿）
      if (buttonState == LOW) {
        // 切换模式
        currentMode = (BlinkMode)((currentMode + 1) % 3);
        // 模式切换时重置LED状态
        ledD4_State = LOW;
        ledD5_State = LOW;
        digitalWrite(LED_D4, ledD4_State);
        digitalWrite(LED_D5, ledD5_State);
        // 串口打印当前模式
        switch (currentMode) {
          case STOP:
            Serial.println("当前模式：停止闪烁");
            break;
          case BLINK_D4:
            Serial.println("当前模式：D4闪烁");
            break;
          case BLINK_D5:
            Serial.println("当前模式：D5闪烁");
            break;
        }
      }
    }
  }
  lastButtonState = reading;

  // ========== 2. 非阻塞闪烁逻辑 ==========
  if (millis() - lastBlinkTime >= BLINK_INTERVAL) {
    lastBlinkTime = millis();
    switch (currentMode) {
      case BLINK_D4:
        ledD4_State = !ledD4_State;
        digitalWrite(LED_D4, ledD4_State);
        Serial.print("D4状态：");
        Serial.println(ledD4_State ? "亮" : "灭");
        break;
      case BLINK_D5:
        ledD5_State = !ledD5_State;
        digitalWrite(LED_D5, ledD5_State);
        Serial.print("D5状态：");
        Serial.println(ledD5_State ? "亮" : "灭");
        break;
      default: // STOP模式
        break;
    }
  }
}