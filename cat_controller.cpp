/**
 * CAT 控制器实现 - 增强版
 * 整合 K7MDL2 的 CI-V 处理逻辑
 * 
 * 改进记录 2026-03-28:
 * - 添加 BLE 分层状态管理支持
 * - 区分 BLE 连接和 CI-V 授权状态
 * 
 * 改进记录 2026-03-28 (2):
 * - 添加 Kenwood CAT 协议支持 (参考 Hamlib)
 * - 添加 Elecraft CAT 协议支持 (参考 Elecraft Programmer's Reference)
 */

#include "cat_controller.h"
#include "icom_civ.h"
#include "yaesu_cat.h"
#include "kenwood_cat.h"
#include "elecraft_cat.h"
#include "flexradio_smsdr.h"
#include "band_calculator.h"
#include "cat_hardware.h"
#include "ble_civ.h"
#include "logger.h"

extern void switchToChannel(int channel);
extern int getCurrentChannel();

static CatControllerConfig catCfg;
static CatControllerStatus catStatus;
static uint32_t lastSwitchTime = 0;
static bool initialized = false;

bool catControllerInit(const CatControllerConfig* config) {
    memcpy(&catCfg, config, sizeof(CatControllerConfig));
    memset(&catStatus, 0, sizeof(CatControllerStatus));
    
    switch (catCfg.type) {
        case CAT_PROTO_ICOM:
            if (!catHardwareInit(catCfg.icom.baudrate)) return false;
            {
                IcomConfig icomCfg = {
                    .radioAddress = catCfg.icom.radioAddress,
                    .controllerAddress = ICOM_ADDR_CONTROLLER_ALT, // 使用 E5 避免冲突
                    .pollInterval = 200,  // 5Hz 高频查询
                    .timeout = 2000,
                    .connType = catCfg.icom.connType
                };
                icomInit(&icomCfg);
                // 根据连接类型设置硬件类型
                switch (catCfg.icom.connType) {
                    case ICOM_CONN_BLE:
                        catHardwareSetType(CAT_HW_BLE);
                        break;
                    case ICOM_CONN_SERIAL:
                    default:
                        catHardwareSetType(CAT_HW_SERIAL);
                        break;
                }
            }
            break;
            
        case CAT_PROTO_YAESU:
            if (!catHardwareInit(catCfg.yaesu.baudrate)) return false;
            {
                YaesuConfig yaesuCfg = {
                    .model = (YaesuModel)catCfg.yaesu.model,
                    .pollInterval = 500,
                    .timeout = 2000
                };
                yaesuInit(&yaesuCfg);
            }
            break;
            
        case CAT_PROTO_KENWOOD:
            if (!catHardwareInit(catCfg.kenwood.baudrate)) return false;
            {
                KenwoodConfig kwCfg = {
                    .model = (KenwoodModel)catCfg.kenwood.model,
                    .pollInterval = 500,
                    .timeout = 2000
                };
                kenwoodInit(&kwCfg);
                catHardwareSetType(CAT_HW_SERIAL);
            }
            break;
            
        case CAT_PROTO_ELECRAFT:
            if (!catHardwareInit(catCfg.elecraft.baudrate)) return false;
            {
                ElecraftConfig eleCfg = {
                    .model = (ElecraftModel)catCfg.elecraft.model,
                    .pollInterval = 500,
                    .timeout = 2000,
                    .extendedMode = true  // 启用扩展模式
                };
                elecraftInit(&eleCfg);
                catHardwareSetType(CAT_HW_SERIAL);
            }
            break;
            
        case CAT_PROTO_FLEXRADIO:
            {
                FlexConfig flexCfg = {
                    .radioIP = catCfg.flex.radioIP,
                    .commandPort = FLEX_COMMAND_PORT,
                    .timeout = 5000,
                    .autoDiscover = catCfg.flex.autoDiscover
                };
                flexInit(&flexCfg);
            }
            break;
            
        default:
            return false;
    }
    
    bandCalcInit(DEFAULT_BANDS, BAND_CALC_DEFAULT_COUNT);
    initialized = true;
    return true;
}

void catControllerDeinit() {
    if (!initialized) return;
    
    switch (catCfg.type) {
        case CAT_PROTO_ICOM:
            icomDeinit();
            catHardwareDeinit();
            break;
        case CAT_PROTO_YAESU:
            yaesuDeinit();
            catHardwareDeinit();
            break;
        case CAT_PROTO_KENWOOD:
            kenwoodDeinit();
            catHardwareDeinit();
            break;
        case CAT_PROTO_ELECRAFT:
            elecraftDeinit();
            catHardwareDeinit();
            break;
        case CAT_PROTO_FLEXRADIO:
            flexDeinit();
            break;
        default:
            break;
    }
    
    initialized = false;
}

void catControllerProcess() {
    if (!initialized) return;
    
    static bool lastPttState = false;
    uint32_t frequency = 0;
    bool connected = false;
    
    switch (catCfg.type) {
        case CAT_PROTO_ICOM: {
            icomProcess();
            
            // 获取频率 (Hz)，不是 MHz
            const IcomStatus* icomSt = icomGetStatus();
            if (icomSt) {
                frequency = (uint32_t)icomSt->frequency;  // Hz 值
            }
            
            // 改进: 对于 BLE 连接，使用分层状态检查
            if (catCfg.icom.connType == ICOM_CONN_BLE) {
                // BLE 模式: 需要 BLE 连接 + CI-V 授权
                bool bleConn = bleCivIsConnected();
                bool civAuth = bleCivIsAuthorized();
                connected = bleConn && civAuth;
                
                // 如果 BLE 已连接但 CI-V 未授权，输出调试信息
                if (bleConn && !civAuth) {
                    static uint32_t lastWarn = 0;
                    if (millis() - lastWarn > 5000) {  // 每5秒输出一次
                        LOG_W(TAG_CAT, "BLE已连接但CI-V未授权");
                        lastWarn = millis();
                    }
                }
            } else {
                // 有线/经典蓝牙模式: 使用传统连接检测
                connected = icomIsConnected();
            }
            
            // 获取详细的 ICOM 状态
            if (icomSt) {
                catStatus.ptt = icomSt->ptt;
                catStatus.mode = icomSt->mode;
                catStatus.rfpwr = icomSt->rfpwr;
                catStatus.smeter = 0; // TODO: 需要转换
                catStatus.connType = icomGetConnType();
            }
            break;
        }
        case CAT_PROTO_YAESU:
            yaesuProcess();
            frequency = yaesuGetLastFrequency();
            connected = yaesuIsConnected();
            catStatus.connType = ICOM_CONN_SERIAL;
            break;
            
        case CAT_PROTO_KENWOOD:
            kenwoodProcess();
            frequency = kenwoodGetLastFrequency();
            connected = kenwoodIsConnected();
            {
                const KenwoodStatus* kwSt = kenwoodGetStatus();
                if (kwSt) {
                    catStatus.ptt = kwSt->ptt;
                    catStatus.mode = kwSt->mode;
                    catStatus.rfpwr = kwSt->rfpwr;
                    catStatus.smeter = kwSt->smeter;
                }
            }
            catStatus.connType = ICOM_CONN_SERIAL;
            break;
            
        case CAT_PROTO_ELECRAFT:
            elecraftProcess();
            frequency = elecraftGetLastFrequency();
            connected = elecraftIsConnected();
            {
                const ElecraftStatus* eleSt = elecraftGetStatus();
                if (eleSt) {
                    catStatus.ptt = eleSt->ptt;
                    catStatus.mode = eleSt->mode;
                    catStatus.rfpwr = eleSt->rfpwr;
                    catStatus.smeter = eleSt->smeter;
                }
            }
            catStatus.connType = ICOM_CONN_SERIAL;
            break;
            
        case CAT_PROTO_FLEXRADIO:
            flexProcess();
            frequency = flexGetTXFrequency();
            connected = flexIsConnected();
            catStatus.connType = ICOM_CONN_NONE;
            break;
            
        default:
            break;
    }
    
    catStatus.frequency = frequency;
    catStatus.connected = connected;
    
    if (connected && frequency != 0 && frequency != catStatus.lastFrequency) {
        catStatus.lastFrequency = frequency;
        
        if (catCfg.autoSwitch) {
            uint32_t now = millis();
            if (now - lastSwitchTime >= catCfg.switchDelay) {
                uint8_t channel = getAntennaChannel(frequency);
                if (channel != getCurrentChannel()) {
                    switchToChannel(channel);
                    lastSwitchTime = now;
                }
            }
        }
    }
    
}

uint32_t catControllerGetFrequency() {
    return catStatus.frequency;
}

uint32_t catControllerGetLastFrequency() {
    return catStatus.lastFrequency;
}

const char* catControllerGetBandName() {
    const AmateurBand* band = freqToBand(catStatus.frequency);
    if (band) return band->name;
    return "--";
}

uint8_t catControllerGetRecommendedChannel() {
    return getAntennaChannel(catStatus.frequency);
}

void catControllerSetAutoSwitch(bool enable) {
    catCfg.autoSwitch = enable;
}

bool catControllerGetAutoSwitch() {
    return catCfg.autoSwitch;
}

bool catControllerIsConnected() {
    if (!initialized) return false;
    
    // 改进: 对于 BLE 模式，实时检查状态
    if (catCfg.type == CAT_PROTO_ICOM && catCfg.icom.connType == ICOM_CONN_BLE) {
        return bleCivIsConnected() && bleCivIsAuthorized();
    }
    
    return catStatus.connected;
}

// 改进2: 新增 - 获取详细连接状态
CatConnDetail catControllerGetConnDetail() {
    CatConnDetail detail = {};
    detail.initialized = initialized;
    detail.protocol = catCfg.type;
    
    if (!initialized) {
        detail.overallState = CAT_CONN_DISCONNECTED;
        return detail;
    }
    
    if (catCfg.type == CAT_PROTO_ICOM) {
        detail.icomConnType = catCfg.icom.connType;
        
        if (catCfg.icom.connType == ICOM_CONN_BLE) {
            // BLE 分层状态
            detail.bleConnected = bleCivIsConnected();
            detail.civAuthorized = bleCivIsAuthorized();
            
            if (!detail.bleConnected) {
                detail.overallState = CAT_CONN_DISCONNECTED;
            } else if (!detail.civAuthorized) {
                detail.overallState = CAT_CONN_BLE_ONLY;
            } else {
                detail.overallState = CAT_CONN_FULL;
            }
        } else {
            // 有线连接
            detail.overallState = icomIsConnected() ? CAT_CONN_FULL : CAT_CONN_DISCONNECTED;
        }
    } else if (catCfg.type == CAT_PROTO_YAESU) {
        detail.overallState = yaesuIsConnected() ? CAT_CONN_FULL : CAT_CONN_DISCONNECTED;
    } else if (catCfg.type == CAT_PROTO_KENWOOD) {
        detail.overallState = kenwoodIsConnected() ? CAT_CONN_FULL : CAT_CONN_DISCONNECTED;
    } else if (catCfg.type == CAT_PROTO_ELECRAFT) {
        detail.overallState = elecraftIsConnected() ? CAT_CONN_FULL : CAT_CONN_DISCONNECTED;
    } else if (catCfg.type == CAT_PROTO_FLEXRADIO) {
        detail.overallState = flexIsConnected() ? CAT_CONN_FULL : CAT_CONN_DISCONNECTED;
    }
    
    return detail;
}

CatProtocolType catControllerGetProtocol() {
    return catCfg.type;
}

const CatControllerStatus* catControllerGetStatus() {
    return &catStatus;
}

bool catControllerGetPTT() {
    return catStatus.ptt;
}

uint8_t catControllerGetMode() {
    return catStatus.mode;
}

uint8_t catControllerGetRFPower() {
    return catStatus.rfpwr;
}

const char* catControllerGetConnTypeStr() {
    return icomGetConnTypeStr(catStatus.connType);
}

void catControllerSetConnType(IcomConnType type) {
    icomSetConnType(type);
    catStatus.connType = type;
}