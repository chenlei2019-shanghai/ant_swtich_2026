# ESP32-S3 天线切换器

[![Version](https://img.shields.io/badge/version-v2.1.0-blue)](CHANGELOG.md)
[![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
[![License](https://img.shields.io/badge/license-MIT-yellow)](LICENSE)

基于 ESP32-S3 的业余无线电天线自动切换系统，支持多种CAT协议，可根据频率自动切换天线。

![系统架构](docs/architecture.png)

## 功能特性

### 核心功能
- ✅ **6通道天线切换** - 继电器控制，支持手动/自动切换
- ✅ **自动波段识别** - 根据频率自动选择预设天线
- ✅ **互锁保护** - PTT发射时禁止切换，防止损坏设备
- ✅ **Web控制界面** - 以太网/WiFi远程控制，响应式设计
- ✅ **VFD数码管显示** - 实时显示当前天线通道

### 支持的CAT协议
| 协议 | 连接方式 | 支持的电台 |
|------|----------|-----------|
| ICOM CI-V | BLE/串口 | IC-705/7300/7610/9700等 |
| YAESU | 串口 | FT-991/FTDX101/FT-891等 |
| Kenwood | 串口 | TS-480/570/590/2000等 |
| Elecraft | 串口 | K2/K3/KX2/KX3 |
| FlexRadio | TCP | SmartSDR系列 |

### 网络功能
- 以太网 (W5500) 有线连接
- WiFi STA/AP 模式
- Web服务器 (端口80)
- 配置备份/恢复

## 硬件需求

### 开发板
- **Waveshare ESP32-S3-ETH** 或兼容板
- 8MB Flash, 320KB SRAM
- 内置蓝牙BLE 5.0

### 外设
| 设备 | 接口 | 说明 |
|------|------|------|
| 继电器模块 | GPIO15-18,21,40,41 | 6通道天线切换 |
| W5500模块 | SPI (GPIO9-14) | 以太网连接 |
| VFD数码管 | GPIO38-42 | 状态显示 |
| 按钮 | GPIO3 | 手动切换 |

### 硬件连接图
```
┌─────────────────────────────────────────────────────┐
│                 ESP32-S3-ETH                        │
│                                                     │
│  GPIO1/2    ───────  CAT电台 (UART1)               │
│  GPIO9-14   ───────  W5500以太网模块               │
│  GPIO15-18  ───────  继电器CH1-4                   │
│  GPIO21     ───────  继电器CH3                     │
│  GPIO40/41  ───────  继电器CH5-6                   │
│  GPIO38-42  ───────  VFD数码管                     │
│  GPIO3      ───────  手动按钮                      │
└─────────────────────────────────────────────────────┘
```

详细接口定义见 [HARDWARE_SOFTWARE_INTERFACE.md](HARDWARE_SOFTWARE_INTERFACE.md)

## 软件安装

### 环境要求
- Arduino IDE 2.x
- ESP32 Board Package 2.0.17+
- 必需库:
  - `ESP32 BLE Arduino` (by Neil Kolban)
  - `ArduinoJson` (by Benoit Blanchon)

### 安装步骤

1. **克隆仓库**
```bash
git clone https://github.com/yourusername/ant-sw-kimicode.git
cd ant-sw-kimicode
```

2. **安装依赖库**
```
Arduino IDE → 工具 → 管理库
搜索并安装:
- "ESP32 BLE Arduino"
- "ArduinoJson"
```

3. **配置开发板**
```
工具 → 开发板 → ESP32 Arduino → ESP32S3 Dev Module
Flash Mode: QIO 80MHz
Partition Scheme: Default 4MB with spiffs
```

4. **编译上传**
```
选择正确的COM端口
点击上传按钮
```

### 首次启动

1. 设备启动后，默认IP: `192.168.1.123`
2. 通过网线连接到同一路由器
3. 浏览器访问 `http://192.168.1.123`
4. 进入CAT配置页面设置电台

## 使用方法

### 快速开始

#### 1. 配置IC-705 (BLE)
```
1. IC-705菜单 → Bluetooth → On
2. IC-705菜单 → Pairing Reception → On
3. Web界面 → CAT配置 → 选择"蓝牙BLE"
4. 点击"扫描"，选择IC-705设备
5. 点击"连接"，等待配对完成
6. 开启"自动切换天线"
```

#### 2. 配置有线电台
```
1. 连接电台CAT接口到ESP32的UART1 (GPIO1/2)
2. Web界面 → CAT配置 → 选择对应协议
3. 设置波特率 (通常ICOM/YAESU: 9600, Elecraft: 38400)
4. 保存并重启
```

#### 3. 配置波段和天线
```
Web界面 → CAT配置 → 波段设置
- 选择波段 (如20m)
- 设置频率范围 (14000-14350 kHz)
- 选择对应天线通道 (1-6)
- 点击"添加"
```

### Web界面

| 页面 | 功能 |
|------|------|
| 仪表板 | 查看连接状态、频率、波段、天线通道 |
| 天线控制 | 手动切换天线，设置别名 |
| CAT配置 | 配置电台协议、波段自动切换 |
| 网络设置 | 配置以太网/WiFi参数 |
| 互锁保护 | 设置PTT保护参数 |
| 系统日志 | 查看运行日志 |
| 备份恢复 | 导出/导入配置文件 |

### API接口

```bash
# 切换天线
GET /switch?ch=1

# 获取CAT状态
GET /catstatus
# {"connected":true,"freq":14200000,"band":"20m","antenna":1}

# 配置CAT
GET /catsave?proto=6&auto=1  # BLE模式，自动切换

# 重启设备
GET /reboot
```

完整API文档见 [HARDWARE_SOFTWARE_INTERFACE.md](HARDWARE_SOFTWARE_INTERFACE.md)

## 项目结构

```
ant-sw-kimicode/
├── antenna_switch.ino      # 主程序
├── config.h                # 全局配置
├── cat_controller.cpp/h    # CAT协议控制器
├── icom_civ.cpp/h          # ICOM CI-V协议
├── yaesu_cat.cpp/h         # YAESU CAT协议
├── kenwood_cat.cpp/h       # Kenwood CAT协议
├── elecraft_cat.cpp/h      # Elecraft CAT协议
├── ble_civ.cpp/h           # BLE CI-V驱动
├── cat_hardware.cpp/h      # 硬件抽象层
├── band_calculator.cpp/h   # 波段计算
├── config_validator.cpp/h  # 配置验证
├── logger.cpp/h            # 日志系统
├── README.md               # 本文件
├── HARDWARE_SOFTWARE_INTERFACE.md  # 接口文档
└── docs/                   # 文档目录
```

## 配置说明

### 默认网络配置
```
IP: 192.168.1.123
Gateway: 192.168.1.1
Subnet: 255.255.255.0
```

### 默认CAT配置
```
协议: 关闭
波特率: 9600
轮询间隔: 200ms (BLE), 500ms (有线)
超时: 2000ms
```

### 互锁保护默认参数
```
PTT保护: 开启
PTT释放延迟: 200ms
继电器冷却: 100ms
```

## 故障排查

### 无法访问Web界面
- 检查网线连接
- 确认电脑与ESP32同网段
- 尝试ping 192.168.1.123

### 频率不显示
- 检查CAT连接状态
- 确认电台CAT功能已开启
- 检查CI-V地址/波特率设置
- 查看串口调试信息 (115200)

### BLE连接失败
- 确认电台BLE已开启
- 确认电台在配对模式
- 重启电台和ESP32后重试

### 天线不自动切换
- 检查波段配置是否正确
- 检查"自动切换天线"是否开启
- 检查PTT是否激活(互锁保护)

## 版本历史

### v2.1.0 (2026-03-30)
- 删除MQTT功能(导致重启问题)
- 删除HC-05经典蓝牙支持
- 优化BLE CI-V查询频率至5Hz
- 修复频率显示问题

### v2.0.0 (2026-03-28)
- 统一使用config.h管理配置
- 修复GPIO冲突问题
- 添加互锁保护功能
- 添加配置备份/恢复
- 添加详细日志系统

### v1.x.x (早期版本)
- 基础天线切换功能
- 简单CAT支持

完整日志见 [CHANGELOG.md](CHANGELOG.md)

## 贡献指南

1. Fork本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送分支 (`git push origin feature/AmazingFeature`)
5. 创建Pull Request

## 许可证

本项目采用 [MIT](LICENSE) 许可证

## 致谢

- [K7MDL2/IC-705-BLE-Serial-Example](https://github.com/K7MDL2/IC-705-BLE-Serial-Example) - IC-705 BLE协议参考
- [Hamlib](https://github.com/Hamlib/Hamlib) - CAT协议参考
- [Waveshare](https://www.waveshare.com/) - ESP32-S3-ETH开发板

## 联系方式

- 项目主页: https://github.com/yourusername/ant-sw-kimicode
- 问题反馈: https://github.com/yourusername/ant-sw-kimicode/issues

---

**免责声明**: 本软件用于业余无线电实验，使用无线电设备请遵守当地法律法规。
