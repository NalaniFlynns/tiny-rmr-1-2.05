# RMR 1:2.05 微型反射式红点瞄准镜 —— 固件与设计说明书

> 适用固件：V4.3.3（2026-08）｜主控：TI MSPM0C1104（Cortex-M0+ 24MHz）｜调试/烧录接口：SWD（无 UART）

---

## 1. 版本结论：特性支持确认

当前固件 **V4.3.3** 已完整支持以下特性（默认全部开启，运行时可用 NVM 标志位关闭其中一部分）：

| 特性 | 编译开关 / 运行标志 | 状态 |
|---|---|---|
| 自适应电压补偿（恒流 PWM 占空比补偿 + 电压线性降额） | FEATURE_VOLTAGE_COMPENSATION / FLAG_VOLTAGE_COMPENSATION(bit0) | 已支持 |
| 自适应档位限制（电压不足时限制档位/标记越限） | FEATURE_ADAPTIVE_GEAR_LIMIT / bit1 | 已支持 |
| 自动感光 ALS（OPT3001） | FEATURE_ALS_MODE / bit2 | 已支持 |
| ALS 亮度偏移调节（±50% 共 5 段） | FEATURE_ALS_OFFSET_ADJUST | 已支持 |
| ALS 平滑与渐变限速 | FEATURE_ALS_SMOOTHING | 已支持 |
| 无操作自动调暗 / 自动关机 | FEATURE_INACTIVITY_AUTO_DIM_OFF / bit3 | 已支持 |
| 配置记忆（NVM 双扇区多槽位） | FEATURE_MEMORY_SAVE | 已支持 |
| NVM 写后校验 + 3 槽位尝试 + 重试 + RAM 影子 | FEATURE_SAVE_VERIFY / FEATURE_SAVE_RAM_SHADOW | 已支持 |
| 低功耗 STANDBY0 深睡（WWDT 拉伸 + IOMUX 唤醒） | FEATURE_LOWPOWER_STANDBY / bit4 | 已支持 |
| LVP 低电压闪烁警告 | FEATURE_LVP_FLASH_WARNING / bit5 | 已支持 |
| OFF 态保持 SWD 可访问 | FEATURE_SWD_IN_OFF_STATE / bit6 | 已支持 |
| 上电自动开机（仅冷启动生效） | FEATURE_AUTO_POWER_ON / bit7 | 已支持 |
| ECO 动态降频 / WFI 睡眠 / 采样电源门控（省电变体） | POWER_SAVE_BUILD | 已支持（ECO_D / ECO_B） |
| 开机测压门控、掉电保存、恢复出厂、调试信箱/注入 | — | 已支持 |

> 结论：**自适应电压补偿已支持**，核心是“恒流模型”——LED 平均电流由 brt/1000 × I_max 决定，PWM 占空比按当前电压下的峰值电流实时反推；电压下降时峰值电流变小，占空比自动增大，从而维持亮度恒定。详见第 7 章。

---

## 2. 产品概述

RMR 1:2.05 是一款微型反射式红点瞄准镜（红色反射式红点），主要组成部分：

- 主控：TI MSPM0C1104（Cortex-M0+，24MHz 总线时钟，1ms 硬件时基由 GPTIMER14 产生）
- 红点光源：APTD1608SECK-J4 红色 LED（Vf 约 2.2V，最大电流 2.8mA），PWM 恒流驱动
- 环境光传感器：TI OPT3001（I2C，软件模拟时序，GPIO 位操作）
- 供电：2×SR516SW 氧化银纽扣电池（标称 3.1V，12.5mAh/节，内阻约 25Ω/节）或 1.6–3.6V 可调稳压电源直连（120–635Ω 可调限流电阻）
- 操作：BT1 / BT2 两颗物理按键（低有效，8 次采样去抖）
- 调试/烧录：仅 SWD（XDS110 或 DAPLink），无 UART

产品渲染图见 image/ 目录（1.png – 15.png）。

## 3. 硬件与结构资料

| 目录 | 内容 |
|---|---|
| PCB/ | 原理图/PCB 工程（ProDoc）、Gerber 打样包、BOM 清单 |
| model/ | 结构 STEP 模型、SHAPR 工程、镜片设计 |
| image/ | 产品渲染图 15 张 |
| firmware/RMR/ | 固件工程（CCS 21） |
| firmware/RMRDebugger/ | 桌面调试器源码（Qt 6） |
| RMR_Factory_Tool_V2.0/ | 打包版调试工具（免编译：rmrdebuger.exe + openocd.exe） |

## 4. 固件版本与变体

V4.3.3 由 app_config.h 中 4 个编译宏组合出 6 个变体：

| 变体 | 版本串 | 宏配置 | 适用场景 |
|---|---|---|---|
| DIRECT | V4.3.3_DIRECT | 默认（POWER_SOURCE_DIRECT=1） | 稳压电源直连（不算电池内阻），普通版 |
| BATT | V4.3.3_BATT | POWER_SOURCE_DIRECT=0 | 2×SR516SW 电池，r_series 含 2×25Ω 内阻 |
| ECO_D | V4.3.3_ECO_D | POWER_SAVE_BUILD=1 + DIRECT | 直连省电版 |
| ECO_B | V4.3.3_ECO_B | POWER_SAVE_BUILD=1 + BATT | 电池省电版 |
| DBG | V4.3.3_DBG | DEBUG_BUILD=1 | 测试板专用，全程 SWD 保活 |
| DBGL | V4.3.3_DBGL | DEBUG_LP_BUILD=1 | 低功耗调试版：OFF 态深睡、运行态 SWD |

编译产物：firmware/RMR/hex/（6 个 hex）；firmware/RMR.hex 为发布镜像。

## 5. 系统状态机

```
                    +------ 双键 1.5s（测压达标）------+
SYS_OFF <-----------+                                v
   ^                |                          SYS_RUN -> SYS_LVP_CRIT（连续 5 次 <2300mV）
   | 双键1.5s熄灯    |                                |  ^
   | (松开<5s进STANDBY0)  v                          |  | 电压回升+迟滞
   |             SYS_FLASH_MODE（单键BT1 0.8s进入，5min超时复位）|
   |                  |                              v  |
   +------------------+---- 双键 1.5s 测压开机 <------+--+
                                              ^
   SYS_ALS_ERR --10s 自恢复--> SYS_RUN（切回手动并落盘）
        ^
        +-- ALS 连续 3 次读取失败（闪烁提示，1.5s 周期）

   SYS_TEST_MODE（调试器授权进入）--> 正常退出 = SYS_FLASH_MODE；授权丢失 = 复位
```

- SYS_OFF：LED 全灭；默认版本若未置位“OFF 态 SWD”标志则进入 STANDBY0 深睡
- SYS_RUN：正常运行（手动/ALS 亮度控制）
- SYS_LVP_CRIT：低电压临界，LED 每 2s 闪 50ms 且亮度降为 1/4 警示
- SYS_FLASH_MODE：烧录/调试模式（VCC_EN 拉高、NRST 恢复、SWD 可连），5min 无操作自动复位
- SYS_ALS_ERR：ALS 故障，LED 以 1.5s 周期闪烁（亮 600ms），10s 后自恢复
- SYS_TEST_MODE：调试器授权后进入，支持注入与系统命令
## 6. 按键操作说明

9 档亮度映射：brt = {5, 20, 50, 150, 300, 450, 600, 800, 1000}（默认第 4 档=300）。
ALS 偏移 5 段：-50% / -30% / 0% / +30% / +50%（默认 0%）。

| 操作 | 状态 | 效果 |
|---|---|---|
| BT2 短按 | RUN 手动 | 亮度升一档（受电压自适应限制，越限档标记 overshot） |
| BT2 短按 | RUN ALS | 亮度偏移 +30%（到 +50% 封顶） |
| BT1 短按 | RUN 手动 | 亮度降一档（越限档先回落到安全亮度） |
| BT1 短按 | RUN ALS | 亮度偏移 -30%（到 -50% 封底） |
| 双键按住 1.5s 熄灯后继续按满 5s | OFF（熄灯后） | 双闪提示 + 切换 ALS<->MAN 重新亮灯（测压达标） |
| 双键同时按住 1.5s | RUN / LVP_CRIT | 立即熄灯进入 OFF（不等松开） |
| 双键按住 1.5s 后松开（未满 5s） | OFF | 确认关机，进入 STANDBY0 深睡 |
| 双键直接按住满 5s | OFF | 双闪提示 + 以 ALS 模式开机（测压达标，含开机失败重试） |
| 双键同时按住 1.5s | OFF | 开机（测压达标才启动，不达标闪 100ms） |
| 双键同时按住 1.5s | FLASH | 测压开机（与 OFF 态一致） |
| BT1 按住 0.8s | OFF | 进入 FLASH 模式（测压达标才进入，冷启动后仅一次） |
| 任意键按下 | STANDBY0 | 立即唤醒（IOMUX 电平比较异步唤醒 + GPIO 边沿双保险） |

说明：1.5s 熄灯不等于立即进深睡——先置 g_off_pending，确认松开且未满 5s 才进 STANDBY0；RUN 熄灯后继续按满 5s 则切换 ALS<->MAN 重新亮灯；OFF 态直接长按满 5s 则以 ALS 模式开机。这是“松开灯又亮”根因修复后的最终行为。

## 7. 亮度控制与自适应电压补偿（核心算法）

### 7.1 亮度标度

- 逻辑亮度 brt：0-1000（1000 = 满亮）
- 物理 PWM 寄存器：0-2399（PWM_REG_MAX），CC=2399 表示占空比 0（灭）

### 7.2 恒流 PWM 补偿（battery_brt_to_pwm）

```text
目标平均电流   i_req = brt / 1000 x I_max            （I_max = 2.8mA 默认）
峰值电流       i_peak = (Vbatt - Vf) / R_total       （Vf = 2.2V）
占空比         duty  = i_req / i_peak（上限 100%）
PWM 装载值      CC = 2399 - duty x 2399 / 1000
```

电压下降 -> i_peak 变小 -> duty 自动上升 -> 亮度维持。这就是自适应电压补偿：不靠查表，完全由恒流模型实时反推。

### 7.3 动态内阻模型（R_total = R_series + R_dyn）

```text
R_dyn = R_DYNAMIC_BASE_MOHM(50mΩ)
      + (Vbatt <= 1900mV ? 300Ω 固定
         : min( 5x10^7 / (Vbatt - 1900mV), 250Ω ))
R_series = NVM 可校准（DIRECT 默认 360Ω；BATT 默认 360Ω + 2x25Ω）
```

### 7.4 五重安全限制（battery_get_safe_brt，取最小值）

| 限制 | 依据 | 说明 |
|---|---|---|
| limit_i_led | 峰值电流 <= i_max_ua(2.8mA) | 防止 LED 过流 |
| limit_v_drop | 压降后电压 >= WALL_MIN_V_BATT_MV(2400mV) | 电池电压“墙”，保底亮度 30 |
| limit_i_brt | 总放电电流 <= BATT_MAX_DISCHARGE_UA(4mA) | 限制电池总放电 |
| limit_p_avg | 总功率 <= BATT_MAX_DISCHARGE_UW(9mW) | 限制电池总功耗 |
| limit_v_derate | 电压 >=3300mV 无降额；线性降至 2400mV 时保底 30 | 电压线性降额 |

压降模型：v_drop_peak = i_peak x R_dynx30%（导通等效），v_drop_avg = i_peak x brt x R_dyn（平均）。
最终输出 safe_brt = min(req_brt, 各限制值)；所有中间量（g_safe_brt_out、g_limit_*、g_dyn_r_mohm、g_est_i_peak_ua）实时镜像到调试信箱，可在 RMRDebugger 中观察是哪一重限制在起作用。

### 7.5 档位自适应（FEATURE_ADAPTIVE_GEAR_LIMIT）

- 升档时若目标档 brt <= max_brt（安全上限）则正常升档
- 目标档超限但差值 <= SNAP_THRESHOLD_BRT(50) 也正常升档
- 明显越限 -> 仍升档但置 g_is_overshot（调试器可读，用于识别“电压不够高”）
- 降档时若当前档已越限 -> 连续回退到安全档

## 8. ALS 自动感光

### 8.1 曲线

```text
brt = ALS_SQRT_FACTOR(5) x sqrt(lux_count / 100)     （lux_count 为 OPT3001 结果寄存器计数）
     x (1 + 偏移%)                                    （5 段：+/-50/30/0%）
下限 = als_min_brt(30) ｜ 低照度上限 600 ｜ 高照度上限 800 ｜ 总上限 1000
```

分界：lux_count <= 1,000,000 用低照度上限 600，更高用 800。

### 8.2 平滑与限速（FEATURE_ALS_SMOOTHING）

- 指数移动平均（1/8 权重）
- 上升限速分档：低亮度每 tick +2、中亮度 +5、其他 +20；下降限速 -20
- 轮询周期：普通版 120ms，ECO 版 1s

### 8.3 故障处理

- 连续 3 次读取失败 -> SYS_ALS_ERR：LED 以 1.5s 周期闪烁（亮 600ms）提示
- 10s 后自恢复：关闭 ALS 切回手动模式并落盘
- 连续 3 次“故障->自恢复”后锁定 ALS（拒绝从手动切回 ALS，避免传感器永久损坏时的闪烁循环；当前在 ALS 时仍可切回手动）
## 9. 电源管理与保护

### 9.1 运行功耗控制

- LED 恒流驱动，电流由档位决定（0-2.8mA）
- ADC/VREF：普通版每 100ms 采样（ECO 版 500ms，采样前后开关电源域）
- ECO 版：主循环 WFI 睡眠替代 24MHz 忙等；OFF/FLASH 态 SYSOSC 降到 4MHz（GPTIMER14 同步重配，tick 恒为 1ms）；PWM 亮度不变时不重算不写寄存器；OFF 态 OPT3001 shutdown（0.4uA）

### 9.2 关机与深睡（STANDBY0）

- 双键 1.5s 熄灯 + 松开确认 -> SYS_OFF -> STANDBY0 深睡
- 深睡准备：WWDT 周期先拉伸再重启（约 8192s，防止 500ms 看门狗深睡复位）；IOMUX WCOMP 匹配低电平（按下即异步唤醒）+ GPIO 边沿中断双保险；受控 __disable_irq()/WFI
- 唤醒后：恢复 500ms 看门狗、禁用唤醒源、重新测压
- FLAG_SWD_IN_OFF_STATE(bit6)：置位则 OFF 态跳过深睡保持 SWD 可连（DBG 版出厂默认置位；ECO/DBGL 出厂默认清零）

### 9.3 无操作超时

- 40 分钟无操作 -> 调暗到 DIM_LEVEL(5)
- 再 10 分钟无操作 -> 自动关机
- 按键任意事件/调暗后按键 -> 立即恢复并清零计时

### 9.4 低压保护（LVP）

| 阈值 | 去抖 | 动作 |
|---|---|---|
| < lvp_crit(2300mV) | 连续 5 次 | SYS_LVP_CRIT：LED 每 2s 闪 50ms、亮度降 1/4 警示 |
| < lvp_ext(2100mV) | 连续 5 次 | 直接关机并保存配置 |
| < 2000mV（掉电） | 连续 3 次 | 保存配置 -> SHUTDOWN（DBG/DBGL 版不 SHUTDOWN，仅存配置保 SWD） |

- 开机测压门控：battery_startup_check() 必须测得电压 > lvp_ext + 100mV 才允许开机
- SYS_LVP_CRIT 回升自动恢复：电压 >= lvp_crit + 100mV 回 SYS_RUN

### 9.5 冷启动判定（AUTO_POWER_ON）

仅冷启动（RSTCAUSE = POR/BOR/SHUTDOWN 退出，或 SRAM g_por_magic 兜底）才执行自动开机；WWDT/SYSRST/调试复位一律保持关机——保证“1.5s 熄灯后不会被看门狗复位点亮”。

## 10. 配置记忆（NVM）

### 10.1 存储布局

- 记录大小固定 64 字节（编译期 _Static_assert），尾部 4 字节 FNV1a-32 CRC
- 存储区：扇区 A（0x3800）/ 扇区 B（0x3C00），各 1KB = 16 槽，双扇区轮转
- 加载：扫描全部槽位，取 CRC 合法且 seq_id 最大者；坏槽标记跳过
- 保存：槽位轮转 + 3 槽位尝试；写后回读校验 + 擦写耗时补偿；失败 3 次放弃本次（保留 RAM 影子与 dirty，后续重试最多 5 次）
- 后台 30s 自动保存（仅 RUN 态）；关机/LVP/掉电路径强制保存

### 10.2 可配置项（NVM 字段）

| 字段 | 默认值 | 说明 |
|---|---|---|
| params | lvl=4, ALS=0, offs=2 | 档位 0-8 ｜ ALS 使能 ｜ 偏移索引 0-4 |
| features | 8 位标志 | 运行时特性开关（受编译期 mask 过滤） |
| r_base | 50 mΩ | 动态内阻基数 |
| r_series | 360000 / 410000 mΩ | 串联限流电阻（电池版含 2x25Ω 内阻，可校准） |
| v_led_fw | 2200 mV | LED 正向压降 |
| i_max_ua | 2800 uA | LED 最大电流 |
| batt_p_uw | 9000 uW | 电池最大放电功率 |
| als_min_brt | 30 | ALS 最低亮度 |
| lvp_crit | 2300 mV | 低压临界 |
| lvp_ext | 2100 mV | 低压极值（关机） |
| als_sqrt_factor | 5 | ALS 曲线系数（0=用默认，NVM 1-20） |
| als_cap_low_x100 / als_cap_high_x100 | 6 / 8 | ALS 低/高照度亮度上限（0=用默认） |
| default_level | 4 | 出厂档位 |
| seq_id | — | 序列号（磨损均衡） |

### 10.3 恢复出厂

- 烧录后首次上电可选清空存储区（DEV_CLEAR_NVM_ON_POR=1）
- 调试器指令 5：恢复出厂 + 立即保存
- 出厂默认值宏在 app_config.h [0] 段，可编译期修改

## 11. 调试接口（RMRDebugger / 调试信箱 / IPC）

### 11.1 调试信箱（g_test_box，SWD 直接读 RAM）

- 授权：magic=0x54455354 + host_version 匹配；授权丢失 -> 复位；正常退出 -> SYS_FLASH_MODE
- 实时监视（每 tick 刷新）：电压（原始/滤波）、估算电流/功率/硬件功率、动态内阻、亮度/PWM、五重限制值、ALS 原始/滤波/传感器状态/错误计数、档位/偏移/模式、NVM 状态、复位原因、无操作秒数等
- 系统命令：2=复位 ｜ 3=写配置（mask 过滤）｜ 4=保存 ｜ 5=恢复出厂+保存 ｜ 6=开机（测压）｜ 7=关机
- 注入：模拟按键（+/-）、ALS 注入（开关+lux）、LED 注入（1=PWM 直驱 2=无限制亮度 3=受限亮度）、拦截物理按键
- 实时诊断：TEST 态每 tick 刷新计算数据（battery_get_safe_brt 全链路）

### 11.2 RMRDebugger（Qt 6）

- XDS110/DAPLink 自动扫描（OpenOCD SWD，5kHz-4MHz）
- 实时遥测：电压/电流/功率/内阻/档位/亮度/PWM/ALS/NVM 全字段 + 三张图表（10s 滚动窗口、Y 轴自适应）
- 一键烧录：擦除+编程+校验+复位；连接失败自动清理残留 OpenOCD 进程
- 测试模式：解锁/读配置/写配置/保存/恢复出厂/开关机/软复位
- 虚拟按键与真实按键语义一致；状态与操作合并显示
- Power-Z KM003C 电压电流计接入（HID，200ms 轮询，10s 滑动窗口平均）
- 隐藏 IPC：127.0.0.1:7345，JSON 行协议（ping/state/scan/setfw/flash/unlock/syscmd/cmd/write/read/poll/speed/key/power/led/als）

## 12. 编译与烧录

### 12.1 编译（CCS 21）

- 打开工程 firmware/RMR/（.ccsproject，tiarmclang 工具链）
- 切换变体：修改 app_config.h 顶部 POWER_SOURCE_DIRECT / POWER_SAVE_BUILD / DEBUG_BUILD / DEBUG_LP_BUILD
- post-build 步骤：tiarmobjcopy 生成 Intel HEX（根治 tiarmhex 版本段 bug）

### 12.2 烧录

方式 A（推荐）：RMRDebugger GUI 一键烧录（自动 halt + flash probe + program verify reset，烧录后清 SRAM 开机门控模拟冷启动）。

方式 B（OpenOCD 命令行）：

```bat
openocd -s <openocd>\scripts -f interface/xds110.cfg -f target/ti_mspm0.cfg ^
  -c "transport select swd" -c "adapter speed 1000" ^
  -c "tcl_port 3334" -c "gdb_port disabled" -c "telnet_port disabled"
```

## 13. 常见问题

- 为什么灯灭了松手又亮？：1.5s 熄灯后必须松开且未满 5s 才会确认关机进 STANDBY0；若一直按到 5s 会转 ALS 亮灯——这是设计行为（详见第 6 章）。
- 为什么 OFF 态 SWD 连不上？：默认版 OFF 态会进 STANDBY0 深睡（SWD 断开）；需要 OFF 态可连请烧录 DBG 变体或在调试器里置位 FLAG_SWD_IN_OFF_STATE。
- 为什么电压低时升不了档？：自适应档位限制在起作用，limit_v_derate/limit_v_drop 会限制亮度上限；调试器里可看到具体是哪一重限制。
- ALS 为什么不动？：检查是否在 ALS 模式（双键 5s 切换）、传感器是否故障（连续 3 次失败会进 ALS_ERR 闪烁并 10s 后自恢复切手动）。

## 14. 修订记录

| 版本 | 日期 | 内容 |
|---|---|---|
| V4.3.3 | 2026-08 | 双键关机确认机制（灭灯不等于 standby）；深睡唤醒根因修复（WWDT 拉伸顺序/WCOMP 方向/冷启动门控）；调试版 DBG/DBGL；ECO 动态降频；NVM 延迟保存；ALS 故障锁定；RMRDebugger 烧录/遥测完善 |
| V4.3.2 | — | 虚拟按键与真实按键一致；调试器连接自动清理；IPC 完善 |
| V4.3.1 | — | 双版本（DIRECT/BATT）；1ms 硬件时基 GPTIMER14；电压降额+PWM 补偿根因修复；tiarmobjcopy 生成 HEX |

---

本文档对应仓库 tiny-rmr-1-2.05；固件源码见 firmware/RMR/，调试器见 firmware/RMRDebugger/。