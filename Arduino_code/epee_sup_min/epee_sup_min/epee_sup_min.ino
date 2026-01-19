#include <Arduino.h>
// 显式引用所有相关头文件，确保类定义可见
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>
#include <NimBLEScan.h>
#include <NimBLEClient.h>
#include <NimBLEUtils.h>

// --- 引脚配置 ---
const int PIN_RED_LED = 4;   // 红方击中灯
const int PIN_GRN_LED = 5;   // 绿方击中灯
const int PIN_BUZZER  = 3;   // 蜂鸣器

const int BTN_NEXT = 7;      // 下一剑按钮
const int BTN_RESET = 6;     // 完全重置按钮
const int LED_BOARD = 8;     // 板载蓝色LED

// --- 时间参数 (FIE 标准) ---
const unsigned long LIGHT_DURATION = 3000;
const unsigned long BEEP_DURATION  = 800;

// --- 状态变量 ---
unsigned long hitEffectStartTime = 0;
bool effectActive = false;

// --- 配置区 ---
static NimBLEUUID serviceUUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static NimBLEUUID charUUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// --- 计分变量 ---
int redScore = 0;
int greenScore = 0;
unsigned long firstHitTime = 0;
bool isLocked = false;
bool redHitReceived = false;
bool greenHitReceived = false;

// --- BLE 状态变量 ---
NimBLEAddress* targetRedAddress = nullptr;
NimBLEAddress* targetGreenAddress = nullptr;

bool redConnected = false;
bool greenConnected = false;
bool shouldConnectRed = false;   
bool shouldConnectGreen = false; 

NimBLEClient* pRedClient = nullptr;
NimBLEClient* pGreenClient = nullptr;

// ===================== 连接重试次数限制配置 =====================
const int MAX_CONNECT_RETRY = 5;  
const int MAX_SCAN_RETRY = 5;
int redRetryCount = 0;
int greenRetryCount = 0;
int scan_count = 0; 
// =============================================================

// =============================================================
//  重点修复：将类定义移到最上方，防止编译器找不到基类
// =============================================================

// --- 扫描回调类定义 ---
// 修正后的回调类
class MyScanCallbacks : public NimBLEScanCallbacks {
    // 注意：参数必须加 const，且类名已变
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        // 使用 -> 访问，因为现在是 const 指针
        String name = advertisedDevice->getName();
        Serial.printf("name is %s.",name);
        Serial.println("");
        if (name == "epee_red" && !redConnected && redRetryCount < MAX_CONNECT_RETRY) {
            Serial.println(">>> 发现 epee_red");
            NimBLEDevice::getScan()->stop();
            if(targetRedAddress) delete targetRedAddress;
            targetRedAddress = new NimBLEAddress(advertisedDevice->getAddress());
            shouldConnectRed = true;
        } 
        else if (name == "epee_green" && !greenConnected && greenRetryCount < MAX_CONNECT_RETRY) {
            Serial.println(">>> 发现 epee_green");
            NimBLEDevice::getScan()->stop();
            if(targetGreenAddress) delete targetGreenAddress;
            targetGreenAddress = new NimBLEAddress(advertisedDevice->getAddress());
            shouldConnectGreen = true;
        }
    }
};

// --- 连接断开回调类定义 ---
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("[BLE] 已连接");
    }

    void onDisconnect(NimBLEClient* pClient) {
        if (targetRedAddress && pClient->getPeerAddress().equals(*targetRedAddress)) {
            Serial.println("[BLE] ⚠️ 红方意外断开连接");
            redConnected = false;
        } 
        else if (targetGreenAddress && pClient->getPeerAddress().equals(*targetGreenAddress)) {
            Serial.println("[BLE] ⚠️ 绿方意外断开连接");
            greenConnected = false;
        }
    }
};

// =============================================================
//  功能函数区
// =============================================================

// --- [核心逻辑] 灯光状态 ---
void updateStatusLed() {
    unsigned long now = millis();
    
    // 1. 双方都上线：常亮 (低电平亮)
    if (redConnected && greenConnected) {
        digitalWrite(LED_BOARD, LOW); 
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
        if (cycle < 200) digitalWrite(LED_BOARD, LOW);           
        else if (cycle < 400) digitalWrite(LED_BOARD, HIGH);     
        else if (cycle < 600) digitalWrite(LED_BOARD, LOW);      
        else digitalWrite(LED_BOARD, HIGH);                      
        return;
    }

    // 4. 无人在线：保持灭灯
    digitalWrite(LED_BOARD, HIGH);
}

// --- 重置比赛逻辑 ---
void resetMatch(bool resetTotalScore) {
    if (resetTotalScore) {
        redScore = 0;
        greenScore = 0;
        Serial.println("\n[系统] >>> 比赛完全重置！比分归零 0:0 <<<");
        redRetryCount = 0;
        greenRetryCount = 0;
        scan_count = 0; 
    } else {
        Serial.println("\n[系统] >>> 回合就绪，准备下一剑 <<<");
    }
    isLocked = false;
    redHitReceived = false;
    greenHitReceived = false;
    firstHitTime = 0;

    effectActive = false;
    digitalWrite(PIN_RED_LED, LOW);
    digitalWrite(PIN_GRN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
}

// --- 判定函数 ---
void evaluateHit() {
    Serial.println("[判定] 判定窗口关闭，正在触发效果...");
    isLocked = true; 
    hitEffectStartTime = millis();
    effectActive = true;

    if (redHitReceived && greenHitReceived) {
        redScore++; greenScore++;
        digitalWrite(PIN_RED_LED, HIGH);
        digitalWrite(PIN_GRN_LED, HIGH);
        Serial.println(">>> 【互中】！");
    } else if (redHitReceived) {
        redScore++;
        digitalWrite(PIN_RED_LED, HIGH);
        Serial.println(">>> 【红方单中】！");
    } else if (greenHitReceived) {
        greenScore++;
        digitalWrite(PIN_GRN_LED, HIGH);
        Serial.println(">>> 【绿方单中】！");
    }

    digitalWrite(PIN_BUZZER, HIGH);
    Serial.printf(">>> 当前总比分 | 红色 %d : %d 绿色 |\n", redScore, greenScore);
}

// --- 处理自动关闭的任务 ---
void handleHitEffects() {
    if (!effectActive) return;
    unsigned long elapsed = millis() - hitEffectStartTime;

    if (elapsed > BEEP_DURATION) digitalWrite(PIN_BUZZER, LOW);

    if (elapsed > LIGHT_DURATION) {
        digitalWrite(PIN_RED_LED, LOW);
        digitalWrite(PIN_GRN_LED, LOW);
        effectActive = false; 
        Serial.println("[系统] 效果显示结束，等待下一剑复位...");
    }
}

// --- 红色设备回调 ---
void redNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
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
void greenNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
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

// --- 连接函数 ---
bool connectToServer(NimBLEAddress* pAddress, bool isRed) {
    Serial.printf("正在连接 %s 方: %s ...\n", isRed ? "红" : "绿", pAddress->toString().c_str());

    NimBLEClient* pClient = nullptr;

    if (NimBLEDevice::getCreatedClientCount()) {
        pClient = NimBLEDevice::getClientByPeerAddress(*pAddress);
        if (pClient) {
            if (!pClient->connect(*pAddress, false)) {
                Serial.println("重连失败");
                return false;
            }
        } else {
            pClient = NimBLEDevice::createClient();
        }
    } else {
        pClient = NimBLEDevice::createClient();
    }
    
    if(!pClient) {
        Serial.println("无法创建客户端");
        return false;
    }

    pClient->setClientCallbacks(new ClientCallbacks(), false);

    pClient->setConnectionParams(12, 12, 0, 200);
    pClient->setConnectTimeout(5);

    if (!pClient->isConnected()) {
        if (!pClient->connect(*pAddress)) {
            Serial.println("连接失败");
            return false;
        }
    }

    NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("未找到服务 UUID");
        pClient->disconnect();
        return false;
    }

    NimBLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteChar == nullptr) {
        Serial.println("未找到特征值 UUID");
        pClient->disconnect();
        return false;
    }

    if (pRemoteChar->canNotify()) {
        if (isRed) {
            pRemoteChar->subscribe(true, redNotifyCallback);
            pRedClient = pClient;
        } else {
            pRemoteChar->subscribe(true, greenNotifyCallback);
            pGreenClient = pClient;
        }
    }

    Serial.println("✅ 连接并订阅成功");
    return true;
}

// =============================================================
//  SETUP & LOOP
// =============================================================

void setup() {
    Serial.begin(115200);
    scan_count = 0;
    pinMode(BTN_NEXT, INPUT_PULLUP); 
    pinMode(BTN_RESET, INPUT_PULLUP);
    pinMode(LED_BOARD, OUTPUT);
    digitalWrite(LED_BOARD, HIGH);

    pinMode(PIN_RED_LED, OUTPUT);
    pinMode(PIN_GRN_LED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    Serial.println("========================================");
    Serial.println("   ESP32-S3 (NimBLE) 重剑计分系统启动   ");
    Serial.println("========================================");

    NimBLEDevice::init("epee_supmin");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); 

    // 获取扫描对象
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    
    // 关键点：这里实例化回调类，此时 MyAdvertisedDeviceCallbacks 已经在上面定义过了
    pBLEScan->setScanCallbacks(new MyScanCallbacks());
    
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
}

void loop() {
    updateStatusLed();
    handleHitEffects();

    // --- 1. 连接红方 ---
    if (shouldConnectRed) {
        shouldConnectRed = false;
        delay(100); 
        if (connectToServer(targetRedAddress, true)) {
            redConnected = true;
            redRetryCount = 0;
        } else {
            redRetryCount++;
            Serial.printf("[错误] 红方连接失败 %d/%d\n", redRetryCount, MAX_CONNECT_RETRY);
            if (redRetryCount >= MAX_CONNECT_RETRY) Serial.println("⚠️ 红方停止重试");
        }
    }

    // --- 2. 连接绿方 ---
    if (shouldConnectGreen) {
        shouldConnectGreen = false;
        delay(100);
        if (connectToServer(targetGreenAddress, false)) {
            greenConnected = true;
            greenRetryCount = 0;
        } else {
            greenRetryCount++;
            Serial.printf("[错误] 绿方连接失败 %d/%d\n", greenRetryCount, MAX_CONNECT_RETRY);
            if (greenRetryCount >= MAX_CONNECT_RETRY) Serial.println("⚠️ 绿方停止重试");
        }
    }

    // --- 3. 自动扫描逻辑 ---
    static unsigned long lastScanCheck = 0;
    if (millis() - lastScanCheck > 2000) {
        lastScanCheck = millis();
        NimBLEScan* pScan = NimBLEDevice::getScan();
        
        bool needRed = (!redConnected && redRetryCount < MAX_CONNECT_RETRY);
        bool needGreen = (!greenConnected && greenRetryCount < MAX_CONNECT_RETRY);

        if ((needRed || needGreen) && !pScan->isScanning() && scan_count <= MAX_SCAN_RETRY) {
            scan_count++;
            Serial.printf("[系统] 启动扫描 (%d/%d) ...\n", scan_count, MAX_SCAN_RETRY);
            
            // start(时间, 是否继续扫描)
            pScan->start(5, false); 
        }
    }

    // --- 按钮处理 ---
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
}