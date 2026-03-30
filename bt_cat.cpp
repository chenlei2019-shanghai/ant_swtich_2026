/**
 * HC-05 蓝牙 CAT 协议实现 - 完整版
 * 使用 UART2 (GPIO1/2) 与 HC-05 模块通信
 * 支持经典蓝牙 SPP 协议
 */

#include "bt_cat.h"

static HardwareSerial BTSerial(BT_UART_NUM);
static BTCatConfig btCfg;
static BTCatState btState = BT_CAT_DISCONNECTED;
static uint32_t lastFrequency = 0;
static uint32_t lastRxTime = 0;
static char connectedDevice[32] = "";
static bool atMode = false;

void btCatInit(const BTCatConfig* config) {
    memcpy(&btCfg, config, sizeof(BTCatConfig));
    
    // 初始化 UART2，蓝牙透传模式 9600bps
    BTSerial.begin(BT_BAUD_RATE, SERIAL_8N1, BT_RX_PIN, BT_TX_PIN);
    
    btState = BT_CAT_DISCONNECTED;
    strncpy(connectedDevice, config->deviceName, sizeof(connectedDevice) - 1);
    atMode = false;
    
    Serial.println("[HC05] 初始化完成，UART2 GPIO1/2，波特率 9600");
    
    // 默认设置为从机模式，等待 IC-R30 连接
    delay(1000);  // 等待模块稳定（HC-05上电后需要时间）
    Serial.println("[HC05] 配置从机模式...");
    
    if (btCatEnterATMode()) {
        String resp;
        
        // 设置从机模式
        resp = btCatSendATCommand("AT+ROLE=0");
        Serial.println("[HC05] ROLE=0: " + resp);
        delay(200);
        
        // 设置任意地址连接模式
        resp = btCatSendATCommand("AT+CMODE=1");
        Serial.println("[HC05] CMODE=1: " + resp);
        delay(200);
        
        // 设置蓝牙名称
        resp = btCatSendATCommand("AT+NAME=ANT-SW");
        Serial.println("[HC05] NAME=ANT-SW: " + resp);
        delay(200);
        
        // 查询当前状态
        resp = btCatSendATCommand("AT+STATE?");
        Serial.println("[HC05] 初始状态: " + resp);
        
        btCatExitATMode();
        Serial.println("[HC05] 从机模式配置完成，等待IC-R30连接...");
    } else {
        Serial.println("[HC05] 错误: 无法进入AT模式");
    }
}

void btCatDeinit() {
    BTSerial.end();
    btState = BT_CAT_DISCONNECTED;
    atMode = false;
}

// 数据读写接口 (供 cat_hardware 调用)
int btCatRead(uint8_t* buf, int len) {
    int count = 0;
    while (count < len && BTSerial.available()) {
        buf[count++] = BTSerial.read();
    }
    if (count > 0) {
        Serial.print("[HC05] 读取 ");
        Serial.print(count);
        Serial.print(" 字节: ");
        for (int i = 0; i < count && i < 16; i++) {
            Serial.printf("%02X ", buf[i]);
        }
        if (count > 16) Serial.print("...");
        Serial.println();
    }
    return count;
}

int btCatWrite(const uint8_t* buf, int len) {
    Serial.print("[HC05] 写入 ");
    Serial.print(len);
    Serial.print(" 字节: ");
    for (int i = 0; i < len && i < 16; i++) {
        Serial.printf("%02X ", buf[i]);
    }
    if (len > 16) Serial.print("...");
    Serial.println();
    return BTSerial.write(buf, len);
}

int btCatAvailable() {
    return BTSerial.available();
}

void btCatFlush() {
    BTSerial.flush();
}

// 连接管理
bool btCatConnect(const char* deviceName) {
    strncpy(connectedDevice, deviceName, sizeof(connectedDevice) - 1);
    btState = BT_CAT_CONNECTING;
    
    // 等待连接建立
    delay(1000);
    
    // 简单判断：如果能收发数据则认为已连接
    btState = BT_CAT_CONNECTED;
    Serial.print("[HC05] 已连接到: ");
    Serial.println(deviceName);
    return true;
}

void btCatDisconnect() {
    // 进入 AT 模式发送断开指令
    if (btCatEnterATMode()) {
        btCatSendATCommand("AT+DISC");
        btCatExitATMode();
    }
    btState = BT_CAT_DISCONNECTED;
    Serial.println("[HC05] 断开连接");
}

bool btCatIsConnected() {
    return btState == BT_CAT_CONNECTED;
}

BTCatState btCatGetState() {
    return btState;
}

// 数据处理 - 检测连接状态
void btCatProcess() {
    static unsigned long lastCheck = 0;
    static bool catControllerInitialized = false;
    
    // 每2秒检查一次连接状态（仅在透传模式下）
    if (!atMode && millis() - lastCheck > 2000) {
        lastCheck = millis();
        
        // 方法1: 检查是否有数据流入
        if (btState != BT_CAT_CONNECTED && BTSerial.available() > 0) {
            btState = BT_CAT_CONNECTED;
            Serial.println("[HC05] 检测到数据流入，连接已建立");
        }
        
        // 方法2: 尝试进入AT模式检查STATE（如果不在连接状态）
        if (btState != BT_CAT_CONNECTED) {
            // 临时检查STATE
            BTSerial.end();
            delay(50);
            BTSerial.begin(AT_BAUD_RATE, SERIAL_8N1, BT_RX_PIN, BT_TX_PIN);
            
            // 发送AT+STATE?
            BTSerial.print("AT+STATE?\r\n");
            delay(200);
            
            String resp = "";
            while (BTSerial.available()) {
                resp += (char)BTSerial.read();
            }
            
            // 恢复透传模式
            BTSerial.end();
            delay(50);
            BTSerial.begin(BT_BAUD_RATE, SERIAL_8N1, BT_RX_PIN, BT_TX_PIN);
            
            // 检查是否已连接
            if (resp.indexOf("+STATE:0") >= 0 || resp.indexOf("CONNECTED") >= 0) {
                btState = BT_CAT_CONNECTED;
                Serial.println("[HC05] AT查询: 连接已建立");
            }
        }
        
        // 如果刚检测到连接且CAT控制器未初始化，初始化它
        if (btState == BT_CAT_CONNECTED && !catControllerInitialized) {
            Serial.println("[HC05] 连接已建立，准备初始化CAT控制器...");
            // 注意：这里不能直接调用catControllerInit，因为会导致循环依赖
            // 需要在主程序中检查并初始化
        }
    }
}

uint32_t btCatGetLastFrequency() {
    return lastFrequency;
}

const char* btCatGetConnectedDevice() {
    return connectedDevice;
}

// AT 指令模式
bool btCatEnterATMode() {
    Serial.println("[HC05] 进入 AT 模式...");
    
    BTSerial.end();
    delay(100);
    BTSerial.begin(AT_BAUD_RATE, SERIAL_8N1, BT_RX_PIN, BT_TX_PIN);
    atMode = true;
    
    // 清空缓冲区
    while (BTSerial.available()) {
        BTSerial.read();
    }
    
    // 发送 AT 测试
    BTSerial.print("AT\r\n");
    delay(500);
    
    String resp = "";
    while (BTSerial.available()) {
        resp += (char)BTSerial.read();
    }
    
    bool success = resp.indexOf("OK") >= 0;
    if (success) {
        Serial.println("[HC05] AT 模式已激活");
    } else {
        Serial.println("[HC05] AT 模式激活失败，响应: " + resp);
    }
    return success;
}

void btCatExitATMode() {
    Serial.println("[HC05] 退出 AT 模式，返回透传模式 9600bps");
    BTSerial.end();
    delay(100);
    BTSerial.begin(BT_BAUD_RATE, SERIAL_8N1, BT_RX_PIN, BT_TX_PIN);
    atMode = false;
}

bool btCatIsATMode() {
    return atMode;
}

String btCatSendATCommand(const char* cmd) {
    if (!atMode) {
        return "NOT_IN_AT_MODE";
    }
    
    // 清空缓冲区
    while (BTSerial.available()) {
        BTSerial.read();
    }
    
    BTSerial.print(cmd);
    BTSerial.print("\r\n");
    
    delay(500);
    
    String resp = "";
    while (BTSerial.available()) {
        resp += (char)BTSerial.read();
    }
    
    return resp;
}

// 设备扫描
String btCatSearchDevices() {
    if (!btCatEnterATMode()) {
        return "ERROR: Cannot enter AT mode";
    }
    
    Serial.println("[HC05] 开始扫描设备...");
    
    // 1. 设置为主机模式
    btCatSendATCommand("AT+ROLE=1");
    delay(100);
    
    // 2. 设置查询参数
    btCatSendATCommand("AT+CMODE=1");  // 任意地址连接模式
    delay(100);
    btCatSendATCommand("AT+INQM=0,10,5");  // 标准模式, 最多10个, 超时5秒
    delay(100);
    
    // 3. 开始查询
    btCatSendATCommand("AT+INQ");
    
    // 4. 等待查询完成
    delay(6000);
    
    // 5. 读取结果
    String result = "";
    while (BTSerial.available()) {
        result += (char)BTSerial.read();
    }
    
    // 6. 停止查询
    btCatSendATCommand("AT+INQC");
    
    Serial.println("[HC05] 扫描结果:\n" + result);
    return result;
}

// 连接到指定地址
bool btCatConnectToAddress(const char* addr) {
    Serial.print("[HC05] 连接设备: ");
    Serial.println(addr);
    
    if (!btCatEnterATMode()) {
        Serial.println("[HC05] 无法进入 AT 模式");
        return false;
    }
    
    // 1. 设置为主机模式
    String resp = btCatSendATCommand("AT+ROLE=1");
    Serial.println("[HC05] ROLE=" + resp);
    delay(100);
    
    // 2. 设置连接模式
    resp = btCatSendATCommand("AT+CMODE=0");  // 指定地址模式
    Serial.println("[HC05] CMODE=" + resp);
    delay(100);
    
    // 3. 设置绑定地址
    String bindCmd = "AT+BIND=" + String(addr);
    resp = btCatSendATCommand(bindCmd.c_str());
    Serial.println("[HC05] BIND=" + resp);
    delay(100);
    
    // 4. 发送连接命令
    String linkCmd = "AT+LINK=" + String(addr);
    resp = btCatSendATCommand(linkCmd.c_str());
    Serial.println("[HC05] LINK=" + resp);
    
    // 5. 等待连接建立
    delay(3000);
    
    // 6. 检查连接状态
    resp = btCatSendATCommand("AT+STATE?");
    Serial.println("[HC05] STATE=" + resp);
    
    bool connected = (resp.indexOf("CONNECTED") >= 0 || resp.indexOf("+STATE:0") >= 0);
    
    if (connected) {
        btState = BT_CAT_CONNECTED;
        strncpy(connectedDevice, addr, sizeof(connectedDevice) - 1);
        Serial.println("[HC05] 连接成功，切换到透传模式");
        
        // 7. 退出 AT 模式，进入透传模式
        btCatExitATMode();
        
        // 8. 清空缓冲区，准备数据传输
        delay(100);
        while (BTSerial.available()) {
            BTSerial.read();
        }
        
        Serial.println("[HC05] 透传模式就绪，可以收发数据");
    } else {
        Serial.println("[HC05] 连接失败");
    }
    
    return connected;
}

// 断开连接
bool btCatDisconnectLink() {
    if (!btCatEnterATMode()) {
        return false;
    }
    String resp = btCatSendATCommand("AT+DISC");
    btCatExitATMode();
    btState = BT_CAT_DISCONNECTED;
    return resp.indexOf("OK") >= 0;
}

// 设置为主机模式
bool btCatSetMasterMode() {
    if (!btCatEnterATMode()) {
        return false;
    }
    String resp = btCatSendATCommand("AT+ROLE=1");
    btCatExitATMode();
    return resp.indexOf("OK") >= 0;
}

// 设置为从机模式
bool btCatSetSlaveMode() {
    if (!btCatEnterATMode()) {
        return false;
    }
    String resp = btCatSendATCommand("AT+ROLE=0");
    btCatExitATMode();
    return resp.indexOf("OK") >= 0;
}

// 设置配对密码
bool btCatSetPinCode(const char* pin) {
    if (!btCatEnterATMode()) {
        return false;
    }
    String cmd = "AT+PSWD=" + String(pin);
    String resp = btCatSendATCommand(cmd.c_str());
    btCatExitATMode();
    return resp.indexOf("OK") >= 0;
}

// 设置设备名称
bool btCatSetDeviceName(const char* name) {
    if (!btCatEnterATMode()) {
        return false;
    }
    String cmd = "AT+NAME=" + String(name);
    String resp = btCatSendATCommand(cmd.c_str());
    btCatExitATMode();
    return resp.indexOf("OK") >= 0;
}