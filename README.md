# dongle — 让 Claude 通过一个小硬件去"摸"各种设备

一个 U 盘大小的东西：插到某个设备上（传感器、红外遥控的家电、路由器的调试串口……），
通过 USB/WiFi 接到 Claude。**dongle 只提供底层的电信号操作，协议和含义由 Claude 自己去摸索。**

```
  ④ 含义：这个字节是温度？是"开机"？      ← Claude：查手册、做实验
  ③ 具体芯片/设备的协议（BME280 寄存器表）  ← Claude
  ② 总线：GPIO / ADC / PWM / I2C / SPI / UART / 红外 / CAN / USB   ← dongle 提供，就这十来种
  ① 电气：电压、接哪个针脚                 ← 人来接（3.3V！接错会烧）
```

不给每个设备写驱动，而是给 Claude 一套原始操作（MCP 工具）。比如插上一个不认识的传感器：
Claude 先 `i2c scan`，看到 0x76 → 猜是 BME280 → 读 ID 寄存器确认 → 按手册算出温度。没有一行 BME280 驱动。

## 为什么是 ESP32-S3

单片机（不是 RK3588 那种跑 Linux 的处理器）：脑子在 Claude 那边，dongle 只要接口多、带无线、小、便宜、低功耗、
时序精确到微秒。ESP32-S3 自带 WiFi + BLE、几十个 GPIO、硬件 UART/I2C/SPI/CAN/ADC/PWM/RMT、还能当 USB 设备。
同类可以对比玩：树莓派 Pico 2 W（RP2350，PIO 适合怪协议和逻辑分析）。

## 现状

| | 状态 |
|---|---|
| **CAN 只读嗅探**（第一个前端，原本为车做的） | ✅ 固件对 S3/C3 零警告编译；主机测试通过；**没上过真硬件**。车的事暂停（贵、有风险），文档留在 `docs/car.md` |
| **通用命令协议 + MCP server**（GPIO/I2C/UART 起步） | ⏳ 设计和任务见 GitHub issue「v1：通用总线工具」 |

## 目录

| | |
|---|---|
| `firmware/` | ESP-IDF 固件。`./build.sh esp32s3` 在 ESP-IDF 官方 docker 镜像里编译，本地只要 docker |
| `firmware/main/frame.c` | 不依赖 ESP-IDF 的纯逻辑，能在电脑上测：`firmware/test/run.sh` |
| `tools/canlog.py` | candump 日志分析：`summary`、`diff`（找"某个动作时才出现的字节值"） |
| `docs/architecture.md` | 设计、分层、路线图 |
| `docs/car.md` | CAN/OBD 接线和奔驰网关问题（暂停中） |
| `docs/uno.md` | ESP32 到货前用 UNO 做的入门实验、出门清单、5V 注意事项 |
| `docs/bench.md` | 手上的工具和 Arduino 套件（按总线分）、还要买什么、5V/3.3V 注意事项 |

## 编译和测试（不需要板子）

```bash
cd firmware && ./build.sh esp32s3 esp32c3   # 交叉编译：x86 上生成 Xtensa / RISC-V 的固件
firmware/test/run.sh                        # 纯逻辑的主机测试 + canlog 读固件输出
python3 -m unittest discover -s tools
```

板子到了再烧录：`(cd firmware/build-esp32s3 && esptool.py --chip esp32s3 -p /dev/cu.usbmodem* write_flash @flash_args)`

## 入门清单（都是 3.3V，安全）

ESP32-S3 开发板（可加 Pico 2 W）、面包板、杜邦线；I2C 传感器（BME280/SHT30）、OLED（SSD1306）；
红外接收头 + 红外发射管（学空调/电视遥控）；WS2812 灯带；一台旧路由器（找调试串口拿 root shell）；
几十块的逻辑分析仪（配 sigrok/PulseView 看波形）。⚠️ 别直接接 5V 信号；RS-232 是 ±12V；永远不碰 220V。
