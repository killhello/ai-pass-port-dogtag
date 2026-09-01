# FoloToy AI Passport - 电子狗牌

English | [简体中文](README.zh_CN.md)

基于 FoloToy AI Passport 硬件的电子狗牌项目。支持 BLE 配对、主人信息绑定、信号强度检测、丢失报警和手机查找设备功能。

## 硬件规格

| 参数 | 规格 |
|------|------|
| 主控 | ESP32-C3 (QFN32) |
| Flash | 8MB (XMC) |
| 显示屏 | ST7789P3, 240×320, RGB565, SPI2 40MHz |
| 音频 | ES8311, I2S0, 全双工 PCM |
| 电池 | CW2017 电量检测 |
| 蓝牙 | NimBLE BLE 5.0 |
| 按键 | UP/DOWN/OK (ADC 电阻分压) |

## 功能特性

### BLE 配对
- 设备启动后进入配对模式，显示"配对模式 请扫描蓝牙 设置主人信息"
- 通过 Web Bluetooth 工具写入主人姓名和电话
- 配对完成后设备进入静默模式，显示姓名、电话和 RSSI 信号强度

### 信号强度显示
- BLE 连接时显示实时 RSSI 值（绿色）
- 断开连接时显示红色 0

### 丢失报警
- BLE 断开后进入静默模式，继续广播
- 超时后触发报警（语音+蜂鸣）

### 查找设备
- 手机端点击"查找设备"按钮触发狗牌响铃
- 支持任意状态下触发

### 数据持久化
- 主人信息（姓名/电话）保存到 NVS，重启后不丢失

## 项目结构

```text
main/
├── ble_dogtag.c/h       # BLE GATT 服务、配对、信号强度
├── dogtag_state.c/h     # 状态机、NVS 存储、定时器
├── dogtag_ui.c/h        # UI 界面（配对/静默/丢失）
├── dogtag_audio.c/h     # 音频播放（语音/蜂鸣）
├── demo_dogtag.c        # 主入口、状态转换
├── wifi_portal.c/h      # WiFi 配网备用
├── font_cn_16.c         # 中文字体 16px
├── font_cn_20.c         # 中文字体 20px
└── ui_pixel.h           # 颜色定义

tools/
├── pair_dogtag.html     # Web Bluetooth 配对工具
└── cn_chars.txt         # 3868 常用汉字字符集

partitions.csv           # 分区表 (factory 3MB)
sdkconfig.defaults       # ESP32-C3 默认配置
```

## BLE GATT 服务

| 特征 | UUID | 权限 | 说明 |
|------|------|------|------|
| 服务 | `f0debc9a-7856-3412-1234-567812345678` | - | 主服务 |
| 姓名 | `f1debc9a-...` | R/W | 主人姓名 |
| 电话 | `f2debc9a-...` | R/W | 联系电话 |
| 电量 | `f3debc9a-...` | R | 电池电量 |
| 指令 | `f4debc9a-...` | W | 配对完成(0x01)/查找设备(0x02) |

## BLE 设备名称

`DogTag%04X` (随机 4 位十六进制后缀)

## 构建

```bash
# 使用 ESP-IDF v5.5.3
idf.py set-target esp32c3
idf.py build
```

## 刷写

使用合并固件 `FoloToy-AI-Passport-full.bin`，刷到 `0x0`：

```bash
python -m esptool --chip esp32c3 --port COM5 --baud 460800 write-flash 0x0 FoloToy-AI-Passport-full.bin
```

或使用 ESP Web Flasher 图形工具。

## Web Bluetooth 配对工具

打开 `tools/pair_dogtag.html`，输入主人姓名和电话，点击"开始配对"。

配对成功后：
- 显示"查找设备"按钮，点击触发狗牌响铃
- 显示"断开连接"按钮
- 保持 BLE 连接可观察 RSSI 信号变化

## GitHub

仓库：https://github.com/killhello/ai-pass-port-dogtag
