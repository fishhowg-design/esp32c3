#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// =====================【参数与引脚】=====================
const int PIN_RED_LED = 4;
const int PIN_GRN_LED = 5;
const int PIN_BUZZER  = 3;
const int BTN_NEXT    = 7;
const int BTN_RESET   = 6;
const int LED_BOARD   = 8;

const unsigned long LIGHT_DURATION = 3000;
const unsigned long BEEP_DURATION  = 800;
const int MAX_CONNECT_RETRY = 5;

static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// --- 串口互斥锁 ---
SemaphoreHandle_t serialMutex;

void lockedPrintf(const char* format, ...) {
    if (serialMutex == NULL) return;
    if (xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) {
        char buffer[128];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        Serial.print(buffer);
        xSemaphoreGive(serialMutex);
    }
}

void lockedPrintln(String msg) {
    if (serialMutex == NULL) return;
    if (xSemaphoreTake(serialMutex, portMAX_DELAY) == pdTRUE) {
        Serial.println(msg);
        xSemaphoreGive(serialMutex);
    }
}

// =====================【跨核同步变量】=====================
volatile bool redHitRaw = false;
volatile bool greenHitRaw = false;
volatile uint32_t redHitTimestamp = 0;
volatile uint32_t greenHitTimestamp = 0;
volatile bool redConnected = false;
volatile bool greenConnected = false;

// =====================【逻辑变量】=====================
int redScore = 0;
int greenScore = 0;
unsigned long firstHitTime = 0;
bool isLocked = false;
bool redHitReceived = false;
bool greenHitReceived = false;
bool effectActive = false;
unsigned long hitEffectStartTime = 0;

static boolean doConnectRed = false;
static boolean doConnectGreen = false;
static BLEAdvertisedDevice* redDevice;
static BLEAdvertisedDevice* greenDevice;
int redRetryCount = 0;
int greenRetryCount = 0;

// =====================【前置函数声明区】=====================
void resetMatch(bool total);
void evaluateHit();
void handleHitEffects();
void checkButtons();
void updateStatusLed();

// =====================【核心功能函数】=====================

void resetMatch(bool total) {
    if (total) { redScore = 0; greenScore = 0; }
    isLocked = false;
    redHitReceived = false;
    greenHitReceived = false;
    firstHitTime = 0;
    redHitRaw = false;
    greenHitRaw = false;
    digitalWrite(PIN_RED_LED, LOW);
    digitalWrite(PIN_GRN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    lockedPrintf("[SYSTEM] %s | Score: R%d - G%d\n", total ? "FULL RESET" : "NEXT POINT", redScore, greenScore);
}

void evaluateHit() {
    isLocked = true;
    hitEffectStartTime = millis();
    effectActive = true;
    digitalWrite(PIN_BUZZER, HIGH);

    if (redHitReceived && greenHitReceived) {
        redScore++; greenScore++;
        digitalWrite(PIN_RED_LED, HIGH);
        digitalWrite(PIN_GRN_LED, HIGH);
        lockedPrintf("[JUDGE] DOUBLE HIT! (Time Diff: %d ms)\n", abs((int)(redHitTimestamp - greenHitTimestamp)));
    } else if (redHitReceived) {
        redScore++;
        digitalWrite(PIN_RED_LED, HIGH);
        lockedPrintln("[JUDGE] RED POINT");
    } else if (greenHitReceived) {
        greenScore++;
        digitalWrite(PIN_GRN_LED, HIGH);
        lockedPrintln("[JUDGE] GREEN POINT");
    }
    lockedPrintf("[SCORE] RED %d : %d GREEN\n", redScore, greenScore);
}

void handleHitEffects() {
    if (!effectActive) return;
    unsigned long elapsed = millis() - hitEffectStartTime;
    if (elapsed > BEEP_DURATION) digitalWrite(PIN_BUZZER, LOW);
    if (elapsed > LIGHT_DURATION) {
        digitalWrite(PIN_RED_LED, LOW);
        digitalWrite(PIN_GRN_LED, LOW);
        effectActive = false;
        lockedPrintln("[SYSTEM] Effect Finished. Waiting for Reset.");
    }
}

void checkButtons() {
    static bool lastNext = HIGH;
    bool currNext = digitalRead(BTN_NEXT);
    if (lastNext == HIGH && currNext == LOW) {
        vTaskDelay(pdMS_TO_TICKS(50)); 
        if (digitalRead(BTN_NEXT) == LOW) {
            lockedPrintln("[INPUT] NEXT Button Pressed");
            resetMatch(false);
        }
    }
    lastNext = currNext;

    static bool lastReset = HIGH;
    bool currReset = digitalRead(BTN_RESET);
    if (lastReset == HIGH && currReset == LOW) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (digitalRead(BTN_RESET) == LOW) {
            lockedPrintln("[INPUT] RESET Button Pressed");
            resetMatch(true);
        }
    }
    lastReset = currReset;
}

void updateStatusLed() {
    static unsigned long lastStatusPrint = 0;
    if (redConnected && greenConnected) {
        digitalWrite(LED_BOARD, LOW); 
    } else {
        digitalWrite(LED_BOARD, (millis() % 500 < 100) ? LOW : HIGH);
    }
    /*
    // 每 5 秒打印一次连接状态，避免刷屏
    if (millis() - lastStatusPrint > 5000) {
        lockedPrintf("[STATUS] Connection: RED:%s | GREEN:%s\n", 
                     redConnected ? "ON" : "OFF", greenConnected ? "ON" : "OFF");
        lastStatusPrint = millis();
    }
    */
}

bool connectToDevice(BLEAdvertisedDevice* target, void (*cb)(BLERemoteCharacteristic*, uint8_t*, size_t, bool), String side) {
    if (target == nullptr) return false;
    lockedPrintf("[BLE] Starting connection to %s...\n", side.c_str());
    
    BLEClient* pClient = BLEDevice::createClient();
    if (!pClient->connect(target)) {
        lockedPrintf("[BLE] Failed to connect %s\n", side.c_str());
        delete pClient;
        return false;
    }
    
    BLERemoteService* pSvc = pClient->getService(serviceUUID);
    if (pSvc == nullptr) { 
        lockedPrintf("[BLE] Service not found on %s\n", side.c_str());
        pClient->disconnect(); delete pClient; return false; 
    }
    
    BLERemoteCharacteristic* pChar = pSvc->getCharacteristic(charUUID);
    if (pChar == nullptr) { 
        lockedPrintf("[BLE] Characteristic not found on %s\n", side.c_str());
        pClient->disconnect(); delete pClient; return false; 
    }
    
    if (pChar->canNotify()) {
        pChar->registerForNotify(cb);
        lockedPrintf("[BLE] Notify registered for %s\n", side.c_str());
    }
    return true;
}

// =====================【任务回调与类】=====================
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    lockedPrintln("[SIGNAL] RED RAW HIT!");
    if (!isLocked) {
        redHitRaw = true;
        redHitTimestamp = millis();
        lockedPrintf("[SIGNAL] RED RAW HIT AT %u\n", redHitTimestamp);
    }
}

static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    lockedPrintln("[SIGNAL] GREEN RAW HIT!");
    if (!isLocked) {
        greenHitRaw = true;
        greenHitTimestamp = millis();
        lockedPrintf("[SIGNAL] GREEN RAW HIT AT %u\n", greenHitTimestamp);
    }
}

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String name = advertisedDevice.getName().c_str();
        if (name == "epee_red" && !redConnected && !doConnectRed) {
            lockedPrintln("[SCAN] Found EPEE_RED!");
            redDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectRed = true;
        } else if (name == "epee_green" && !greenConnected && !doConnectGreen) {
            lockedPrintln("[SCAN] Found EPEE_GREEN!");
            greenDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectGreen = true;
        }
    }
};

// =====================【多核任务函数】=====================

void TaskLogic(void *pvParameters) {
    lockedPrintln("[CORE 1] Logic Task Started");
    for (;;) {
        updateStatusLed();
        if (!isLocked) {
            if (redHitRaw) {
                if (firstHitTime == 0) {
                    firstHitTime = redHitTimestamp;
                    lockedPrintf("[LOGIC] Red triggered window at %u\n", firstHitTime);
                }
                if (redHitTimestamp - firstHitTime <= 40) redHitReceived = true;
                redHitRaw = false;
            }
            if (greenHitRaw) {
                if (firstHitTime == 0) {
                    firstHitTime = greenHitTimestamp;
                    lockedPrintf("[LOGIC] Green triggered window at %u\n", firstHitTime);
                }
                if (greenHitTimestamp - firstHitTime <= 40) greenHitReceived = true;
                greenHitRaw = false;
            }
            if (firstHitTime > 0 && (millis() - firstHitTime > 45)) {
                evaluateHit();
            }
        }
        handleHitEffects();
        checkButtons();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void TaskBLE(void *pvParameters) {
    lockedPrintln("[CORE 0] BLE Task Started");
    for (;;) {
        if (doConnectRed && !redConnected && redRetryCount < MAX_CONNECT_RETRY) {
            if (connectToDevice(redDevice, redNotifyCallback, "RED")) {
                redConnected = true;
                redRetryCount = 0;
            } else { redRetryCount++; }
            doConnectRed = false;
        }
        if (doConnectGreen && !greenConnected && greenRetryCount < MAX_CONNECT_RETRY) {
            if (connectToDevice(greenDevice, greenNotifyCallback, "GREEN")) {
                greenConnected = true;
                greenRetryCount = 0;
            } else { greenRetryCount++; }
            doConnectGreen = false;
        }
        
        // 只有在没连全且没在尝试连接时才扫描
        if (((!redConnected && redRetryCount < MAX_CONNECT_RETRY) || 
             (!greenConnected && greenRetryCount < MAX_CONNECT_RETRY)) && 
             (!doConnectRed && !doConnectGreen)) {
            BLEDevice::getScan()->start(1, false); 
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// =====================【Arduino 标准入口】=====================

void setup() {
    Serial.begin(115200);
    serialMutex = xSemaphoreCreateMutex();
    
    delay(1000); 
    lockedPrintln("\n==============================");
    lockedPrintln("   EPEE S3 SYSTEM STARTING...  ");
    lockedPrintln("==============================");

    pinMode(PIN_RED_LED, OUTPUT);
    pinMode(PIN_GRN_LED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(BTN_NEXT, INPUT_PULLUP);
    pinMode(BTN_RESET, INPUT_PULLUP);
    pinMode(LED_BOARD, OUTPUT);

    BLEDevice::init("epee_master_s3");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);

    xTaskCreatePinnedToCore(TaskLogic, "Logic", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(TaskBLE, "BLE", 8192, NULL, 1, NULL, 0);
    
    lockedPrintln("[SYSTEM] Tasks Pinned to Cores.");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}