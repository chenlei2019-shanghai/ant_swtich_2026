// BLE CI-V Driver for ICOM IC-705
// Based on K7MDL2/IC-705-BLE-Serial-Example
// Implements full IC-705 pairing protocol with correct message formats
// 
// 改进记录 2026-03-28:
// - 添加数据就绪标志防止数据竞争 (改进1)
// - 添加分层连接状态管理 (改进2)
// - 添加 CI-V 授权状态跟踪

#include "ble_civ.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

static BLEClient* pClient = nullptr;
static BLERemoteCharacteristic* pCharacteristic = nullptr;  // IC-705 uses same UUID for RX/TX
static void (*dataCallback)(const uint8_t* data, size_t length) = nullptr;

// 改进2: 分层连接状态管理
// BLE 连接状态 (底层蓝牙连接)
static bool ble_connected = false;
// CI-V 授权状态 (应用层协议握手完成)
static bool civ_authorized = false;
// 旧的兼容标志
static bool connected = false;  // 保留兼容，等于 ble_connected && civ_authorized
static bool paired = false;

// BLE 数据接收缓冲区
#define BLE_RX_BUFFER_SIZE 256
static uint8_t bleRxBuffer[BLE_RX_BUFFER_SIZE];
static int bleRxHead = 0;
static int bleRxTail = 0;
static uint32_t bleRxOverflowCount = 0;  // 溢出计数器，用于调试

// 改进1: 数据就绪标志 - 防止数据竞争
//  volatile 确保多线程安全 (BLE 回调在主循环不同上下文执行)
static volatile bool ble_data_ready = false;
static uint8_t ble_message_buffer[64];   // 存储完整消息
static volatile size_t ble_message_length = 0;

// Scan results
struct BleDeviceInfo {
    String name;
    String addr;
    int rssi;
};
static std::vector<BleDeviceInfo> scanResults;

// Target device address for connection
static String targetDeviceAddr = "";

// Pairing state flags (from K7MDL2)
static bool BT_ADDR_confirm = false;
static bool Name_confirm = false;
static bool Token_confirm = false;
static bool Pairing_Accepted = false;
static bool CIV_granted = false;

// IC-705 Pairing Messages (from K7MDL2)
// UUID message - 41 bytes total. Any less and will not get a reply from the Name msg 0x62.
// Here is a generated UUID from https://www.uuidgenerator.net/version4
// Format: FE F1 00 61 <36-byte UUID string> FD
static const uint8_t CIV_ID0[] = {
    0xFE, 0xF1, 0x00, 0x61,
    0x35, 0x36, 0x41, 0x35, 0x36, 0x37, 0x33, 0x30, 0x2D,  // 56a56730-
    0x45, 0x38, 0x42, 0x43, 0x2D,                          // E8BC-
    0x34, 0x39, 0x30, 0x30, 0x2D,                          // 4930-
    0x38, 0x31, 0x42, 0x36, 0x2D,                          // 81B6-
    0x45, 0x46, 0x33, 0x33, 0x45, 0x39, 0x37, 0x33, 0x38, 0x34, 0x32, 0x42,  // EF33E973842B
    0xFD
};

// Name message - "IC705 Decoder 01" - 21 bytes total
// Name must be exactly 16 bytes, msg total 21 bytes
static const uint8_t CIV_ID1[] = {
    0xFE, 0xF1, 0x00, 0x62,
    0x49, 0x43, 0x37, 0x30, 0x35, 0x2D, 0x44, 0x65, 0x63, 0x6F, 0x64, 0x65, 0x72, 0x2D, 0x30, 0x31,  // IC705-Decoder-01
    0xFD
};

// Token message - fixed value 9 bytes (from K7MDL2)
static const uint8_t CIV_ID2[] = {
    0xFE, 0xF1, 0x00, 0x63, 0xEE, 0x39, 0x09, 0x10, 0xFD
};

// Notification on/off values
const uint8_t notificationOff[] = {0x0, 0x0};
const uint8_t notificationOn[] = {0x1, 0x0};

// Notify callback for IC-705 data
static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
    
    Serial.print("[BLE RX] ");
    for (size_t i = 0; i < length && i < 16; i++) {
        Serial.printf("%02X ", pData[i]);
    }
    if (length > 16) Serial.print("...");
    Serial.println();
    
    // Process pairing responses (from K7MDL2)
    if (length >= 5 && pData[0] == 0xFE && pData[1] == 0xF1 && pData[2] == 0x00) {
        switch (pData[3]) {
            case 0x61:
                Serial.println("[BLE] Got BT_ADDR message confirmation");
                BT_ADDR_confirm = true;
                break;
            case 0x62:
                Serial.println("[BLE] Got NAME message confirmation");
                Name_confirm = true;
                break;
            case 0x63:
                Serial.println("[BLE] Got TOKEN message confirmation");
                Token_confirm = true;
                if (length > 4 && pData[4] == 0x01) {
                    Pairing_Accepted = true;
                    Serial.println("[BLE] Pairing accepted!");
                }
                break;
            case 0x64:
                Serial.println("[BLE] CI-V bus ACCESS granted!");
                CIV_granted = true;
                paired = true;
                break;
        }
    }
    
    // 改进1: 数据就绪标志保护 - 防止数据竞争
    // 只有当主程序已读取上一帧数据后，才写入新数据
    if (length > 0 && length < sizeof(ble_message_buffer)) {
        if (!ble_data_ready) {
            // 安全复制数据到消息缓冲区
            memcpy((void*)ble_message_buffer, pData, length);
            ble_message_length = length;
            ble_data_ready = true;
            
            // 同时复制到环形缓冲区保持兼容
            size_t stored = 0;
            size_t dropped = 0;
            for (size_t i = 0; i < length; i++) {
                int next = (bleRxHead + 1) % BLE_RX_BUFFER_SIZE;
                if (next != bleRxTail) {
                    bleRxBuffer[bleRxHead] = pData[i];
                    bleRxHead = next;
                    stored++;
                } else {
                    dropped++;
                    bleRxOverflowCount++;
                }
            }
            
            if (dropped > 0 && (bleRxOverflowCount % 100 == 1)) {
                Serial.printf("[BLE] Warning: Buffer overflow, dropped %u bytes (total: %lu)\n", 
                              dropped, bleRxOverflowCount);
            }
        } else {
            // 上一帧数据还未被读取，丢弃新数据
            Serial.println("[BLE] Warning: Data dropped - previous message not processed");
        }
    }
    
    // 保留回调兼容性
    if (dataCallback) {
        dataCallback(pData, length);
    }
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
        Serial.println("[BLE] Connected (BLE layer)");
        ble_connected = true;
        connected = true;  // 兼容旧代码
    }
    void onDisconnect(BLEClient* pclient) {
        Serial.println("[BLE] Disconnected (BLE layer)");
        // 改进2: 完整的连接状态重置
        ble_connected = false;
        civ_authorized = false;
        connected = false;
        paired = false;
        BT_ADDR_confirm = false;
        Name_confirm = false;
        Token_confirm = false;
        Pairing_Accepted = false;
        CIV_granted = false;
        // 清除数据就绪标志
        ble_data_ready = false;
        ble_message_length = 0;
    }
};

// Scan callbacks
class BleScanCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String name = advertisedDevice.getName().c_str();
        String addr = advertisedDevice.getAddress().toString().c_str();
        int rssi = advertisedDevice.getRSSI();
        
        // Check for IC-705 service UUID
        bool hasIC705Service = false;
        if (advertisedDevice.haveServiceUUID()) {
            BLEUUID serviceUUID = BLEUUID(IC705_SERVICE_UUID);
            if (advertisedDevice.isAdvertisingService(serviceUUID)) {
                hasIC705Service = true;
            }
        }
        
        if (name.length() > 0 || hasIC705Service) {
            // 去重检查 - 如果设备已存在，更新RSSI（如果更强）
            for (auto& dev : scanResults) {
                if (dev.addr == addr) {
                    // 更新RSSI如果新信号更强
                    if (rssi > dev.rssi) {
                        dev.rssi = rssi;
                        Serial.printf("[BLE] Updated RSSI for %s: %d dBm\n", 
                                      name.c_str(), rssi);
                    }
                    return;  // 已存在，跳过添加
                }
            }
            
            // 新设备，添加到列表
            Serial.print("[BLE] Found: ");
            Serial.print(name.length() > 0 ? name : "(no name)");
            Serial.print(" [");
            Serial.print(addr);
            Serial.print("] RSSI:");
            Serial.print(rssi);
            if (hasIC705Service) {
                Serial.print(" [ICOM-BLE]");
            }
            Serial.println();
            
            BleDeviceInfo dev;
            // 如果设备名称为空，但检测到ICOM BLE服务，显示[ICOM-BLE]
            if (name.length() > 0) {
                dev.name = name;
            } else if (hasIC705Service) {
                dev.name = "[ICOM-BLE]";
            } else {
                dev.name = addr;
            }
            dev.addr = addr;
            dev.rssi = rssi;
            scanResults.push_back(dev);
        }
    }
};

bool bleCivInit(const char* deviceName) {
    Serial.println("[BLE] Initializing...");
    BLEDevice::init(deviceName);
    Serial.println("[BLE] Ready");
    return true;
}

void bleCivDeinit() {
    bleCivDisconnect();
}

// 检查设备是否已在结果列表中
static bool isDeviceInResults(const String& addr) {
    for (const auto& dev : scanResults) {
        if (dev.addr == addr) {
            return true;
        }
    }
    return false;
}

String bleCivScanDevices(uint32_t timeoutMs) {
    scanResults.clear();
    
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new BleScanCallbacks());
    pBLEScan->setActiveScan(true);
    
    // 优化扫描参数 - 更密集的扫描以提高发现概率
    // 这些参数提供更频繁的扫描窗口
    pBLEScan->setInterval(100);   // 降低间隔，增加扫描频率
    pBLEScan->setWindow(99);      // 接近间隔的窗口，几乎连续扫描
    
    Serial.printf("[BLE] Starting scan for %lu ms...\n", timeoutMs);
    uint32_t startTime = millis();
    
    // 使用异步扫描，分批处理避免阻塞
    pBLEScan->start(timeoutMs / 1000, false);
    
    // 分段等待，期间可以处理其他事件
    while (millis() - startTime < timeoutMs) {
        delay(100);
        // 喂狗，防止看门狗复位
        yield();
    }
    
    pBLEScan->stop();
    Serial.printf("[BLE] Scan complete, found %d devices\n", scanResults.size());
    
    String json = "[";
    for (size_t i = 0; i < scanResults.size(); i++) {
        if (i > 0) json += ",";
        json += "{\"name\":\"" + scanResults[i].name + "\",";
        json += "\"address\":\"" + scanResults[i].addr + "\",";
        json += "\"rssi\":" + String(scanResults[i].rssi) + "}";
    }
    json += "]";
    
    return json;
}

// Perform IC-705 pairing protocol (from K7MDL2)
static bool performIC705Pairing() {
    Serial.println("[BLE] Starting IC-705 pairing protocol...");
    
    // Reset pairing flags
    BT_ADDR_confirm = false;
    Name_confirm = false;
    Token_confirm = false;
    Pairing_Accepted = false;
    CIV_granted = false;
    
    // Get IC-705 service
    BLERemoteService* pService = pClient->getService(IC705_SERVICE_UUID);
    if (pService == nullptr) {
        Serial.println("[BLE] IC-705 service not found");
        return false;
    }
    Serial.println("[BLE] IC-705 service found");
    
    // Get the characteristic (IC-705 uses same UUID for RX/TX)
    pCharacteristic = pService->getCharacteristic(IC705_CHAR_UUID);
    if (pCharacteristic == nullptr) {
        Serial.println("[BLE] Characteristic not found");
        return false;
    }
    Serial.println("[BLE] Characteristic found");
    
    // 首先启用通知，确保能接收配对响应
    if (pCharacteristic->canNotify()) {
        pCharacteristic->registerForNotify(notifyCallback);
        // 启用通知描述符
        pCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t*)notificationOn, 2, true);
        Serial.println("[BLE] Notifications registered and enabled");
        delay(100);  // 等待通知启用完成
    }
    
    // Step 1: Send UUID (41 bytes)
    Serial.println("[BLE] Step 1: Sending UUID...");
    pCharacteristic->writeValue((uint8_t*)CIV_ID0, sizeof(CIV_ID0), true);
    delay(100);  // 增加延迟，等待设备响应
    
    // Step 2: Send Name (21 bytes)
    Serial.println("[BLE] Step 2: Sending device name...");
    pCharacteristic->writeValue((uint8_t*)CIV_ID1, sizeof(CIV_ID1), true);
    delay(100);  // 增加延迟，等待设备响应
    
    // Step 3: Send Token (9 bytes)
    Serial.println("[BLE] Step 3: Sending pairing token...");
    pCharacteristic->writeValue((uint8_t*)CIV_ID2, sizeof(CIV_ID2), true);
    delay(100);  // 增加延迟，等待设备处理
    
    // Wait for CI-V authorization (timeout 8 seconds - 增加超时时间)
    Serial.println("[BLE] Waiting for CI-V authorization...");
    unsigned long startTime = millis();
    bool authReceived = false;
    while (millis() - startTime < 8000) {
        if (CIV_granted) {
            authReceived = true;
            break;
        }
        delay(10);
    }
    
    if (authReceived) {
        Serial.println("[BLE] Pairing completed successfully!");
    } else {
        Serial.println("[BLE] Pairing timeout - no CI-V authorization");
        Serial.printf("[BLE] Status: BT_ADDR=%d, Name=%d, Token=%d, Paired=%d\n",
                      BT_ADDR_confirm, Name_confirm, Token_confirm, Pairing_Accepted);
        // 没有授权，连接不可用
        return false;
    }
    
    return true;
}

bool bleCivConnect(const char* deviceName) {
    Serial.printf("[BLE] bleCivConnect called with '%s'\n", deviceName);
    
    // Find address from scan results
    targetDeviceAddr = "";
    Serial.printf("[BLE] scanResults.size() = %d\n", scanResults.size());
    for (auto& dev : scanResults) {
        Serial.printf("[BLE] Checking: '%s' vs '%s' / '%s'\n", deviceName, dev.name.c_str(), dev.addr.c_str());
        if (dev.name == deviceName || dev.addr == deviceName) {
            targetDeviceAddr = dev.addr;
            break;
        }
    }
    
    // 如果在扫描结果中找不到，但 deviceName 看起来像 MAC 地址，直接使用
    if (targetDeviceAddr == "" && strlen(deviceName) == 17 && deviceName[2] == ':') {
        Serial.println("[BLE] Using provided MAC address directly");
        targetDeviceAddr = deviceName;
    }
    
    if (targetDeviceAddr == "") {
        Serial.println("[BLE] Device not found in scan results");
        return false;
    }
    
    Serial.print("[BLE] Connecting to: ");
    Serial.println(targetDeviceAddr.c_str());
    
    BLEAddress addr(targetDeviceAddr.c_str());
    
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    
    if (!pClient->connect(addr)) {
        Serial.println("[BLE] Connection failed");
        delete pClient;
        pClient = nullptr;
        return false;
    }
    
    Serial.println("[BLE] Connected to device");
    connected = true;
    
    // Perform IC-705 pairing protocol
    if (!performIC705Pairing()) {
        Serial.println("[BLE] Pairing failed");
        bleCivDisconnect();
        return false;
    }
    
    // 改进2: 更新分层状态
    if (CIV_granted) {
        civ_authorized = true;
        Serial.println("[BLE] CI-V authorization granted - fully operational");
    } else {
        Serial.println("[BLE] CI-V authorization pending - limited functionality");
    }
    
    // 保存成功连接的设备地址
    bleCivSaveLastDevice(targetDeviceAddr.c_str());
    
    Serial.println("[BLE] Ready for CI-V communication");
    return true;
}

void bleCivDisconnect() {
    if (pClient) {
        pClient->disconnect();
        delete pClient;
        pClient = nullptr;
    }
    pCharacteristic = nullptr;
    
    // 改进2: 完整的连接状态重置
    ble_connected = false;
    civ_authorized = false;
    connected = false;
    paired = false;
    BT_ADDR_confirm = false;
    Name_confirm = false;
    Token_confirm = false;
    Pairing_Accepted = false;
    CIV_granted = false;
    
    // 清除数据就绪标志
    ble_data_ready = false;
    ble_message_length = 0;
}

bool bleCivIsConnected() {
    // 改进2: 检查 BLE 层连接状态
    return ble_connected && pClient && pClient->isConnected();
}

// 改进2: 新增 - 检查 CI-V 授权状态
bool bleCivIsAuthorized() {
    return civ_authorized && CIV_granted;
}

// 改进2: 新增 - 获取详细连接状态
BleConnStatus bleCivGetStatus() {
    if (!ble_connected || !pClient || !pClient->isConnected()) {
        return BLE_STATUS_DISCONNECTED;
    }
    if (!civ_authorized || !CIV_granted) {
        return BLE_STATUS_CONNECTED_NO_CIV;
    }
    return BLE_STATUS_FULLY_OPERATIONAL;
}

bool bleCivIsPaired() {
    return paired;
}

// 改进1: 新增 - 检查数据就绪状态
bool bleCivDataReady() {
    return ble_data_ready;
}

// 改进1: 新增 - 获取完整消息
int bleCivGetMessage(uint8_t* buf, int maxLen) {
    if (!ble_data_ready || buf == nullptr || maxLen <= 0) {
        return 0;
    }
    
    // 复制消息到用户缓冲区
    size_t copyLen = ble_message_length;
    if (copyLen > (size_t)maxLen) {
        copyLen = maxLen;
    }
    memcpy(buf, (const void*)ble_message_buffer, copyLen);
    
    // 清除就绪标志，允许接收新数据
    ble_data_ready = false;
    ble_message_length = 0;
    
    return copyLen;
}

bool bleCivWrite(const uint8_t* data, size_t length) {
    if (!bleCivIsConnected() || pCharacteristic == nullptr) {
        return false;
    }
    
    Serial.print("[BLE TX] ");
    for (size_t i = 0; i < length && i < 16; i++) {
        Serial.printf("%02X ", data[i]);
    }
    if (length > 16) Serial.print("...");
    Serial.println();
    
    pCharacteristic->writeValue((uint8_t*)data, length, false);
    return true;
}

int bleCivRead(uint8_t* buf, int len) {
    if (buf == nullptr || len <= 0) return 0;
    
    // 如果有消息缓冲区数据未处理，先复制到环形缓冲区
    if (ble_data_ready && ble_message_length > 0) {
        // 将消息缓冲区数据复制到环形缓冲区
        for (size_t i = 0; i < ble_message_length && bleRxHead != bleRxTail; i++) {
            int next = (bleRxHead + 1) % BLE_RX_BUFFER_SIZE;
            if (next != bleRxTail) {
                bleRxBuffer[bleRxHead] = ble_message_buffer[i];
                bleRxHead = next;
            }
        }
        // 清除消息缓冲区标志
        ble_data_ready = false;
        ble_message_length = 0;
    }
    
    int count = 0;
    while (count < len && bleRxHead != bleRxTail) {
        buf[count++] = bleRxBuffer[bleRxTail];
        bleRxTail = (bleRxTail + 1) % BLE_RX_BUFFER_SIZE;
    }
    return count;
}

int bleCivAvailable() {
    // 如果有消息缓冲区数据，也算作可用
    int msgCount = ble_data_ready ? ble_message_length : 0;
    
    int bufCount = bleRxHead - bleRxTail;
    if (bufCount < 0) bufCount += BLE_RX_BUFFER_SIZE;
    
    return msgCount + bufCount;
}

void bleCivSetCallback(void (*callback)(const uint8_t* data, size_t length)) {
    dataCallback = callback;
}

void bleCivProcess() {
    // 改进2: 连接状态维护
    bool phyConnected = pClient && pClient->isConnected();
    
    if (ble_connected && !phyConnected) {
        Serial.println("[BLE] Connection lost detected - resetting all states");
        ble_connected = false;
        civ_authorized = false;
        connected = false;
        paired = false;
        BT_ADDR_confirm = false;
        Name_confirm = false;
        Token_confirm = false;
        Pairing_Accepted = false;
        CIV_granted = false;
        ble_data_ready = false;
        ble_message_length = 0;
    } else if (!ble_connected && phyConnected) {
        // 物理连接存在但状态未更新 (可能在配对过程中)
        ble_connected = true;
        connected = true;
        Serial.println("[BLE] Physical connection confirmed");
    }
    
    // 更新 CI-V 授权状态
    if (CIV_granted && !civ_authorized) {
        civ_authorized = true;
        Serial.println("[BLE] CI-V authorization state updated");
    }
}

String bleCivGetConnectedDevice() {
    return connected ? targetDeviceAddr : "";
}

int bleCivGetRSSI() {
    if (pClient && pClient->isConnected()) {
        return pClient->getRssi();
    }
    return -100;
}

// ============ 自动重连功能 ============
#include <Preferences.h>

// 检查是否启用自动重连
bool bleCivIsAutoReconnectEnabled() {
    Preferences prefs;
    prefs.begin("ble", true);
    bool enabled = prefs.getBool("autoReconnect", true);  // 默认启用
    prefs.end();
    return enabled;
}

// 设置自动重连开关
void bleCivSetAutoReconnect(bool enable) {
    Preferences prefs;
    prefs.begin("ble", false);
    prefs.putBool("autoReconnect", enable);
    prefs.end();
    Serial.printf("[BLE] 自动重连已%s\n", enable ? "启用" : "禁用");
}

// 清除保存的设备
void bleCivClearLastDevice() {
    Preferences prefs;
    prefs.begin("ble", false);
    prefs.remove("lastDevice");
    prefs.remove("lastConnTime");
    prefs.end();
    Serial.println("[BLE] 已清除保存的设备");
}

void bleCivSaveLastDevice(const char* deviceAddr) {
    if (deviceAddr == nullptr || strlen(deviceAddr) == 0) return;
    
    Preferences prefs;
    prefs.begin("ble", false);
    prefs.putString("lastDevice", deviceAddr);
    prefs.putUInt("lastConnTime", millis() / 1000);  // 保存连接时间戳
    prefs.end();
    
    Serial.printf("[BLE] 已保存上次连接设备: %s\n", deviceAddr);
}

String bleCivGetLastDevice() {
    Preferences prefs;
    prefs.begin("ble", true);
    String lastDevice = prefs.getString("lastDevice", "");
    prefs.end();
    return lastDevice;
}

bool bleCivAutoConnect(uint32_t scanTimeoutMs) {
    String lastDevice = bleCivGetLastDevice();
    Serial.printf("[BLE] bleCivAutoConnect called, lastDevice='%s', timeout=%lu\n", 
                  lastDevice.c_str(), scanTimeoutMs);
    
    if (lastDevice.length() == 0) {
        Serial.println("[BLE] 没有保存的设备，跳过自动连接");
        return false;
    }
    
    Serial.printf("[BLE] 开始自动扫描上次设备: %s (timeout=%lu ms)\n", lastDevice.c_str(), scanTimeoutMs);
    
    // 扫描设备 - 使用优化参数
    scanResults.clear();
    BLEScan* scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new BleScanCallbacks());
    scan->setActiveScan(true);
    scan->setInterval(100);   // 优化扫描间隔
    scan->setWindow(99);      // 接近连续扫描
    
    uint32_t startTime = millis();
    scan->start(scanTimeoutMs / 1000, false);
    
    // 分段等待，避免阻塞
    while (millis() - startTime < scanTimeoutMs) {
        delay(100);
        yield();
    }
    
    scan->stop();
    
    // 查找上次连接的设备
    bool found = false;
    String targetName = "";
    
    for (auto& dev : scanResults) {
        if (dev.addr == lastDevice) {
            found = true;
            targetName = dev.name;
            break;
        }
    }
    
    if (!found) {
        Serial.println("[BLE] 未找到上次连接的设备");
        return false;
    }
    
    Serial.printf("[BLE] 找到设备 %s [%s]，开始连接...\n", 
                  targetName.c_str(), lastDevice.c_str());
    
    // 尝试连接
    Serial.printf("[BLE] 调用 bleCivConnect('%s')...\n", lastDevice.c_str());
    bool result = bleCivConnect(lastDevice.c_str());
    if (result) {
        Serial.println("[BLE] 自动连接成功！");
    } else {
        Serial.println("[BLE] 自动连接失败");
    }
    return result;
}

// 在连接成功后保存设备地址
void onBleConnected(const char* deviceAddr) {
    bleCivSaveLastDevice(deviceAddr);
}