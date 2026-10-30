# 基于 STM32 的智能温控终端（PID 闭环 + 故障联锁）

> STM32F103C8T6（64KB Flash / 20KB RAM）· HAL（FW_F1 V1.8.7）· Keil MDK5 · Python 上位机
> 温度闭环（PID + 1kHz PWM 加热）· 风扇辅助散热 · 独立看门狗（IWDG）· 按键菜单 · Flash 参数 · 串口协议与实时曲线

---

## 1. 当前状态（先看这里）

| 项目 | 状态 |
|------|------|
| 全工程 Keil 编译 | ✅ **实际编译通过：0 Error / 0 Warning**（Code 17950B / RO 2042B / RW 64B / ZI 3176B，含 IWDG 与热失控判据） |
| 宿主机单元测试（PID 20 + 参数编解码 30 + 协议 46） | ✅ **实际运行，96/96 通过**（含 2000 次协议模糊测试与连续 0xAA 重同步测试） |
| 无硬件数据链路演练（合成帧 → 解析 → 渲染） | ✅ 实际运行，30 帧 / 3 个坏帧被正确丢弃 |
| Python GUI 窗口运行 | ⚠️ **未运行**：本机 Python 缺 tkinter；已通过语法校验，协议与越界拦截逻辑已被测试覆盖 |
| 上板功能验证（12 项 + 安全专项） | ⏳ **全部待测试**，清单见 `docs/TEST_REPORT.md` §4 / §7 / §8 |
| **安全审查（第二轮 + 第三轮 IWDG 修复）** | ✅ 已修复并重编译 0E0W；复测结论见 `docs/SECURITY_REVIEW_2.md`（上板项待测） |

> 诚实声明：所有"通过"都来自实际执行的编译或测试；上板相关一律标"待测试"，不以模拟结果冒充实测。

---

## 2. 快速开始

```bash
# 1) 编译固件（Keil5 图形界面）
打开 MDK-ARM/TempCtrl.uvprojx → F7 编译 → F8 用 ST-Link 下载

# 2) 命令行编译（可选，需已安装 Keil）
cd MDK-ARM && "<Keil安装路径>/UV4/UV4.exe" -r TempCtrl.uvprojx -t TempCtrl -o rebuild.log

# 3) 宿主机测试（Windows + MSVC，路径见 tests/run_tests.bat 顶部；亦支持 GCC/Clang）
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
├── Core/        外设初始化（adc/tim/usart/gpio/iwdg/msp/it）+ main.c（三档时间片+喂狗）
├── Drivers/     ST HAL 库（未改动）
├── Hardware/    oled（软件 I2C）/ sensor（NTC+模拟双底层）/ heater / fan / key
├── App/         pid（纯C）· control（状态机+联锁）· menu · comm（帧协议）
│                · flash_param（末页 Flash）· param_codec（序列化+CRC32+范围校验）
├── MDK-ARM/     Keil 工程、启动文件
├── tests/       宿主机 C 单元测试（MSVC/GCC）+ run_tests.bat + 实测日志
├── host/        Python：protocol.py / app_gui.py / monitor_cli.py / test_protocol.py
└── docs/        ARCHITECTURE · WIRING · PROTOCOL · TEST_REPORT · RESUME_AND_INTERVIEW · SECURITY_REVIEW_2
```

---

## 5. 文档索引

| 文档 | 内容 |
|------|------|
| `docs/RELEASE_CHECKLIST.md` | **7 项发布清单状态**：基线统一 / NTC 灵敏度 / 术语 / 断线延迟 / IWDG / 编译测试 / GUI 闭环（含证据与阻塞项） |
| `docs/PREBOARD_TEST_PLAN.md` | **上板前必测 7 项**：控制周期 / OLED 耗时 / UART 丢包 / NTC 噪声 / PID 整定 / GUI 启动 / 故障恢复路径（含判据与记录模板） |
| `docs/ARCHITECTURE.md` | 分层架构图、时间片表、控制数据流、状态机、设计决策、资源占用 |
| `docs/WIRING.md` | 引脚表、**待回填的元件参数表**、NTC 分压、MOSFET 选型原则、共地要求、上电检查清单 |
| `docs/PROTOCOL.md` | 帧格式、命令表、状态载荷、错误码（含 5/6）、实测示例帧、上位机用法 |
| `docs/TEST_REPORT.md` | §0 当前测试基线（153 项）+ 编译/单测实测 + 上板待测清单 + 记录模板 |
| `docs/TUNING.md` | PID 整定流程：为什么必须整定（仿真敏感性数据）、开环辨识 → 初值 → 闭环微调、当前固件限制、真机数据回填表 |
| `docs/RESUME_AND_INTERVIEW.md` | 简历条目（含**验证边界**说明）、STAR 口述、20 个高频面试问答 |
| `docs/DEVELOPMENT_PLAN.md` | 八个阶段的开发记录、验收标准与遗留问题 |
| `docs/SECURITY_REVIEW_2.md` | 第二轮 + 第三轮（IWDG）安全审查与修复记录 |

---

## 6. 安全设计要点

1. **上电默认关闭**：栅极 10k 下拉（复位期间浮空也关断）+ 固件"先写低再配输出" + TIM1 CCR=0 + **软件输出许可**（`Heater_EnableOutput`）默认关闭。
2. **模拟数据不能驱动真实负载**：`SENSOR_SIM_ENABLE` 是编译期开关（默认 0）；即使开启，控制层也会关闭加热器的**软件输出许可**（`Heater_EnableOutput(0)`），PA8 恒低。
3. **软件联锁**：传感器无效 / 温度 >75℃ / PID 输入异常 / 热失控（占空比 ≥80% 且离目标 >3℃，60s 内温升 <2℃，用于 NTC 脱落等 75℃ 上限触发不了的情况）→ 立即关加热、开风扇、锁定 FAULT，仅人工确认可解除；确认时还会复核传感器与当前温度，仍无效或仍超温则保持锁定。
4. **入口无旁路**：按键与串口 START 都经过 `Control_Start()` 的新鲜读数与超温校验。
5. **独立看门狗（IWDG）+ 异常处理关断路径**：`NMI/HardFault/MemManage/BusFault/UsageFault` 与 `Error_Handler` 共用 `Heater_PanicOff()`（MOE=0 → 停计数 → PA8 切回 GPIO 推挽 → 拉低 → 开风扇）；主循环由 LSI 独立时钟驱动的 IWDG（标称 1.0s，实际 0.67~1.33s）守护，CPU 死机后触发硬件复位，PA8 回到高阻 + 10k 下拉，重启后读 `RCC_CSR` 在 OLED 显示 `WDG` 告警。
   **说明**：该路径**设计为**快速关断，**实际响应时间与切换瞬间是否存在毛刺尚未示波器验证**；且软件关闭 PWM **不等于**独立硬件断电——真实加热器仍必须串联独立过温保护。
6. **独立硬件过温保护**：常闭温度开关串联在加热回路（器件级物理断电）—— 需在真实硬件阶段加装。
7. **参数防脏**：Flash 魔数 + 版本 + CRC32 + 数值范围/NaN 校验，任一不符即回默认值。

---

## 7. 已知限制

- NTC 参数（10k / B3950 / 分压 10k）为假设值，**未实测校准**，绝对精度未验证。
- PID 参数（Kp 8 / Ki 0.10 / Kd 1.5）是仿真初值：在纯滞后较大的仿真对象上会明显超调并振荡（见 `docs/TUNING.md` §1），真实加热器必须重新整定；固件目前没有开环占空比指令，辨识流程暂不能完整走通（`docs/TUNING.md` §3）。
- 最近一轮改动（微分作用于测量值 + 低通、0.1% 占空比分辨率、热失控判据、仅 STANDBY 允许写 Flash）已通过宿主机测试，**尚未用 Keil 重新编译**，编译尺寸以下一次全量编译为准。
- 模拟数据恒带 `SIM` 标识（OLED 与串口状态帧均透传），不会与真实测量混淆。
