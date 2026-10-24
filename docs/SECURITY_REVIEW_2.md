# 安全审查报告（第二轮）

> 仓库：zmjkk / TempCtrl　审查时间：2026-10-18　基线提交：`dc5302d`
> 性质：**静态代码审查**（未上板），所有结论附触发条件；修复后重新编译并跑宿主机测试。

---

## 0. 结论摘要

发现 **1 个致命安全缺陷**（P0，可导致真实加热器在无真实温度反馈下持续加热）及其 3 个关联缺陷，另有 5 项中低风险问题。致命缺陷的根因不是某一行代码写错，而是**架构上缺少"模拟数据不得驱动真实执行器"的硬隔离**：SIM 只是显示层标识，没有在驱动层形成闸门。

| 编号 | 文件 | 问题 | 严重度 |
|------|------|------|--------|
| P0-1 | `Hardware/sensor.c:118-137` | 上电自检失败**自动切换**模拟模式 | **致命** |
| P0-2 | `Hardware/heater.c:29-38` | PWM 无硬件输出总闸，模拟模式照样驱动 PA8 | **致命** |
| P0-3 | `App/Src/control.c` | RUNNING 不判 SIM；`Control_Start`/`AckFault` 不校验传感器 | **致命** |
| P0-4 | `App/Src/comm.c` | 串口 START 入口可绕过安全检查（依赖 P0-3 修复） | 高（关联） |
| P1-1 | `Hardware/sensor.c:84-89` | NTC 换算无合理性上下限 | 中 |
| P1-2 | `App/Src/flash_param.c:24-42` | Flash 参数只校验 CRC，不校验数值范围/NaN | 中 |
| P1-3 | `App/Src/comm.c:207` | Flash 保存失败误报为"参数越界" | 中 |
| P1-4 | `Core/Src/main.c:79-100` | 时间片用 `+=` 补执行，PID 可能被连续调用多次 | 中 |
| P1-5 | `Core/Src/usart.c:95` | 发送阻塞 100ms + Flash 关中断，可能丢接收字节（有 CRC 兜底） | 低 |

---

## P0-1 上电自检失败自动切模拟（致命）

**代码**：`sensor.c` `Sensor_Init()` 读一次 ADC，贴轨（<5 或 >4090）就 `s_simulated = 1`。

**触发条件**（真实场景很容易同时满足）：
1. 加热器/驱动已接好，但 NTC 还没焊上、或 NTC 线断、或分压电阻漏焊；
2. 上电 → ADC 贴轨 → 自动进模拟模式；
3. 用户按 KEY1 或串口 START 启动 → 状态机 RUNNING → PID 用**模拟温度**算出占空比 → `Heater_SetDuty()` 真实驱动 PA8。

**后果**：加热器按假温度持续加热，而假温度永远达不到 75℃ 联锁阈值 → **软件超温保护完全失效**，只有硬件温控开关（若已装）能救。这是本项目最危险的失效模式。

**修复**：删除"自动切换"。模拟模式改为**编译期显式开关** `SENSOR_SIM_ENABLE`（默认 0）；自检失败一律 `is_valid=0`，由控制层进入 FAULT 锁定，绝不"换个数据源接着跑"。

---

## P0-2 加热器缺少硬件输出总闸（致命）

**代码**：`Heater_SetDuty()` 无条件 `__HAL_TIM_SET_COMPARE(... duty*10)`。

**问题**：即使上层知道"现在是模拟模式"，驱动层也没有任何手段阻止 PA8 输出。安全策略只存在于调用者的自觉里 —— 这不符合"安全要能在驱动层兜底"的原则。

**修复**：新增 `Heater_EnableOutput()` / `Heater_OutputEnabled()`，默认**关闭**；关闭时任何 `SetDuty` 都只记逻辑占空比、CCR 强制写 0；`Heater_Off()` 同时清逻辑占空比并关闸。控制层仅在「REAL 模式 + RUNNING + 传感器有效」三者同时成立时开闸。

---

## P0-3 控制层缺 SIM/启动校验（致命）

**代码**：`Control_Tick()` RUNNING 分支无条件 `Heater_SetDuty((uint8_t)u)`；`Control_Start()` 只检查 `state == STANDBY`。

**问题**：
1. 模拟数据直接进入执行器（P0-1/P0-2 的放大器）；
2. 启动瞬间不校验传感器是否有效，可能带着陈旧快照启动；
3. `Control_AckFault()` 无条件回 STANDBY，传感器仍坏也能确认掉故障。

**修复**：
- RUNNING：`Heater_EnableOutput(d.is_simulated ? 0 : 1)`，且仅当 `d.is_valid`（联锁已保证）；
- `Control_Start()`：先做一次**新鲜** `Sensor_Read()`，无效则直接进 FAULT 并返回 3（传感器无效），不启动；
- `Control_AckFault()`：复核传感器，仍无效则**保持 FAULT**（加热关、风扇开），返回非 0。

---

## P0-4 串口 START 入口（高，随 P0-3 一并解决）

`comm.c` 的 `CMD_START` 直接调 `Control_Start()`。修复前它和按键一样没有校验；修复后所有入口共用 `Control_Start()` 内部校验，**不存在旁路**。同时把返回值映射为明确错误码（新增 6=传感器无效），避免上位机把"传感器坏了"误读成"状态不允许"。

---

## P1-1 NTC 换算无合理性检查（中）

`ntc_raw_to_celsius()` 只在 raw 贴轨时被判无效；若接错电阻、NTC 短路到某个中间值，Beta 公式可能算出 -80℃ 或 250℃ 这类值并被当作有效测量。修复：换算后做 `[-40℃, 150℃]` 合理性判断，越界返回 invalid（不 clamp，直接判故障）。

## P1-2 Flash 参数只校验 CRC（中）

`Param_Load()` 通过魔数/版本/CRC 后直接返回给 `Control_ApplyParams()`。虽然控制层会二次校验范围，但**加载层本身**应拒绝荒谬值（例如 target=1e6、kp=NaN 且 CRC 正好正确——概率低但属于"信任边界"问题）。修复：`param_codec` 增加纯函数 `Param_ValidateRanges()`，Load 时校验失败即回默认值并置默认恢复标志；补宿主机测试。

## P1-3 保存失败错误码错位（中）

`CMD_SAVE_PARAMS` 失败（擦除/写入/回读不符）返回 `ACK_RANGE_ERR=3`，上位机会提示"参数越界"，掩盖真实故障。修复：新增 `ACK_FLASH_ERR=5`，与传感器错误 `ACK_SENSOR_ERR=6` 一起写入 PROTOCOL.md。

## P1-4 时间片补执行导致 PID 连算（中）

`main.c` 用 `last_500ms += 500u`：一旦主循环被阻塞（UART 阻塞发送最长 100ms、Flash 擦写 20~40ms、OLED 刷屏），`now - last` 会累积，主循环下一次、再下一次都会立刻满足条件 → `Control_Tick()`/`pid_update()` 在几十毫秒内被连续调用多次，而 PID 内部仍按固定 `Ts=0.5s` 计算 → 等效积分增益被放大数倍，可能引起振荡或过冲。

**修复**：三档时间片全部改为 `last = now`（丢弃错过的时间片，不做补执行）。代价：极端阻塞时控制周期轻微抖动，可接受；收益：PID 永不连算。

## P1-5 中断保护期间可能丢串口字节（低）

`Param_Save()` 关中断擦写（20~40ms），期间 USART 以 115200 收字节（约 87µs/字节），必然丢若干字节。缓解：协议有 AA55 帧头 + CRC16 重同步 + 200ms 周期上报，丢帧可自愈；实测未验证。建议：后续用 DMA 接收或把写 Flash 拆到停机窗口，本轮**不改**（属架构改动，超出"修复问题"范围）。

## 已检查但判定无问题（记录备查）

| 检查项 | 结论 |
|--------|------|
| PID 输入 | `setpoint`/`measurement` 做 NaN（自比较）+ [-40,150] 界检查，异常返回 0 且不污染积分 |
| 占空比转换 | `(uint8_t)u`，u 已限幅 [0,100]，无溢出 |
| 超温联锁次序 | `safety_interlock()` 在 switch 之前、PID 之前，FAULT 态强制 `Heater_Off()+Fan_On()` |
| 扇热迟滞 | 目标+2.0 开 / +0.5 关，目标上限 70 < 安全上限 75，无冲突 |
| Flash 擦写失败 | 返回错误码并回读比对，菜单显示 SAVE FAIL；数据丢失风险已知（整页擦除无法原子化） |
| 参数上限 | 目标 0~70、Kp 0~50、Ki 0~5、Kd 0~20，`Control_SetTarget/SetPid` 双重限幅 |

---

## 修复计划（按序执行，每步编译 + 测）

**阶段 1（P0）**：`sensor.c/h` 编译期模式开关 → `heater.c/h` 输出总闸 → `control.c/h` 启动/确认/运行门控 → `comm.c` 错误码映射
**阶段 2（P1）**：NTC 合理性检查 → `param_codec` 范围校验 + `flash_param` 调用 → `main.c` 时间片改 `= now` → 补 `test_param` 用例
**验证**：UV4 全量重编译（要求 0E0W）+ `tests/run_tests.bat` + `host/test_protocol.py`
**不做**：不改通信协议帧结构（仅新增错误码）、不重写模块、不动无关文件、不提交 Git（等你审核）

---

## 修复执行记录（2026-10-18）

### 阶段 1：P0 修复

| 文件 | 关键差异 |
|------|----------|
| `Hardware/sensor.h/.c` | 删除"自检失败 → 自动模拟"；改为编译期 `SENSOR_SIM_ENABLE`（默认 0）。删除可运行时改模式的 `Sensor_ForceSimulation()`（消除旁路）。NTC 换算增加 `[-40,150]℃` 合理性判定 |
| `Hardware/heater.h/.c` | 新增逻辑占空比 / 硬件闸门双状态：`s_duty` + `s_hw_en`（默认 0）；`hw_apply()` 在闸门关时 CCR 恒 0；新增 `Heater_EnableOutput()` / `Heater_OutputEnabled()`；`Heater_Off()` = 清零 + 关闸 |
| `App/Inc/control.h`、`App/Src/control.c` | `Control_Start()` 先做**新鲜** `Sensor_Read()`，无效则进 FAULT 并返回 3；RUNNING 中 `Heater_EnableOutput(d.is_simulated ? 0 : 1)`；`Control_AckFault()` 改为返回 `uint8_t`，传感器仍无效则保持锁定返回 1 |
| `App/Src/comm.c` | 新增 `ACK_FLASH_ERR=5`、`ACK_SENSOR_ERR=6` 与 `map_ctrl_status()`；START/STOP/ACK 全部按真实返回值回码 |
| `App/Src/menu.c` | START 被拒显示 `START BLOCKED`；故障确认被拒显示 `SENSOR STILL ERR`（不再无条件显示 CLEARED） |

**修复后的行为（关键不变量）**
1. 无 NTC 上电：`Sensor_Read` 恒 `is_valid=0` → INIT 阶段即 `FAULT`，加热闸门关闭、风扇开；
2. 按键或串口 START：均经过 `Control_Start()` 的新鲜读数校验，无旁路；
3. 运行中 NTC 失联：联锁 → FAULT + `Heater_Off()`（同时关闸），不会转模拟；
4. 即使 `SENSOR_SIM_ENABLE=1`：PID 照算、曲线照走，但 `Heater_EnableOutput(0)` → **PA8 恒低**，模拟数据无法驱动真实负载；
5. SIM/REAL 标识仍原样透传 OLED 与状态帧第 11 字节。

### 阶段 2：P1 修复

| 文件 | 关键差异 |
|------|----------|
| `App/Inc/param_codec.h`、`App/Src/param_codec.c` | 新增 `Param_ValidateRanges()`（NaN/Inf + 四参数范围） |
| `App/Src/flash_param.c` | `Param_Load()` 在 CRC 通过后**再校验数值范围**，不合规即回默认并置 `s_default_restored` |
| `Core/Src/main.c` | 三档时间片 `last += period` → `last = now`（丢弃漏拍、不补执行），杜绝 PID 连算 |
| `tests/test_param.c` | 新增 T6 共 8 项断言（70.1/‑0.1/50.1/5.01/20.1/NaN/Inf） |
| `host/protocol.py` | 同步 `ACK_FLASH_ERR/ACK_SENSOR_ERR` 文案 |

### 验证结果（实际执行）

```
UV4 全量重编译：Code=17306 RO=2042 RW=48 ZI=3160 -> 0 Error(s), 0 Warning(s)
tests\run_tests.bat：50 项检查全部 PASS（PID 20 + 参数编解码 30）
python host/test_protocol.py：46/46 PASS
```

### 仍未验证（待上板）

| 项 | 说明 |
|----|------|
| PA8 在 SIM 构建下恒低 | 需示波器/万用表实测（静态审查已保证 CCR 写 0，但未实测引脚） |
| 无 NTC 上电即 FLT | 需实机断电重启观察 |
| 运行中拔线 → 立即关断 | 需实机带电拔插 |
| Flash 保存失败路径 | 需人为制造写入失败（较难，可能长期无法验证） |

### 遗留建议（本轮未改，需你决策）

1. `Usart_Send` 阻塞 100ms 与 Flash 关中断窗口可能丢接收字节：建议后续改 DMA 接收或发送队列。
2. `Param_Save` 整页擦除非原子：掉电瞬间可能丢失全部参数（可接受，但可加 A/B 双页备份）。
3. PID 的 `Ts` 为编译常量，实际周期受调度抖动影响；若上板测得抖动 >10%，建议用实测 dt 参与积分项。

---

## 第三轮补充修复（2026-10-18）：独立看门狗

> 本节由外部审查补充。前两轮把"模拟数据驱动真实加热器"定为 P0，但漏掉了另一条
> 同等严重、且不需要任何人为操作就会触发的链路。

### 补充缺陷 P0-5：CPU 挂死时加热器锁死在最后占空比

**现象**：`Heater_Off()` 通过 `s_hw_en = 0` 把 CCR 写 0，但这是一个**内存变量**。
一旦 CPU 停止执行（HardFault、`Error_Handler()` 的 `while(1)`、任意 while 卡住），
没有任何人再去调用 `Heater_Off()`；而 TIM1 是独立外设，会继续按最后一次的 CCR
输出 PWM，PA8 保持占空比 —— 加热器持续加热，软件超温联锁（75℃）永远不会再执行。

**与 P0-1/P0-2 的区别**：
- P0-1/P0-2 需要"用户按了 START"才可能触发；
- P0-5 不需要任何操作，电压跌落、EMI、栈溢出、外设异常都可能触发；
- 对加热类执行器，这是最经典的失效模式，且原方案只把它列进了
  `RESUME_AND_INTERVIEW.md` 的"未来改进 ⑤"。

**修复**：加入 IWDG（`Core/Src/iwdg.c`）。
- 预分频 64、重装 625 → 标称 1.0s（LSI 30~60kHz 下实际 0.67~1.33s）；
- 所有初始化完成后才启动（避免慢速初始化被误复位）；
- 主循环每轮开头喂狗，卡在任意一处都会停止喂狗；
- 上电读并清 `RCC_CSR` 的 `IWDGRST` 标志，在 OLED 第 4 行右上角显示 `WDG`，
  避免"自己重启了但没人知道"；
- 复位后 PA8 回到浮空输入，配合板上 10k 下拉 → 加热器关断。

**为什么 1s 够安全**：主循环最长单次阻塞为 `Usart_Send` 的 100ms（HAL 超时），
加 Flash 擦写约 40ms、OLED 整屏刷新，最坏约 250ms，对 0.67s 下限仍有 2.7 倍余量。
1s 内的失控加热对热系统而言可忽略。

### 验证（实际执行）

```
UV4 全量重编译：Code=17542 RO=2042 RW=48 ZI=3168 -> 0 Error(s), 0 Warning(s)
tests\run_tests.bat：HOST TESTS: ALL PASS（50 项）
```

补充：首次加 IWDG 时编译报 `L6218E: Undefined symbol HAL_IWDG_Init`，
原因是工程文件里从未加入 `stm32f1xx_hal_iwdg.c`（此前无人使用）——已一并加入。

### 仍未验证（必须上板）

| 项 | 说明 |
|----|------|
| 1s 是否误复位 | 实际 LSI 频率未知，需实测 |
| 卡死后能否复位 | 需人为制造死循环 |
| 复位后 PA8 是否为低 | 需示波器/万用表，且依赖 10k 下拉是否实装 |
| `WDG` 标记显示 | 需真实触发一次看门狗复位 |
