#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// --- 引脚配置 ---
const int BTN_NEXT = 7;   // 下一剑按钮：GPIO 7
const int BTN_RESET = 6;  // 完全重置按钮：GPIO 6
const int LED_BOARD = 8;  // 板载蓝色LED：GPIO 8

// --- 配置区 ---
static BLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// --- 计分变量 ---
int redScore = 0;
int greenScore = 0;
unsigned long firstHitTime = 0; // 记录回合第一次击中的时间
bool isLocked = false;          // 回合锁定状态
bool redHitReceived = false;    // 本回合红色是否有效击中
bool greenHitReceived = false;  // 本回合绿色是否有效击中

// --- BLE 状态变量 ---
static boolean doConnectRed = false;
static boolean doConnectGreen = false;
static BLEAdvertisedDevice* redDevice;
static BLEAdvertisedDevice* greenDevice;
static boolean redConnected = false;
static boolean greenConnected = false;

// --- [核心逻辑] 仅通过灯光频率区分红绿在线状态 ---
void updateStatusLed() {
    unsigned long now = millis();
    
    // 1. 双方都上线：常亮 (最高优先级)
    if (redConnected && greenConnected) {
        digitalWrite(LED_BOARD, LOW); // 低电平点亮
        return;
    }

    // 2. 只有红方在线：每2秒闪烁 1 下
    if (redConnected && !greenConnected) {
        int cycle = now % 2000;
        if (cycle < 200) digitalWrite(LED_BOARD, LOW); 
        else digitalWrite(LED_BOARD, HIGH);           
        return;
    }

    // 3. 只有绿方在线：每2秒闪烁 2 下
    if (!redConnected && greenConnected) {
        int cycle = now % 2000;
        if (cycle < 200) digitalWrite(LED_BOARD, LOW);           // 第一闪
        else if (cycle < 400) digitalWrite(LED_BOARD, HIGH);     // 间隔
        else if (cycle < 600) digitalWrite(LED_BOARD, LOW);      // 第二闪
        else digitalWrite(LED_BOARD, HIGH);                      // 停顿
        return;
    }

    // 4. 其他状态（连接中、无人在线）：保持灯灭，避免干扰
    digitalWrite(LED_BOARD, HIGH);
}

// --- 重置比赛逻辑 (保持不变) ---
void resetMatch(bool resetTotalScore) {
    if (resetTotalScore) {
        redScore = 0;
        greenScore = 0;
        Serial.println("\n[系统] >>> 比赛完全重置！比分归零 0:0 <<<");
    } else {
        Serial.println("\n[系统] >>> 回合就绪，准备下一剑 <<<");
    }
    isLocked = false;
    redHitReceived = false;
    greenHitReceived = false;
    firstHitTime = 0;
}

// --- 核心计分判定  (FIE 规则: 40ms 互中窗口) ---
void evaluateHit() {
    Serial.println("[判定] 判定窗口关闭，正在计算...");
    if (redHitReceived && greenHitReceived) {
        redScore++; greenScore++;
        Serial.println(">>> 结果: 【互中】！双方各加 1 分");
    } else if (redHitReceived) {
        redScore++;
        Serial.println(">>> 结果: 【红方单中】！红色加 1 分");
    } else if (greenHitReceived) {
        greenScore++;
        Serial.println(">>> 结果: 【绿方单中】！绿色加 1 分");
    }
    Serial.printf(">>> 当前总比分 | 红色 %d : %d 绿色 |\n", redScore, greenScore);
    Serial.println("[提示] 按钮7:下一剑 | 按钮6:总重置");
    isLocked = true; 
}

// --- 红色设备回调 ---
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (isLocked) return; 
    unsigned long currentTime = millis();
    if (firstHitTime == 0) {
        firstHitTime = currentTime;
        Serial.println("\n[信号] 红色首击！开启 40ms 窗口...");
    }
    if (currentTime - firstHitTime <= 40) {
        if (!redHitReceived) {
            redHitReceived = true;
            Serial.println("[日志] epee_red 信号确认有效");
        }
    }
}

// --- 绿色设备回调 ---
static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (isLocked) return;
    unsigned long currentTime = millis();
    if (firstHitTime == 0) {
        firstHitTime = currentTime;
        Serial.println("\n[信号] 绿色首击！开启 40ms 窗口...");
    }
    if (currentTime - firstHitTime <= 40) {
        if (!greenHitReceived) {
            greenHitReceived = true;
            Serial.println("[日志] epee_green 信号确认有效");
        }
    }
}

// --- 扫描与连接逻辑 ---
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String name = advertisedDevice.getName().c_str();
        if (name == "epee_red" && !redConnected) {
            Serial.println(">>> 发现 epee_red");
            redDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectRed = true;
        } else if (name == "epee_green" && !greenConnected) {
            Serial.println(">>> 发现 epee_green");
            greenDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectGreen = true;
        }
    }
};

bool connectToDevice(BLEAdvertisedDevice* targetDevice, void (*callback)(BLERemoteCharacteristic*, uint8_t*, size_t, bool)) {
    Serial.print("正在尝试连接: ");
    Serial.println(targetDevice->getName().c_str());
    BLEClient* pClient = BLEDevice::createClient();
    if (!pClient->connect(targetDevice)) return false;
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) { pClient->disconnect(); return false; }
    BLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteChar == nullptr) { pClient->disconnect(); return false; }
    if (pRemoteChar->canNotify()) pRemoteChar->registerForNotify(callback);
    return true;
}

void setup() {
    Serial.begin(115200);
    pinMode(BTN_NEXT, INPUT_PULLUP); 
    pinMode(BTN_RESET, INPUT_PULLUP);
    pinMode(LED_BOARD, OUTPUT);
    digitalWrite(LED_BOARD, HIGH); // 初始灭灯

    Serial.println("========================================");
    Serial.println("   ESP32-C3 国际重剑计分裁判系统启动   ");
    Serial.println("========================================");

    BLEDevice::init("epee_supmin");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(15, false);
}

void loop() {
    updateStatusLed();

    if (doConnectRed) {
        if (connectToDevice(redDevice, redNotifyCallback)) {
            Serial.println("[状态] epee_red 已上线");
            redConnected = true;
        }
        doConnectRed = false;
    }
    if (doConnectGreen) {
        if (connectToDevice(greenDevice, greenNotifyCallback)) {
            Serial.println("[状态] epee_green 已上线");
            greenConnected = true;
        }
        doConnectGreen = false;
    }

    if (firstHitTime > 0 && !isLocked) {
        if (millis() - firstHitTime > 45) evaluateHit();
    }

    if (Serial.available()) {
        char cmd = Serial.read();
        if (cmd == 'r') resetMatch(true);
        if (cmd == 'n') resetMatch(false);
    }

    static bool lastNextState = HIGH;
    static bool lastResetState = HIGH;
    bool currNext = digitalRead(BTN_NEXT);
    bool currReset = digitalRead(BTN_RESET);

    if (lastNextState == HIGH && currNext == LOW) {
        delay(50);
        if (digitalRead(BTN_NEXT) == LOW) resetMatch(false);
    }
    if (lastResetState == HIGH && currReset == LOW) {
        delay(50);
        if (digitalRead(BTN_RESET) == LOW) resetMatch(true);
    }
    lastNextState = currNext;
    lastResetState = currReset;

    delay(1); 
}