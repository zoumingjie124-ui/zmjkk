# 基于 STM32 的智能温控与故障监测终端

> STM32F103C8T6（64KB Flash / 20KB RAM）· HAL（FW_F1 V1.8.7）· Keil MDK5 · Python 上位机
> 温度闭环（PID + 1kHz PWM 加热）· 风扇辅助散热 · 按键菜单 · Flash 参数 · 串口协议与实时曲线

---

## 1. 当前状态（先看这里）

| 项目 | 状态 |
|------|------|
| 全工程 Keil 编译 | ✅ **实际编译通过：0 Error / 0 Warning**（Code 17114B / RAM 3160B） |
| 宿主机单元测试（PID 20 + 参数编解码 21 + 协议 46） | ✅ **实际运行，87/87 通过**（含 2000 次协议模糊测试） |
| 无硬件数据链路演练（合成帧 → 解析 → 渲染） | ✅ 实际运行，30 帧 / 3 个坏帧被正确丢弃 |
| Python GUI 窗口运行 | ⚠️ **未运行**：本机 Python 缺 tkinter；仅通过语法校验，协议逻辑已被测试覆盖 |
| **上板功能验证（12 项）** | ⏳ **全部待测试**，清单见 `docs/TEST_REPORT.md` §4 |

> 诚实声明：所有"通过"都来自实际执行的编译或测试；上板相关一律标"待测试"，不以模拟结果冒充实测。

---

## 2. 快速开始

```bash
# 1) 编译固件（Keil5 图形界面）
打开 MDK-ARM/TempCtrl.uvprojx → F7 编译 → F8 用 ST-Link 下载

# 2) 命令行编译（可选，需已安装 Keil）
cd MDK-ARM && "<Keil安装路径>/UV4/UV4.exe" -r TempCtrl.uvprojx -t TempCtrl -o rebuild.log

# 3) 宿主机测试（Windows + MSVC，路径见 tests/run_tests.bat 顶部）
cd tests && run_tests.bat

# 4) 协议测试（纯 Python）
cd host && python test_protocol.py

# 5) 上位机
cd host && python app_gui.py            # 需要 tkinter + pyserial
cd host && python monitor_cli.py --demo # 无硬件也能跑通解析链路
```

---

## 3. 引脚分配

| 功能 | 引脚 | 说明 |
|------|------|------|
| OLED SCL / SDA | PB6 / PB7 | 软件 I2C（SSD1306，地址 0x78） |
| 加热器 PWM | PA8 | TIM1_CH1，1kHz，占空比 0~100% |
| 风扇开关 | PB0 | 高电平开（预留 TIM3_CH3 调速） |
| NTC 温度 | PA0 | ADC1_IN0，10k 分压（参数见 WIRING.md） |
| 串口 | PA9 / PA10 | USART1 115200 8N1 |
| 按键 ×3 | PB12 / PB13 / PB14 | 内部上拉，按下接地 |
| 调试 | PA13 / PA14 | SWD，禁止挪用 |

> ⚠️ 加热器与风扇**必须经 MOSFET 低边驱动**，严禁直接接 GPIO；真实负载接入前先用 LED 假负载验证 PA8 波形。

---

## 4. 目录结构

```
TempCtrl/
├── Core/        外设初始化（adc/tim/usart/gpio/msp/it）+ main.c（三档时间片调度）
├── Drivers/     ST HAL 库（未改动）
├── Hardware/    oled（软件 I2C）/ sensor（NTC+模拟双底层）/ heater / fan / key
├── App/         pid（纯C）· control（状态机+联锁）· menu · comm（帧协议）
│                · flash_param（末页 Flash）· param_codec（序列化+CRC32）
├── MDK-ARM/     Keil 工程、启动文件、编译日志
├── tests/       宿主机 C 单元测试（MSVC）+ run_tests.bat + 实测日志
├── host/        Python：protocol.py / app_gui.py / monitor_cli.py / test_protocol.py
└── docs/        ARCHITECTURE · WIRING · PROTOCOL · TEST_REPORT · RESUME_AND_INTERVIEW
```

---

## 5. 文档索引

| 文档 | 内容 |
|------|------|
| `docs/ARCHITECTURE.md` | 分层架构图、时间片表、控制数据流、状态机、设计决策、资源占用 |
| `docs/WIRING.md` | 引脚表、NTC 分压接线、MOSFET 选型原则、共地要求、上电检查清单 |
| `docs/PROTOCOL.md` | 帧格式、命令表、状态载荷、错误码、实测示例帧、上位机用法 |
| `docs/TEST_REPORT.md` | 编译/单测/协议实测结果 + 12 项上板待测清单 + 记录模板 |
| `docs/RESUME_AND_INTERVIEW.md` | 简历条目（完整/精简）、STAR 口述、20 个高频面试问答 |
| `docs/DEVELOPMENT_PLAN.md` | 八个阶段的开发记录、验收标准与遗留问题 |

---

## 6. 安全设计要点

1. **上电默认关闭**：栅极 10k 下拉（复位期间浮空也关断）+ 固件"先写低再配输出" + TIM1 CCR=0。
2. **软件联锁**：传感器无效 / 温度 >75℃ / PID 输入异常 → 立即关加热、开风扇、锁定 FAULT，仅人工确认可解除。
3. **独立硬件过温保护**：常闭温度开关串联在加热回路（固件跑飞也能断电）—— 需在真实硬件阶段加装。
4. **参数防脏**：Flash 魔数 + 版本 + CRC32，校验失败回默认值，不拿坏参数跑 PID。

---

## 7. 已知限制

- NTC 参数（10k / B3950 / 分压 10k）为假设值，**未实测校准**，绝对精度未验证。
- PID 参数（Kp 8 / Ki 0.10 / Kd 1.5）仅与软件模拟对象匹配，真实加热器需重新整定。
- 模拟数据恒带 `SIM` 标识（OLED 与串口状态帧均透传），不会与真实测量混淆。
