#include <NimBLEDevice.h>

// --- 引脚配置 ---
const int PIN_RED_LED = 4;   // 红方击中灯
const int PIN_GRN_LED = 5;   // 绿方击中灯
const int PIN_BUZZER  = 3;   // 蜂鸣器

// --- 时间参数 (FIE 标准) ---
const unsigned long LIGHT_DURATION = 3000; // 亮灯 3 秒
const unsigned long BEEP_DURATION  = 800;  // 鸣叫 0.8 秒

// --- 状态变量 ---
unsigned long hitEffectStartTime = 0; 
bool effectActive = false;           

// --- 引脚配置 ---
const int BTN_NEXT = 7;   
const int BTN_RESET = 6;  
const int LED_BOARD = 8;  

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

// --- NimBLE 状态变量 ---
static boolean doConnectRed = false;
static boolean doConnectGreen = false;
static NimBLEAdvertisedDevice* redDevice = nullptr;
static NimBLEAdvertisedDevice* greenDevice = nullptr;
static boolean redConnected = false;
static boolean greenConnected = false;

static std::string EPEE_GREEN_MAC ="48:F6:EE:22:82:BE";

// ===================== 连接重试次数限制配置 =====================
const int MAX_CONNECT_RETRY = 5;  
const int MAX_SCAN_RETRY = 5;  
int redRetryCount = 0; 
int greenRetryCount = 0; 
int scan_count = 0; 
// ==============================================================

// --- [核心逻辑] 状态灯 ---
void updateStatusLed() {
    unsigned long now = millis();
    if (redConnected && greenConnected) {
        digitalWrite(LED_BOARD, LOW); 
        return;
    }
    if (redConnected && !greenConnected) {
        int cycle = now % 2000;
        if (cycle < 200) digitalWrite(LED_BOARD, LOW); 
        else digitalWrite(LED_BOARD, HIGH);           
        return;
    }
    if (!redConnected && greenConnected) {
        int cycle = now % 2000;
        if (cycle < 200) digitalWrite(LED_BOARD, LOW);           
        else if (cycle < 400) digitalWrite(LED_BOARD, HIGH);     
        else if (cycle < 600) digitalWrite(LED_BOARD, LOW);      
        else digitalWrite(LED_BOARD, HIGH);                      
        return;
    }
    digitalWrite(LED_BOARD, HIGH);
}

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
    effectActive = false;
    digitalWrite(PIN_RED_LED, LOW);
    digitalWrite(PIN_GRN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
}

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
static void redNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.println("[日志] epee_red 回调！");
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
static void greenNotifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    Serial.println("[日志] epee_green 回调");
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
class MyBLEDeviceCallbacks: public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
       // --- 暴力调试：打印每一个搜到的设备 ---
        Serial.printf("[物理发现] 地址: %s | RSSI: %d | 名称: %s\n", 
                      advertisedDevice->getAddress().toString().c_str(),
                      advertisedDevice->getRSSI(),
                      advertisedDevice->getName().c_str());
       
        String name = advertisedDevice->getName().c_str();
        //Serial.printf("[发现] %s | RSSI: %d \n", name.c_str(), advertisedDevice->getRSSI());
        //Serial.println();
        if (name == "epee_green"){
            Serial.println(">>> 锁定 epee_green");
        }
        if (name == "epee_red" && !redConnected && !doConnectRed && redRetryCount < MAX_CONNECT_RETRY) {
            Serial.println(">>> 锁定 epee_red");
            // 关键修改：必须用 new 拷贝一份，否则回调结束后指针会失效
            if (redDevice) delete redDevice; 
            redDevice = new NimBLEAdvertisedDevice(*advertisedDevice); 
            doConnectRed = true;
        } 
        else if (name == "epee_green" && !greenConnected && !doConnectGreen && greenRetryCount < MAX_CONNECT_RETRY) {
            Serial.println(">>> 锁定 epee_green");
           if (greenDevice) delete redDevice; 
            greenDevice = new NimBLEAdvertisedDevice(*advertisedDevice); 
            doConnectGreen = true;
        }
    }
};

bool connectToDevice(NimBLEAdvertisedDevice* targetDevice, void (*callback)(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool)) {
    Serial.print("正在连接: ");
    Serial.println(targetDevice->getName().c_str());
    
    NimBLEClient* pClient = NimBLEDevice::createClient();
    delay(100); 

    if (!pClient->connect(targetDevice)) {
        Serial.println("连接失败，等待下次扫描");
        NimBLEDevice::deleteClient(pClient);
        return false;
    }
    
    NimBLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("未找到目标服务UUID");
        pClient->disconnect();
        return false;
    }
    
    NimBLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteChar == nullptr) {
        Serial.println("未找到目标特征值UUID");
        pClient->disconnect();
        return false;
    }
    
    if (pRemoteChar->canNotify()) {
        pRemoteChar->subscribe(true, callback);
    }
    Serial.println("✅ 特征值订阅成功");
    return true;
}

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
    Serial.println("   ESP32-S3 NimBLE 裁判系统启动 ");
    Serial.println("========================================");

    NimBLEDevice::init("epee_supmin");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(new MyBLEDeviceCallbacks(), false);
    pScan->setActiveScan(true); 
    pScan->setInterval(200);    
    pScan->setWindow(180);       
    pScan->setMaxResults(0); // [必须] 设为0，否则扫描会提前结束
    pScan->setDuplicateFilter(false); // 允许重复发现同一个设备，防止缓存判定已找到
    Serial.println("[系统] 正在开启初始扫描...");
    pScan->start(5, false);     
}

void loop() {
    updateStatusLed();   
    handleHitEffects();  

    if (doConnectRed && !redConnected && redRetryCount < MAX_CONNECT_RETRY) {
        Serial.printf(">>> 准备连接红方 [当前重试次数: %d/%d] \n", redRetryCount+1, MAX_CONNECT_RETRY);
        NimBLEDevice::getScan()->stop(); 
        delay(100); 

        if (connectToDevice(redDevice, redNotifyCallback)) {
            Serial.println("[状态] ✅ epee_red 已上线");
            redConnected = true;
            redRetryCount = 0; 
        } else {
            redRetryCount++;   
            Serial.printf("[错误] ❌ 红方连接失败，剩余重试次数: %d\n", MAX_CONNECT_RETRY - redRetryCount);
            if(redRetryCount >= MAX_CONNECT_RETRY){
                Serial.println("⚠️ [严重错误] epee_red 连接重试上限！");
                doConnectRed = false;
            }
        }
        doConnectRed = false; 
    }

    if (doConnectGreen && !greenConnected && greenRetryCount < MAX_CONNECT_RETRY) {
        Serial.printf(">>> 准备连接绿方 [当前重试次数: %d/%d] \n", greenRetryCount+1, MAX_CONNECT_RETRY);
        NimBLEDevice::getScan()->stop();
        delay(100); 

        if (connectToDevice(greenDevice, greenNotifyCallback)) {
            Serial.println("[状态] ✅ epee_green 已上线");
            greenConnected = true;
            greenRetryCount = 0; 
        } else {
            greenRetryCount++;   
            Serial.printf("[错误] ❌ 绿方连接失败，剩余重试次数: %d\n", MAX_CONNECT_RETRY - greenRetryCount);
            if(greenRetryCount >= MAX_CONNECT_RETRY){
                Serial.println("⚠️ [严重错误] epee_green 连接重试上限！");
                doConnectGreen = false;
            }
        }
        doConnectGreen = false;
    }

    static unsigned long scanTimer = 0;
    if ((( !redConnected && redRetryCount < MAX_CONNECT_RETRY ) || ( !greenConnected && greenRetryCount < MAX_CONNECT_RETRY ) ) 
    && scan_count <= MAX_SCAN_RETRY) {
        if (!doConnectRed && !doConnectGreen) {
            if (millis() - scanTimer > 2000 && !NimBLEDevice::getScan()->isScanning()) { 
            Serial.printf("[系统] 启动一轮新扫描, 当前次数: %d \n", scan_count);
            scan_count++;
            // 扫描时长设为 0 表示持续扫描，或者设为 5
            // --- 核心修复步骤 ---
            NimBLEDevice::getScan()->stop();         // 确保先停止
            NimBLEDevice::getScan()->clearResults(); // 必须：清理内存中的扫描结果

           // 尝试不带参数启动，或者使用非常长的时间
            if(NimBLEDevice::getScan()->start(0, false)) { // 0 表示持续扫描
                Serial.println("D NimBLEScan: 物理扫描窗口已开启...");
            } else {
                Serial.println("E NimBLEScan: 扫描开启失败！");
            }
            scanTimer = millis();
        }
        }
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