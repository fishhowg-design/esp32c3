#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- 引脚配置 ---
const int BTN_NEXT  = 7; 
const int BTN_RESET = 6;
const int LED_BOARD = 48; // S3 SuperMini/DevKit 常用 RGB 引脚，普通引脚请根据实际修改

// --- UUID 配置 ---
static BLEUUID PHONE_SVC_UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID PHONE_CHR_UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID SENSOR_SVC_UUID("4fafc201-1fb5-459e-8fcc-c5c9c331914b");
static BLEUUID SENSOR_CHR_UUID("beb5483e-36e1-4688-b7f5-ea07361b26a8");

// --- 全局计分变量 (增加 volatile 保证多核可见性) ---
volatile int redScore = 0;
volatile int greenScore = 0;
volatile unsigned long firstHitTime = 0;
volatile bool isLocked = false;
volatile bool redHitReceived = false;
volatile bool greenHitReceived = false;

// --- 状态变量 ---
bool redConnected = false;
bool greenConnected = false;
bool phoneConnected = false;

// --- BLE 实例 ---
BLECharacteristic *pNotifyChar;
BLEScan* pBLEScan;
BLEClient* pClientRed;
BLEClient* pClientGreen;
static BLEAdvertisedDevice* redDevice = nullptr;
static BLEAdvertisedDevice* greenDevice = nullptr;
volatile bool doConnectRed = false;
volatile bool doConnectGreen = false;

// --- 手机 Server 回调 ---
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { phoneConnected = true; Serial.println(">>> 手机已连接"); }
    void onDisconnect(BLEServer* pServer) { 
        phoneConnected = false; 
        Serial.println(">>> 手机已断开");
        pServer->getAdvertising()->start(); 
    }
};

// --- 重置函数 ---
void resetMatch(bool total) {
    if(total) { redScore = 0; greenScore = 0; }
    isLocked = false; redHitReceived = false; greenHitReceived = false; firstHitTime = 0;
    Serial.printf("[系统] 重置: %d:%d\n", redScore, greenScore);
}

// --- 传感器信号解析 (Core 0/1 共享) ---
void handleSensorHit(String name) {
    if (isLocked) return;
    unsigned long now = millis();
    if (firstHitTime == 0) firstHitTime = now;
    
    if (now - firstHitTime <= 40) {
        if (name == "epee_red") redHitReceived = true;
        if (name == "epee_green") greenHitReceived = true;
    }
}

// --- 传感器 Client 回调 ---
static void redNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    handleSensorHit("epee_red");
}
static void greenNotifyCallback(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    handleSensorHit("epee_green");
}

// --- 扫描回调 ---
class MyScanCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String name = advertisedDevice.getName().c_str();
        if (name == "epee_red" && !redConnected) {
            redDevice = new BLEAdvertisedDevice(advertisedDevice); doConnectRed = true;
        } else if (name == "epee_green" && !greenConnected) {
            greenDevice = new BLEAdvertisedDevice(advertisedDevice); doConnectGreen = true;
        }
    }
};

// --- 连接传感器函数 ---
bool connectToSensor(BLEAdvertisedDevice* target, bool isRed) {
    BLEClient* pClient = BLEDevice::createClient();
    if (!pClient->connect(target)) return false;
    BLERemoteService* pSvc = pClient->getService(SENSOR_SVC_UUID);
    if (!pSvc) { pClient->disconnect(); return false; }
    BLERemoteCharacteristic* pChar = pSvc->getCharacteristic(SENSOR_CHR_UUID);
    if (!pChar) { pClient->disconnect(); return false; }
    pChar->registerForNotify(isRed ? redNotifyCallback : greenNotifyCallback);
    return true;
}

// --- [核心任务] Core 1：处理高精度计分、按键和灯光 ---
void TaskCore1(void * pvParameters) {
    for(;;) {
        // 1. 判定逻辑
        if (firstHitTime > 0 && !isLocked) {
            if (millis() - firstHitTime > 45) {
                if (redHitReceived && greenHitReceived) { redScore++; greenScore++; }
                else if (redHitReceived) redScore++;
                else if (greenHitReceived) greenScore++;
                isLocked = true;
                
                // 通知手机
                if (phoneConnected) {
                    char buf[16]; sprintf(buf, "R%dG%d", redScore, greenScore);
                    pNotifyChar->setValue(buf); pNotifyChar->notify();
                }
                Serial.printf(">>> 判定结束: 红 %d : 绿 %d\n", redScore, greenScore);
            }
        }

        // 2. 灯光状态指示 (电报码逻辑)
        unsigned long now = millis();
        if (redConnected && greenConnected) digitalWrite(LED_BOARD, LOW); // 常亮
        else if (redConnected && !greenConnected) {
            digitalWrite(LED_BOARD, (now % 2000 < 200) ? LOW : HIGH);
        } else if (!redConnected && greenConnected) {
            int cycle = now % 2000;
            if (cycle < 200 || (cycle > 400 && cycle < 600)) digitalWrite(LED_BOARD, LOW);
            else digitalWrite(LED_BOARD, HIGH);
        } else {
            digitalWrite(LED_BOARD, HIGH); // 都不在则灭
        }

        // 3. 按键扫描
        static bool lastN = HIGH, lastR = HIGH;
        bool curN = digitalRead(BTN_NEXT), curR = digitalRead(BTN_RESET);
        if (lastN == HIGH && curN == LOW) { delay(50); resetMatch(false); }
        if (lastR == HIGH && curR == LOW) { delay(50); resetMatch(true); }
        lastN = curN; lastR = curR;

        vTaskDelay(5 / portTICK_PERIOD_MS); 
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BOARD, OUTPUT);
    pinMode(BTN_NEXT, INPUT_PULLUP);
    pinMode(BTN_RESET, INPUT_PULLUP);
    
    // 初始化蓝牙 (运行在 Core 0)
    BLEDevice::init("Epee_S3_Master");

    // 配置 Server (手机端)
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pPhoneSvc = pServer->createService(PHONE_SVC_UUID);
    pNotifyChar = pPhoneSvc->createCharacteristic(PHONE_CHR_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    pNotifyChar->addDescriptor(new BLE2902());
    pPhoneSvc->start();
    pServer->getAdvertising()->addServiceUUID(PHONE_SVC_UUID);
    pServer->getAdvertising()->start();

    // 配置扫描
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyScanCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->setInterval(150);
    pBLEScan->setWindow(100);
    pBLEScan->start(0, true);

    // 启动核心任务在 Core 1
    xTaskCreatePinnedToCore(TaskCore1, "ScoreTask", 4096, NULL, 1, NULL, 1);
    
    Serial.println(">>> S3 系统启动完毕，正在等待连接...");
}

void loop() {
    // Loop 运行在 Core 1，专门处理耗时的“连接动作”，避免阻塞计分任务
    if (doConnectRed) {
        pBLEScan->stop();
        if (connectToSensor(redDevice, true)) { redConnected = true; Serial.println("epee_red 上线"); }
        delete redDevice; redDevice = nullptr; doConnectRed = false;
        pBLEScan->start(0, true);
    }
    
    if (doConnectGreen) {
        pBLEScan->stop();
        if (connectToSensor(greenDevice, false)) { greenConnected = true; Serial.println("epee_green 上线"); }
        delete greenDevice; greenDevice = nullptr; doConnectGreen = false;
        pBLEScan->start(0, true);
    }
    
    delay(100); 
}