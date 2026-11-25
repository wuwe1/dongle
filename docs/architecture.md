# 设计

## 原则：脑子不在硬件上

dongle 是"手"，Claude 是"脑"。硬件只做：电信号 ↔ 字节，再经 USB/WiFi 交给 Claude。
所以硬件可以便宜、简单，能力随 Claude 变强。通用性来自**分层**：底层总线只有十来种（dongle 全做一遍），
上层的芯片协议和含义有无数种（交给 Claude 现场查手册、做实验）。

## 组成

```
 设备 ──电线──▶ dongle 固件 ──USB 串口（文本命令）──▶ host/mcp_server.py ──MCP（stdio）──▶ Claude Code
                    │                                        ▲
                    └── 以后：WiFi ─────────────────────────┘（再往后接手机、voice-agent）
```

- **固件**：解析一行一行的文本命令，执行，回结果；异步事件（收到的 CAN 帧、红外码、UART 数据）主动推。
- **mcp_server.py**：把命令包装成 MCP 工具，工具说明里写清电压、用法、怎么解读结果，Claude 直接用。
- **sim.py**：一个模拟固件协议的假设备（带几个模拟芯片），让 MCP server 和 Claude 的整套流程**没有板子也能测**。

## 路线

1. **v1**：命令协议 + GPIO / ADC / I2C / UART + MCP server + 模拟器（详见 GitHub issue）
2. 红外收发（RMT）、WS2812（RMT）、PWM（LEDC）、SPI、把 CAN 嗅探并入命令协议
3. **逻辑捕获**：用 S3 的 RMT 或 LCD_CAM 外设采样几根线的波形，Claude 从波形推断未知协议
4. USB 设备模式（TinyUSB）：dongle 插电脑时扮演键盘/串口——只用于自己的设备
5. WiFi 传输 + 远程 MCP：接手机上的 Claude 和 z423 上的 voice-agent（手机 Claude App 不能直连 BLE）

## 已有的部分

- CAN 只读嗅探（`firmware/main/main.c`、`nus.c`）：TWAI 硬件 listen-only，按帧输出 candump 格式，BLE 推送过滤后的帧。
  v1 里它要改造成命令协议下的 `can listen` 前端。
- `frame.c` + `firmware/test/`：把不依赖 ESP-IDF 的逻辑拆出来在电脑上测——v1 的命令解析也照这个模式。
- `tools/canlog.py`：candump 日志的 summary / diff。
