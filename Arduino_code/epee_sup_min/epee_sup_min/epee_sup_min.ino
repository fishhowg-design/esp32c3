#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// --- 新增引脚配置 ---
const int PIN_RED_LED = 4;   // 红方击中灯
const int PIN_GRN_LED = 5;   // 绿方击中灯
const int PIN_BUZZER  = 3;   // 蜂鸣器

// --- 时间参数 (FIE 标准) ---
const unsigned long LIGHT_DURATION = 3000; // 亮灯 3 秒
const unsigned long BEEP_DURATION  = 800;  // 鸣叫 0.8 秒

// --- 状态变量 ---
unsigned long hitEffectStartTime = 0; // 效果开始时间
bool effectActive = false;           // 效果是否正在运行

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

// ===================== 新增：连接重试次数限制核心配置 =====================
const int MAX_CONNECT_RETRY = 5;  // 单个设备最大重试连接次数
const int MAX_SCAN_RETRY = 5;  // 最大扫描次数
int redRetryCount = 0;             // 红方连接失败重试计数
int greenRetryCount = 0;           // 绿方连接失败重试计数
int scan_count = MAX_SCAN_RETRY;  //扫描计数 
// =========================================================================

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

    effectActive = false;
    digitalWrite(PIN_RED_LED, LOW);
    digitalWrite(PIN_GRN_LED, LOW);
    digitalWrite(PIN_BUZZER, LOW);
}

// --- 修改后的核心判定函数 ---
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

    // 触发蜂鸣器
    digitalWrite(PIN_BUZZER, HIGH);
    
    Serial.printf(">>> 当前总比分 | 红色 %d : %d 绿色 |\n", redScore, greenScore);
}

// --- 处理灯光和声音自动关闭的任务 ---
void handleHitEffects() {
    if (!effectActive) return;

    unsigned long elapsed = millis() - hitEffectStartTime;

    // 1. 处理蜂鸣器：0.8秒后关闭
    if (elapsed > BEEP_DURATION) {
        digitalWrite(PIN_BUZZER, LOW);
    }

    // 2. 处理灯光：3秒后关闭
    if (elapsed > LIGHT_DURATION) {
        digitalWrite(PIN_RED_LED, LOW);
        digitalWrite(PIN_GRN_LED, LOW);
        effectActive = false; 
        Serial.println("[系统] 效果显示结束，等待下一剑复位...");
    }
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
        if (name == "epee_red" && !redConnected && !doConnectRed && redRetryCount < MAX_CONNECT_RETRY) {
            Serial.println(">>> 锁定 epee_red");
            redDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectRed = true;
        } 
        else if (name == "epee_green" && !greenConnected && !doConnectGreen && greenRetryCount < MAX_CONNECT_RETRY) {
            Serial.println(">>> 锁定 epee_green");
            greenDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnectGreen = true;
        }
    }
};

bool connectToDevice(BLEAdvertisedDevice* targetDevice, void (*callback)(BLERemoteCharacteristic*, uint8_t*, size_t, bool)) {
    Serial.print("正在连接: ");
    Serial.println(targetDevice->getName().c_str());
    
    BLEClient* pClient = BLEDevice::createClient();
    
    // 给射频模块 100ms 喘息时间
    delay(100); 

    if (!pClient->connect(targetDevice)) {
        Serial.println("连接失败，等待下次扫描");
        pClient->disconnect();  // 新增：释放客户端，防止内存泄漏
        delete pClient;         // 新增：销毁对象，优化内存
        return false;
    }
    
    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (pRemoteService == nullptr) {
        Serial.println("未找到目标服务UUID");
        pClient->disconnect();
        delete pClient;
        return false;
    }
    
    BLERemoteCharacteristic* pRemoteChar = pRemoteService->getCharacteristic(charUUID);
    if (pRemoteChar == nullptr) {
        Serial.println("未找到目标特征值UUID");
        pClient->disconnect();
        delete pClient;
        return false;
    }
    
    if (pRemoteChar->canNotify()) {
        pRemoteChar->registerForNotify(callback);
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
    digitalWrite(LED_BOARD, HIGH); // 初始灭灯

    //击中的灯
    pinMode(PIN_RED_LED, OUTPUT);
    pinMode(PIN_GRN_LED, OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    Serial.println("========================================");
    Serial.println("   ESP32-S3 国际重剑计分裁判系统启动   ");
    Serial.println("========================================");

    BLEDevice::init("epee_supmin");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true); // 主动扫描
    pBLEScan->setInterval(100);    // 扫描间隔
    pBLEScan->setWindow(99);       // 扫描窗口接近间隔

    Serial.println("[系统] 正在开启初始扫描...");
    pBLEScan->start(5, false);     // 每次只扫5秒，扫完会停，这样最稳
}

void loop() {
    updateStatusLed();   // 维护连接状态灯
    handleHitEffects();  // 维护击中后的声光效果

    // --- 优化后的红方连接处理 + 重试计数+超次数提示 ---
    if (doConnectRed && !redConnected && redRetryCount < MAX_CONNECT_RETRY) {
        Serial.printf(">>> 准备连接红方 [当前重试次数: %d/%d] \n", redRetryCount+1, MAX_CONNECT_RETRY);
        BLEDevice::getScan()->stop(); 
        delay(500); // 关键：给底层协议栈 500ms 彻底退出的时间

        if (connectToDevice(redDevice, redNotifyCallback)) {
            Serial.println("[状态] ✅ epee_red 已上线");
            redConnected = true;
            redRetryCount = 0; // 连接成功，重置重试计数
        } else {
            redRetryCount++;   // 连接失败，重试计数+1
            Serial.printf("[错误] ❌ 红方连接失败，剩余重试次数: %d\n", MAX_CONNECT_RETRY - redRetryCount);
            
            // 新增：红方超过10次连接失败
            if(redRetryCount >= MAX_CONNECT_RETRY){
                Serial.println("==================================================");
                Serial.println("⚠️ [严重错误] epee_red 连接重试超过10次！建议重启设备恢复！");
                Serial.println("==================================================");
                doConnectRed = false;
            }
        }
        doConnectRed = false; 
    }

    // --- 优化后的绿方连接处理 + 重试计数+超次数提示 ---
    if (doConnectGreen && !greenConnected && greenRetryCount < MAX_CONNECT_RETRY) {
        Serial.printf(">>> 准备连接绿方 [当前重试次数: %d/%d] \n", greenRetryCount+1, MAX_CONNECT_RETRY);
        BLEDevice::getScan()->stop();
        delay(500); // 关键：冷静期

        if (connectToDevice(greenDevice, greenNotifyCallback)) {
            Serial.println("[状态] ✅ epee_green 已上线");
            greenConnected = true;
            greenRetryCount = 0; // 连接成功，重置重试计数
        } else {
            greenRetryCount++;   // 连接失败，重试计数+1
            Serial.printf("[错误] ❌ 绿方连接失败，剩余重试次数: %d\n", MAX_CONNECT_RETRY - greenRetryCount);
            
            // 新增：绿方超过10次连接失败
            if(greenRetryCount >= MAX_CONNECT_RETRY){
                Serial.println("==================================================");
                Serial.println("⚠️ [严重错误] epee_green 连接重试超过10次！建议重启设备恢复！");
                Serial.println("==================================================");
                doConnectGreen = false;
            }
        }
        doConnectGreen = false;
    }

    // --- 核心逻辑：如果有人没连上+重试次数没超，就开启扫描 ---
    static unsigned long scanTimer = 0;
    if ((( !redConnected && redRetryCount < MAX_CONNECT_RETRY ) || ( !greenConnected && greenRetryCount < MAX_CONNECT_RETRY ) ) 
    && scan_count <= MAX_SCAN_RETRY) {
        if (!doConnectRed && !doConnectGreen) {
            if (millis() - scanTimer > 2000) { // 每2秒检查一次是否需要重启扫描
                Serial.printf("[系统] 启动一轮新扫描,扫描次数%d \n", scan_count);
                scan_count ++;
                BLEDevice::getScan()->start(5, false); // 扫5秒
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