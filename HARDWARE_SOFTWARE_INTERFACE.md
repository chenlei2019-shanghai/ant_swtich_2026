# 天线切换器 - 软硬件接口文档

**版本**: v2.1.0  
**日期**: 2026-03-30  
**硬件**: Waveshare ESP32-S3-ETH

---

## 一、硬件接口

### 1.1 引脚分配表

| 功能 | 引脚 | 方向 | 说明 |
|------|------|------|------|
| **继电器控制** ||||
| CH1 | GPIO17 | 输出 | 天线通道1 |
| CH2 | GPIO18 | 输出 | 天线通道2 |
| CH3 | GPIO21 | 输出 | 天线通道3 |
| CH4 | GPIO15 | 输出 | 天线通道4 |
| CH5 | GPIO40 | 输出 | 天线通道5 |
| CH6 | GPIO41 | 输出 | 天线通道6 |
| RESET | GPIO16 | 输出 | 继电器复位 |
| **CAT通信** ||||
| UART1_TX | GPIO1 | 输出 | CAT协议发送 |
| UART1_RX | GPIO2 | 输入 | CAT协议接收 |
| **以太网 (W5500)** ||||
| ETH_MOSI | GPIO11 | 输出 | SPI数据输出 |
| ETH_MISO | GPIO12 | 输入 | SPI数据输入 |
| ETH_SCK | GPIO13 | 输出 | SPI时钟 |
| ETH_CS | GPIO14 | 输出 | SPI片选 |
| ETH_RST | GPIO9 | 输出 | 以太网复位 |
| ETH_INT | GPIO10 | 输入 | 以太网中断 |
| **显示 (VFD)** ||||
| DISP_DATA | GPIO42 | 输出 | 数码管数据 |
| DISP_CLK | GPIO39 | 输出 | 数码管时钟 |
| DISP_LATCH | GPIO38 | 输出 | 数码管锁存 |
| **其他** ||||
| BUTTON | GPIO3 | 输入 | 手动切换按钮 |

### 1.2 硬件连接图

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3-ETH 开发板                      │
├─────────────────────────────────────────────────────────────┤
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐  │
│  │   继电器模块  │    │   CAT电台    │    │   W5500     │  │
│  │              │    │   (UART1)    │    │  以太网模块  │  │
│  │ CH1  GPIO17  │    │              │    │              │  │
│  │ CH2  GPIO18  │    │ TX  GPIO1    │    │ MOSI GPIO11  │  │
│  │ CH3  GPIO21  │    │ RX  GPIO2    │    │ MISO GPIO12  │  │
│  │ CH4  GPIO15  │    │              │    │ SCK  GPIO13  │  │
│  │ CH5  GPIO40  │    │              │    │ CS   GPIO14  │  │
│  │ CH6  GPIO41  │    │              │    │ RST  GPIO9   │  │
│  │ RST  GPIO16  │    │              │    │ INT  GPIO10  │  │
│  └──────────────┘    └──────────────┘    └──────────────┘  │
│                                                             │
│  ┌──────────────┐    ┌──────────────┐                      │
│  │   VFD显示    │    │   控制按钮   │                      │
│  │              │    │              │                      │
│  │ DATA GPIO42  │    │ BTN  GPIO3   │                      │
│  │ CLK  GPIO39  │    │              │                      │
│  │ LAT  GPIO38  │    │              │                      │
│  └──────────────┘    └──────────────┘                      │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 网络配置

| 参数 | 默认值 | 说明 |
|------|--------|------|
| IP地址 | 192.168.1.123 | 静态IP |
| 网关 | 192.168.1.1 | 默认网关 |
| 子网掩码 | 255.255.255.0 | 子网 |
| DNS | 8.8.8.8 | DNS服务器 |
| Web端口 | 80 | HTTP服务 |

---

## 二、软件接口

### 2.1 Web API 接口

#### 天线控制
```
GET /switch?ch={channel}
```
- 切换天线通道 (0-6)
- 0: 全部断开, 1-6: 对应通道

#### CAT状态
```
GET /catstatus
```
- 返回JSON格式CAT状态
```json
{
  "connected": true,
  "ptt": false,
  "freq": 14200000,
  "band": "20m",
  "antenna": 1,
  "connType": "蓝牙BLE",
  "model": "ICOM",
  "mode": 0,
  "rfpwr": 100,
  "bleOnly": false,
  "civAuthorized": true
}
```

#### CAT配置保存
```
GET /catsave?proto={type}&auto={0/1}&[参数...]
```

| 协议 | proto值 | 额外参数 |
|------|---------|----------|
| 关闭 | 0 | - |
| ICOM | 1 | addr=0xA4&baud=115200&conn=ble/serial |
| YAESU | 2 | model=FT-991&baud=9600 |
| Kenwood | 3 | model=TS-2000&baud=9600 |
| Elecraft | 4 | model=K3&baud=38400 |
| FlexRadio | 5 | ip=192.168.1.100&autodisc=1 |
| 蓝牙BLE | 6 | btdev=设备名 |

#### 网络状态
```
GET /netstatus
GET /wifistatus
```

#### 波段配置
```
GET /bandconfig          # 获取波段配置
GET /bandadd?name=20m&min=14000000&max=14350000&ant=1
GET /bandupdate?idx=0&field=ant&value=2
GET /banddelete?idx=0
```

#### 互锁控制
```
GET /interlockstatus
GET /interlocksave?enabled=1&delay=200&cooldown=100
```

#### 配置备份/恢复
```
GET /configexport        # 导出配置(JSON)
POST /configimport       # 导入配置
GET /configreset         # 恢复出厂设置
```

#### 系统控制
```
GET /reboot              # 重启设备
```

### 2.2 BLE通信接口

#### IC-705 BLE配对流程

```
1. 扫描设备
   bleCivScanDevices(timeoutMs)
   
2. 连接设备
   bleCivConnect(deviceAddr)
   
3. 自动配对流程 (K7MDL2协议)
   - 发送UUID: FE F1 00 61 <36字节UUID> FD
   - 发送名称: FE F1 00 62 "IC705-Decoder-01" FD
   - 发送Token: FE F1 00 63 EE 39 09 10 FD
   - 等待授权: FE F1 00 64 FD (CI-V授权成功)
   
4. CI-V通信
   - 频率查询: FE FE A4 E5 03 FD
   - PTT查询:  FE FE A4 E5 1C 00 FD
   - 模式查询: FE FE A4 E5 04 FD
```

#### CI-V命令格式

| 命令 | 代码 | 数据长度 | 说明 |
|------|------|----------|------|
| 读频率 | 0x03 | 0 | 查询当前频率 |
| 读模式 | 0x04 | 0 | 查询工作模式 |
| 读PTT | 0x1C | 1 (0x00) | 查询发射状态 |
| 读S表 | 0x15 | 1 (0x02) | 查询信号强度 |
| 读功率 | 0x14 | 1 (0x0A) | 查询RF功率 |

#### CI-V地址定义

| 设备 | 地址 |
|------|------|
| 控制器 | 0xE0 / 0xE5 |
| IC-705 | 0xA4 |
| IC-7300 | 0x94 |
| IC-7610 | 0x98 |
| IC-9700 | 0xA2 |
| IC-R30 | 0x9C |

---

## 三、通信协议

### 3.1 CAT协议支持

| 协议 | 连接方式 | 波特率 | 说明 |
|------|----------|--------|------|
| ICOM CI-V | 串口/BLE | 9600-115200 | 支持IC-705/7300/7610等 |
| YAESU | 串口 | 4800-115200 | FT-991/FTDX101/FT-891等 |
| Kenwood | 串口 | 9600 | TS-480/570/590/2000等 |
| Elecraft | 串口 | 38400 | K2/K3/KX2/KX3 |
| FlexRadio | TCP | - | SmartSDR API |

### 3.2 自动波段切换

```
频率范围(MHz) -> 波段 -> 天线通道

1.8-2.0  : 160m -> Ant X
3.5-4.0  : 80m  -> Ant X
7.0-7.3  : 40m  -> Ant X
14.0-14.35: 20m -> Ant X
21.0-21.45: 15m -> Ant X
28.0-29.7: 10m  -> Ant X
50.0-54.0: 6m   -> Ant X
```

配置存储在 `bands[]` 数组中，通过 `/bandconfig` API管理。

### 3.3 互锁保护机制

```
PTT激活时:
  ├─ 禁止天线切换
  ├─ 缓存切换请求
  └─ PTT释放后延迟执行

继电器冷却:
  └─ 两次切换间隔 >= 100ms (可配置)
```

---

## 四、配置存储

### 4.1 NVS存储结构

| 命名空间 | 键 | 类型 | 说明 |
|----------|-----|------|------|
| antsw | channel | int | 当前天线通道 |
| antsw | alias0-5 | string | 天线别名 |
| antsw | bands | json | 波段配置 |
| antsw | cat_proto | int | CAT协议类型 |
| antsw | cat_cfg | json | CAT详细配置 |
| antsw | interlock | json | 互锁配置 |
| network | dhcp | bool | 是否DHCP |
| network | ip | string | 静态IP |
| network | gateway | string | 网关 |

### 4.2 配置备份格式

```json
{
  "version": "2.1.0",
  "timestamp": 1712345678,
  "config": {
    "antenna": {
      "currentChannel": 1,
      "aliases": ["ANT-1", "ANT-2", ...]
    },
    "bands": [...],
    "cat": {...},
    "interlock": {...},
    "network": {...}
  }
}
```

---

## 五、串口调试

### 5.1 调试信息

- **波特率**: 115200
- **格式**: [TAG] 消息

常见TAG:
- `[MAIN]` 主程序
- `[CAT]` CAT控制器
- `[BLE]` 蓝牙BLE
- `[NET]` 网络
- `[WEB]` Web服务器
- `[HW]` 硬件
- `[ICOM]` ICOM协议

### 5.2 调试命令

通过Web日志页面或串口查看实时日志。

---

## 六、版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| v2.0.0 | 2026-03-28 | 统一配置，修复引脚冲突 |
| v2.1.0 | 2026-03-30 | 删除MQTT/HC-05，优化BLE 5Hz查询 |

---

## 附录 A: 硬件接线示例

### ICOM电台 (有线)
```
ICOM电台 CI-V接口          ESP32
┌──────────────┐          ┌──────────┐
│ 3.5mm音频口   │          │          │
│ Tip (信号)   ────────────┤ GPIO2 (RX)│
│ Ring (GND)   ────────────┤ GND      │
└──────────────┘          └──────────┘

波特率: 9600-115200 (根据电台设置)
协议: CI-V
```

### YAESU/Kenwood电台
```
电台 CAT接口              ESP32
┌──────────────┐          ┌──────────┐
│ 3.5mm或DB9   │          │          │
│ TX           ────────────┤ GPIO2 (RX)│
│ RX           ────────────┤ GPIO1 (TX)│
│ GND          ────────────┤ GND      │
└──────────────┘          └──────────┘

波特率: 9600 (YAESU/Kenwood默认)
协议: 各品牌CAT协议
```

### IC-705 (BLE)
```
无需接线，通过蓝牙BLE连接
```

---

## 附录 B: 常见问题

### Q1: 频率显示为"--"
- 检查CAT连接状态
- 检查CI-V地址是否正确
- 查看串口调试信息

### Q2: 天线不自动切换
- 检查波段配置
- 检查频率是否在配置范围内
- 检查互锁状态

### Q3: BLE连接失败
- 确认IC-705已开启BLE
- 确认IC-705在配对模式
- 重新扫描并连接

---

**文档结束**
