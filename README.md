# dongle — 让 Claude 通过一个小硬件接进别的设备

一个 U 盘大小的东西：插进某个设备（第一个目标是车的 OBD-II 口），通过蓝牙/WiFi 连到手机或电脑，
由 Claude 来读懂、以后再驱动那台设备。硬件只是一根"管子"，脑子在 Claude 那边。

**v0（现在）：只读的 CAN 嗅探器**。ESP32 + CAN 收发器，用 TWAI 控制器的硬件 listen-only 模式——
它**物理上不发送任何东西，连 ACK 位都不回**，所以插在车上不可能干扰总线；固件里也没有任何发帧的代码路径。

```
OBD-II ──CAN──▶ 收发器 ──▶ ESP32 ──USB──▶ 电脑：完整 candump 日志 ──▶ tools/canlog.py / Claude Code
                                  └─BLE──▶ 手机：按 ID 过滤的帧、stats、打 mark
```

## 目录

| | |
|---|---|
| `firmware/` | ESP-IDF 固件（ESP32-S3 / C3）。`./build.sh` 在 ESP-IDF 的 docker 镜像里编译，本地只要 docker |
| `firmware/test/run.sh` | 主机上的测试：固件的行格式（C，gcc 编译）+ canlog 能读懂固件写的每一行 |
| `tools/canlog.py` | 读 candump 日志：`summary` 每个 ID 的周期和哪些字节在动；`diff` 找"按下某个键时才出现的字节值" |
| `docs/car.md` | 接线、供电、奔驰的网关问题、安全与法律、第一次上车的实验步骤 |
| `docs/architecture.md` | 为什么这样设计，v1/v2 怎么把它接到手机上的 Claude（和 voice-agent） |

## 快速开始

```bash
cd firmware && ./build.sh esp32s3           # 或 esp32c3；改引脚/波特率：见 main/Kconfig.projbuild
# 烧录（需要本机有 esptool：pip install esptool），端口按实际：
(cd build-esp32s3 && esptool.py --chip esp32s3 -p /dev/cu.usbmodem* write_flash @flash_args)
cat /dev/cu.usbmodem* | tee drive.log        # 收日志；grep '^(' 就是纯 candump 格式
cd ..; python3 tools/canlog.py summary drive.log
python3 tools/canlog.py diff drive.log --base idle --action lock
```

手机端 v0 用任意 BLE 串口工具（nRF Connect、LightBlue）连 `dongle`，订阅 TX，往 RX 写命令：
`stats` · `mark lock` · `filter 2B0,7E8` · `filter off`。

## 状态

在云端验证过的：固件对 ESP32-S3 和 C3 都能零警告编译；行格式和过滤解析有主机测试；canlog 用模拟的
"按锁车键"数据能找出目标字节，不会被滚动计数器干扰。**还没上过真硬件、真车**——见 `docs/car.md` 的第一次实验。
