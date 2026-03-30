/**
 * ESP32-S3-ETH-WiFi 天线切换器 - 完整版
 * 
 * 修改记录:
 * - 2026-03-28: 统一使用config.h,删除重复定义,修复USB引脚冲突
 * - 2026-03-30: 删除MQTT功能(导致重启),整合IC-705 BLE CI-V高频查询(5Hz)
 */

#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
#include <Preferences.h>

#include "config.h"           // 统一配置头文件
#include "cat_controller.h"
#include "cat_hardware.h"
#include "ble_civ.h"
#include "yaesu_cat.h"
#include "kenwood_cat.h"
#include "elecraft_cat.h"
#include "logger.h"             // 日志系统
#include "config_validator.h"   // 配置验证

// 注意: 所有引脚定义已移至 config.h,避免重复定义和维护困难

const int chPins[] = {PIN_CH1, PIN_CH2, PIN_CH3, PIN_CH4, PIN_CH5, PIN_CH6};
int currentCh = 0;
String aliases[6] = {"ANT-1", "ANT-2", "ANT-3", "ANT-4", "ANT-5", "ANT-6"};

// 波段配置结构
struct BandConfig {
  char name[16];
  uint32_t minFreq;
  uint32_t maxFreq;
  uint8_t antenna;
  bool enabled;
};

#define MAX_BANDS 20
BandConfig bands[MAX_BANDS];
int bandCount = 0;

// ============ 互锁保护配置 ============
struct InterlockConfig {
  bool pttLockEnabled;        // PTT互锁使能
  uint32_t pttReleaseDelay;   // PTT释放后等待时间(ms)
  uint32_t relayCooldownMs;   // 继电器冷却时间(ms)
  uint32_t lastSwitchTime;    // 上次切换时间
  bool lastPttState;          // 上次PTT状态
};

InterlockConfig interlockCfg = {
  .pttLockEnabled = true,     // 默认开启
  .pttReleaseDelay = 200,     // PTT释放后200ms才能切换
  .relayCooldownMs = 100,     // 继电器最小间隔100ms
  .lastSwitchTime = 0,
  .lastPttState = false
};

// 切换请求队列（PTT激活时缓存请求）
struct SwitchRequest {
  int channel;
  uint32_t requestTime;
  bool pending;
};
SwitchRequest pendingSwitch = {0, 0, false};

// 网络配置
bool useDHCP = false;
IPAddress staticIP(192,168,1,123);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);
IPAddress dns(8,8,8,8);

// WiFi配置
String wifiSSID = "";
String wifiPass = "";
bool wifiConnected = false;

int relayState = 0;
unsigned long relayTimer = 0;
int targetCh = 0;
unsigned long lastBtn = 0;
bool lastBtnState = HIGH;

WebServer server(80);
Preferences prefs;

// ============ 继电器控制 ============
void updateRelay() {
  switch (relayState) {
    case 1:
      if (millis() - relayTimer >= RESET_PULSE_WIDTH) {
        digitalWrite(PIN_RESET, LOW);
        relayTimer = millis();
        relayState = 2;
      }
      break;
    case 2:
      if (millis() - relayTimer >= 50) {
        if (targetCh > 0) {
          digitalWrite(chPins[targetCh-1], HIGH);
          relayTimer = millis();
          relayState = 3;
        } else {
          currentCh = 0;
          saveChannel(0);
          relayState = 0;
          refreshVFD();
        }
      }
      break;
    case 3:
      if (millis() - relayTimer >= PULSE_WIDTH) {
        digitalWrite(chPins[targetCh-1], LOW);
        currentCh = targetCh;
        saveChannel(currentCh);
        relayState = 0;
        refreshVFD();
        
        // 天线切换完成
      }
      break;
  }
}

// ============ 互锁保护检查 ============
bool canSwitchNow(int ch, String& reason) {
  // 检查基本参数
  if (ch < 0 || ch > 6) {
    reason = "Invalid channel";
    return false;
  }
  
  // 检查是否正在切换中
  if (relayState != 0) {
    reason = "Relay busy";
    return false;
  }
  
  // 检查目标通道是否已经是当前通道
  if (ch == currentCh) {
    reason = "Already on this channel";
    return false;
  }
  
  // 检查PTT互锁
  if (interlockCfg.pttLockEnabled) {
    bool pttActive = catControllerGetPTT();
    
    if (pttActive) {
      reason = "PTT active - switch blocked";
      LOG_W(TAG_HW, "PTT激活，禁止切换到CH%d", ch);
      return false;
    }
    
    // 检查PTT释放后的等待时间
    if (interlockCfg.lastPttState && !pttActive) {
      uint32_t pttReleaseTime = millis() - interlockCfg.lastSwitchTime;
      if (pttReleaseTime < interlockCfg.pttReleaseDelay) {
        reason = "PTT release delay";
        LOG_W(TAG_HW, "PTT刚释放，等待 %lu ms", 
              interlockCfg.pttReleaseDelay - pttReleaseTime);
        return false;
      }
    }
    
    interlockCfg.lastPttState = pttActive;
  }
  
  // 检查继电器冷却时间
  uint32_t timeSinceLastSwitch = millis() - interlockCfg.lastSwitchTime;
  if (timeSinceLastSwitch < interlockCfg.relayCooldownMs) {
    reason = "Relay cooling down";
    return false;
  }
  
  return true;
}

void switchCh(int ch) {
  String reason;
  if (!canSwitchNow(ch, reason)) {
    // 如果是因为PTT，缓存请求
    if (reason.indexOf("PTT") >= 0) {
      pendingSwitch.channel = ch;
      pendingSwitch.requestTime = millis();
      pendingSwitch.pending = true;
      LOG_I(TAG_HW, "切换请求CH%d已缓存，等待PTT释放", ch);
    }
    return;
  }
  
  // 执行切换
  targetCh = ch;
  relayState = 1;
  relayTimer = millis();
  interlockCfg.lastSwitchTime = millis();
  pendingSwitch.pending = false;
  
  digitalWrite(PIN_RESET, HIGH);
  LOG_I(TAG_HW, "开始切换到CH%d", ch);
}

// 检查并处理缓存的切换请求
void processPendingSwitch() {
  if (!pendingSwitch.pending) return;
  
  // 检查请求是否超时（5秒）
  if (millis() - pendingSwitch.requestTime > 5000) {
    LOG_W(TAG_HW, "切换请求CH%d已超时", pendingSwitch.channel);
    pendingSwitch.pending = false;
    return;
  }
  
  String reason;
  if (canSwitchNow(pendingSwitch.channel, reason)) {
    LOG_I(TAG_HW, "执行缓存的切换请求CH%d", pendingSwitch.channel);
    switchCh(pendingSwitch.channel);
  }
}

void saveChannel(int ch) {
  prefs.begin("ant", false);
  prefs.putInt("ch", ch);
  prefs.end();
}

// ============ CAT 控制器回调函数 ============
void switchToChannel(int channel) {
  if (channel >= 0 && channel <= 6) {
    switchCh(channel);
  }
}

int getCurrentChannel() {
  return currentCh;
}

// ============ VFD 驱动 ============
void shiftOutVFD(uint8_t data) {
  for (int i = 0; i < 8; i++) {
    digitalWrite(DISP_DATA, (data & (1 << (7-i))) ? HIGH : LOW);
    digitalWrite(DISP_CLK, HIGH);
    digitalWrite(DISP_CLK, LOW);
  }
}

void refreshVFD() {
  uint8_t buf[4];
  if (currentCh == 0) {
    buf[0] = CHR_O; buf[1] = CHR_P; buf[2] = CHR_E; buf[3] = CHR_N;
  } else {
    buf[0] = CHR_C; buf[1] = CHR_H; buf[2] = CHR_BAR; buf[3] = SEG_MAP[currentCh];
  }
  for (int i = 3; i >= 0; i--) shiftOutVFD(buf[i]);
  digitalWrite(DISP_LATCH, HIGH);
  digitalWrite(DISP_LATCH, LOW);
}

// ============ 配置加载/保存 ============
void loadCfg() {
  prefs.begin("ant", true);
  currentCh = prefs.getInt("ch", 0);
  for (int i = 0; i < 6; i++) aliases[i] = prefs.getString(("a" + String(i)).c_str(), aliases[i]);
  
  useDHCP = prefs.getBool("dhcp", false);
  staticIP = IPAddress(prefs.getUInt("ip", (uint32_t)IPAddress(192,168,1,123)));
  gateway = IPAddress(prefs.getUInt("gw", (uint32_t)IPAddress(192,168,1,1)));
  subnet = IPAddress(prefs.getUInt("sn", (uint32_t)IPAddress(255,255,255,0)));
  
  wifiSSID = prefs.getString("ssid", "");
  wifiPass = prefs.getString("pass", "");
  prefs.end();
  
  loadBands(); // 加载波段配置
  
  // 加载互锁配置
  prefs.begin("interlock", true);
  interlockCfg.pttLockEnabled = prefs.getBool("enabled", true);
  interlockCfg.pttReleaseDelay = prefs.getUInt("delay", 200);
  interlockCfg.relayCooldownMs = prefs.getUInt("cooldown", 100);
  prefs.end();
}

// 加载波段配置
void loadBands() {
  prefs.begin("bands", true);
  bandCount = prefs.getInt("count", 0);
  if (bandCount == 0) {
    // 首次运行，初始化默认波段
    initDefaultBands();
  } else {
    for (int i = 0; i < bandCount; i++) {
      String key = "b" + String(i);
      prefs.getBytes(key.c_str(), &bands[i], sizeof(BandConfig));
    }
  }
  prefs.end();
}

// 保存波段配置
void saveBands() {
  prefs.begin("bands", false);
  prefs.putInt("count", bandCount);
  for (int i = 0; i < bandCount; i++) {
    String key = "b" + String(i);
    prefs.putBytes(key.c_str(), &bands[i], sizeof(BandConfig));
  }
  prefs.end();
}

// 初始化默认波段
void initDefaultBands() {
  bandCount = 13;
  strcpy(bands[0].name, "160m"); bands[0].minFreq = 1800000; bands[0].maxFreq = 2000000; bands[0].antenna = 1; bands[0].enabled = true;
  strcpy(bands[1].name, "80m"); bands[1].minFreq = 3500000; bands[1].maxFreq = 3900000; bands[1].antenna = 2; bands[1].enabled = true;
  strcpy(bands[2].name, "40m"); bands[2].minFreq = 7000000; bands[2].maxFreq = 7300000; bands[2].antenna = 3; bands[2].enabled = true;
  strcpy(bands[3].name, "30m"); bands[3].minFreq = 10100000; bands[3].maxFreq = 10150000; bands[3].antenna = 4; bands[3].enabled = true;
  strcpy(bands[4].name, "20m"); bands[4].minFreq = 14000000; bands[4].maxFreq = 14350000; bands[4].antenna = 4; bands[4].enabled = true;
  strcpy(bands[5].name, "17m"); bands[5].minFreq = 18068000; bands[5].maxFreq = 18168000; bands[5].antenna = 5; bands[5].enabled = true;
  strcpy(bands[6].name, "15m"); bands[6].minFreq = 21000000; bands[6].maxFreq = 21450000; bands[6].antenna = 5; bands[6].enabled = true;
  strcpy(bands[7].name, "12m"); bands[7].minFreq = 24890000; bands[7].maxFreq = 24990000; bands[7].antenna = 6; bands[7].enabled = true;
  strcpy(bands[8].name, "10m"); bands[8].minFreq = 28000000; bands[8].maxFreq = 29700000; bands[8].antenna = 6; bands[8].enabled = true;
  strcpy(bands[9].name, "6m"); bands[9].minFreq = 50000000; bands[9].maxFreq = 54000000; bands[9].antenna = 0; bands[9].enabled = true;
  strcpy(bands[10].name, "2m"); bands[10].minFreq = 144000000; bands[10].maxFreq = 148000000; bands[10].antenna = 0; bands[10].enabled = true;
  strcpy(bands[11].name, "0.7m"); bands[11].minFreq = 430000000; bands[11].maxFreq = 450000000; bands[11].antenna = 0; bands[11].enabled = true;
  strcpy(bands[12].name, "0.23m"); bands[12].minFreq = 1240000000; bands[12].maxFreq = 1300000000; bands[12].antenna = 0; bands[12].enabled = true;
  saveBands();
}

void saveNetConfig() {
  prefs.begin("ant", false);
  prefs.putBool("dhcp", useDHCP);
  prefs.putUInt("ip", (uint32_t)staticIP);
  prefs.putUInt("gw", (uint32_t)gateway);
  prefs.putUInt("sn", (uint32_t)subnet);
  prefs.end();
}

void saveWiFiConfig() {
  prefs.begin("ant", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("pass", wifiPass);
  prefs.end();
}

// ============ 网络初始化 ============
void initNet() {
  Serial.println("[NET] Initializing network...");
  
  SPI.begin(13, 12, 11, 14);
  pinMode(ETH_RST, OUTPUT);
  digitalWrite(ETH_RST, LOW); delay(50);
  digitalWrite(ETH_RST, HIGH); delay(50);
  
  Serial.println("[NET] Starting Ethernet...");
  bool ethOk = ETH.begin(ETH_PHY_W5500, 0, ETH_CS, ETH_INT, ETH_RST, SPI);
  if (ethOk) {
    Serial.println("[NET] Ethernet started OK");
  } else {
    Serial.println("[NET] Ethernet start FAILED!");
  }
  
  if (useDHCP) {
    Serial.println("[NET] Using DHCP");
  } else {
    Serial.println("[NET] Using static IP");
    ETH.config(staticIP, gateway, subnet, dns);
  }
  
  // 连接WiFi
  if (wifiSSID.length() > 0) {
    Serial.print("[NET] Connecting to WiFi: ");
    Serial.println(wifiSSID);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
  }
  
  Serial.println("[NET] Network init complete");
}

// ============ Web 页面 ============
const char DASHBOARD_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>概览 - 天线切换器</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a15;color:#00ffcc;padding:20px}
.box{max-width:400px;margin:0 auto}
.nav{display:flex;gap:10px;margin-bottom:20px}
.nav a{flex:1;padding:10px;text-align:center;background:rgba(255,255,255,.1);border-radius:8px;color:#00ffcc;text-decoration:none;font-size:12px}
.nav a.active{background:#00ff88;color:#000}
.vfd{background:linear-gradient(180deg,#001515 0%,#000a0a 100%);border:2px solid #00ffcc;border-radius:16px;padding:30px 20px;text-align:center;margin-bottom:24px;box-shadow:0 0 30px rgba(0,255,200,.3)}
.vfd-time{font-size:48px;font-weight:bold;color:#00ffcc;text-shadow:0 0 10px #00ffcc;animation:breathe 2s ease-in-out infinite}
@keyframes breathe{0%,100%{opacity:1}50%{opacity:.7}}
.status-card{background:rgba(255,255,255,.05);border:1px solid rgba(0,255,204,.3);border-radius:12px;padding:20px;margin-bottom:15px}
.status-title{font-size:14px;color:#00ffcc;margin-bottom:15px;padding-bottom:10px;border-bottom:1px solid rgba(0,255,204,.3)}
.status-row{display:flex;justify-content:space-between;margin:10px 0;font-size:14px}
.status-label{opacity:.7}
.status-value{font-weight:bold}
.status-value.on{color:#0f0}
.status-value.off{color:#f55}
.status-value.bt{color:#48f}
.status-value.wired{color:#ff0}
.ch-display{font-size:36px;font-weight:bold;text-align:center;color:#00ffcc;margin:10px 0}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin:20px 0}
.ch{background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.1);border-radius:12px;padding:15px;text-align:center;cursor:pointer;transition:all .3s;position:relative}
.ch:hover{background:rgba(255,255,255,.1)}
.ch.active{background:linear-gradient(135deg,#00ff88 0%,#00cc66 100%);border-color:#00ff88;color:#000}
.ch.disabled{opacity:.5;pointer-events:none}
.ch .num{font-size:11px;opacity:.6}
.ch .name{font-size:14px;font-weight:600;word-break:break-all}
.btn{width:100%;padding:15px;margin:5px 0;border:none;border-radius:8px;cursor:pointer;font-size:16px}
.btn-r{background:linear-gradient(135deg,#ff5555 0%,#cc4444 100%);color:#fff}
</style>
</head>
<body>
<div class='box'>
<div class='nav'>
<a href='/' class='active'>概览</a>
<a href='/ctrl'>控制</a>
<a href='/net'>网络</a>
<a href='/wifi'>WiFi</a>
<a href='/cat'>CAT</a>
</div>
<div class='vfd'><div class='vfd-time' id='t'>--:--:--</div></div>
<div class='status-card'>
<div class='status-title'>📡 天线状态</div>
<div class='ch-display' id='ant'>CH --</div>
<div class='grid' id='g'></div>
</div>
<div class='status-card'>
<div class='status-title'>📻 CAT 连接</div>
<div class='status-row'><span class='status-label'>连接状态:</span><span class='status-value off' id='cat-status'>未连接</span></div>
<div class='status-row'><span class='status-label'>连接方式:</span><span class='status-value' id='cat-type'>--</span></div>
<div class='status-row'><span class='status-label'>电台型号:</span><span class='status-value' id='cat-model'>--</span></div>
<div class='status-row'><span class='status-label'>当前频率:</span><span class='status-value' id='cat-freq'>--</span></div>
<div class='status-row'><span class='status-label'>当前波段:</span><span class='status-value' id='cat-band'>--</span></div>
</div>
<div class='status-card'>
<div class='status-title'>🌐 网络状态</div>
<div class='status-row'><span class='status-label'>以太网IP:</span><span class='status-value' id='eth-ip'>--</span></div>
<div class='status-row'><span class='status-label'>WiFi状态:</span><span class='status-value' id='wifi-status'>未连接</span></div>
<div class='status-row'><span class='status-label'>WiFi名称:</span><span class='status-value' id='wifi-ssid'>--</span></div>
</div>
<button class='btn btn-r' onclick='s(0)'>断开全部天线</button>
</div>
<script>
let currentCh=0;
function ck(){const n=new Date();document.getElementById('t').textContent=String(n.getHours()).padStart(2,'0')+':'+String(n.getMinutes()).padStart(2,'0')+':'+String(n.getSeconds()).padStart(2,'0');}
async function ld(){
  try{
    const r=await fetch('/status'),j=await r.json();
    currentCh=j.c;
    document.getElementById('ant').textContent=j.c>0?'CH '+j.c:'断开';
    let h='';
    for(let i=1;i<=6;i++){const d=j.busy?'disabled':'';const a=i==j.c?'active':'';h+=`<div class='ch ${a} ${d}' onclick='s(${i})'><div class='num'>CH${i}</div><div class='name'>${j.a[i-1]}</div></div>`;}
    document.getElementById('g').innerHTML=h;
  }catch(e){}
}
async function ldCat(){
  try{
    const r=await fetch('/catstatus'),j=await r.json();
    document.getElementById('cat-status').textContent=j.connected?'已连接':'未连接';
    document.getElementById('cat-status').className='status-value '+(j.connected?'on':'off');
    document.getElementById('cat-type').textContent=j.connType||'--';
    document.getElementById('cat-model').textContent=j.model||'--';
    document.getElementById('cat-freq').textContent=j.freq?(j.freq/1000000).toFixed(6)+' MHz':'--';
    document.getElementById('cat-band').textContent=j.band||'--';
  }catch(e){}
}
async function ldNet(){
  try{
    const r1=await fetch('/netstatus'),j1=await r1.json();
    document.getElementById('eth-ip').textContent=j1.curIP||'--';
    const r2=await fetch('/wifistatus'),j2=await r2.json();
    document.getElementById('wifi-status').textContent=j2.connected?'已连接':'未连接';
    document.getElementById('wifi-status').className='status-value '+(j2.connected?'on':'off');
    document.getElementById('wifi-ssid').textContent=j2.ssid||'--';
  }catch(e){}
}
async function s(c){if(c==currentCh)return;await fetch('/switch?ch='+c);ld();}
ck();setInterval(ck,1000);
ld();setInterval(ld,1000);
ldCat();setInterval(ldCat,2000);
ldNet();setInterval(ldNet,5000);
</script>
</body></html>
)rawliteral";

const char MAIN_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>天线切换器</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a15;color:#00ffcc;padding:20px}
.box{max-width:400px;margin:0 auto}
.nav{display:flex;gap:10px;margin-bottom:20px}
.nav a{flex:1;padding:10px;text-align:center;background:rgba(255,255,255,.1);border-radius:8px;color:#00ffcc;text-decoration:none}
.nav a.active{background:#00ff88;color:#000}
.vfd{background:linear-gradient(180deg,#001515 0%,#000a0a 100%);border:2px solid #00ffcc;border-radius:16px;padding:30px 20px;text-align:center;margin-bottom:24px;box-shadow:0 0 30px rgba(0,255,200,.3)}
.vfd-time{font-size:48px;font-weight:bold;color:#00ffcc;text-shadow:0 0 10px #00ffcc;animation:breathe 2s ease-in-out infinite}
@keyframes breathe{0%,100%{opacity:1}50%{opacity:.7}}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin:20px 0}
.ch{background:rgba(255,255,255,.05);border:1px solid rgba(255,255,255,.1);border-radius:12px;padding:15px;text-align:center;cursor:pointer;transition:all .3s;position:relative}
.ch:hover{background:rgba(255,255,255,.1)}
.ch.active{background:linear-gradient(135deg,#00ff88 0%,#00cc66 100%);border-color:#00ff88;color:#000}
.ch.disabled{opacity:.5;pointer-events:none}
.ch .num{font-size:11px;opacity:.6}
.ch .name{font-size:14px;font-weight:600;word-break:break-all}
.ch .edit-btn{position:absolute;top:2px;right:2px;width:20px;height:20px;background:rgba(255,255,255,.2);border:none;border-radius:50%;cursor:pointer;font-size:12px;color:#fff;display:none}
.ch:hover .edit-btn{display:block}
.progress{height:4px;background:rgba(0,255,200,.1);border-radius:2px;margin:15px 0;overflow:hidden;display:none}
.progress.show{display:block}
.progress-bar{height:100%;background:linear-gradient(90deg,#00ffcc,#00ff88);width:0%}
.btn{width:100%;padding:15px;margin:5px 0;border:none;border-radius:8px;cursor:pointer;font-size:16px}
.btn-r{background:linear-gradient(135deg,#ff5555 0%,#cc4444 100%);color:#fff}
.modal{display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.8);z-index:100;justify-content:center;align-items:center}
.modal.show{display:flex}
.modal-content{background:#1a1a2e;border:1px solid #00ffcc;border-radius:12px;padding:20px;width:90%;max-width:300px}
.modal-title{color:#00ffcc;margin-bottom:15px;text-align:center}
.modal-input{width:100%;padding:10px;background:#0a0a15;border:1px solid #00ffcc;border-radius:8px;color:#00ffcc;font-size:16px;margin-bottom:15px}
.modal-btns{display:flex;gap:10px}
.modal-btns button{flex:1;padding:10px;border:none;border-radius:8px;cursor:pointer}
.modal-save{background:#00ff88;color:#000}
.modal-cancel{background:#444;color:#fff}
</style>
</head>
<body>
<div class='box'>
<div class='nav'>
<a href='/' class=''>概览</a>
<a href='/ctrl' class='active'>控制</a>
<a href='/net'>网络</a>
<a href='/wifi'>WiFi</a>
<a href='/cat'>CAT</a>
</div>
<div class='vfd'><div class='vfd-time' id='t'>--:--:--</div></div>
<div class='progress' id='prog'><div class='progress-bar' id='prog-bar'></div></div>
<div class='grid' id='g'></div>
<button class='btn btn-r' id='btn-open' onclick='s(0)'>断开全部</button>
</div>
<div class='modal' id='modal'><div class='modal-content'>
<div class='modal-title'>设置别名 (最大20字符)</div>
<input type='text' class='modal-input' id='alias-input' maxlength='20'>
<div class='modal-btns'><button class='modal-cancel' onclick='closeModal()'>取消</button><button class='modal-save' onclick='saveAlias()'>保存</button></div>
</div></div>
<script>
let switching=false,editingCh=null;
function ck(){const n=new Date();document.getElementById('t').textContent=String(n.getHours()).padStart(2,'0')+':'+String(n.getMinutes()).padStart(2,'0')+':'+String(n.getSeconds()).padStart(2,'0');}
async function ld(){try{const r=await fetch('/status'),j=await r.json();let h='';
for(let i=1;i<=6;i++){const d=(switching||j.busy)?'disabled':'';const a=i==j.c?'active':'';
h+=`<div class='ch ${a} ${d}' onclick='s(${i})'><div class='num'>CH${i}</div><div class='name' id='n${i}'>${j.a[i-1]}</div><button class='edit-btn' onclick='event.stopPropagation();openModal(${i})'>✎</button></div>`;}
document.getElementById('g').innerHTML=h;document.getElementById('btn-open').disabled=switching||j.busy;
if(j.busy&&!switching)startProgress();}catch(e){}}
function openModal(ch){editingCh=ch;document.getElementById('alias-input').value=document.getElementById('n'+ch).textContent;document.getElementById('modal').classList.add('show');}
function closeModal(){document.getElementById('modal').classList.remove('show');editingCh=null;}
async function saveAlias(){if(!editingCh)return;const n=document.getElementById('alias-input').value.trim().substring(0,20);if(n){await fetch('/alias?ch='+editingCh+'&name='+encodeURIComponent(n));closeModal();ld();}}
function startProgress(){switching=true;document.getElementById('prog').classList.add('show');let w=0;const t=setInterval(()=>{w+=5;document.getElementById('prog-bar').style.width=w+'%';if(w>=100)clearInterval(t);},50);setTimeout(()=>{switching=false;document.getElementById('prog').classList.remove('show');document.getElementById('prog-bar').style.width='0%';ld();},1000);}
async function s(c){if(switching)return;startProgress();await fetch('/switch?ch='+c);}
document.getElementById('alias-input').addEventListener('keypress',e=>{if(e.key==='Enter')saveAlias()});
ck();setInterval(ck,1000);ld();setInterval(ld,500);
</script>
</body></html>
)rawliteral";

const char NET_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>网络设置</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a15;color:#00ffcc;padding:20px}
.box{max-width:400px;margin:0 auto}
.nav{display:flex;gap:10px;margin-bottom:20px}
.nav a{flex:1;padding:10px;text-align:center;background:rgba(255,255,255,.1);border-radius:8px;color:#00ffcc;text-decoration:none}
.nav a.active{background:#00ff88;color:#000}
h2{color:#00ffcc;margin-bottom:20px;text-align:center}
.form-group{margin-bottom:15px}
label{display:block;margin-bottom:5px;font-size:14px}
input[type='text'],input[type='number']{width:100%;padding:10px;background:#0a0a15;border:1px solid #00ffcc;border-radius:8px;color:#00ffcc;font-size:16px}
.checkbox{display:flex;align-items:center;gap:10px;margin-bottom:20px}
.checkbox input{width:20px;height:20px}
.btn{width:100%;padding:15px;margin:5px 0;border:none;border-radius:8px;cursor:pointer;font-size:16px}
.btn-g{background:#00ff88;color:#000}
.btn-r{background:#ff5555;color:#fff}
.status{margin-top:20px;padding:15px;background:rgba(255,255,255,.05);border-radius:8px}
.status-item{display:flex;justify-content:space-between;margin:5px 0}
</style>
</head>
<body>
<div class='box'>
<div class='nav'>
<a href='/' class=''>概览</a>
<a href='/ctrl' class=''>控制</a>
<a href='/net' class='active'>网络</a>
<a href='/wifi'>WiFi</a>
<a href='/cat'>CAT</a>
</div>
<h2>以太网设置</h2>
<div class='checkbox'><input type='checkbox' id='dhcp' onchange='toggleDHCP()'><label for='dhcp'>使用 DHCP</label></div>
<div id='static-fields'>
<div class='form-group'><label>IP 地址</label><input type='text' id='ip' placeholder='192.168.1.123'></div>
<div class='form-group'><label>网关</label><input type='text' id='gw' placeholder='192.168.1.1'></div>
<div class='form-group'><label>子网掩码</label><input type='text' id='sn' placeholder='255.255.255.0'></div>
</div>
<button class='btn btn-g' onclick='saveNet()'>保存设置</button>
<button class='btn btn-r' onclick='reboot()'>重启设备</button>
<div class='status' id='status'>加载中...</div>
</div>
<script>
async function load(){const r=await fetch('/netstatus'),j=await r.json();document.getElementById('dhcp').checked=j.dhcp;document.getElementById('ip').value=j.ip;document.getElementById('gw').value=j.gw;document.getElementById('sn').value=j.sn;toggleDHCP();document.getElementById('status').innerHTML=`<div class='status-item'><span>当前IP:</span><span>${j.curIP}</span></div><div class='status-item'><span>MAC:</span><span>${j.mac}</span></div>`;}
function toggleDHCP(){const d=document.getElementById('dhcp').checked;document.getElementById('static-fields').style.display=d?'none':'block';}
async function saveNet(){const dhcp=document.getElementById('dhcp').checked;const ip=document.getElementById('ip').value;const gw=document.getElementById('gw').value;const sn=document.getElementById('sn').value;await fetch(`/netsave?dhcp=${dhcp}&ip=${ip}&gw=${gw}&sn=${sn}`);alert('保存成功，请重启设备');}
function reboot(){if(confirm('确定要重启设备吗？')){fetch('/reboot');}}
load();
</script>
</body></html>
)rawliteral";

const char WIFI_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>WiFi设置</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a15;color:#00ffcc;padding:20px}
.box{max-width:400px;margin:0 auto}
.nav{display:flex;gap:10px;margin-bottom:20px}
.nav a{flex:1;padding:10px;text-align:center;background:rgba(255,255,255,.1);border-radius:8px;color:#00ffcc;text-decoration:none}
.nav a.active{background:#00ff88;color:#000}
h2{color:#00ffcc;margin-bottom:20px;text-align:center}
.wifi-list{margin-bottom:20px}
.wifi-item{display:flex;justify-content:space-between;align-items:center;padding:12px;background:rgba(255,255,255,.05);border-radius:8px;margin-bottom:8px;cursor:pointer;transition:all .2s}
.wifi-item:hover{background:rgba(255,255,255,.1)}
.wifi-item.selected{background:#00ffcc;color:#000}
.wifi-name{font-weight:600}
.wifi-signal{font-size:12px;opacity:.7}
.form-group{margin-bottom:15px}
label{display:block;margin-bottom:5px;font-size:14px}
input[type='text'],input[type='password']{width:100%;padding:10px;background:#0a0a15;border:1px solid #00ffcc;border-radius:8px;color:#00ffcc;font-size:16px}
.btn{width:100%;padding:15px;margin:5px 0;border:none;border-radius:8px;cursor:pointer;font-size:16px}
.btn-g{background:#00ff88;color:#000}
.btn-b{background:#48f;color:#fff}
.status{margin-top:20px;padding:15px;background:rgba(255,255,255,.05);border-radius:8px}
</style>
</head>
<body>
<div class='box'>
<div class='nav'>
<a href='/' class=''>概览</a>
<a href='/ctrl' class=''>控制</a>
<a href='/net' class=''>网络</a>
<a href='/wifi' class='active'>WiFi</a>
<a href='/cat'>CAT</a>
</div>
<h2>WiFi 设置</h2>
<div class='wifi-list' id='list'><div class='status'>扫描中...</div></div>
<button class='btn btn-b' onclick='scan()'>重新扫描</button>
<hr style='border-color:#00ffcc;margin:20px 0'>
<h3>手动连接</h3>
<div class='form-group'><label>SSID</label><input type='text' id='ssid' placeholder='WiFi名称'></div>
<div class='form-group'><label>密码</label><input type='password' id='pass' placeholder='WiFi密码'></div>
<button class='btn btn-g' onclick='connect()'>连接</button>
<div class='status' id='status'></div>
</div>
<script>
let selectedSSID='';
async function scan(){document.getElementById('list').innerHTML='<div class="status">扫描中...</div>';const r=await fetch('/wifiscan');const j=await r.json();let h='';j.networks.forEach((n,i)=>{h+=`<div class='wifi-item' id='wifi-${i}' onclick='select("${n.ssid}",${i})'><span class='wifi-name'>${n.ssid}</span><span class='wifi-signal'>${n.rssi}dBm ${n.secure?'🔒':''}</span></div>`;});document.getElementById('list').innerHTML=h||'<div class="status">未找到网络</div>';}
function select(ssid,idx){selectedSSID=ssid;document.getElementById('ssid').value=ssid;document.querySelectorAll('.wifi-item').forEach(el=>el.classList.remove('selected'));const el=document.getElementById('wifi-'+idx);if(el)el.classList.add('selected');}
async function connect(){const ssid=document.getElementById('ssid').value;const pass=document.getElementById('pass').value;if(!ssid){alert('请输入SSID');return;}await fetch('/wificonnect?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass));document.getElementById('status').textContent='连接中...';setTimeout(checkStatus,5000);}
async function checkStatus(){const r=await fetch('/wifistatus');const j=await r.json();document.getElementById('status').innerHTML=j.connected?`<span style='color:#0f0'>已连接: ${j.ssid}</span>`:`<span style='color:#f55'>未连接</span>`;}
scan();checkStatus();
</script>
</body></html>
)rawliteral";

const char CAT_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width,initial-scale=1.0'>
<title>CAT设置</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Courier New',monospace;background:#0a0a15;color:#00ffcc;padding:20px}
.box{max-width:500px;margin:0 auto}
.nav{display:flex;gap:10px;margin-bottom:20px}
.nav a{flex:1;padding:10px;text-align:center;background:rgba(255,255,255,.1);border-radius:8px;color:#00ffcc;text-decoration:none}
.nav a.active{background:#00ff88;color:#000}
h2{color:#00ffcc;margin-bottom:20px;text-align:center}
h3{color:#00ffcc;margin:20px 0 10px;font-size:16px}
.radio-group{display:flex;gap:10px;margin-bottom:15px;flex-wrap:wrap}
.radio-group label{flex:1;min-width:70px;padding:10px;text-align:center;background:rgba(255,255,255,.05);border-radius:8px;cursor:pointer;border:1px solid transparent}
.radio-group input{display:none}
.radio-group input:checked+label{background:#00ff88;color:#000;border-color:#00ff88}
.form-group{margin-bottom:15px}
label{display:block;margin-bottom:5px;font-size:14px}
select,input{width:100%;padding:10px;background:#0a0a15;border:1px solid #00ffcc;border-radius:8px;color:#00ffcc;font-size:16px}
.checkbox{display:flex;align-items:center;gap:10px;margin:15px 0}
.checkbox input{width:auto}
.btn{width:100%;padding:12px;margin:5px 0;border:none;border-radius:8px;cursor:pointer;font-size:16px}
.btn-g{background:#00ff88;color:#000}
.btn-b{background:#48f;color:#fff}
.btn-r{background:#ff5555;color:#fff}
.btn-s{padding:8px 15px;width:auto;font-size:14px}
.status{margin-top:20px;padding:15px;background:rgba(255,255,255,.05);border-radius:8px}
.status-item{display:flex;justify-content:space-between;margin:5px 0}
.device-list{margin-bottom:15px;max-height:150px;overflow-y:auto}
.device-item{display:flex;justify-content:space-between;align-items:center;padding:10px;background:rgba(255,255,255,.05);border-radius:8px;margin-bottom:8px;cursor:pointer;transition:all .2s}
.device-item:hover{background:rgba(255,255,255,.1)}
.device-item.connected{background:rgba(0,255,136,.2);border:1px solid #00ff88}
.device-item.selected{background:#00ffcc;color:#000}
.band-table{width:100%;border-collapse:collapse;margin:10px 0;font-size:14px}
.band-table th,.band-table td{padding:8px;border:1px solid rgba(0,255,204,.3);text-align:center}
.band-table th{background:rgba(0,255,204,.1)}
.band-table input{width:100%;padding:5px;background:#0a0a15;border:1px solid #00ffcc;color:#00ffcc;text-align:center}
.band-table select{width:100%;padding:5px;background:#0a0a15;border:1px solid #00ffcc;color:#00ffcc}
.band-table .ant-open{color:#888}
.band-table .ant-ch1{color:#0f0}.band-table .ant-ch2{color:#0f0}.band-table .ant-ch3{color:#0f0}
.band-table .ant-ch4{color:#ff0}.band-table .ant-ch5{color:#ff0}.band-table .ant-ch6{color:#f55}
.add-band{display:flex;gap:10px;margin:10px 0;flex-wrap:wrap}
.add-band input{flex:1;min-width:100px}
.tab-btn{background:rgba(255,255,255,.1);color:#00ffcc;padding:10px 20px;border:none;border-radius:8px 8px 0 0;cursor:pointer}
.tab-btn.active{background:#00ff88;color:#000}
.tab-content{display:none;padding:20px;background:rgba(255,255,255,.05);border-radius:0 8px 8px 8px}
.tab-content.active{display:block}
#icom-cfg,#yaesu-cfg,#flex-cfg,#bt-cfg{display:none}
hr{border-color:rgba(0,255,204,.3);margin:20px 0}
</style>
</head>
<body>
<div class='box'>
<div class='nav'>
<a href='/' class=''>概览</a>
<a href='/ctrl' class=''>控制</a>
<a href='/net' class=''>网络</a>
<a href='/wifi'>WiFi</a>
<a href='/cat' class='active'>CAT</a>
</div>
<!-- 标签页按钮 -->
<div style='display:flex;gap:5px;margin-bottom:15px'>
<button class='tab-btn active' onclick='showTab("cat-cfg-tab")' id='tab-cat-cfg'>CAT配置</button>
<button class='tab-btn' onclick='showTab("interlock-tab")' id='tab-interlock'>互锁保护</button>
<button class='tab-btn' onclick='showTab("logs-tab")' id='tab-logs'>系统日志</button>
<button class='tab-btn' onclick='showTab("backup-tab")' id='tab-backup'>备份恢复</button>
</div>

<!-- CAT配置标签 -->
<div id='cat-cfg-tab' class='tab-content active'>
<h2>CAT 电台控制</h2>
<div class='radio-group'>
<input type='radio' name='proto' id='p0' value='0' checked><label for='p0'>关闭</label>
<input type='radio' name='proto' id='p1' value='1'><label for='p1'>ICOM</label>
<input type='radio' name='proto' id='p2' value='2'><label for='p2'>YAESU</label>
<input type='radio' name='proto' id='p3' value='3'><label for='p3'>Kenwood</label>
<input type='radio' name='proto' id='p4' value='4'><label for='p4'>Elecraft</label>
<input type='radio' name='proto' id='p5' value='5'><label for='p5'>FlexRadio</label>
<input type='radio' name='proto' id='p6' value='6'><label for='p6'>蓝牙BLE</label>
</div>
<div id='icom-cfg'>
<div class='form-group'><label>电台型号</label>
<select id='icom-model' onchange='onIcomModelChange()'><option value='0xA4'>IC-705 (BLE) - 0xA4</option><option value='0x94'>IC-7300 - 0x94</option><option value='0x98'>IC-7610 - 0x98</option><option value='0xA2'>IC-9700 - 0xA2</option><option value='0xAC'>IC-905 - 0xAC</option><option value='0x9C'>IC-7851 - 0x9C</option><option value='0xA0'>IC-7100 - 0xA0</option><option value='0x9E'>IC-7200 - 0x9E</option><option value='0x92'>IC-7410 - 0x92</option><option value='0x96'>IC-7700 - 0x96</option><option value='0x9A'>IC-7800 - 0x9A</option><option value='0xA6'>IC-R30 (接收机) - 0xA6</option><option value='0xA8'>IC-R8600 (接收机) - 0xA8</option><option value='custom'>自定义地址...</option></select></div>
<div class='form-group' id='icom-addr-custom' style='display:none'><label>自定义 CI-V 地址 (十六进制)</label><input type='text' id='icom-addr-input' placeholder='例如: A6' value='A6'></div>
<div class='form-group'><label>连接方式</label>
<select id='icom-conn'><option value='ble'>BLE 蓝牙 - 板载蓝牙 (IC-705)</option><option value='serial'>有线串口</option></select></div>
<div class='form-group'><label>波特率</label>
<select id='icom-baud'><option value='9600'>9600</option><option value='19200'>19200</option><option value='38400'>38400</option><option value='115200'>115200</option></select></div>
</div>
<div id='yaesu-cfg'>
<div class='form-group'><label>电台型号</label>
<select id='yaesu-model'><option value='FT-991'>FT-991 / FT-991A</option><option value='FTDX101'>FTDX101D / FTDX101MP</option><option value='FTDX10'>FTDX10</option><option value='FT-891'>FT-891</option><option value='FT-818'>FT-818ND</option><option value='FT-857'>FT-857D</option><option value='FT-897'>FT-897D</option><option value='FT-950'>FT-950</option><option value='FT-2000'>FT-2000</option><option value='FT-1200'>FT-1200</option><option value='FT-450'>FT-450D</option><option value='FT-710'>FT-710</option><option value='FT-847'>FT-847</option><option value='FT-920'>FT-920</option><option value='FT-1000'>FT-1000MP</option><option value='FT-DX3000'>FT-DX3000</option><option value='FT-DX5000'>FT-DX5000</option><option value='FT-DX9000'>FT-DX9000</option><option value='VR-5000'>VR-5000 (接收机)</option></select></div>
<div class='form-group'><label>波特率</label>
<select id='yaesu-baud'><option value='4800'>4800</option><option value='9600' selected>9600</option><option value='19200'>19200</option><option value='38400'>38400</option><option value='57600'>57600</option><option value='115200'>115200</option></select></div>
</div>
<div id='kenwood-cfg' style='display:none'>
<div class='form-group'><label>电台型号</label>
<select id='kenwood-model'><option value='TS-480'>TS-480SAT/HX</option><option value='TS-570'>TS-570S/D</option><option value='TS-590'>TS-590S/G</option><option value='TS-870'>TS-870S</option><option value='TS-990'>TS-990S</option><option value='TS-2000'>TS-2000</option><option value='TS-50'>TS-50S</option><option value='TS-140'>TS-140S</option><option value='TS-440'>TS-440S</option><option value='TS-450'>TS-450S</option><option value='TS-690'>TS-690S</option><option value='TS-850'>TS-850S</option><option value='TS-950'>TS-950S/DX</option><option value='TM-D710'>TM-D710</option><option value='TH-D72'>TH-D72A</option><option value='TH-K20'>TH-K20A</option></select></div>
<div class='form-group'><label>波特率</label>
<select id='kenwood-baud'><option value='4800'>4800</option><option value='9600' selected>9600</option><option value='19200'>19200</option><option value='38400'>38400</option><option value='57600'>57600</option></select></div>
</div>
<div id='elecraft-cfg' style='display:none'>
<div class='form-group'><label>电台型号</label>
<select id='elecraft-model'><option value='K2'>K2</option><option value='K3'>K3 / K3S</option><option value='KX2'>KX2</option><option value='KX3'>KX3</option></select></div>
<div class='form-group'><label>波特率</label>
<select id='elecraft-baud'><option value='4800'>4800</option><option value='9600' selected>9600</option><option value='19200'>19200</option><option value='38400'>38400</option></select></div>
</div>
<div id='flex-cfg' style='display:none'>
<div class='form-group'><label>电台 IP</label><input type='text' id='flex-ip' placeholder='192.168.1.100'></div>
<div class='checkbox'><input type='checkbox' id='flex-auto' checked><label for='flex-auto'>自动发现</label></div>
</div>
<div id='bt-cfg'>
<h3>蓝牙设备</h3>
<div class='device-list' id='bt-list'><div class='status'>点击扫描查找设备</div></div>
<button class='btn btn-b' onclick='btScan()' id='bt-scan-btn'>扫描设备</button>
<button class='btn btn-g' onclick='btConnect()' id='bt-conn-btn' disabled>连接选中设备</button>
<button class='btn btn-r' onclick='btDisconnect()' id='bt-disc-btn' style='display:none'>断开连接</button>
</div>
<div class='checkbox'><input type='checkbox' id='auto-switch' checked><label for='auto-switch'>自动切换天线</label></div>
<button class='btn btn-g' onclick='save()'>保存设置</button>
<button class='btn btn-r' onclick='reboot()'>重启设备</button>
<hr>
<h3>波段-天线绑定配置</h3>
<table class='band-table' id='band-table'>
<tr><th>波段</th><th>频率范围(MHz)</th><th>天线</th><th>操作</th></tr>
</table>
<div class='add-band'>
<select id='new-band-select' onchange='onBandSelect()'>
<option value=''>选择波段...</option>
<option value='160m|1800000|2000000'>160m (1.8-2.0 MHz)</option>
<option value='80m|3500000|3900000'>80m (3.5-3.9 MHz)</option>
<option value='60m|5330500|5406400'>60m (5.3-5.4 MHz)</option>
<option value='40m|7000000|7300000'>40m (7.0-7.3 MHz)</option>
<option value='30m|10100000|10150000'>30m (10.1-10.15 MHz)</option>
<option value='20m|14000000|14350000'>20m (14.0-14.35 MHz)</option>
<option value='17m|18068000|18168000'>17m (18.068-18.168 MHz)</option>
<option value='15m|21000000|21450000'>15m (21.0-21.45 MHz)</option>
<option value='12m|24890000|24990000'>12m (24.89-24.99 MHz)</option>
<option value='10m|28000000|29700000'>10m (28.0-29.7 MHz)</option>
<option value='6m|50000000|54000000'>6m (50-54 MHz)</option>
<option value='4m|70000000|70500000'>4m (70-70.5 MHz)</option>
<option value='2m|144000000|148000000'>2m (144-148 MHz)</option>
<option value='1.25m|222000000|225000000'>1.25m (222-225 MHz)</option>
<option value='0.7m|430000000|450000000'>70cm (430-450 MHz)</option>
<option value='0.33m|902000000|928000000'>33cm (902-928 MHz)</option>
<option value='0.23m|1240000000|1300000000'>23cm (1.24-1.3 GHz)</option>
</select>
<select id='new-band-ant'><option value='0'>OPEN</option><option value='1'>CH1</option><option value='2'>CH2</option><option value='3'>CH3</option><option value='4'>CH4</option><option value='5'>CH5</option><option value='6'>CH6</option></select>
<button class='btn btn-s btn-b' onclick='addBand()'>添加</button>
</div>
<div class='status' id='status'>
<div class='status-item'><span>连接状态:</span><span id='conn'>--</span></div>
<div class='status-item'><span>当前频率:</span><span id='freq'>--</span></div>
<div class='status-item'><span>当前波段:</span><span id='band'>--</span></div>
<div class='status-item'><span>天线通道:</span><span id='ant'>--</span></div>
</div>
</div>

<!-- 互锁保护标签 -->
<div id='interlock-tab' class='tab-content'>
<h2>互锁保护设置</h2>
<div class='checkbox'><input type='checkbox' id='interlock-enabled' checked><label for='interlock-enabled'>启用PTT互锁保护</label></div>
<div class='form-group'><label>PTT释放后等待时间 (ms)</label>
<select id='interlock-delay'><option value='100'>100ms</option><option value='200' selected>200ms</option><option value='500'>500ms</option><option value='1000'>1000ms</option></select></div>
<div class='form-group'><label>继电器最小间隔 (ms)</label>
<select id='interlock-cooldown'><option value='50'>50ms</option><option value='100' selected>100ms</option><option value='200'>200ms</option><option value='500'>500ms</option></select></div>
<button class='btn btn-g' onclick='saveInterlock()'>保存互锁设置</button>
<div class='status' style='margin-top:15px'>
<div class='status-item'><span>PTT状态:</span><span id='interlock-ptt'>--</span></div>
<div class='status-item'><span>切换允许:</span><span id='interlock-can'>--</span></div>
<div class='status-item'><span>缓存请求:</span><span id='interlock-pending'>--</span></div>
</div>
</div>

<!-- 系统日志标签 -->
<div id='logs-tab' class='tab-content'>
<h2>系统日志</h2>
<div class='form-group'><label>日志级别</label>
<select id='log-level' onchange='setLogLevel()'><option value='0'>NONE (关闭)</option><option value='1'>ERROR (错误)</option><option value='2'>WARN (警告)</option><option value='3' selected>INFO (信息)</option><option value='4'>DEBUG (调试)</option></select></div>
<div style='background:rgba(0,0,0,.5);border:1px solid #00ffcc;border-radius:8px;padding:10px;height:300px;overflow-y:auto;font-size:12px;font-family:monospace;margin:10px 0' id='log-content'>
<div style='color:#888'>点击刷新加载日志...</div>
</div>
<button class='btn btn-b' onclick='refreshLogs()'>刷新日志</button>
<button class='btn btn-s btn-r' onclick='clearLogs()'>清空日志</button>
</div>

<!-- 备份恢复标签 -->
<div id='backup-tab' class='tab-content'>
<h2>配置备份/恢复</h2>
<h3>导出配置</h3>
<button class='btn btn-b' onclick='exportConfig()'>下载配置文件</button>
<h3 style='margin-top:20px'>导入配置</h3>
<div class='form-group'><textarea id='import-config-text' style='width:100%;height:150px;font-size:12px' placeholder='将配置文件内容粘贴到这里...'></textarea></div>
<button class='btn btn-g' onclick='importConfig()'>导入配置</button>
<h3 style='margin-top:20px;color:#f55'>危险区域</h3>
<button class='btn btn-r' onclick='factoryReset()'>恢复出厂设置</button>
</div>

<script>
function showTab(tabId){
  document.querySelectorAll('.tab-content').forEach(el=>el.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(el=>el.classList.remove('active'));
  document.getElementById(tabId).classList.add('active');
  const btnId='tab-'+tabId.replace('-tab','');
  const btn=document.getElementById(btnId);if(btn)btn.classList.add('active');
}
async function saveInterlock(){
  const enabled=document.getElementById('interlock-enabled').checked?1:0;
  const delay=document.getElementById('interlock-delay').value;
  const cooldown=document.getElementById('interlock-cooldown').value;
  await fetch(`/interlocksave?enabled=${enabled}&delay=${delay}&cooldown=${cooldown}`);
  alert('互锁设置已保存');
}
async function loadInterlock(){
  const r=await fetch('/interlockstatus');
  const j=await r.json();
  document.getElementById('interlock-enabled').checked=j.enabled;
  document.getElementById('interlock-delay').value=j.pttReleaseDelay;
  document.getElementById('interlock-cooldown').value=j.relayCooldown;
  document.getElementById('interlock-ptt').textContent=j.ptt?'激活':'未激活';
  document.getElementById('interlock-ptt').style.color=j.ptt?'#f55':'#0f0';
  document.getElementById('interlock-can').textContent=j.canSwitch?'允许':'禁止';
  document.getElementById('interlock-can').style.color=j.canSwitch?'#0f0':'#f55';
  document.getElementById('interlock-pending').textContent=j.pending?`CH${j.pendingChannel}`:'无';
}
async function refreshLogs(){
  const r=await fetch('/logget');
  const text=await r.text();
  const el=document.getElementById('log-content');
  el.innerHTML=text.split('\n').map(l=>{
    if(l.includes('[E]'))return'<div style="color:#f55">'+l+'</div>';
    if(l.includes('[W]'))return'<div style="color:#ff0">'+l+'</div>';
    if(l.includes('[I]'))return'<div style="color:#0f0">'+l+'</div>';
    if(l.includes('[D]'))return'<div style="color:#888">'+l+'</div>';
    return'<div>'+l+'</div>';
  }).join('');
  el.scrollTop=el.scrollHeight;
}
async function clearLogs(){
  if(!confirm('确定清空日志?'))return;
  await fetch('/logclear');
  refreshLogs();
}
async function setLogLevel(){
  const level=document.getElementById('log-level').value;
  await fetch('/logconfig?level='+level);
}
async function exportConfig(){
  const r=await fetch('/configexport');
  const config=await r.json();
  const blob=new Blob([JSON.stringify(config,null,2)],{type:'application/json'});
  const url=URL.createObjectURL(blob);
  const a=document.createElement('a');
  a.href=url;
  a.download='antenna-switch-config.json';
  a.click();
  URL.revokeObjectURL(url);
}
async function importConfig(){
  const text=document.getElementById('import-config-text').value.trim();
  if(!text){alert('请输入配置内容');return;}
  try{JSON.parse(text);}catch(e){alert('无效的JSON格式');return;}
  if(!confirm('导入配置将覆盖当前设置，确定继续?'))return;
  const r=await fetch('/configimport',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'config='+encodeURIComponent(text)});
  const result=await r.text();
  alert(result);
}
async function factoryReset(){
  if(!confirm('警告:此操作将清除所有配置!\n确定恢复出厂设置?'))return;
  if(!confirm('再次确认:所有配置将丢失!'))return;
  await fetch('/configreset');
  alert('已恢复出厂设置，请重启设备');
}
let selectedBTDevice='',btConnected=false,bandConfigs=[];
function onIcomModelChange(){const model=document.getElementById('icom-model').value;const customAddr=document.getElementById('icom-addr-custom');if(model==='custom'){customAddr.style.display='block';}else{customAddr.style.display='none';}}
document.querySelectorAll('input[name="proto"]').forEach(r=>{r.addEventListener('change',()=>{['icom-cfg','yaesu-cfg','kenwood-cfg','elecraft-cfg','flex-cfg','bt-cfg'].forEach(id=>{const el=document.getElementById(id);if(el)el.style.display='none';});const v=r.value;if(v=='1')document.getElementById('icom-cfg').style.display='block';if(v=='2')document.getElementById('yaesu-cfg').style.display='block';if(v=='3')document.getElementById('kenwood-cfg').style.display='block';if(v=='4')document.getElementById('elecraft-cfg').style.display='block';if(v=='5')document.getElementById('flex-cfg').style.display='block';if(v=='6'){document.getElementById('bt-cfg').style.display='block';document.getElementById('icom-cfg').style.display='block';}});});
let isScanning=false;async function btScan(){if(isScanning){console.log('Scan already in progress');return;}isScanning=true;document.getElementById('bt-list').innerHTML='<div class="status">扫描中...</div>';document.getElementById('bt-scan-btn').disabled=true;try{const r=await fetch('/btscan');const j=await r.json();let h='';j.devices.forEach((d,i)=>{h+=`<div class='device-item ${d.connected?"connected":""}' id='bt-${i}' onclick='selectBT("${d.name}",${d.connected},${i})'><span>${d.name}</span><span>${d.connected?"已连接":d.rssi+"dBm"}</span></div>`;});document.getElementById('bt-list').innerHTML=h||'<div class="status">未找到设备</div>';}catch(e){console.error('Scan error:',e);document.getElementById('bt-list').innerHTML='<div class="status">扫描出错</div>';}finally{isScanning=false;document.getElementById('bt-scan-btn').disabled=false;}}
function selectBT(name,connected,idx){selectedBTDevice=name;btConnected=connected;document.querySelectorAll('.device-item').forEach(el=>el.classList.remove('selected'));const el=document.getElementById('bt-'+idx);if(el)el.classList.add('selected');const btn=document.getElementById('bt-conn-btn');btn.disabled=false;btn.textContent='连接选中设备';btn.style.display='inline-block';document.getElementById('bt-disc-btn').style.display='none';}
async function btConnect(){if(!selectedBTDevice)return;document.getElementById('bt-conn-btn').textContent='连接中...';document.getElementById('bt-conn-btn').disabled=true;try{await fetch('/btconnect?name='+encodeURIComponent(selectedBTDevice));document.getElementById('bt-conn-btn').textContent='已连接';}catch(e){console.error('Connect error:',e);document.getElementById('bt-conn-btn').textContent='连接失败';}}
async function btDisconnect(){await fetch('/btdisconnect');setTimeout(btScan,1000);}
async function loadBands(){const r=await fetch('/bandconfig');const j=await r.json();bandConfigs=j.bands;renderBands();}
function renderBands(){let h='<tr><th>波段</th><th>频率范围(MHz)</th><th>天线</th><th>操作</th></tr>';const antNames=['OPEN','CH1','CH2','CH3','CH4','CH5','CH6'];bandConfigs.forEach((b,i)=>{let opts='';for(let a=0;a<=6;a++){opts+=`<option value='${a}' ${b.ant==a?'selected':''}>${antNames[a]}</option>`;}h+=`<tr><td>${b.name}</td><td>${(b.min/1000000).toFixed(2)} - ${(b.max/1000000).toFixed(2)}</td><td><select onchange='updateBand(${i},"ant",this.value)'>${opts}</select></td><td><button class='btn btn-s btn-r' onclick='deleteBand(${i})'>删除</button></td></tr>`;});document.getElementById('band-table').innerHTML=h;}
async function addBand(){const select=document.getElementById('new-band-select');const ant=document.getElementById('new-band-ant').value;if(!select.value){alert('请选择波段');return;}const parts=select.value.split('|');const name=parts[0],min=parseInt(parts[1]),max=parseInt(parts[2]);await fetch(`/bandadd?name=${encodeURIComponent(name)}&min=${min}&max=${max}&ant=${ant}`);select.value='';loadBands();}
async function updateBand(idx,field,value){await fetch(`/bandupdate?idx=${idx}&field=${field}&value=${value}`);}
async function deleteBand(idx){if(!confirm('确定删除此波段配置?'))return;await fetch('/banddelete?idx='+idx);loadBands();}
async function load(){const r=await fetch('/catstatus');const j=await r.json();document.getElementById('conn').textContent=j.connected?'已连接':'未连接';document.getElementById('conn').style.color=j.connected?'#0f0':'#f55';document.getElementById('freq').textContent=j.freq?j.freq+' Hz':'--';document.getElementById('band').textContent=j.band||'--';document.getElementById('ant').textContent=j.antenna||'--';}
async function save(){const proto=document.querySelector('input[name="proto"]:checked').value;const auto=document.getElementById('auto-switch').checked;let url='/catsave?proto='+proto+'&auto='+(auto?1:0);if(proto=='1'){url+='&addr='+document.getElementById('icom-addr').value;url+='&baud='+document.getElementById('icom-baud').value;url+='&conn='+document.getElementById('icom-conn').value;}else if(proto=='2'){url+='&model='+document.getElementById('yaesu-model').value;url+='&baud='+document.getElementById('yaesu-baud').value;}else if(proto=='3'){url+='&model='+document.getElementById('kenwood-model').value;url+='&baud='+document.getElementById('kenwood-baud').value;}else if(proto=='4'){url+='&model='+document.getElementById('elecraft-model').value;url+='&baud='+document.getElementById('elecraft-baud').value;}else if(proto=='5'){url+='&ip='+document.getElementById('flex-ip').value;url+='&autodisc='+(document.getElementById('flex-auto').checked?1:0);}else if(proto=='6'){url+='&btdev='+encodeURIComponent(selectedBTDevice);}await fetch(url);alert('保存成功，请重启设备');}
function reboot(){if(confirm('确定要重启设备吗？'))fetch('/reboot');}
// 加载CAT配置并设置选中状态
async function loadCatConfig(){
  try{
    const r=await fetch('/catstatus');
    const j=await r.json();
    // 根据连接类型设置选中的协议
    let proto='0';
    if(j.connType==='蓝牙BLE')proto='6';
    else if(j.model==='ICOM')proto='1';
    else if(j.model==='YAESU')proto='2';
    else if(j.model==='Kenwood')proto='3';
    else if(j.model==='Elecraft')proto='4';
    else if(j.model==='FlexRadio')proto='5';
    
    const radio=document.querySelector('input[name="proto"][value="'+proto+'"]');
    if(radio){
      radio.checked=true;
      radio.dispatchEvent(new Event('change'));
    }
  }catch(e){console.error('Load CAT config error:',e);}
}
loadBands();setInterval(load,1000);load();
loadInterlock();setInterval(loadInterlock,1000);
loadCatConfig();
</script>
</body></html>
)rawliteral";

// ============ Web 处理函数 ============
void handleRoot() { server.send(200, "text/html", DASHBOARD_PAGE); }
void handleNet() { server.send(200, "text/html", NET_PAGE); }
void handleWiFiPage() { server.send(200, "text/html", WIFI_PAGE); }
void handleCatPage() { server.send(200, "text/html", CAT_PAGE); }

void handleStatus() {
  bool busy = (relayState != 0);
  String json = "{\"c\":" + String(currentCh) + ",\"busy\":" + String(busy ? "true" : "false") + ",\"a\":[\"" + aliases[0] + "\",\"" + aliases[1] + "\",\"" + aliases[2] + "\",\"" + aliases[3] + "\",\"" + aliases[4] + "\",\"" + aliases[5] + "\"]}";
  server.send(200, "application/json", json);
}

void handleSwitch() {
  if (server.hasArg("ch")) {
    switchCh(server.arg("ch").toInt());
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "ERR");
  }
}

void handleSetAlias() {
  if (server.hasArg("ch") && server.hasArg("name")) {
    int ch = server.arg("ch").toInt();
    String name = server.arg("name");
    if (ch >= 1 && ch <= 6) {
      aliases[ch-1] = name;
      prefs.begin("ant", false);
      prefs.putString(("a" + String(ch-1)).c_str(), name);
      prefs.end();
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Invalid");
    }
  } else {
    server.send(400, "text/plain", "Missing args");
  }
}

void handleNetStatus() {
  String json = "{\"dhcp\":" + String(useDHCP ? "true" : "false");
  json += ",\"ip\":\"" + staticIP.toString() + "\"";
  json += ",\"gw\":\"" + gateway.toString() + "\"";
  json += ",\"sn\":\"" + subnet.toString() + "\"";
  json += ",\"curIP\":\"" + ETH.localIP().toString() + "\"";
  json += ",\"mac\":\"" + ETH.macAddress() + "\"}";
  server.send(200, "application/json", json);
}

void handleNetSave() {
  if (server.hasArg("dhcp")) useDHCP = server.arg("dhcp") == "true";
  if (server.hasArg("ip")) staticIP.fromString(server.arg("ip"));
  if (server.hasArg("gw")) gateway.fromString(server.arg("gw"));
  if (server.hasArg("sn")) subnet.fromString(server.arg("sn"));
  saveNetConfig();
  server.send(200, "text/plain", "OK");
}

void handleWiFiScan() {
  String json = "{\"networks\":[";
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n && i < 20; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + String(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
  WiFi.scanDelete();
}

void handleWiFiConnect() {
  if (server.hasArg("ssid") && server.hasArg("pass")) {
    wifiSSID = server.arg("ssid");
    wifiPass = server.arg("pass");
    saveWiFiConfig();
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing args");
  }
}

void handleWiFiStatus() {
  String json = "{\"connected\":" + String(WiFi.isConnected() ? "true" : "false");
  json += ",\"ssid\":\"" + String(WiFi.SSID()) + "\"";
  json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"}";
  server.send(200, "application/json", json);
}

void handleReboot() {
  server.send(200, "text/plain", "Rebooting...");
  delay(100);
  ESP.restart();
}

void handleBTScan() {
  // BLE 扫描 IC-705
  Serial.println("[BLE] Web 请求扫描设备...");
  
  String devices = bleCivScanDevices(5000);  // 扫描5秒
  String json = "{\"devices\":" + devices + "}";
  
  Serial.print("[BLE] 扫描结果: ");
  Serial.println(json);
  
  server.send(200, "application/json", json);
}

void handleBTConnect() {
  if (server.hasArg("name")) {
    String deviceName = server.arg("name");
    Serial.print("[BLE] 连接设备: ");
    Serial.println(deviceName);
    
    // BLE 连接 IC-705
    bool success = bleCivConnect(deviceName.c_str());
    
    // 连接成功后，初始化 CAT 控制器使用 BLE
    if (success) {
      Serial.println("[BLE] 连接成功，初始化 CAT 控制器...");
      
      // 配置 CAT 控制器使用 BLE 连接
      CatControllerConfig catCfg = {
        .type = CAT_PROTO_ICOM,
        .autoSwitch = true,
        .switchDelay = 1000,
        .icom = {
          .radioAddress = ICOM_ADDR_IC705,  // IC-705 默认地址
          .baudrate = 115200,
          .connType = ICOM_CONN_BLE         // 使用 BLE 连接
        }
      };
      
      if (catControllerInit(&catCfg)) {
        Serial.println("[CAT] 控制器初始化成功 (BLE模式)");
      } else {
        Serial.println("[CAT] 控制器初始化失败");
      }
    }
    
    server.send(200, "text/plain", success ? "OK" : "FAIL");
  } else {
    server.send(400, "text/plain", "Missing name");
  }
}

void handleBTDisconnect() {
  Serial.println("[BLE] 断开连接");
  bleCivDisconnect();
  server.send(200, "text/plain", "OK");
}

void handleBandConfig() {
  // 返回波段配置
  String json = "{\"bands\":[";
  for (int i = 0; i < bandCount; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + String(bands[i].name) + "\",";
    json += "\"min\":" + String(bands[i].minFreq) + ",";
    json += "\"max\":" + String(bands[i].maxFreq) + ",";
    json += "\"ant\":" + String(bands[i].antenna) + ",";
    json += "\"enabled\":" + String(bands[i].enabled ? "true" : "false") + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleBandAdd() {
  if (bandCount >= MAX_BANDS) {
    server.send(400, "text/plain", "Maximum bands reached");
    return;
  }
  if (server.hasArg("name") && server.hasArg("min") && server.hasArg("max") && server.hasArg("ant")) {
    String name = server.arg("name");
    uint32_t min = server.arg("min").toInt();
    uint32_t max = server.arg("max").toInt();
    uint8_t ant = server.arg("ant").toInt();
    
    strncpy(bands[bandCount].name, name.c_str(), 15);
    bands[bandCount].name[15] = '\0';
    bands[bandCount].minFreq = min;
    bands[bandCount].maxFreq = max;
    bands[bandCount].antenna = ant;
    bands[bandCount].enabled = true;
    bandCount++;
    saveBands();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleBandUpdate() {
  if (server.hasArg("idx") && server.hasArg("field") && server.hasArg("value")) {
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= bandCount) {
      server.send(400, "text/plain", "Invalid index");
      return;
    }
    String field = server.arg("field");
    String value = server.arg("value");
    
    if (field == "name") {
      strncpy(bands[idx].name, value.c_str(), 15);
      bands[idx].name[15] = '\0';
    } else if (field == "min") {
      bands[idx].minFreq = value.toInt();
    } else if (field == "max") {
      bands[idx].maxFreq = value.toInt();
    } else if (field == "ant") {
      bands[idx].antenna = value.toInt();
    } else if (field == "enabled") {
      bands[idx].enabled = (value == "true" || value == "1");
    }
    saveBands();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleBandDelete() {
  if (server.hasArg("idx")) {
    int idx = server.arg("idx").toInt();
    if (idx < 0 || idx >= bandCount) {
      server.send(400, "text/plain", "Invalid index");
      return;
    }
    // 删除指定索引的波段，后面的元素前移
    for (int i = idx; i < bandCount - 1; i++) {
      bands[i] = bands[i + 1];
    }
    bandCount--;
    saveBands();
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing idx parameter");
  }
}

void handleCatStatus() {
  // 改进: 获取详细连接状态
  CatConnDetail connDetail = catControllerGetConnDetail();
  
  // 根据详细状态确定显示状态
  bool connected = (connDetail.overallState == CAT_CONN_FULL);
  bool bleOnly = (connDetail.overallState == CAT_CONN_BLE_ONLY);
  
  uint32_t freq = catControllerGetFrequency();
  const char* band = catControllerGetBandName();
  uint8_t antenna = catControllerGetRecommendedChannel();
  const char* connType = catControllerGetConnTypeStr();
  
  // 根据协议类型确定电台型号
  const char* model = "--";
  CatProtocolType proto = catControllerGetProtocol();
  switch (proto) {
    case CAT_PROTO_ICOM: model = "ICOM"; break;
    case CAT_PROTO_YAESU: model = "YAESU"; break;
    case CAT_PROTO_FLEXRADIO: model = "FlexRadio"; break;
    default: model = "--"; break;
  }
  
  // 获取详细状态
  const CatControllerStatus* status = catControllerGetStatus();
  bool ptt = status ? status->ptt : false;
  uint8_t mode = status ? status->mode : 0;
  uint8_t rfpwr = status ? status->rfpwr : 0;
  
  // 获取互锁状态
  String interlockReason = "";
  bool canSwitch = canSwitchNow(0, interlockReason); // 检查CH0来获取当前状态
  
  String json = "{";
  json += "\"connected\":" + String(connected ? "true" : "false") + ",";
  json += "\"ptt\":" + String(ptt ? "true" : "false") + ",";
  json += "\"pttLockEnabled\":" + String(interlockCfg.pttLockEnabled ? "true" : "false") + ",";
  json += "\"canSwitch\":" + String(canSwitch ? "true" : "false") + ",";
  json += "\"interlockReason\":\"" + interlockReason + "\",";
  json += "\"pendingSwitch\":" + String(pendingSwitch.pending ? "true" : "false") + ",";
  if (pendingSwitch.pending) {
    json += "\"pendingChannel\":" + String(pendingSwitch.channel) + ",";
  }
  json += "\"bleOnly\":" + String(bleOnly ? "true" : "false") + ",";  // 新增: BLE 连接但未授权
  json += "\"bleConnected\":" + String(connDetail.bleConnected ? "true" : "false") + ",";  // 新增
  json += "\"civAuthorized\":" + String(connDetail.civAuthorized ? "true" : "false") + ",";  // 新增
  json += "\"connState\":" + String((int)connDetail.overallState) + ",";  // 新增: 0=断开, 1=BLE only, 2=完全连接
  json += "\"freq\":" + String(freq) + ",";
  json += "\"band\":\"" + String(band) + "\",";
  json += "\"antenna\":" + String(antenna) + ",";
  json += "\"connType\":\"" + String(connType) + "\",";
  json += "\"model\":\"" + String(model) + "\",";
  json += "\"ptt\":" + String(ptt ? "true" : "false") + ",";
  json += "\"mode\":" + String(mode) + ",";
  json += "\"rfpwr\":" + String(rfpwr);
  json += "}";
  server.send(200, "application/json", json);
}

void handleCatSave() {
  if (!server.hasArg("proto")) {
    LOG_W(TAG_WEB, "CAT保存请求缺少协议参数");
    server.send(400, "text/plain", "Missing protocol");
    return;
  }
  
  int proto = server.arg("proto").toInt();
  bool autoSwitch = server.hasArg("auto") ? (server.arg("auto").toInt() == 1) : false;
  
  CatControllerConfig catCfg = {};
  catCfg.autoSwitch = autoSwitch;
  catCfg.switchDelay = 1000;
  
  // 根据协议解析配置
  switch (proto) {
    case 1: { // ICOM
      catCfg.type = CAT_PROTO_ICOM;
      catCfg.icom.radioAddress = server.hasArg("addr") ? strtol(server.arg("addr").c_str(), NULL, 16) : 0xA4;
      catCfg.icom.baudrate = server.hasArg("baud") ? server.arg("baud").toInt() : 9600;
      
      // 根据连接方式参数设置类型
      String conn = server.hasArg("conn") ? server.arg("conn") : "serial";
      if (conn == "ble") {
        catCfg.icom.connType = ICOM_CONN_BLE;
      } else {
        catCfg.icom.connType = ICOM_CONN_SERIAL;
      }
      
      LOG_I(TAG_CAT, "配置ICOM: addr=0x%02X, baud=%d, conn=%s", 
            catCfg.icom.radioAddress, catCfg.icom.baudrate, 
            catCfg.icom.connType == ICOM_CONN_BLE ? "BLE" : "Serial");
      break;
    }
    
    case 2: { // YAESU
      catCfg.type = CAT_PROTO_YAESU;
      catCfg.yaesu.baudrate = server.hasArg("baud") ? server.arg("baud").toInt() : 9600;
      
      // 映射型号字符串到枚举 (只支持已定义的型号)
      String model = server.hasArg("model") ? server.arg("model") : "FT-991";
      if (model == "FT-991") catCfg.yaesu.model = YAESU_FT991;
      else if (model == "FT-991A") catCfg.yaesu.model = YAESU_FT991A;
      else if (model == "FTDX101" || model == "FTDX101D" || model == "FTDX101MP") catCfg.yaesu.model = YAESU_FTDX101;
      else if (model == "FTDX10") catCfg.yaesu.model = YAESU_FTDX10;
      else if (model == "FT-891") catCfg.yaesu.model = YAESU_FT891;
      else catCfg.yaesu.model = YAESU_FT991;  // 默认
      
      LOG_I(TAG_CAT, "配置YAESU: model=%s (enum=%d), baud=%d", 
            model.c_str(), catCfg.yaesu.model, catCfg.yaesu.baudrate);
      break;
    }
    
    case 3: { // Kenwood
      catCfg.type = CAT_PROTO_KENWOOD;
      catCfg.kenwood.baudrate = server.hasArg("baud") ? server.arg("baud").toInt() : 9600;
      
      // 映射型号字符串到枚举
      String model = server.hasArg("model") ? server.arg("model") : "TS-2000";
      if (model == "TS-480") catCfg.kenwood.model = KENWOOD_TS480;
      else if (model == "TS-570") catCfg.kenwood.model = KENWOOD_TS570;
      else if (model == "TS-590") catCfg.kenwood.model = KENWOOD_TS590;
      else if (model == "TS-870") catCfg.kenwood.model = KENWOOD_TS870;
      else if (model == "TS-990") catCfg.kenwood.model = KENWOOD_TS990;
      else if (model == "TS-2000") catCfg.kenwood.model = KENWOOD_TS2000;
      else if (model == "TS-50") catCfg.kenwood.model = KENWOOD_TS50;
      else if (model == "TS-450") catCfg.kenwood.model = KENWOOD_TS450;
      else if (model == "TM-D710") catCfg.kenwood.model = KENWOOD_TM_D710;
      else catCfg.kenwood.model = KENWOOD_TS2000;  // 默认
      
      LOG_I(TAG_CAT, "配置Kenwood: model=%s, baud=%d", 
            model.c_str(), catCfg.kenwood.baudrate);
      break;
    }
    
    case 4: { // Elecraft
      catCfg.type = CAT_PROTO_ELECRAFT;
      catCfg.elecraft.baudrate = server.hasArg("baud") ? server.arg("baud").toInt() : 38400;
      catCfg.elecraft.extendedMode = true;
      
      // 映射型号字符串到枚举
      String model = server.hasArg("model") ? server.arg("model") : "K3";
      if (model == "K2") catCfg.elecraft.model = ELECRAFT_K2;
      else if (model == "K3") catCfg.elecraft.model = ELECRAFT_K3;
      else if (model == "KX2") catCfg.elecraft.model = ELECRAFT_KX2;
      else if (model == "KX3") catCfg.elecraft.model = ELECRAFT_KX3;
      else catCfg.elecraft.model = ELECRAFT_K3;  // 默认
      
      LOG_I(TAG_CAT, "配置Elecraft: model=%s, baud=%d", 
            model.c_str(), catCfg.elecraft.baudrate);
      break;
    }
    
    case 5: { // FlexRadio
      catCfg.type = CAT_PROTO_FLEXRADIO;
      String ip = server.hasArg("ip") ? server.arg("ip") : "192.168.1.100";
      catCfg.flex.radioIP.fromString(ip);
      catCfg.flex.autoDiscover = server.hasArg("autodisc") ? (server.arg("autodisc").toInt() == 1) : true;
      
      LOG_I(TAG_CAT, "配置FlexRadio: ip=%s, autodisc=%d", 
            ip.c_str(), catCfg.flex.autoDiscover);
      break;
    }
    
    case 6: { // 蓝牙 BLE (IC-705)
      catCfg.type = CAT_PROTO_ICOM;
      catCfg.icom.radioAddress = ICOM_ADDR_IC705;
      catCfg.icom.baudrate = 115200;
      catCfg.icom.connType = ICOM_CONN_BLE;
      
      LOG_I(TAG_CAT, "配置ICOM BLE (IC-705)");
      break;
    }
    
    default:
      LOG_E(TAG_WEB, "无效的协议类型: %d", proto);
      server.send(400, "text/plain", "Invalid protocol");
      return;
  }
  
  // ===== 配置验证 =====
  ValidationResult valResult = validateCatConfig(&catCfg);
  
  // 尝试自动修复
  if (!valResult.valid) {
    LOG_W(TAG_CAT, "配置验证失败: %s", valResult.errorMsg);
    
    ValidationResult fixResult;
    if (autoFixCatConfig(&catCfg, &fixResult)) {
      LOG_I(TAG_CAT, "配置已自动修复: %s", fixResult.errorMsg);
      valResult = validateCatConfig(&catCfg);  // 重新验证
    }
  }
  
  // 验证失败，返回错误
  if (!valResult.valid) {
    LOG_E(TAG_CAT, "配置验证失败且无法修复: %s", valResult.errorMsg);
    String errorMsg = "Config validation failed: ";
    errorMsg += valResult.errorMsg;
    if (valResult.suggestion[0] != '\0') {
      errorMsg += " | Suggestion: ";
      errorMsg += valResult.suggestion;
    }
    server.send(400, "text/plain", errorMsg);
    return;
  }
  
  // 有警告但可以通过
  if (valResult.autoFixed || strlen(valResult.errorMsg) > 0) {
    LOG_W(TAG_CAT, "配置警告: %s", valResult.errorMsg);
  }
  
  // ===== 初始化CAT控制器 =====
  bool success = catControllerInit(&catCfg);
  
  if (success) {
    LOG_I(TAG_CAT, "CAT控制器初始化成功");
    server.send(200, "text/plain", "OK");
  } else {
    LOG_E(TAG_CAT, "CAT控制器初始化失败");
    server.send(500, "text/plain", "Failed to initialize CAT controller");
  }
}

// ============ 互锁控制 API ============
void handleInterlockStatus() {
  String json = "{";
  json += "\"enabled\":" + String(interlockCfg.pttLockEnabled ? "true" : "false") + ",";
  json += "\"pttReleaseDelay\":" + String(interlockCfg.pttReleaseDelay) + ",";
  json += "\"relayCooldown\":" + String(interlockCfg.relayCooldownMs) + ",";
  json += "\"pending\":" + String(pendingSwitch.pending ? "true" : "false") + ",";
  json += "\"pendingChannel\":" + String(pendingSwitch.channel);
  json += "}";
  server.send(200, "application/json", json);
}

void handleInterlockSave() {
  if (server.hasArg("enabled")) {
    interlockCfg.pttLockEnabled = (server.arg("enabled").toInt() == 1);
  }
  if (server.hasArg("delay")) {
    interlockCfg.pttReleaseDelay = server.arg("delay").toInt();
  }
  if (server.hasArg("cooldown")) {
    interlockCfg.relayCooldownMs = server.arg("cooldown").toInt();
  }
  
  // 保存到NVS
  prefs.begin("interlock", false);
  prefs.putBool("enabled", interlockCfg.pttLockEnabled);
  prefs.putUInt("delay", interlockCfg.pttReleaseDelay);
  prefs.putUInt("cooldown", interlockCfg.relayCooldownMs);
  prefs.end();
  
  LOG_I(TAG_HW, "互锁配置已保存: enabled=%d, delay=%lu, cooldown=%lu",
        interlockCfg.pttLockEnabled, interlockCfg.pttReleaseDelay, interlockCfg.relayCooldownMs);
  server.send(200, "text/plain", "OK");
}

// ============ 日志查看 API ============
void handleLogGet() {
  char buf[2048];
  int len = loggerGetBuffer(buf, sizeof(buf));
  server.send(200, "text/plain", String(buf));
}

void handleLogClear() {
  loggerClearBuffer();
  LOG_I(TAG_MAIN, "日志缓冲区已清空");
  server.send(200, "text/plain", "OK");
}

void handleLogConfig() {
  if (server.hasArg("level")) {
    int level = server.arg("level").toInt();
    if (level >= 0 && level <= 4) {
      loggerSetLevel((LogLevel)level);
      LOG_I(TAG_MAIN, "日志级别已设置为: %d", level);
    }
  }
  
  // 返回当前配置
  String json = "{";
  json += "\"level\":" + String((int)loggerGetLevel()) + ",";
  json += "\"levelName\":\"" + String(loggerLevelToStr(loggerGetLevel())) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// ============ 配置备份/恢复 API ============
void handleConfigExport() {
  // 构建配置JSON
  String json = "{";
  json += "\"version\":1,";
  json += "\"exportTime\":" + String(millis() / 1000) + ",";
  
  // 互锁配置
  json += "\"interlock\":{";
  json += "\"enabled\":" + String(interlockCfg.pttLockEnabled ? "true" : "false") + ",";
  json += "\"pttReleaseDelay\":" + String(interlockCfg.pttReleaseDelay) + ",";
  json += "\"relayCooldown\":" + String(interlockCfg.relayCooldownMs);
  json += "},";
  
  // 网络配置
  json += "\"network\":{";
  json += "\"useDHCP\":" + String(useDHCP ? "true" : "false") + ",";
  json += "\"staticIP\":\"" + staticIP.toString() + "\",";
  json += "\"gateway\":\"" + gateway.toString() + "\",";
  json += "\"subnet\":\"" + subnet.toString() + "\",";
  json += "\"dns\":\"" + dns.toString() + "\"";
  json += "},";
  
  // 波段配置
  json += "\"bands\":[";
  for (int i = 0; i < bandCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"name\":\"" + String(bands[i].name) + "\",";
    json += "\"minFreq\":" + String(bands[i].minFreq) + ",";
    json += "\"maxFreq\":" + String(bands[i].maxFreq) + ",";
    json += "\"antenna\":" + String(bands[i].antenna) + ",";
    json += "\"enabled\":" + String(bands[i].enabled ? "true" : "false");
    json += "}";
  }
  json += "],";
  
  // 天线别名
  json += "\"aliases\":[";
  for (int i = 0; i < 6; i++) {
    if (i > 0) json += ",";
    json += "\"" + aliases[i] + "\"";
  }
  json += "]";
  
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleConfigImport() {
  if (!server.hasArg("config")) {
    server.send(400, "text/plain", "Missing config parameter");
    return;
  }
  
  String config = server.arg("config");
  
  // 简单的JSON解析（实际使用ArduinoJson库更好）
  // 这里简化处理，检查基本格式
  if (!config.startsWith("{") || !config.endsWith("}")) {
    server.send(400, "text/plain", "Invalid JSON format");
    return;
  }
  
  // 保存到临时存储
  prefs.begin("config_backup", false);
  prefs.putString("last_import", config);
  prefs.end();
  
  LOG_I(TAG_MAIN, "配置已导入，需要重启生效");
  server.send(200, "text/plain", "Config imported. Please reboot to apply.");
}

void handleConfigReset() {
  // 清除所有配置
  prefs.begin("ant", false);
  prefs.clear();
  prefs.end();
  
  prefs.begin("interlock", false);
  prefs.clear();
  prefs.end();
  
  LOG_I(TAG_MAIN, "配置已恢复出厂设置");
  server.send(200, "text/plain", "Factory reset completed. Please reboot.");
}

void handleDashboard() { server.send(200, "text/html", DASHBOARD_PAGE); }
void handleControl() { server.send(200, "text/html", MAIN_PAGE); }



// ============ 初始化 ============
void setup() {
  Serial.begin(115200);
  delay(100);
  
  // 初始化日志系统
  loggerInit();
  LOG_I(TAG_MAIN, "ESP32-S3 天线切换器启动");
  
  // 初始化GPIO
  for (int i = 0; i < 6; i++) { pinMode(chPins[i], OUTPUT); digitalWrite(chPins[i], LOW); }
  pinMode(PIN_RESET, OUTPUT); digitalWrite(PIN_RESET, LOW);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(DISP_DATA, OUTPUT); pinMode(DISP_CLK, OUTPUT); pinMode(DISP_LATCH, OUTPUT);
  LOG_I(TAG_HW, "GPIO初始化完成");
  
  // 加载配置
  loadCfg();
  LOG_I(TAG_MAIN, "配置加载完成");
  
  if (currentCh >= 1 && currentCh <= 6) switchCh(currentCh);
  
  // 初始化 BLE 客户端模式
  LOG_I(TAG_BLE, "初始化BLE客户端模式...");
  if (bleCivInit("ANT-SW")) {
    LOG_I(TAG_BLE, "BLE客户端已启动");
  } else {
    LOG_E(TAG_BLE, "BLE初始化失败!");
  }
  delay(500);
  
  // 最后初始化网络（WiFi/以太网）
  initNet();
  delay(1000);
  
  server.on("/", handleDashboard);
  server.on("/ctrl", handleControl);
  server.on("/net", handleNet);
  server.on("/wifi", handleWiFiPage);
  server.on("/cat", handleCatPage);
  server.on("/status", handleStatus);
  server.on("/switch", handleSwitch);
  server.on("/alias", handleSetAlias);
  server.on("/netstatus", handleNetStatus);
  server.on("/netsave", handleNetSave);
  server.on("/wifiscan", handleWiFiScan);
  server.on("/wificonnect", handleWiFiConnect);
  server.on("/wifistatus", handleWiFiStatus);
  server.on("/reboot", handleReboot);
  server.on("/btscan", handleBTScan);
  server.on("/btconnect", handleBTConnect);
  server.on("/btdisconnect", handleBTDisconnect);
  server.on("/bandconfig", handleBandConfig);
  server.on("/bandadd", handleBandAdd);
  server.on("/bandupdate", handleBandUpdate);
  server.on("/banddelete", handleBandDelete);
  server.on("/catstatus", handleCatStatus);
  server.on("/catsave", handleCatSave);
  
  // 互锁控制 API
  server.on("/interlockstatus", handleInterlockStatus);
  server.on("/interlocksave", handleInterlockSave);
  
  // 日志 API
  server.on("/logget", handleLogGet);
  server.on("/logclear", handleLogClear);
  server.on("/logconfig", handleLogConfig);
  
  // 配置备份/恢复 API
  server.on("/configexport", handleConfigExport);
  server.on("/configimport", handleConfigImport);
  server.on("/configreset", handleConfigReset);
  
  server.begin();
  
  LOG_I(TAG_NET, "Web服务器已启动");
  LOG_I(TAG_MAIN, "系统初始化完成");
  
  // 输出系统状态
  loggerPrintSystemStats();
}

void loop() {
  server.handleClient();
  updateRelay();
  
  // 处理缓存的切换请求（PTT释放后）
  processPendingSwitch();
  
  // 处理 CAT 控制器（包括 BLE 数据轮询）
  catControllerProcess();
  
  bool btn = digitalRead(PIN_BUTTON);
  if (btn == LOW && lastBtnState == HIGH && millis() - lastBtn > DEBOUNCE_DELAY) {
    int next = currentCh + 1;
    if (next > 6) next = 0;
    switchCh(next);
    lastBtn = millis();
  }
  lastBtnState = btn;
  
  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh >= 50) {
    lastRefresh = millis();
    refreshVFD();
  }
}
