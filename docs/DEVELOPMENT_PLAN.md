# 智能温控与故障监测终端 — 开发计划与测试记录

> 项目：《基于 STM32 的智能温控与故障监测终端》
> 工程位置：`C:\Users\26313\Desktop\stm32-hal\TempCtrl\`
> 平台：STM32F103C8T6 · HAL（FW_F1 V1.8.7）+ Keil5（ARMCC V5.06）+ ST-Link
> 规则：分阶段开发；每阶段验收+审核后才 Git 提交并进入下一阶段；严格区分【模拟】/【真实硬件】测试，未执行的一律标"待测试"。

---

## 1. 模块职责与依赖关系（规划）

```
                    ┌─────────────────────────────┐
                    │  main.c（初始化+非阻塞调度）  │
                    │  HAL_GetTick 时间片，无长Delay│
                    └──────────────┬──────────────┘
           ┌───────────────┬─────┼──────────┬──────────────┐
           ▼               ▼     ▼          ▼              ▼
      App/control.c   App/menu.c  App/comm.c    App/flash_param.c
      控制状态机+PID调度  按键菜单   USART1 协议   Flash 参数读写
           │               │          │              │
           ▼               ▼          ▼              ▼
      ┌────────────────────────────────────────────────────┐
      │ Hardware 层：oled.c / sensor.c / heater.c /        │
      │ fan.c / key.c  （只依赖 HAL，封装 GPIO/I2C/TIM）    │
      ├────────────────────────────────────────────────────┤
      │ App/pid.c：纯算法，【不依赖 HAL】，可离线单元测试    │
      └────────────────────────────────────────────────────┘
           │ 依赖方向只能向下；Hardware 不依赖 App
           ▼
      Core/ + Drivers/（外设初始化与 HAL 库）
```

| 模块 | 职责 | 依赖 | 状态 |
|------|------|------|------|
| `Core/` | 时钟、GPIO 初始化、中断向量 | HAL | ✅ 阶段 1 已建 |
| `Hardware/oled.c/h` | OLED(SSD1306, 0x78) 软件 I2C 显示，PB6/PB7 | HAL GPIO | ✅ 阶段 1 已移植 |
| `Hardware/sensor.c/h` | 温湿度读取（**型号未定，留待确认**） | HAL | ⬜ 阶段 2 |
| `Hardware/heater.c/h` | 加热 PWM 0–100%，上电默认关 | TIM1_CH1 | ⬜ 阶段 3 |
| `Hardware/fan.c/h` | 风扇开关（v1），预留 PWM 调速 | PB0 | ⬜ 阶段 3 |
| `Hardware/key.c/h` | 按键扫描、消抖、长短按 | GPIO | ⬜ 阶段 6 |
| `App/pid.c/h` | PID：限幅、积分抗饱和、异常输入处理 | 无（纯 C） | ⬜ 阶段 4 |
| `App/control.c/h` | 状态机：自检/待机/运行/故障 | pid + Hardware | ⬜ 阶段 4-5 |
| `App/menu.c/h` | OLED 菜单与按键交互 | oled + key | ⬜ 阶段 6 |
| `App/flash_param.c/h` | 参数掉电保存（末页 Flash） | HAL FLASH | ⬜ 阶段 7 |
| `App/comm.c/h` | USART1 帧协议 + Python 上位机 | HAL UART | ⬜ 阶段 8 |

---

## 2. 阶段划分与验收标准

| 阶段 | 内容 | 验收标准 | 测试性质 | 状态 |
|------|------|----------|----------|------|
| 1 | 工程骨架 + OLED 显示固定文字 | 编译 0E0W；OLED 显示固定字符+秒数递增 | 编译【已执行】/ 显示【真实硬件】 | ✅ 已审核通过，Git 基线 `97106ef` |
| 2 | 传感器模块（NTC 真实底层 + 软件模拟底层，统一 Sensor_Read 接口） | 编译 0E0W；SIM/REAL 标识正确；读数合理 | 编译【已执行】/ 硬件【真实硬件】 | 编译✅ 上板⏳待测试 |
| 3 | 加热器 PWM（TIM1_CH1 1kHz）+ 风扇开关 | 编译 0E0W；PA8 波形正确；LED 假负载验证 | 编译【已执行】/ 波形【真实硬件】 | 编译✅ 上板⏳待测试 |
| 4 | PID 单元测试 | 限幅/抗饱和/异常输入用例全过 | 【模拟】 | 待测试 |
| 5 | 真实闭环 | 超调/稳态误差达标（指标待定）；断线/超温保护动作 | 【真实硬件】 | 待测试 |
| 6 | 按键与菜单 | 启停/设温/故障确认完整 | 【真实硬件】 | 待测试 |
| 7 | Flash 参数保存 | 掉电保存、上电恢复 | 【真实硬件】 | 待测试 |
| 8 | USART1 + Python 上位机 | 实时曲线、远程设温、数据记录 | 【真实硬件】 | 待测试 |

---

## 3. 阶段 1 记录（2026-10-17）

### 3.1 检查结果
- 芯片：STM32F103C8T6（LQFP48，64KB Flash / 20KB RAM）。
- 现有工程：`stm32-hal\DataTerm`（项目一 V1）与 `Data acquisition`（项目一 V5）**未做任何改动**；TempCtrl 为新建目录。
- OLED 驱动来源：`stm32project\4-1 OLED显示屏\Hardware\OLED.c`，SSD1306 兼容、地址 0x78、原为标准库软件 I2C（PB8/PB9）。
- Git：`stm32-hal` 目录尚不是 Git 仓库（建议阶段 1 审核通过后 `git init` 并做首次提交）。

### 3.2 新建文件（全部位于 `stm32-hal\TempCtrl\`）
| 文件 | 职责 |
|------|------|
| `Core/Inc/main.h` | 全工程引脚映射（OLED/加热器/风扇/按键），统一出处 |
| `Core/Inc/gpio.h`、`Core/Src/gpio.c` | GPIO 初始化；PA8/PB0 **先写低再配输出**（上电默认关） |
| `Core/Inc/stm32f1xx_it.h`、`Core/Src/stm32f1xx_it.c` | 内核异常 + SysTick（HAL 1ms 时基），暂无外设中断 |
| `Core/Src/stm32f1xx_hal_msp.c` | MSP 全局初始化（关 JTAG 留 SWD） |
| `Core/Src/main.c` | 入口：HAL_Init→72MHz 时钟→GPIO→OLED 固定文字+非阻塞秒计数 |
| `Core/Src/system_stm32f1xx.c` | 复用 DataTerm（CMSIS 标准文件） |
| `Hardware/oled.c/h`、`OLED_Font.h` | 课程例程移植：HAL 化、引脚改 PB6/PB7、GPIO 初始化移入 gpio.c |
| `App/README.md` | App 层规划占位（pid/control/menu/flash/comm） |
| `MDK-ARM/TempCtrl.uvprojx` | Keil 工程（手写的 CubeMX 风格结构，模板来自 DataTerm） |
| `MDK-ARM/startup_stm32f103xb.s` | 复用 DataTerm 启动文件（中容量 F103xB 向量表） |
| `Drivers/` | 复用 DataTerm 的 HAL+CMSIS（FW_F1 V1.8.7），未改动 |
| `docs/DEVELOPMENT_PLAN.md` | 本文件 |

### 3.3 移植改动点（相对课程例程 OLED.c）
1. `GPIO_WriteBit` → `HAL_GPIO_WritePin`，引脚 PB8/PB9 → PB6/PB7
2. 引脚初始化从 `OLED_I2C_Init()` 移到 `MX_GPIO_Init()`（CubeMX 风格集中管理）
3. 上电延时 for 空循环 → `HAL_Delay(100)`
4. 显示时序、命令序列、字库索引：**未改动**

### 3.4 编译结果（实际执行）
```
Rebuild target 'TempCtrl'（UV4 命令行，ARMCC V5.06 build 528）
Program Size: Code=3628 RO-data=1952 RW-data=16 ZI-data=2656
"TempCtrl\TempCtrl.axf" - 0 Error(s), 0 Warning(s).
```
日志：`MDK-ARM/rebuild.log`。Flash 占用约 5.5KB / 64KB，RAM 约 2.7KB / 20KB。

### 3.5 如何在 Keil5 中编译与 ST-Link 验证
- 编译：Keil5 打开 `MDK-ARM\TempCtrl.uvprojx` → F7（Build）或 Rebuild → 期望 0 Error 0 Warning。
- 烧录：ST-Link 接 SWDIO(PA13)/SWCLK(PA14)/GND；Keil → F8（Download）。首次打开工程请在 Options → Debug → Settings 确认识别到 ST-Link 与芯片。
- 验收（真实硬件）：OLED 第 1 行 `TEMP-CTRL  v0.1`、第 2 行 `STAGE1 SKELETON`、第 3 行 `UPTIME:` 后秒数每秒 +1（证明 SysTick 与非阻塞循环正常）。
- 接线注意：OLED 接到 **PB6(SCL)/PB9→改为 PB7(SDA)**，即 OLED_SDA=PB7，不再是课程例程的 PB8/PB9。
- 硬件显示验证状态：**待测试**（需要你上板执行）。

### 3.6 阶段 1 遗留问题
1. 若烧录后停在 Error_Handler（OLED 不亮且秒数不动）：多为外部晶振不起振，需把 `SystemClock_Config()` 改为 HSI 方案。
2. 按键接法（按下接地？）未确认——gpio.c 按"内部上拉+按下接地"配置。
3. 温湿度传感器型号未确认——阶段 2 的阻塞项。
4. Git 未初始化：待阶段 1 审核通过后执行 `git init` + 首次提交。
5. OLED 软件 I2C 无应答检测：SSD1306 不应答也照常发，若屏幕不亮先用万用表量 PB6/PB7 电平翻转。

### 3.7 下一阶段（阶段 2）预告
温湿度传感器接入。**开始前必须先确认传感器型号**（DHT11/DHT22 单总线 还是 SHT30/AHT20 等 I2C），两者驱动与接线完全不同。

---

## 4. 阶段 2+3 记录（2026-10-17，待审核）

> 用户决策调整：传感器不等待型号确认，改为**双底层**方案——NTC(ADC1_IN0/PA0) 作为默认真实硬件 + 纯软件热力学模拟；统一 `Sensor_Read()` 接口，`is_simulated` 标志贯穿全链路。

### 4.1 新增/修改文件
| 文件 | 改动 | 职责 |
|------|------|------|
| `Hardware/sensor.c/h` | 新增 | 统一温度接口：NTC 真实底层（Beta 公式）+ 一阶惯性模拟底层；上电自检自动选模式 |
| `Hardware/heater.c/h` | 新增 | TIM1_CH1(PA8) 1kHz PWM，占空比 0–100 限幅，上电默认 0 |
| `Hardware/fan.c/h` | 新增 | PB0 开关控制，上电默认关 |
| `Core/Src/adc.c`、`Core/Inc/adc.h` | 新增 | ADC1 通道 0 单次转换软件触发（NTC 分压采样） |
| `Core/Src/tim.c`、`Core/Inc/tim.h` | 新增 | TIM1 PWM 1kHz（PSC=71/ARR=999，CCR1=0 启动） |
| `Core/Src/stm32f1xx_hal_msp.c` | 修改 | 增加 `HAL_ADC_MspInit`（PA0 模拟输入）与 `HAL_TIM_PWM_MspInit`（PA8 复用推挽） |
| `Core/Src/main.c` | 重写 | 500ms 传感器+显示时间片；SIM/REAL 标识；联调演示序列（DEMO_ENABLE=1） |
| `MDK-ARM/TempCtrl.uvprojx` | 修改 | 新增 adc.c/tim.c/sensor.c/heater.c/fan.c/hal_adc.c/hal_adc_ex.c 进编译 |

### 4.2 关键设计说明
1. **模式自检**：`Sensor_Init()` 读 ADC，分压点贴轨（≈0 或 ≈4095，即未接 NTC）→ 自动进模拟模式；否则真实模式。运行中 NTC 失联 → `is_valid=0`（阶段 5 的状态机将据此关加热）。
2. **模拟模型**：一阶惯性，`steady = 25℃ + 40℃×duty`（风扇开启增益 ×0.6），时间常数自然散热 60s / 风扇 25s；dt 用 HAL_GetTick 实测间隔。模拟输入直接取 `Heater_GetDuty()`/`Fan_IsOn()` 的真实输出状态，可验证闭环逻辑。
3. **诚信约束**：模拟模式 OLED 第一行恒显 `SIM`，真实模式显 `REAL`；无效读数显 `ERR`。
4. **NTC 接线假设**：3.3V—10kΩ—PA0—NTC(10k B3950)—GND。**阻值/B 值未经实测校准**，读数可能偏差 ±2℃ 量级。
5. **演示序列**（阶段 6 删除）：0–10s 全关 → 10–40s HEAT 30% → 40–70s HEAT 50%+FAN → 70s 起全关。把 `main.c` 中 `DEMO_ENABLE` 改为 0 可禁用。**警告：PA8 接驱动电路时演示会真实输出 PWM，真实加热器接入前只允许 LED 假负载。**

### 4.3 编译结果（实际执行，UV4 命令行全量重编译）
```
Rebuild target 'TempCtrl'
Program Size: Code=9666 RO-data=2042 RW-data=32 ZI-data=2776
"TempCtrl\TempCtrl.axf" - 0 Error(s), 0 Warning(s).
```

### 4.4 上板验收清单（待测试，需你执行）
| # | 项目 | 预期 | 状态 |
|---|------|------|------|
| 1 | 不接 NTC 上电 | L1 显示 `SIM  T:25.0C` 附近，温度随演示序列上升/回落 | ⏳ 待测试 |
| 2 | 万用表/示波器测 PA8 | 10–40s 期间 1kHz、30% 占空比；40–70s 50%；其余时间低电平 | ⏳ 待测试 |
| 3 | PB0 | 40–70s 期间高电平，其余低电平 | ⏳ 待测试 |
| 4 | 接 NTC 分压上电 | L1 显示 `REAL T:xx.xC`，手捏 NTC 温度上升 | ⏳ 待测试（依赖 NTC 到货/焊接） |
| 5 | 拔掉 NTC 运行 | 显示 `REAL T: ERR`（is_valid=0 路径） | ⏳ 待测试 |

### 4.5 遗留问题
1. NTC 实际参数（阻值/B 值/分压电阻）未实测校准。
2. 模拟模型参数（增益/时间常数）是估计值，真实闭环前需用实测升温曲线标定。
3. 阶段 2+3 未提交 Git：等你审核通过后提交。
4. 下一阶段（阶段 4）：PID 模块（纯 C 无 HAL 依赖）+ 离线单元测试。
