# 设计

## 想法

"脑子"不放在硬件上。dongle 只负责：物理接口（CAN、USB、串口……）↔ 无线（BLE / WiFi）。
理解协议、做决定的是 Claude。这样硬件可以很便宜、很笨，能力随着 Claude 和工具变强而变强。

## 为什么 v0 这样做

- **只读**：第一步是"读懂"，不需要发送；硬件 listen-only 让它插在任何总线上都不可能出事故。
- **candump 格式**：can-utils、SavvyCAN、python-can 都认，Claude 也熟；日志就是文本，可以直接贴进对话。
- **USB 全量 + BLE 过滤**：BLE 的吞吐（每秒几百行）跟不上一条繁忙的总线（每秒几千帧），
  所以完整数据走 USB 给电脑；手机只看过滤后的少数几个 ID，外加发 `mark`。过不去的计入 `dropped`，`stats` 能看到。
- **ESP-IDF 而不是 Arduino**：TWAI 和 NimBLE 都是 IDF 原生的；`build.sh` 用官方 docker 镜像，版本固定。

## 往后：把它接到手机上的 Claude

难点：**手机上的 Claude App 不能直接连蓝牙设备**。几条路：

| | 做法 | 代价 |
|---|---|---|
| **A（倾向）** | dongle 用 **WiFi** 连手机热点 → 主动连到一个中继（edge-gz 有公网）→ 中继提供 **MCP 工具**：`sniff(秒数, 过滤)`、`mark`、`summary`、`diff` | 不用写 App；canlog.py 直接在服务端复用 |
| B | 写一个 iOS App 做 BLE ↔ 中继的桥 | 要写、要签名、要常驻 |
| C | 只用电脑（Claude Code + USB） | 现在就能用，但不随身 |

走 A 之后，同一套 MCP 工具可以同时给：手机 Claude App（自定义 connector）、z423 上的 **voice-agent**
（打电话问"车现在什么状态"）、电脑上的 Claude Code。

## 再往后

- **知识积累**：把识别出来的 ID/字节写成 DBC 文件，`canlog.py` 用它把帧翻译成"门锁=锁定"。
- **发送**（例如锁车）：只在识别清楚之后、停车状态、物理开关打开时才允许；每一次发送都要求确认。
- **别的设备**：同一个 dongle 换前端。ESP32-S3 能当 USB 设备（键盘/U 盘），还能接串口调试口、红外、HDMI-CEC。
  "插进任何东西"本质上是一组可替换的前端，加上同一套 Claude 工具。
