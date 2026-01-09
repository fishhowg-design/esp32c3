#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>


#define DEBOUNCE_DELAY 20  // 消抖时间（20ms，适配重剑接触的机械抖动）
#define DEVICE_NAME "Epee_Red" // 另一块改为 "Epee_Green"
#define SENSOR_PIN  12
#define HIT_LED     4
#define BUZZER      5
#define BT_LED      19


// 状态变量
bool hitState = false;       // 是否击中（false=未击中，true=击中）
bool lastHitState = false;   // 上一次击中状态
unsigned long lastDebounceTime = 0;

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        digitalWrite(BT_LED, HIGH);
    };
    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        digitalWrite(BT_LED, LOW);
        pServer->getAdvertising()->start();
    }
};

void setup() {
    pinMode(SENSOR_PIN, INPUT_PULLUP);
    pinMode(HIT_LED, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(BT_LED, OUTPUT);

    BLEDevice::init(DEVICE_NAME);
    BLEServer *pServer = BLEDevice::createServer();
    
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService("4fafc201-1fb5-459e-8fcc-c5c9c331914b");

    pCharacteristic = pService->createCharacteristic(
                        "beb5483e-36e1-4688-b7f5-ea07361b26a8",
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
    pCharacteristic->addDescriptor(new BLE2902());
    pService->start();
    pServer->getAdvertising()->start();
}

void loop() {
    // 1. 读取当前GPIO状态（INPUT_PULLUP模式：击中=LOW，未击中=HIGH）
    bool currentReading = digitalRead(SENSOR_PIN);



    currentReading = !currentReading; // 反转：true=击中，false=未击中（符合直觉）

    // 2. 消抖处理（过滤接触抖动/电磁干扰）
    if (currentReading != lastHitState) {
       lastDebounceTime = millis();
     }

    // 3. 稳定状态判定（消抖时间后确认击中/未击中）
    if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    // 状态发生变化（击中/松开）
    if (currentReading != hitState) {
      hitState = currentReading;
      
      // 击中事件触发
      if (hitState) {
        Serial.print("【击中！】时间戳：");
        Serial.println(millis());
        // 可扩展：触发蜂鸣器/LED提示、记录击中次数、上传数据等
        hitEvent(); 
      } else {
        Serial.println("【未击中】");
      }
    }
  }
// 4. 更新上一次状态
  lastHitState = currentReading;

    if (digitalRead(SENSOR_PIN) == LOW) {
        
        Serial.print("digitalRead(SENSOR_PIN) == LOW");
        
        unsigned long hitTime = millis();
        
        // 1. 本地同步反馈
        digitalWrite(HIT_LED, HIGH);
        digitalWrite(BUZZER, HIGH);

        // 2. 发送信号给小程序
        if (deviceConnected) {
            String data = String(hitTime);
            pCharacteristic->setValue(data.c_str());
            pCharacteristic->notify();
        }

        delay(700); // 击中显示持续时间
        digitalWrite(HIT_LED, LOW);
        digitalWrite(BUZZER, LOW);
        delay(300); // 防止抖动
    }
    else
    {
        Serial.print("digitalRead(SENSOR_PIN) != LOW");
    }
}

// 击中事件处理函数（可自定义功能）
void hitEvent() {
  // 示例1：控制蜂鸣器响0.5秒（提示击中）
 // pinMode(LED_D4, OUTPUT);
 // pinMode(LED_D5, OUTPUT);
 // digitalWrite(LED_D4, HIGH);
 // digitalWrite(LED_D5, HIGH);
 //delay(500);
 // digitalWrite(LED_D4, LOW);
 // digitalWrite(LED_D5, LOW);
  
  // 示例2：记录击中次数（全局变量累加）
  static int hitCount = 0;
  hitCount++;
  Serial.print("累计击中次数：");
  Serial.println(hitCount);
}