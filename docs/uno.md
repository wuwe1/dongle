# 用 Arduino UNO 打基础

ESP32 到货之前，先用套件里的 UNO 把各种总线亲手摸一遍。每个实验都对应 dongle 以后的一种能力。

## 出门前

- [ ] Mac 上装 **Arduino IDE**；第一次打开时装 "Arduino AVR Boards"；在库管理里装 **IRremote**
- [ ] 可选：`brew install arduino-cli`，让 Claude Code 能自己编译、烧录、读串口
- [ ] 带上：**整个套件盒**、UNO 的方口 USB-B 线**加 Type-C 转接头**（Mac 只有 C 口）、面包板、杜邦线
- [ ] 拔掉遥控器电池仓里的塑料绝缘片
- 不用带：Fluke（要插 220V），焊台、热风枪

## 实验

| | 实验 | 学到什么 |
|---|---|---|
| 1 | **Blink**：板载 LED 闪烁，改节奏 | GPIO 输出 |
| 2 | **按钮控制 LED**：先看引脚悬空时读数乱跳，再改用 `INPUT_PULLUP` | GPIO 输入、上拉电阻 |
| 3 | **电位器加串口监视器**：`analogRead` 得到 0 到 1023。回家后用 Fluke 量中间脚的电压对比，算换算关系 | ADC |
| 4 | **串口打印**，再故意把波特率选错，看乱码 | UART、波特率 |
| 5 | **用串口命令控制 LED**：输入 `on`/`off` 开关灯 | 最小版的 dongle 命令协议 |
| 6 | **红外解码**（IRremote）：看遥控器每个键的编码，也拿家里电视的遥控试试 | 红外、NEC 协议 |
| 7 | 舵机跟着电位器转、DHT11 测温湿度、超声波测距 | PWM、单总线时序 |
| 8 | OLED 到货后：DHT11 加 OLED，做一个温湿度显示屏 | I2C |

进阶：把实验 5 扩展成命令解释器（`led on`、`adc 0`、`i2c scan`），再让 Claude Code 用 `arduino-cli` 来操作它。
这就是跑在 UNO 上的 dongle#1 雏形。

## 注意

- UNO 是 **5V** 的。改接线前先拔掉 USB；不要让 5V 和 GND 短路
- 继电器只用来练习开关控制，**不接 220V 市电**；MQ 气体传感器通电后会发烫
- **AHT20 + BMP280 组合板先别接 UNO**：BMP280 是 3.3V 芯片，而 UNO 的 I2C 会把线上拉到 5V。如果板子上同时有稳压芯片
  （三五只脚的小黑芯片）和电平转换 MOSFET，才能接 UNO；拿不准就只接 ESP32。OLED 带稳压，接 UNO 没问题
- 逻辑分析仪到货后：GND 先接 UNO 的 GND，CH0 接 1 号脚（TX）看实验 4 的串口波形；再接红外接收头输出，看实验 6 的
  遥控波形。PulseView 驱动选 fx2lafw，采样率 1MHz，解码器选 UART（9600）和 IR NEC
