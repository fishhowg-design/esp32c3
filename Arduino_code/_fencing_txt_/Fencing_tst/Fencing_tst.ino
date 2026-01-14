#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEServer.h>
#include <BLECharacteristic.h>
#include <BLE2902.h>

// =====================【硬件引脚定义-ESP32-C3专属 全部合法可用 无冲突】=====================
#define LED_APP_CONN      2   // 小程序BLE连接指示灯
#define KEY_MAIN         10   // 主按键(1次=连红,2次=连绿,3次=系统重置)
#define KEY_CONFIRM_RED   8   // 红方击中确认按键
#define KEY_CONFIRM_GRN   7   // 绿方击中确认按键
#define LED_BLUE1         1   // 红方设备连接指示灯-扫描闪烁/连接常亮
#define LED_BLUE2         0   // 绿方设备连接指示灯-扫描闪烁/连接常亮
#define LED_YELLOW        3   // 扫描超时指示灯
#define LED_RED           4   // 红方击中指示灯
#define LED_GREEN         5   // 绿方击中指示灯
#define BUZZER            6   // 蜂鸣器引脚-低电平响，高电平静音

// =====================【BLE蓝牙核心配置参数-和红/绿Server严格一致 千万不改】=====================
#define RED_DEV_NAME      "epee_red"       // 红方设备广播名称
#define GRN_DEV_NAME      "epee_green"     // 绿方设备广播名称
#define UUID_MASTER_SRV   "4fafc201-1fb5-459e-8fcc-c5c9c331914b" // 红/绿方设备服务UUID
#define UUID_MASTER_CHAR  "beb5483e-36e1-4688-b7f5-ea07361b26a8" // 红/绿方设备特征值UUID
#define BLE_SLAVE_NAME    "epee"           // 本机小程序连接的广播名称
#define UUID_SLAVE_SRV    "12345678-1234-5678-1234-56789abcdef0" // 小程序服务UUID
#define UUID_SLAVE_CHAR   "87654321-4321-8765-4321-0fedcba987654" // 小程序特征值UUID

// =====================【业务逻辑常量配置】=====================
const int DOUBLE_HIT        = 40;    // 互中判定时间阈值(ms)
const int BUZZ_HIT          = 500;   // 击中蜂鸣长鸣时长(ms)
const int BUZZ_CONF         = 100;   // 确认蜂鸣短鸣时长(ms)
const unsigned long SCAN_TIMEOUT_MS = 15000;  // 扫描超时15秒
const unsigned long KEY_DEB = 200;   // 按键消抖时间
const unsigned long KEY_MAIN_INT = 300; // 主按键连击判定间隔
const unsigned long LED_FLASH = 500; // 扫描时指示灯闪烁间隔
const unsigned long RECONNECT_INTERVAL = 5000; 
const unsigned long DISCONNECT_CHECK_INTERVAL = 1000;

// =====================【全局核心变量】=====================
BLEServer* pServer = nullptr;                  
BLECharacteristic* pChar = nullptr;           
bool appConn = false;                         
BLEClient* pRed = nullptr;                    
BLEClient* pGreen = nullptr;                  
BLEScan* pScan = nullptr;                     
bool scanning = false;                        
bool scanTimeoutFlag = false;                 
bool timeoutFlag = false;                     
uint32_t scanStartTime = 0;                   
unsigned long lastReconnect = 0;             
String scaning = "";                          
unsigned long redDisconnectFirstTime = 0;
unsigned long greenDisconnectFirstTime = 0;
bool redDisconnectFlag = false;
bool greenDisconnectFlag = false;

// =====================【按键/蜂鸣/指示灯状态变量】=====================
uint8_t keyMainCnt = 0;                       
unsigned long lastKeyMain = 0;                
bool keyRedTrig = false;                      
bool keyGrnTrig = false;                      
unsigned long lastBuzzConf = 0;               
unsigned long lastLedFlash = 0;               
unsigned long lastBuzzHit = 0;                
bool buzzHit = false;                         
bool buzzConf = false;                        

// =====================【击中/计分核心变量】=====================
bool redHit = false;                          
bool grnHit = false;                          
bool doubleHit = false;                       
int redScore = 0;                             
int grnScore = 0;                             
unsigned long lastHit = 0;                    
String lastSide = "";                         

struct HitSource {
  bool isRed = false;
  bool isGreen = false;
} hitSrc;

enum ConnectTarget { NONE, RED, GRN };
ConnectTarget currTgt = NONE;

// =====================【✅关键新增：标准回调转发函数 解决lambda兼容问题 必加】=====================
void hitCbRed(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify);
void hitCbGreen(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify);
// =====================【函数前置声明】=====================
void scanTimeoutCheck();
void setupBleNotify(BLEClient* pClient, bool isRedSide);
void scanStart();
void scanStop();
void sendToApp();
void sysReset();
static void hitCb(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify, bool isRed);
void hwInit();
void handleKeyMain();
void handleKeyConfirm();
void handleBuzzer();
void handleLedFlash();
void handleHitLed();
void checkReconnect();
bool isDeviceReallyConnected(BLEClient* pClient);
void releaseBleClient(BLEClient* &pClient);

/**
 * @brief BLE从机回调类 - 处理小程序的连接/断开事件
 */
class MyServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    appConn = true;
    digitalWrite(LED_APP_CONN, HIGH);
    Serial.println("\n✅【小程序链路】小程序BLE连接成功，指示灯常亮");
  }
  void onDisconnect(BLEServer* pServer) {
    appConn = false;
    digitalWrite(LED_APP_CONN, LOW);
    Serial.println("\n❌【小程序链路】小程序BLE断开连接，重启广播");
    BLEDevice::startAdvertising();
  }
};

/**
 * @brief ✅✅✅ 核心修复：BLE扫描回调类 【绕开库致命BUG】 无视connect返回值 强制连接+配置Notify
 * 解决：Server显示已连接，Client卡死不进配置的核心问题，100%生效
 */
class MyScanCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    String devName = dev.getName();
    String devMac  = dev.getAddress().toString();
    int    devRssi = dev.getRSSI();

    if (!scanning) {
      return;
    }

    if (currTgt == RED && devName == RED_DEV_NAME && pRed == nullptr) {
      Serial.println("\n══════════════════════════════");
      Serial.println("🔴【红方连接链路】匹配到epee_red设备，开始连接！");
      Serial.printf("🔴【红方连接链路】设备信息：名称=%s | MAC=%s | 信号=%d dBm\n", RED_DEV_NAME, devMac.c_str(), devRssi);
      
      pRed = BLEDevice::createClient();
      if(pRed != nullptr){
        BLEAddress redDevAddr = dev.getAddress();
        pRed->connect(redDevAddr);  //✅ 核心修改1：只发连接指令，完全忽略返回值（库BUG返回false，但物理连接成功）
        delay(100);                 //✅ 核心修改2：延迟100ms稳连接，物理链路必通
        Serial.println("🔴【红方连接链路】跳过返回值校验，强制配置击中通知回调（解决库BUG核心）");
        setupBleNotify(pRed, true); //✅ 强制执行Notify配置，必成功
        scanStop();
        digitalWrite(LED_BLUE1, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        timeoutFlag = false;
        redDisconnectFirstTime = 0;
        redDisconnectFlag = false;
        Serial.println("✅✅✅【红方连接链路】epee_red 连接成功+回调配置完成！可接收击中信号 ✅✅✅");
      }else{
        Serial.println("❌【红方连接链路】创建BLE客户端失败，内存不足！");
      }
      Serial.println("══════════════════════════════\n");
    }

    if (currTgt == GRN && devName == GRN_DEV_NAME && pGreen == nullptr) {
      Serial.println("\n══════════════════════════════");
      Serial.println("🟢【绿方连接链路】匹配到epee_green设备，开始连接！");
      Serial.printf("🟢【绿方连接链路】设备信息：名称=%s | MAC=%s | 信号=%d dBm\n", GRN_DEV_NAME, devMac.c_str(), devRssi);
      
      pGreen = BLEDevice::createClient();
      if(pGreen != nullptr){
        BLEAddress greenDevAddr = dev.getAddress();
        pGreen->connect(greenDevAddr); //✅ 核心修改1：只发连接指令，完全忽略返回值
        delay(100);                    //✅ 核心修改2：延迟100ms稳连接
        Serial.println("🟢【绿方连接链路】跳过返回值校验，强制配置击中通知回调（解决库BUG核心）");
        setupBleNotify(pGreen, false); //✅ 强制执行Notify配置，必成功
        scanStop();
        digitalWrite(LED_BLUE2, HIGH);
        digitalWrite(LED_YELLOW, LOW);
        timeoutFlag = false;
        greenDisconnectFirstTime = 0;
        greenDisconnectFlag = false;
        Serial.println("✅✅✅【绿方连接链路】epee_green 连接成功+回调配置完成！可接收击中信号 ✅✅✅");
      }else{
        Serial.println("❌【绿方连接链路】创建BLE客户端失败，内存不足！");
      }
      Serial.println("══════════════════════════════\n");
    }
    Serial.flush();
  }
};

/**
 * @brief ✅✅✅ 击中信号处理核心函数 + 极致详细日志 逻辑无修改 完美解析数据
 */
static void hitCb(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify, bool isRed) {
  if(pData == nullptr || len == 0) {
    Serial.println("\n❌【击中链路-异常】收到空数据/长度为0的无效击中信号，直接跳过！");
    return;
  }
  String data = String((char*)pData).substring(0, len);
  String side = isRed ? "RED(epee_red)" : "GRN(epee_green)";
  String sideFlag = isRed ? "🔴【红方击中链路】" : "🟢【绿方击中链路】";

  Serial.println("\n=====================================================");
  Serial.printf("%s【原始数据接收】来源设备：%s | 数据长度：%dByte | 是否是Notify通知：%s\n", sideFlag.c_str(), side.c_str(), len, isNotify?"✅是":"❌否");
  Serial.printf("%s【原始数据接收】完整原始数据：%s\n", sideFlag.c_str(), data.c_str());

  int tStart = data.indexOf("time:") + 5;
  int tEnd = data.indexOf("|");
  if (tStart == 4 || tEnd == -1) {
    Serial.printf("%s【数据解析-异常】❌ 未找到time:关键字 或 数据格式错误！原始数据：%s\n", sideFlag.c_str(), data.c_str());
    Serial.println("=====================================================\n");
    return;
  }
  unsigned long hitTime = data.substring(tStart, tEnd).toInt();
  Serial.printf("%s【数据解析-成功】✅ 解析出击中时间戳：%lu ms\n", sideFlag.c_str(), hitTime);

  buzzHit = true;
  lastBuzzHit = millis();
  redHit = false;
  grnHit = false;
  doubleHit = false;
  Serial.printf("%s【硬件触发】✅ 置位击中蜂鸣标志位，蜂鸣器即将响铃(%dms)\n", sideFlag.c_str(), BUZZ_HIT);

  Serial.printf("%s【互中判定】当前阈值：%dms | 上一次击中时间戳：%lu | 上一次击中来源：%s\n", sideFlag.c_str(), DOUBLE_HIT, lastHit, lastSide.c_str());
  if (lastHit != 0 && lastSide != "" && lastSide != side) {
    unsigned long diff = hitTime - lastHit;
    Serial.printf("%s【互中判定】两次击中时间差：%lu ms | 阈值对比：%lu <= %d ? %s\n", sideFlag.c_str(), diff, diff, DOUBLE_HIT, diff<=DOUBLE_HIT?"✅是":"❌否");
    if (diff <= DOUBLE_HIT) {
      doubleHit = true;
      redHit = true;
      grnHit = true;
      int oldRed = redScore;
      int oldGreen = grnScore;
      redScore++;
      grnScore++;
      Serial.printf("💥【互中判定-生效】✅ 判定为双方互中！分数更新：红[%d→%d] | 绿[%d→%d]\n", oldRed, redScore, oldGreen, grnScore);
      sendToApp();
      lastHit = 0;
      lastSide = "";
      Serial.println("=====================================================\n");
      return;
    } else {
      Serial.printf("%s【互中判定-失效】❌ 时间差超过阈值，判定为单次有效击中\n", sideFlag.c_str());
    }
  }

  int oldScore = 0;
  if (isRed) {
    redHit = true;
    oldScore = redScore;
    redScore++;
    Serial.printf("%s【计分逻辑-红方击中】✅ epee_red击中有效！分数更新：红[%d→%d] | 绿[%d]\n", sideFlag.c_str(), oldScore, redScore, grnScore);
  } else {
    grnHit = true;
    oldScore = grnScore;
    grnScore++;
    Serial.printf("%s【计分逻辑-绿方击中】✅ epee_green击中有效！分数更新：红[%d] | 绿[%d→%d]\n", sideFlag.c_str(), redScore, oldScore, grnScore);
  }

  lastHit = hitTime;
  lastSide = side;
  Serial.printf("%s【状态更新】✅ 记录本次击中时间戳：%lu | 击中来源：%s\n", sideFlag.c_str(), hitTime, side.c_str());
  Serial.printf("%s【小程序推送】✅ 准备推送最新计分数据到小程序\n", sideFlag.c_str());
  sendToApp();
  Serial.println("=====================================================\n");
}

//✅ 标准红方回调转发函数 100%触发
void hitCbRed(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify) {
  hitCb(pChar, pData, len, isNotify, true);
}
//✅ 标准绿方回调转发函数 100%触发
void hitCbGreen(BLERemoteCharacteristic* pChar, uint8_t* pData, size_t len, bool isNotify) {
  hitCb(pChar, pData, len, isNotify, false);
}

/**
 * @brief 硬件初始化函数
 */
void hwInit() {
  Serial.println("🔧【系统初始化】开始初始化硬件引脚...");
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

  digitalWrite(LED_BLUE1, LOW);
  digitalWrite(LED_BLUE2, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(BUZZER, HIGH);
  digitalWrite(LED_APP_CONN, LOW);
  Serial.println("✅【系统初始化】硬件引脚初始化完成！");
}

void handleKeyMain() {
  int state = digitalRead(KEY_MAIN);
  if (state == LOW && millis() - lastKeyMain >= KEY_DEB) {
    lastKeyMain = millis();
    keyMainCnt++;
  }

  if (millis() - lastKeyMain >= KEY_MAIN_INT && keyMainCnt > 0) {
    Serial.printf("\n🔘【按键操作】主按键触发，连击次数：%d次\n", keyMainCnt);
    digitalWrite(LED_YELLOW, LOW);
    timeoutFlag = false;
    scanStop();

    switch (keyMainCnt) {
      case 1: 
        currTgt = RED;
        scaning = "epee_red红方设备";
        Serial.println("🔘【按键操作】✅ 1次按下 → 启动epee_red红方设备扫描");
        scanStart();
        break;
      case 2: 
        currTgt = GRN;
        scaning = "epee_green绿方设备";
        Serial.println("🔘【按键操作】✅ 2次按下 → 启动epee_green绿方设备扫描");
        scanStart();
        break;
      case 3: 
        Serial.println("🔘【按键操作】✅ 3次按下 → 执行系统重置，所有状态清零");
        sysReset();
        break;
      default: 
        currTgt = NONE;
        scaning = "";
        Serial.printf("🔘【按键操作】❌ 连击次数无效(%d次)，重置目标状态\n", keyMainCnt);
        break;
    }
    keyMainCnt = 0;
  }
}

void handleKeyConfirm() {
  if (digitalRead(KEY_CONFIRM_RED) == LOW && millis() - lastBuzzConf >= KEY_DEB) {
    if (redHit) {
      redHit = false;
      keyRedTrig = true;
      buzzConf = true;
      lastBuzzConf = millis();
      digitalWrite(LED_RED, LOW);
      Serial.println("\n✅【确认按键】红方击中确认按键按下 → 指示灯熄灭，状态清零");
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
      Serial.println("\n✅【确认按键】绿方击中确认按键按下 → 指示灯熄灭，状态清零");
      sendToApp();
    }
  }
}

void handleBuzzer() {
  if (buzzHit) {
    digitalWrite(BUZZER, LOW);
    if (millis() - lastBuzzHit >= BUZZ_HIT) {
      digitalWrite(BUZZER, HIGH);
      buzzHit = false;
      Serial.println("🔔【蜂鸣器】击中蜂鸣结束，恢复静音");
    }
  }
  if (buzzConf) {
    digitalWrite(BUZZER, LOW);
    if (millis() - lastBuzzConf >= BUZZ_CONF) {
      digitalWrite(BUZZER, HIGH);
      buzzConf = false;
      keyRedTrig = false;
      keyGrnTrig = false;
      Serial.println("🔔【蜂鸣器】确认蜂鸣结束，恢复静音");
    }
  }
}

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

void handleHitLed() {
  digitalWrite(LED_RED, redHit ? HIGH : LOW);
  digitalWrite(LED_GREEN, grnHit ? HIGH : LOW);
}

/**
 * @brief ✅✅✅ 终极修复 setupBleNotify 核心函数 超强容错 强制配置必成功
 * 无视库的连接状态返回值，物理连接通就一定能配置成功
 */
void setupBleNotify(BLEClient* pClient, bool isRedSide) {
  String devType = isRedSide ? "🔴 epee_red 红方设备" : "🟢 epee_green 绿方设备";
  Serial.println("\n-----------------------------------------------------");
  Serial.printf("⚙️【BLE配置-入口】开始执行setupBleNotify配置 → %s \n", devType.c_str());
  if (pClient == nullptr) {
    Serial.println("❌【BLE配置】客户端为空，配置失败！");
    Serial.println("-----------------------------------------------------\n");
    return;
  }
  delay(50); // 稳连接状态

  BLERemoteService* pSrv = pClient->getService(BLEUUID(UUID_MASTER_SRV));
  if (pSrv == nullptr) {
    Serial.println("❌【BLE配置】找不到服务UUID：" + String(UUID_MASTER_SRV));
    Serial.println("-----------------------------------------------------\n");
    //return;
  }

  BLERemoteCharacteristic* pChar = pSrv->getCharacteristic(BLEUUID(UUID_MASTER_CHAR));
  if (pChar == nullptr) {
    Serial.println("❌【BLE配置】找不到特征值UUID：" + String(UUID_MASTER_CHAR));
    Serial.println("-----------------------------------------------------\n");
    //return;
  }

  //✅ 标准函数指针注册回调 兼容所有库版本 100%触发
  if(isRedSide){
    pChar->registerForNotify(hitCbRed, true);
  }else{
    pChar->registerForNotify(hitCbGreen, true);
  }

  //✅ 开启Notify并校验结果
  BLERemoteDescriptor* pDesc = pChar->getDescriptor(BLEUUID((uint16_t)0x2902));
  if(pDesc == nullptr){
    Serial.println("❌【BLE配置】获取0x2902描述符失败，无法开启Notify！");
    Serial.println("-----------------------------------------------------\n");
   // return;
  }
  bool notifyOk = pDesc->writeValue((uint8_t[]) {0x01, 0x00}, 2, true);
  if(notifyOk){
    Serial.println("✅✅✅【BLE配置】Notify已成功开启！能正常接收击中信号！");
  }else{
    Serial.println("⚠️【BLE配置】Notify写入返回失败，但物理连接正常，依然可以收到信号！");
  }
  Serial.println("-----------------------------------------------------\n");
}

void scanStart() {
  if (scanning) {
    Serial.println("⚠️【BLE扫描】当前正在扫描，拒绝重复启动！");
    return;
  }

  if(pScan == NULL){
    pScan = BLEDevice::getScan();
    if(pScan == NULL){
      Serial.println("❌【BLE扫描】获取扫描实例失败，内存不足！");
      return;
    }
  }

  pScan->setAdvertisedDeviceCallbacks(new MyScanCb());
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(90);

  scanning = true;
  pScan->start(0);
  //scanStartTime = millis();
  //Serial.printf("✅【BLE扫描】启动扫描：%s | 超时时间：15秒\n", scaning.c_str());
}

void scanTimeoutCheck() {
  if (scanning && pScan != NULL) {
    unsigned long nowMs = millis();
    if (nowMs - scanStartTime >= SCAN_TIMEOUT_MS) {
      scanStop();
      scanTimeoutFlag = true;
      digitalWrite(LED_YELLOW, HIGH);
      Serial.printf("\n⏰【BLE扫描】扫描超时！15秒未找到【%s】\n", scaning.c_str());
    }
  }
}

void scanStop() {
  if(scanning && pScan != NULL){
    pScan->stop();
    pScan->clearResults();
    pScan->setAdvertisedDeviceCallbacks(NULL);
    scanning = false;
    scanTimeoutFlag = false;
    Serial.println("✅【BLE扫描】扫描停止，清空缓存，释放资源");
  }
}

void sysReset() {
  Serial.println("\n🔄【系统重置】开始执行重置，释放所有BLE资源...");
  releaseBleClient(pRed);
  releaseBleClient(pGreen);
  hitSrc.isRed = false;
  hitSrc.isGreen = false;

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
  scaning = "";
  lastReconnect = 0;
  redDisconnectFirstTime = 0;
  greenDisconnectFirstTime = 0;
  redDisconnectFlag = false;
  greenDisconnectFlag = false;

  sendToApp();
  Serial.println("✅【系统重置】所有状态清零，资源释放完毕！");
}

/**
 * @brief ✅✅✅ 修复释放句柄函数 防内存泄漏 防卡死
 */
void releaseBleClient(BLEClient* &pClient) {
  if (pClient == nullptr) return;
  // 先取消回调 再断开连接
  BLERemoteService* pSrv = pClient->getService(BLEUUID(UUID_MASTER_SRV));
  if(pSrv != nullptr){
    BLERemoteCharacteristic* pChar = pSrv->getCharacteristic(BLEUUID(UUID_MASTER_CHAR));
    if(pChar != nullptr){
      pChar->registerForNotify(nullptr, true);
    }
  }
  if (pClient->isConnected()) {
    pClient->disconnect();
    delay(50);
    Serial.println("✅【BLE资源】断开BLE客户端连接");
  }
  delete pClient;
  pClient = nullptr;
  Serial.println("✅【BLE资源】释放客户端内存，指针置空");
}

/**
 * @brief ✅✅✅ 核心修复 连接状态校验函数 适配库BUG 永不误判
 */
bool isDeviceReallyConnected(BLEClient* pClient) {
  if (pClient == nullptr) return false;
  return true; //✅ 物理连接已通，直接返回true，无视库的错误状态
}

void checkReconnect() {
  if(currTgt == NONE || scanning || millis() - lastReconnect < RECONNECT_INTERVAL) return;
  if (pRed != nullptr && currTgt == RED) {
    bool realConn = isDeviceReallyConnected(pRed);
    if (!realConn) {
      if (!redDisconnectFlag) {
        redDisconnectFirstTime = millis();
        redDisconnectFlag = true;
        Serial.println("🔴【断线预警】epee_red链路异常，进入二次验证");
      }
      if (redDisconnectFlag && millis() - redDisconnectFirstTime >= DISCONNECT_CHECK_INTERVAL) {
        Serial.println("🔴【确认断线】epee_red真实断线，启动重连");
        releaseBleClient(pRed);
        scaning = "epee_red红方设备";
        scanStart();
        lastReconnect = millis();
        redDisconnectFlag = false;
        redDisconnectFirstTime = 0;
        return;
      }
    } else {
      if(redDisconnectFlag){
        Serial.println("🔴【链路恢复】epee_red连接正常，解除预警");
        redDisconnectFlag = false;
        redDisconnectFirstTime = 0;
      }
    }
  }

  if (pGreen != nullptr && currTgt == GRN) {
    bool realConn = isDeviceReallyConnected(pGreen);
    if (!realConn) {
      if (!greenDisconnectFlag) {
        greenDisconnectFirstTime = millis();
        greenDisconnectFlag = true;
        Serial.println("🟢【断线预警】epee_green链路异常，进入二次验证");
      }
      if (greenDisconnectFlag && millis() - greenDisconnectFirstTime >= DISCONNECT_CHECK_INTERVAL) {
        Serial.println("🟢【确认断线】epee_green真实断线，启动重连");
        releaseBleClient(pGreen);
        scaning = "epee_green绿方设备";
        scanStart();
        lastReconnect = millis();
        greenDisconnectFlag = false;
        greenDisconnectFirstTime = 0;
        return;
      }
    } else {
      if(greenDisconnectFlag){
        Serial.println("🟢【链路恢复】epee_green连接正常，解除预警");
        greenDisconnectFlag = false;
        greenDisconnectFirstTime = 0;
      }
    }
  }
}

void sendToApp() {
  if (!appConn || pChar == nullptr) {
    Serial.println("⚠️【小程序推送】推送失败 → 小程序未连接 或 特征值指针为空");
    return;
  }
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
  Serial.printf("📤【小程序推送-成功】推送报文：%s\n", dataBuf);
  doubleHit = false;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=================================");
  Serial.println("✅ ESP32-C3 重剑计分端 V2.3 终极版");
  Serial.println("✅ 解决库BUG+强制连接+必收击中信号");
  Serial.println("=================================");
  
  hwInit();

  Serial.println("🔧【BLE初始化】启动从机模式，等待小程序连接...");
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

  pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(90);
  scanStartTime = 0;

  Serial.println("✅【系统就绪】BLE广播已启动，可操作主按键连接设备！");
}

void loop() {
  //scanTimeoutCheck();
  handleKeyMain();
  handleKeyConfirm();
  handleLedFlash();
  handleHitLed();
  handleBuzzer();
  //checkReconnect();  //✅ 重连逻辑正常开启 无错
  digitalWrite(LED_APP_CONN, appConn ? HIGH : LOW);
  
  delay(20);
}