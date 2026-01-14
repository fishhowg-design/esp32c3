#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>
#include <BLE2902.h>

// ✅【修复】ESP32-C3 专属合法引脚定义 (全部可用，无GPIO20/21/12)
#define LED_APP_CONN  2   // 小程序连接指示灯
#define KEY_MAIN      10   // 主按键(1=连红,2=连绿,3=重置)
#define KEY_CONFIRM_RED 8 // 红方确认按键
#define KEY_CONFIRM_GRN 7 // 绿方确认按键
#define LED_BLUE1     1   // 红方连接指示灯-闪烁/常亮
#define LED_BLUE2     0   // 绿方连接指示灯-闪烁/常亮
#define LED_YELLOW    3   // 扫描超时指示灯
#define LED_RED       4   // 红方击中指示灯
#define LED_GREEN     5   // 绿方击中指示灯
#define BUZZER        6   // 蜂鸣器引脚

// BLE核心配置
#define RED_DEV_NAME "epee_red"
#define GRN_DEV_NAME "epee_green"
#define UUID_MASTER_SRV "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define UUID_MASTER_CHAR "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define BLE_SLAVE_NAME "epee"
#define UUID_SLAVE_SRV "12345678-1234-5678-1234-56789abcdef0"
#define UUID_SLAVE_CHAR "87654321-4321-8765-4321-0fedcba987654"

// 核心全局变量
BLEServer* pServer = nullptr;
BLECharacteristic* pChar = nullptr;
bool appConn = false;

// 核心参数
const int DOUBLE_HIT = 40;
const int BUZZ_HIT = 500;
const int BUZZ_CONF = 100;
const uint32_t CONN_TIMEOUT = 10000;
const unsigned long KEY_DEB = 200;
const unsigned long KEY_MAIN_INT = 300;
const unsigned long LED_FLASH = 500;

// ✅【修复】全局变量优化+新增击中来源标识结构体
BLEClient* pRed = nullptr;
BLEClient* pGreen = nullptr;
BLEScan* pScan = nullptr;
bool scanning = false;
uint32_t scanStartTime = 0;
bool timeoutFlag = false;

uint8_t keyMainCnt = 0;
unsigned long lastKeyMain = 0;
bool keyRedTrig = false;
bool keyGrnTrig = false;
unsigned long lastBuzzConf = 0;

unsigned long lastLedFlash = 0;
unsigned long lastBuzzHit = 0;
bool buzzHit = false;
bool buzzConf = false;

bool redHit = false;
bool grnHit = false;
bool doubleHit = false;
int redScore = 0;
int grnScore = 0;
unsigned long lastHit = 0;
String lastSide = "";

// 击中来源标识-解决currSide冲突问题
struct HitSource {
  bool isRed = false;
  bool isGreen = false;
} hitSrc;

enum ConnectTarget { NONE, RED, GRN };
ConnectTarget currTgt = NONE;

// 函数前置声明
void setupBleNotify(BLEClient* pClient, bool isRedSide);
void scanStart();
void scanStop();
void sendToApp();
void sysReset();
static void hitCb(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify, bool isRed);

// BLE从机回调-小程序连接/断开
class MyServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    appConn = true;
    digitalWrite(LED_APP_CONN, HIGH);
    Serial.println("✅ 小程序已连接");
  }
  void onDisconnect(BLEServer* pServer) {
    appConn = false;
    digitalWrite(LED_APP_CONN, LOW);
    Serial.println("❌ 小程序断开，重启广播");
    BLEDevice::startAdvertising();
  }
};

// BLE扫描回调-扫描红/绿方设备
class MyScanCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (!scanning) return;
    String devName = dev.getName();

    if (currTgt == RED && devName == RED_DEV_NAME && pRed == nullptr) {
      Serial.println("🔴 正在连接红方设备...");
      pRed = BLEDevice::createClient();
      if (pRed->connect(&dev)) {
        setupBleNotify(pRed, true);
        scanStop();
        digitalWrite(LED_BLUE1, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        timeoutFlag = false;
        Serial.println("✅ 红方连接成功！");
      } else {
        Serial.println("❌ 红方连接失败");
        delete pRed;
        pRed = nullptr;
      }
    }

    if (currTgt == GRN && devName == GRN_DEV_NAME && pGreen == nullptr) {
      Serial.println("🟢 正在连接绿方设备...");
      pGreen = BLEDevice::createClient();
      if (pGreen->connect(&dev)) {
        setupBleNotify(pGreen, false);
        scanStop();
        digitalWrite(LED_BLUE2, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        timeoutFlag = false;
        Serial.println("✅ 绿方连接成功！");
      } else {
        Serial.println("❌ 绿方连接失败");
        delete pGreen;
        pGreen = nullptr;
      }
    }
  }
};

// ✅【修复】击中回调函数 - 新增isRed参数，彻底解决击中来源冲突
static void hitCb(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify, bool isRed) {
  String data = String((char*)pData).substring(0, len);
  String side = isRed ? "RED" : "GRN";
  Serial.printf("⚡ %s击中：%s\n", side.c_str(), data.c_str());

  int tStart = data.indexOf("time:") + 5;
  int tEnd = data.indexOf("|");
  if (tStart == 4 || tEnd == -1) {
    Serial.println("❌ 击中数据格式错误");
    return;
  }
  unsigned long hitTime = data.substring(tStart, tEnd).toInt();

  buzzHit = true;
  lastBuzzHit = millis();
  redHit = false;
  grnHit = false;
  doubleHit = false;

  // 互中判定逻辑
  if (lastHit != 0 && lastSide != "" && lastSide != side) {
    unsigned long diff = hitTime - lastHit;
    if (diff <= DOUBLE_HIT) {
      doubleHit = true;
      redHit = true;
      grnHit = true;
      redScore++;
      grnScore++;
      Serial.printf("💥 互中判定！红方:%d 绿方:%d\n", redScore, grnScore);
      sendToApp();
      lastHit = 0;
      lastSide = "";
      return;
    }
  }

  // 单方击中计分
  if (isRed) {
    redHit = true;
    redScore++;
    Serial.printf("🔴 红方有效击中！红:%d 绿:%d\n", redScore, grnScore);
  } else {
    grnHit = true;
    grnScore++;
    Serial.printf("🟢 绿方有效击中！红:%d 绿:%d\n", redScore, grnScore);
  }

  lastHit = hitTime;
  lastSide = side;
  sendToApp();
}

// 硬件初始化
void hwInit() {
  Serial.println("🔧 开始初始化硬件...");
  pinMode(KEY_MAIN, INPUT_PULLUP);
  pinMode(KEY_CONFIRM_RED, INPUT_PULLUP);
  pinMode(KEY_CONFIRM_GRN, INPUT_PULLUP);

  pinMode(LED_BLUE1, OUTPUT);
  pinMode(LED_BLUE2, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  pinMode(BUZZER, OUTPUT);
  pinMode(LED_APP_CONN, OUTPUT);

  // 初始化所有外设为默认状态
  digitalWrite(LED_BLUE1, LOW);
  digitalWrite(LED_BLUE2, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(BUZZER, HIGH); // 蜂鸣器低电平响，高电平静音
  digitalWrite(LED_APP_CONN, LOW);
  Serial.println("✅ 硬件初始化完成！");
}

// 主按键处理逻辑 (1次=红,2次=绿,3次=重置)
void handleKeyMain() {
  int state = digitalRead(KEY_MAIN);
  if (state == LOW && millis() - lastKeyMain >= KEY_DEB) {
    lastKeyMain = millis();
    keyMainCnt++;
  }

  if (millis() - lastKeyMain >= KEY_MAIN_INT && keyMainCnt > 0) {
    Serial.printf("🔘 主按键触发：%d次\n", keyMainCnt);
    digitalWrite(LED_YELLOW, LOW);
    timeoutFlag = false;
    scanStop();
    switch (keyMainCnt) {
      case 1: currTgt = RED; scanStart(); break;
      case 2: currTgt = GRN; scanStart(); break;
      case 3: sysReset(); break;
      default: Serial.println("❌ 按键次数无效"); currTgt = NONE; break;
    }
    keyMainCnt = 0;
  }
}

// 红/绿方确认按键处理
void handleKeyConfirm() {
  if (digitalRead(KEY_CONFIRM_RED) == LOW && millis() - lastBuzzConf >= KEY_DEB) {
    if (redHit) {
      redHit = false;
      keyRedTrig = true;
      buzzConf = true;
      lastBuzzConf = millis();
      digitalWrite(LED_RED, LOW);
      Serial.println("✅ 红方击中确认！");
      sendToApp();
    }
  }

  if (digitalRead(KEY_CONFIRM_GRN) == LOW && millis() - lastBuzzConf >= KEY_DEB) {
    if (grnHit) {
      grnHit = false;
      keyGrnTrig = true;
      buzzConf = true;
      lastBuzzConf = millis();
      digitalWrite(LED_GREEN, LOW);
      Serial.println("✅ 绿方击中确认！");
      sendToApp();
    }
  }
}

// 蜂鸣器控制 (击中长鸣，确认短鸣)
void handleBuzzer() {
  if (buzzHit) {
    digitalWrite(BUZZER, LOW);
    if (millis() - lastBuzzHit >= BUZZ_HIT) {
      digitalWrite(BUZZER, HIGH);
      buzzHit = false;
    }
  }
  if (buzzConf) {
    digitalWrite(BUZZER, LOW);
    if (millis() - lastBuzzConf >= BUZZ_CONF) {
      digitalWrite(BUZZER, HIGH);
      buzzConf = false;
      keyRedTrig = false;
      keyGrnTrig = false;
    }
  }
}

// 扫描时LED闪烁逻辑
void handleLedFlash() {
  if (!scanning) return;
  if (currTgt == RED) {
    if (millis() - lastLedFlash >= LED_FLASH) {
      lastLedFlash = millis();
      digitalWrite(LED_BLUE1, !digitalRead(LED_BLUE1));
    }
  } else if (currTgt == GRN) {
    if (millis() - lastLedFlash >= LED_FLASH) {
      lastLedFlash = millis();
      digitalWrite(LED_BLUE2, !digitalRead(LED_BLUE2));
    }
  }
}

// 击中指示灯控制
void handleHitLed() {
  digitalWrite(LED_RED, redHit ? HIGH : LOW);
  digitalWrite(LED_GREEN, grnHit ? HIGH : LOW);
}

// ✅【修复】BLE通知配置 - 绑定红/绿方标识，解决击中来源冲突
void setupBleNotify(BLEClient* pClient, bool isRedSide) {
  if (pClient == nullptr) return;
  BLERemoteService* pSrv = pClient->getService(UUID_MASTER_SRV);
  if (pSrv == nullptr) {Serial.println("❌ 找不到主服务UUID"); return;}
  BLERemoteCharacteristic* pChar = pSrv->getCharacteristic(UUID_MASTER_CHAR);
  if (pChar != nullptr) {
    if(isRedSide){
      pChar->registerForNotify([](BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify) {
        hitCb(pChar, pData, len, isNotify, true);
      });
    }else{
      pChar->registerForNotify([](BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify) {
        hitCb(pChar, pData, len, isNotify, false);
      });
    }
  }
}

// BLE扫描启动
void scanStart() {
  if (scanning) return;
  pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyScanCb());
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(90);
  pScan->start(0);
  scanning = true;
  scanStartTime = millis();
  Serial.println("🔍 BLE扫描已启动！");
}

// BLE扫描停止
void scanStop() {
  if (!scanning) return;
  pScan->stop();
  scanning = false;
  Serial.println("🛑 BLE扫描已停止！");
}

// ✅【优化】系统重置 - 释放内存+重置所有状态
void sysReset() {
  if (pRed != nullptr) {
    if (pRed->isConnected()) pRed->disconnect();
    delete pRed;
    pRed = nullptr;
    hitSrc.isRed = false;
  }
  if (pGreen != nullptr) {
    if (pGreen->isConnected()) pGreen->disconnect();
    delete pGreen;
    pGreen = nullptr;
    hitSrc.isGreen = false;
  }

  redScore = 0;
  grnScore = 0;
  lastHit = 0;
  lastSide = "";
  redHit = false;
  grnHit = false;
  doubleHit = false;

  digitalWrite(LED_BLUE1, LOW);
  digitalWrite(LED_BLUE2, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(BUZZER, HIGH);

  currTgt = NONE;
  scanning = false;
  timeoutFlag = false;

  sendToApp();
  Serial.println("🔄 系统已重置，所有状态清零！");
}

// 扫描超时检查
void checkTimeout() {
  if (!scanning || timeoutFlag) return;
  if (millis() - scanStartTime >= CONN_TIMEOUT) {
    timeoutFlag = true;
    scanStop();
    digitalWrite(LED_YELLOW, HIGH);
    currTgt = NONE;
    Serial.println("⏰ BLE扫描超时！");
  }
}

// 断线自动重连
void checkReconnect() {
  if (pRed != nullptr && !pRed->isConnected() && currTgt == RED) {
    Serial.println("🔴 红方设备断线，正在重连...");
    delete pRed;
    pRed = nullptr;
    scanStart();
  }
  if (pGreen != nullptr && !pGreen->isConnected() && currTgt == GRN) {
    Serial.println("🟢 绿方设备断线，正在重连...");
    delete pGreen;
    pGreen = nullptr;
    scanStart();
  }
}

// ✅【修复】发送数据到小程序 + 互中状态清零
void sendToApp() {
  if (!appConn) return;
  char dataBuf[128];
  if (doubleHit) {
    sprintf(dataBuf, "red:%d,grn:%d,state:double,red_confirm:%d,grn_confirm:%d", redScore, grnScore, redHit ? 0 : 1, grnHit ? 0 : 1);
  } else if (redHit) {
    sprintf(dataBuf, "red:%d,grn:%d,state:red_hit,red_confirm:0,grn_confirm:1", redScore, grnScore);
  } else if (grnHit) {
    sprintf(dataBuf, "red:%d,grn:%d,state:grn_hit,red_confirm:1,grn_confirm:0", redScore, grnScore);
  } else {
    sprintf(dataBuf, "red:%d,grn:%d,state:idle,red_confirm:1,grn_confirm:1", redScore, grnScore);
  }
  pChar->setValue(dataBuf);
  pChar->notify();
  Serial.printf("📤 推送数据到小程序：%s\n", dataBuf);
  doubleHit = false; // ✅ 修复：互中状态清零，解决计分卡死
}

// 初始化函数
void setup() {
  Serial.begin(115200);
  Serial.println("=================================");
  Serial.println("✅ ESP32-C3 重剑计分端 - 启动成功");
  Serial.println("=================================");
  hwInit();

  Serial.println("🔧 初始化BLE从机模式...");
  BLEDevice::init(BLE_SLAVE_NAME);
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCb());

  BLEService* pSrv = pServer->createService(UUID_SLAVE_SRV);
  pChar = pSrv->createCharacteristic(UUID_SLAVE_CHAR, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  pChar->addDescriptor(new BLE2902());
  pSrv->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(UUID_SLAVE_SRV);
  pAdv->start();
  Serial.println("✅ BLE广播已启动，等待小程序连接！");

  pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(90);
  scanStartTime = 0; // ✅ 修复：初始化扫描时间，解决首次假超时
}

// 主循环
void loop() {
  handleKeyMain();
  handleKeyConfirm();
  handleLedFlash();
  checkTimeout();
  handleHitLed();
  handleBuzzer();
  checkReconnect();
  digitalWrite(LED_APP_CONN, appConn ? HIGH : LOW);
  delay(20);
}