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
const unsigned long SCAN_TIMEOUT_MS = 15000;  // ✅新增：15秒扫描超时 (15000毫秒)
bool scanTimeoutFlag = false;   // ✅新增：扫描超时标志位，标记本次是否超时停止

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
void scanTimeoutCheck();
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
    // 提取设备所有核心信息，全部打印，调试必备
    String devName = dev.getName();
    String devMac  = dev.getAddress().toString();
    int    devRssi = dev.getRSSI();
    bool   hasName = dev.haveName();
    // ✅ 修复日志小错误：原日志打印的是devName，改为真实的扫描目标（红/绿/无），日志更准确
    Serial.printf("[BLE扫描-调试] 📌 进入 BLEAdvertisedDeviceCallbacks() 函数 | 当前scanning状态：%s | 扫描目标：%s\n", scanning?"✅正在扫描":"❌未扫描", currTgt == RED ? "🔴红方" : (currTgt == GRN ? "🟢绿方" : "⚫无目标"));
    // ====== 前置校验+基础日志【必看】：扫描状态+设备基础信息全打印 ======

    if (!scanning) {
      Serial.printf("[BLE回调-过滤] ⚠️ 扫描已停止，过滤本次设备广播 | 设备名：%s | MAC：%s\n", dev.getName().c_str(), dev.getAddress().toString().c_str());
      return;
    }
   

    Serial.printf("\n[BLE回调-发现设备] 📡 检测到BLE设备 → 名称：%s | MAC地址：%s | 信号强度：%d dBm | 有名称：%s\n",
                  devName.isEmpty()?"【空名称/无广播名】":devName.c_str(),
                  devMac.c_str(),//✅ 恢复你注释掉的MAC地址打印，调试必须看MAC
                  devRssi,
                  hasName?"✅是":"❌否");

    // ====== 匹配条件前置校验日志：当前目标+设备名+指针状态，一目了然 ======
    Serial.printf("[BLE回调-匹配校验] 📌 当前扫描目标：%s | 匹配设备名要求：%s | 红方指针状态：%s | 绿方指针状态：%s\n",
                  currTgt == RED ? "🔴红方" : (currTgt == GRN ? "🟢绿方" : "⚫无目标"),
                  currTgt == RED ? RED_DEV_NAME : (currTgt == GRN ? GRN_DEV_NAME : "无"),
                  pRed == nullptr ? "✅空(可连接)" : "❌非空(已连接)",
                  pGreen == nullptr ? "✅空(可连接)" : "❌非空(已连接)");

    // ====== 红方设备匹配+连接逻辑【原逻辑不变+修复2个致命BUG+全流程日志】 ======
    if (currTgt == RED && devName == RED_DEV_NAME && pRed == nullptr) {
      Serial.println("══════════════════════════════");
      Serial.println("[BLE回调-红方] 🔴 ✅ 满足红方连接条件 → 开始执行红方设备连接流程！");
      Serial.printf("[BLE回调-红方] 🔴 待连接设备：名称=%s | MAC=%s | RSSI=%d dBm\n", RED_DEV_NAME, devMac.c_str(), devRssi);
      
      pRed = BLEDevice::createClient();
      Serial.printf("[BLE回调-红方] 🔴 创建BLE客户端实例 → pRed指针状态：%s\n", pRed == nullptr ? "❌创建失败" : "✅创建成功");
      
      // ✅ 修复BUG1：空指针校验，防止创建失败后访问空指针触发崩溃
      if(pRed != nullptr){
        // ✅ 修复BUG2：用MAC地址创建永久BLEAddress对象连接，替代临时dev对象，彻底解决Load access fault
        BLEAddress redDevAddr = dev.getAddress();
        if (pRed->connect(redDevAddr)) {
          Serial.println("[BLE回调-红方] 🔴 ✅ BLE底层连接成功！开始配置Notify通知回调...");
          setupBleNotify(pRed, true);
          scanStop(); // 调用你的停止扫描函数
          // 指示灯状态更新日志
          digitalWrite(LED_BLUE1, HIGH);
          digitalWrite(LED_YELLOW, LOW);
          timeoutFlag = false;
          Serial.println("[BLE回调-红方] ✅✅✅ 红方设备连接+配置全部完成！✅✅✅");
        } else {
          Serial.println("[BLE回调-红方] 🔴 ❌ BLE底层连接失败！设备拒绝连接/超时/信号差");
          delete pRed; // 释放内存
          pRed = nullptr; // 重置指针
          Serial.println("[BLE回调-红方] 🔴 ❌ 已释放红方客户端内存，指针重置为NULL");
        }
      }else{
        // ✅ 新增日志：创建客户端失败的提示
        Serial.println("[BLE回调-红方] 🔴 ❌ 创建BLE客户端失败！内存不足或BLE资源被占用");
      }
      Serial.println("══════════════════════════════\n");
    }

    // ====== 绿方设备匹配+连接逻辑【原逻辑不变+同样修复2个致命BUG+全流程日志】 ======
    if (currTgt == GRN && devName == GRN_DEV_NAME && pGreen == nullptr) {
      Serial.println("══════════════════════════════");
      Serial.println("[BLE回调-绿方] 🟢 ✅ 满足绿方连接条件 → 开始执行绿方设备连接流程！");
      Serial.printf("[BLE回调-绿方] 🟢 待连接设备：名称=%s | MAC=%s | RSSI=%d dBm\n", GRN_DEV_NAME, devMac.c_str(), devRssi);
      
      pGreen = BLEDevice::createClient();
      Serial.printf("[BLE回调-绿方] 🟢 创建BLE客户端实例 → pGreen指针状态：%s\n", pGreen == nullptr ? "❌创建失败" : "✅创建成功");
      
      // ✅ 修复BUG1：空指针校验
      if(pGreen != nullptr){
        // ✅ 修复BUG2：用MAC地址连接，彻底解决崩溃
        BLEAddress greenDevAddr = dev.getAddress();
        if (pGreen->connect(greenDevAddr)) {
          Serial.println("[BLE回调-绿方] 🟢 ✅ BLE底层连接成功！开始配置Notify通知回调...");
          setupBleNotify(pGreen, false);
          scanStop(); // 调用你的停止扫描函数
          // 指示灯状态更新日志
          digitalWrite(LED_BLUE2, HIGH);
          digitalWrite(LED_YELLOW, LOW);
          timeoutFlag = false;
          Serial.println("[BLE回调-绿方] ✅✅✅ 绿方设备连接+配置全部完成！✅✅✅");
        } else {
          Serial.println("[BLE回调-绿方] 🟢 ❌ BLE底层连接失败！设备拒绝连接/超时/信号差");
          delete pGreen; // 释放内存
          pGreen = nullptr; // 重置指针
          Serial.println("[BLE回调-绿方] 🟢 ❌ 已释放绿方客户端内存，指针重置为NULL");
        }
      }else{
        // ✅ 新增日志：创建客户端失败的提示
        Serial.println("[BLE回调-绿方] 🟢 ❌ 创建BLE客户端失败！内存不足或BLE资源被占用");
      }
      Serial.println("══════════════════════════════\n");
    }

    // ====== 未匹配到目标的补充日志【调试关键】：告诉你为什么没连接 ======
    if( !(currTgt == RED && devName == RED_DEV_NAME && pRed == nullptr) && !(currTgt == GRN && devName == GRN_DEV_NAME && pGreen == nullptr) ){
      Serial.printf("[BLE回调-过滤] ⚪ 本次设备不满足连接条件 → 原因：目标=%s | 设备名=%s | 红指针=%s | 绿指针=%s\n",
                    currTgt == RED ? "红" : (currTgt == GRN ? "绿" : "无"),
                    devName.c_str(),
                    pRed==nullptr?"空":"非空",
                    pGreen==nullptr?"空":"非空");
    }
    Serial.flush(); // 强制刷出所有日志，防止丢失
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
  //Serial.printf("[主按键调试] 读取按键电平状态: %d (LOW=按下, HIGH=松开)\n", state);
  if (state == LOW && millis() - lastKeyMain >= KEY_DEB) {
    lastKeyMain = millis();
    keyMainCnt++;
  }

  if (millis() - lastKeyMain >= KEY_MAIN_INT && keyMainCnt > 0) {
    Serial.printf("🔘 主按键触发：%d次\n", keyMainCnt);
    digitalWrite(LED_YELLOW, LOW);
    timeoutFlag = false;
    scanStop();
    Serial.printf("进入switch");
    switch (keyMainCnt) {
      case 1: 
        currTgt = RED;
         Serial.printf("[主按键调试] ✅ 按键1次 → 执行【连接红方】逻辑 | currTgt = RED | 调用 scanStart() 启动红方扫描\n");
        scanStart();
       
        break;
      case 2: 
        currTgt = GRN;
        Serial.printf("[主按键调试] ✅ 按键2次 → 执行【连接绿方】逻辑 | currTgt = GRN | 调用 scanStart() 启动绿方扫描\n");
        scanStart();
        
        break;
      case 3: 
      Serial.printf("[主按键调试] ✅ 按键3次 → 执行【系统重置】逻辑 | 调用 sysReset() 全部状态清零\n");
        sysReset();
        
        break;
      default: 
        Serial.printf("[主按键调试] ❌ 按键次数无效 → 次数：%d | 执行 currTgt = NONE\n", keyMainCnt);
        currTgt = NONE; 
        break;
    }
    Serial.printf("出switch");
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
String scaning = "";            // 你的扫描状态字符串（红方/绿方）

void scanStart() {
  // ========== 【日志1】进入函数+当前扫描状态预检 ==========
  Serial.printf("[BLE扫描-调试] 📌 进入 scanStart() 函数 | 当前scanning状态：%s | 扫描目标：%s\n", scanning?"✅正在扫描":"❌未扫描", scaning.c_str());
  
  // 1. 修复BUG1：原判断逻辑无日志，不知道是否触发「重复扫描拦截」
  if (scanning) {
    Serial.printf("[BLE扫描-警告] ⚠️ 当前正在扫描中，拒绝重复调用 scanStart()，直接退出函数！\n");
    Serial.flush(); // 强制刷出日志，防止丢失
    return;
  }

  // ========== 【日志2】通过预检，开始初始化BLE扫描参数 ==========
  Serial.println("[BLE扫描-信息] ✅ 通过状态预检，开始初始化BLE扫描配置...");

  // 2. 获取BLE扫描实例
  pScan = BLEDevice::getScan();
  if(pScan == NULL){
    Serial.printf("[BLE扫描-错误] ❌ 获取BLE扫描实例失败 pScan = NULL，初始化失败！\n");
    scanning = false;
    Serial.flush();
    return;
  }
  Serial.println("[BLE扫描-成功] ✔️ BLE扫描实例获取成功 pScan ✔️");

  // 3. 设置扫描回调函数
  pScan->setAdvertisedDeviceCallbacks(new MyScanCb());
  Serial.println("[BLE扫描-成功] ✔️ 已绑定扫描回调函数 MyScanCb() ✔️");

  // 4. 设置主动扫描（必须开启，扫描BLE从机必备）
  pScan->setActiveScan(true);
  Serial.printf("[BLE扫描-配置] ⚙️ 设置扫描模式：主动扫描 ActiveScan = true\n");

  // 5. 设置BLE扫描的时间参数
  pScan->setInterval(100);  // 扫描间隔 100ms
  pScan->setWindow(90);     // 扫描窗口 90ms
  Serial.printf("[BLE扫描-配置] ⚙️ 设置扫描参数 | 间隔：%d ms | 窗口：%d ms\n", 100, 90);

  // ========== 【日志3】所有配置完成，启动扫描 ==========
  Serial.println("[BLE扫描-执行] 🚀 配置全部完成，准备启动BLE无限时扫描...");
  scanning = true;  // 标记为【正在扫描】
  pScan->start(0);  // start(0) = 无限扫描，直到调用 stop() 才停止
  
  scanStartTime = millis(); // 记录扫描启动的时间戳

  // ========== 【日志4】扫描启动成功 最终状态日志 ==========
  Serial.printf("[BLE扫描-成功] 🎯 BLE扫描【%s】启动成功！扫描开始时间戳：%lu ms | 扫描模式：无限扫描\n", scaning.c_str(), scanStartTime);
  Serial.println("---------------------------------------------------");
  Serial.flush(); // 强制刷新串口缓冲区，确保所有日志都能打印出来，不丢失
}

void scanTimeoutCheck() {
  // 只有【正在扫描】状态，才需要检测超时
  if (scanning && pScan != NULL) {
    unsigned long nowMs = millis();
    // 核心判断：15秒超时条件
    if (nowMs - scanStartTime >= SCAN_TIMEOUT_MS) {
      // 执行超时停止操作
      pScan->stop();                // ✅停止BLE扫描
      pScan->clearResults();        // ✅清空扫描结果缓存，释放内存
      scanTimeoutFlag = true;       // ✅标记本次扫描【超时停止】
      scanning = false;             // ✅扫描状态置为 停止
      
      // ========== 超时报错日志【醒目】 ==========
      Serial.println("\n=====================================");
      Serial.printf("[BLE扫描-超时] ⏰ ⚠️ 【%s】扫描超时！已扫描满%d秒未找到目标设备，自动停止扫描\n", scaning.c_str(), SCAN_TIMEOUT_MS/1000);
      Serial.println("=====================================\n");
      Serial.flush();
    }
  }
}

// BLE扫描停止
void scanStop() {
  Serial.println("\n[BLE扫描] ⏹️ 执行 scanStop() 停止扫描函数！");
  if(scanning && pScan != NULL){
    pScan->stop();                // 停止扫描
    pScan->clearResults();        // 清空扫描结果缓存，释放内存
    scanning = false;             // 重置扫描状态
    scanTimeoutFlag = false;      // 重置超时标志
    Serial.println("[BLE扫描] ✅ BLE扫描已停止 + 缓存已清空 + 状态已重置！");
  } else {
    Serial.println("[BLE扫描] ⚠️ 扫描未运行，无需停止！");
  }
  Serial.flush();
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
  scanTimeoutCheck();  // ✅必须加：15秒扫描超时检测，放在loop最顶部
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