## ?????2026-08-13?????????????standby?+ RMRDebugger ??????
- **??**?RUN ?????? 1.5s ?????????????? 5s ??????
- **??**?1.5s ??????? SYS_OFF???????? STANDBY0 ?????WCOMP/GPIO ????????????????????????
- **??????**???"????"????`hal_keys.c` ?? `EVT_BOTH_RELEASE_1_5S`???? 1.5s ?????? 5s ???????????`empty_mspm0c1104.c` ?? `g_off_pending` ???1.5s ???? pending???????**?? standby**????????? 5s ? ???? STANDBY0???? 5s ? ?? pending ??? + ALS ??
- **???RMRDebugger ???**?`debugworkers.cpp` executeFlash ? program ?? `halt` + `flash probe 0`??? examine ? target ? halt?FACTORYREGION ??? flash size=0?program ? "Target missing or protected"?????????? SRAM ?????`g_off_intent`=0x20000299 / `g_por_magic`=0x2000029C???????????????????"??????"?? OFF ????????????
- **??**?DEBUG_LP_BUILD / DEBUG_BUILD / ????? 0 ?? 0 ???`firmware/RMR/hex/RMR_DBGL.hex`?V4.3.3_DBGL???? RMRDebugger ?????????1.5s ?? ? ???????? standby?????? 5s ? ?? + ALS ?? ?

## 更新记录：2026-08-11，深睡后“松开灯又亮”根因修复（双键 1.5s 熄灯 → 松开应保持灭灯）
- 症状：RUN 态双键按住满 1.5s 灯灭后，松开灯又亮（独立供电同样复现）。根因并非按键/邮箱逻辑，而是深睡路径三处硬件级缺陷叠加
- 根因 1（WWDT 拉伸未生效）：进入 STANDBY0 前代码先 `DL_WWDT_restart()` 再写 `WWDTCTL0` 拉伸周期——但 MSPM0 的 WWDT **新周期只在下次计数器重启时装载**，顺序反了导致深睡期间仍是 500ms 周期，约 0.5s 后 WWDT 超时复位设备。修复：**先写 `WWDTCTL0` 拉伸配置，再 `DL_WWDT_restart()`**，深睡时实际生效 ~8192s 周期
- 根因 2（IOMUX 唤醒比较方向错误）：按键为低有效（空闲=1、按下=0），代码却配 `DL_GPIO_WAKEUP_COMPARE_VALUE_1`（匹配高电平）——进 standby 时引脚已为高 → 立即误唤醒/无法保持深睡；按下（1→0）不产生唤醒 → “有概率按键无法开机唤醒”。修复：改为 **`DL_GPIO_WAKEUP_COMPARE_VALUE_0`**（匹配低电平，按下即异步唤醒）
- 根因 3（AUTO_POWER_ON 不区分复位来源）：`FEATURE_AUTO_POWER_ON` 在**任何复位**后都自动开机。WWDT 复位 → 重启 → 看到 NVM 中 auto-power-on 标志 → 直接 RUN → 灯亮。修复：启动时读 `SYSCTL->SOCLOCK.RSTCAUSE`（读后自动清零，仅冷启动 POR/BOR/SHUTDOWN 退出视为 first_boot），**WWDT/SYSRST/调试复位一律保持关机**；`RSTCAUSE` 为 0 时用 `.TI.noinit` SRAM magic 兜底
- 新增观测：`g_test_box.rst_cause`（复用原 `_pad0` 字节，偏移不变，调试器无需改布局）镜像本次复位原因；`g_debug_str` 追加 `Rst:xx` 字段，烧录后可确认复位来源（0x0E=WWDT0 违规）
- 验证：DEBUG_LP_BUILD / DEBUG_BUILD / 默认三配置 0 错误 0 警告；`firmware/RMR/hex/RMR_DBGL.hex`（V4.3.3_DBGL）已更新并已通过 RMRDebugger 烧录（OpenOCD Verify OK）。待独立供电实测：双键 1.5s 熄灯 → 松开保持灭灯；关机态按任意键应立即唤醒（不再等松开）
## 更新记录：2026-08-11，按键交互调整：双键长按 1.5s 立即熄灯 / 5s 转 ALS 亮灯 / 中途松开关机
- **RUN/LVP 态按住双键**：1.5s 到点**立即熄灯**（无需松开），进入 OFF；继续按住到 5s → 闪烁提示（led_blink_twice）并**以 ALS 模式开机**（测压达标才启动，强制写入 ALS 标志并落盘）；1.5s~5s 之间松开 → 保持关机
- **实现**：`hal_keys.c` 1.5s 事件由"仅 OFF 态按住触发"改为全状态按住触发（RUN 态立即响应熄灯）；`empty_mspm0c1104.c` OFF 态新增 `EVT_BOTH_LONG_5S` 分支（闪烁 + ALS 开机）
- **保持原行为**：OFF 态双键 1.5s 开机、RUN 态 5s 模式切换（ALS<->手动，TEST 态同）、FLASH 模式单键 0.8s 进入、调试器恢复出厂指令均不变
- **验证**：DEBUG_LP_BUILD / DEBUG_BUILD / 默认三配置 0 错误 0 警告；`firmware/RMR/hex/RMR_DBGL.hex`（V4.3.3_DBGL）已更新
## 更新记录：2026-08-11，固件深睡唤醒修复（STANDBY0 按键唤醒/进入 standby 概率性问题）
- **根因 1：WWDT 深睡复位**：WWDT 由 LFCLK 驱动，`DL_WWDT_STOP_IN_SLEEP` 只覆盖 SLEEP 模式；进入 STANDBY0 后 LFCLK 继续运行，500ms 看门狗因主循环停摆而超时 → 每 500ms 周期性 SYSRST，导致"有概率无法进入 standby"且按键唤醒被复位窗口吞掉。修复：进入 STANDBY0 前将 WWDT 周期临时拉伸到最大（PER_EN_25 + CLKDIV /8 ≈ 8192s），唤醒后立即恢复 500ms 配置并喂狗
- **根因 2：唤醒源只配了 GPIO 边沿中断**：STANDBY0 深睡下边沿中断依赖 ULPCLK 采样，按下瞬间易被丢失。修复：新增 IOMUX IO 唤醒（`DL_GPIO_setWakeupCompareValue` + `DL_GPIO_enableWakeUp`，WCOMP 匹配高电平、按键按下拉低即异步唤醒），GPIO 边沿中断保留作双保险；唤醒后 `DL_GPIO_disableWakeUp` 清理
- **根因 3：WFI 可被 1ms tick 打断**：进入深睡前清 `GPIOA/TIMG14` pending，并用 `__disable_irq()/__WFI()/__enable_irq()` 受控进入，杜绝"概率性无法进入 standby"的 tick 竞态
- **修复代码位置**：`empty_mspm0c1104.c` OFF 深睡段（STANDBY0 入口/出口），对 `DEBUG_LP_BUILD`、正式版（FLAG_SWD_IN_OFF_STATE 未置位）同样生效
- **验证**：`DEBUG_LP_BUILD=1` / `DEBUG_BUILD=1` / 默认三配置均 0 错误 0 警告；产物 `firmware/RMR/hex/RMR_DBGL.hex`（V4.3.3_DBGL）已更新，建议重新烧录后实测：power off → 深睡（电流应稳定 μA 级无周期性尖峰）→ 双键 1.5s 应 100% 唤醒开机
## 更新记录（2026-08-11，RMRDebugger：Power-Z 平均统计）
- **Power-Z 10s 滑动窗口平均统计**：`PowerZWorker` 新增软件统计——200ms 采样压入窗口、弹出超窗样本，实时计算最近 10s 平均电压/电流/功耗（约 50 样本）
- **GUI**：Power-Z 区域新增一行 `Avg V / Avg I / Avg P / 10s Win(s)` + `Reset Stats` 按钮（清零重填窗口）
- **IPC**：`hello` 与 `powerz` 事件新增 `avg_v / avg_a / avg_w / stat_sec` 字段；新增命令 `{"cmd":"powerz","reset":true}` 清零统计
- **验证**：KM003C 已连接实测——`avg_v` 3.2009V 稳定、窗口满后 `stat_sec` 保持 ~10s、清零后从 0 重新填充；`rmrdebuger.exe` 已更新（firmware/RMRDebugger/ 与 RMR_Factory_Tool_V2.0/）
## 更新记录（2026-08-11，固件 V4.3.3 低功耗调试版 DEBUG_LP_BUILD：OFF 态 WFI/STANDBY0 深睡 + 开机态 SWD，已烧录验证）
- **新增调试编译宏 `DEBUG_LP_BUILD`**（`app_config.h`，默认 0，与 `DEBUG_BUILD`/`POWER_SOURCE_DIRECT`/`POWER_SAVE_BUILD` 正交）：1=低功耗调试专用，版本串 `V4.3.3_DBGL`
- **OFF 态彻底低功耗**：强制进入深睡段（关 ADC/VREF + WFI + STANDBY0，忽略 NVM SWD 位），SWD 自然断开可接受；出厂默认清 `FLAG_SWD_IN_OFF_STATE`
- **开机/运行态 SWD 保持**：RUN 态正常可调试，telemetry 实时；掉电保护不执行 SHUTDOWN（仅存脏配置），主循环 WFI 睡眠等待 1ms tick
- **保留特性**：ECO 动态降频、NVM 校验/重试/多槽位、ALS 故障锁、LVP、FLASH 模式等全部不变
- **产物**：`firmware/RMR/hex/RMR_DBGL.hex`（`V4.3.3_DBGL`）
- **验证**：DEBUG_LP_BUILD=1 与 =0 均 0 错误 0 警告；已烧录测试板：RUN 态 SWD+telemetry 实时；`power off` 后 STANDBY0 深睡（OpenOCD target unknown、数据冻结）；OpenOCD 复位唤醒后自动开机、SWD 自动重连
## 更新记录（2026-08-11，固件 V4.3.3 调试版 DEBUG_BUILD：测试板直供 + 全程 SWD，已烧录验证）
- **新增调试编译宏 `DEBUG_BUILD`**（`app_config.h`，默认 0，与 `POWER_SOURCE_DIRECT`/`POWER_SAVE_BUILD` 正交）：1=测试板直供调试专用，版本串 `V4.3.3_DBG`
- **全程 SWD 保活**（仅调试版生效，其余特性不变）：OFF 态跳过 STANDBY0 深睡段（不关 ADC/VREF、不 WFI 深睡）；掉电保护路径不执行 SHUTDOWN（仅保留脏配置落盘）；出厂默认置位 `FLAG_SWD_IN_OFF_STATE`
- **保留特性**：ECO 动态降频、NVM 校验/重试/多槽位、ALS 故障锁、LVP、FLASH 模式等全部不变
- **产物**：`firmware/RMR/hex/RMR_DBG.hex`（`V4.3.3_DBG`）
- **验证**：DEBUG_BUILD=1 与 =0 两配置均 0 错误 0 警告；已烧录测试板（XDS110 SWD），OFF 态 SWD 连接保持、telemetry 150ms 实时刷新
## 更新记录（2026-08-11，固件 V4.3.3 优化：ECO 动态降频 + NVM 磨损 + ALS 故障锁 + 杂项）
- **ECO 动态降频**：关机/FLASH 态 SYSOSC 切 4MHz 低功耗模式（24MHz→4MHz，睡眠电流大幅下降），开机/运行态自动恢复 24MHz；GPTIMER14 同步重配 LOAD，`g_tick_ms` 恒为真实 1ms（按键 1.5s/5s、FLASH 5min 超时、轮询计时全部不受影响）
- **NVM 磨损优化**：按键调档/模式切换不再立即写 FLASH，只置 dirty 交给 30s 后台自动保存（连按调档只记一次）；关机/LVP/掉电强制保存路径保留
- **ALS 故障锁**：连续 `ALS_ERR_LOCKOUT_COUNT`(3) 次 10s 自恢复后锁定 ALS——拒绝再切回（双键 5s 闪灯提示），防止传感器持续损坏时每 10s 闪一次死循环；重启/恢复出厂解除
- **启动延迟**：ECO 启动后先断 ADC/VREF 寄存器电源域，首次采样/开机测压前再上电；VREF 内部基准采样间隙关闭（`DL_VREF_enable/disableInternalRef` 门控，battery_startup_check/resume 同步）
- **flash 超时差值**：FLASH 模式 5min 超时改 `g_tick_ms` 差值比较（原累加计数，防 49.7 天回绕）
- **验证**：DIRECT / BATT / ECO_D / ECO_B 四变体 0 错误 0 警告；未烧录

## 更新记录（2026-08-11，固件 V4.3.3 省电版 ECO：关闭一切非必要开销，未烧录仅编译验证）
- **新增省电编译宏 `POWER_SAVE_BUILD`**（`app_config.h`，默认 0，与 `POWER_SOURCE_DIRECT` 正交）：1=省电版，版本串 `V4.3.3_ECO_D`（直连）/ `V4.3.3_ECO_B`（电池）
- **省电措施**：主循环 24MHz 忙等改 `__WFI()` 睡眠等待 1ms tick 中断（GPTIMER14 唤醒）；调试箱 `test_mailbox_task` 每 tick 实时刷新整体关闭（GPIO 读/64 位乘除/镜像同步/诊断计算）；ADC/VREF 采样间隙断电、采样间隔 500ms（原 100ms，LVP 去抖 5 次约 2.5s 响应）；OPT3001 手动模式与 OFF 态发 shutdown 配置（0.4µA 级，原连续转换约 1.8µA）；ALS 轮询 1s（原 120ms，转换占空比约 10%）；LED 亮度/状态未变化时不重算 PWM（原每 tick 64 位乘除+写寄存器）；出厂默认清 SWD 保活位 -> OFF 态进 STANDBY0 深睡（µA 级）
- **注意**：烧录省电版后若 NVM 已有旧配置（features=0xFF），OFF 态仍不会进 STANDBY0；需"恢复出厂"或经调试器清 bit6 才生效；省电版 OFF 态 SWD 不可访问（深睡保电），日常调试请用普通版
- **产物**：`firmware/RMR/hex/RMR_DIRECT_ECO.hex`（`V4.3.3_ECO_D`）、`RMR_BATT_ECO.hex`（`V4.3.3_ECO_B`）；普通版 DIRECT/BATT hex 同步刷新
- **验证**：DIRECT / BATT / ECO_D / ECO_B 四变体全部 0 错误 0 警告；未烧录（设备仍运行 V4.3.3_DIRECT）

## 更新记录（2026-08-11，固件 V4.3.3：FLASH 模式长按 1.5s 开机 + 调试器 cfg_features 实时显示）
- **固件 V4.3.3**：进入 FLASH 模式后双键长按 1.5s 也可正常开机——与 OFF 态一致先 `battery_startup_check()` 测压达标才启动（达标→SYS_RUN+mode_init，不达标→LED 闪 100ms 提示）；FLASH 模式 5 分钟超时自动复位逻辑保留
- **调试器修复**：telemetry 新增 `cfg_features`（mailbox 偏移 0x78，授权后实时显示 0xFF），修复此前该字段恒为 0 被误判为 features 被清零的问题；实际设备 features 一直是 0xFF（自动开机/无操作调暗/LVP/standby 位全开），OFF 态不进 STANDBY0 保持 SWD 可访问属设计行为（bit6 置位）
- **产物**：`firmware/RMR/hex/RMR_DIRECT.hex`（`V4.3.3_DIRECT`）、`RMR_BATT.hex`（`V4.3.3_BATT`）；`RMR_Factory_Tool_V2.0/rmrdebuger.exe` 已更新
- **实测**：unlock 后 cfg_features=0xFF、feat=255、magic=0x54455354；FLASH 开机链路 state 0→3→1 全程 SWD 真机验证（GPIOA 模拟 BT1 短按进 FLASH，双键 1.6s 松开正常开机）

## 更新记录（2026-08-10 深夜，固件 V4.3.2 + 调试器：虚拟按键真实语义 + 连接自动清理）
- **固件 V4.3.2**：虚拟按键注入条件由「TEST 态 + magic」放宽为「授权(magic+host_version)后任意状态生效」，与真实按键走同一套去抖/事件状态机；TEST 态保持当前模式——ALS 就是 ALS（走真实 OPT3001 传感器曲线）、MAN 就是 MAN（走档位亮度），不再被测试态强制切模式；`+` 键 MAN 加档 / ALS 偏移+，`-` 键 MAN 减档 / ALS 偏移-，双键 5s 切换 ALS<->手动，均与真实按键一致
- **调试器自动清理**：OpenOCD 连接失败/初始化失败时自动 `taskkill` 残留 openocd.exe 进程（上次崩溃/异常遗留、占用 XDS110 或 3334 端口导致连不上），限频 5s 防抖，清理后自动重启 openocd 重连；IPC `key` 命令与 UI 虚拟按键一致，非 TEST 态先自动解锁
- **产物**：`firmware/RMR/hex/RMR_DIRECT.hex`（`V4.3.2_DIRECT`）、`RMR_BATT.hex`（`V4.3.2_BATT`）；`RMR_Factory_Tool_V2.0/rmrdebuger.exe` 已更新
- **实测**：XDS110 + MSPM0C1104 上验证 MAN 加减档、ALS 偏移加减、双键 5s 模式切换、模式保持；模拟残留 openocd 占用探针后重启调试器，自动清理并重连成功

## 更新记录（2026-08-10，固件 V4.3.1 双版本：直连 / 2xSR516SW 电池）
- **双版本编译**：`app_config.h` 新增编译期电源模式宏 `POWER_SOURCE_DIRECT`——`1`=稳压电源直连版（1.6-3.6V 可调电源 + 120-635Ω 可调限流电阻，不算电池内阻），`0`=电池版（2x SR516SW 串联 3.1V 标称，计入电池内阻）
- **产物**：`firmware/RMR/hex/RMR_DIRECT.hex`（版本串 `V4.3.1_DIRECT`）、`firmware/RMR/hex/RMR_BATT.hex`（版本串 `V4.3.1_BATT`）；出厂默认 `r_series`：直连=360000mΩ（限流电阻），电池=410000mΩ（360Ω 限流 + 2×25Ω 电池内阻，SR516SW 规格书未标注内阻，取氧化银纽扣电池典型值，可经调试器写 NVM `r_series` 校准）
- **器件规格核对**：LED=APTD1608SECK/J4-PF（超亮橙 AlGaInP，VF 2.2V 典型/2.8V max @20mA，IF DC 30mA max）——固件 `HW_LED_FORWARD_V_MV=2200`、`HW_LED_MAX_CURRENT_UA=2800` 与规格书一致；电池=SR516SW（1.55V、12.5mAh、标准放电 20µA）；MCU=MSPM0C1104（Cortex-M0+，10-bit ADC，1.4V 内部 VREF，无 SysTick 用 GPTIMER14）
- **注意**：升级烧录后若 NVM 已有旧配置，`r_series` 等参数保留旧值；需"恢复出厂"或调试器写入以应用新默认参数
## 更新记录（2026-08-10，调试器：接入 Power-Z KM003C 真实电压电流计）
- **硬件接入**：RMRDebugger 新增 Power-Z KM003C（VID 0x5FC9 / PID 0x0063）真实电压电流计支持，通过 Windows HID 接口（Basic Mode，免驱动）以 200ms 间隔轮询 ADC（GetData 0x0C → PutData 0x41，44 字节 AdcDataRaw）
- **遥测显示**：监视器固定长条新增一行 6 位小数实时数据：真实电压 V / 真实电流 A / 实时功耗 W（=V×I）/ 平均电压 V / 平均电流 A / 温度 °C；连接状态实时指示（绿=已连接，红=断开，拔插自动重连）
- **IPC 扩展**：新增 `{"type":"powerz", ...}` 广播帧（`vbus_v` / `ibus_a` / `vbus_avg_v` / `ibus_avg_a` / `power_w` / `temp_c`），`hello` 响应新增 `powerz` 字段（含 connected 与最新值），供外部工具与自动化直接读取
- **新增源码**：`firmware/RMRDebugger/PowerZWorker.{h,cpp}`（HID 枚举/读写/解析/重连），CMake 链接 `hid`/`setupapi`
# tiny-rmr-1-2.05 — RMR 智能照明控制项目

基于 **TI MSPM0C1104**（Cortex-M0+）的低功耗智能照明/测试设备完整项目，包含：

- `firmware/RMR` — 设备固件（CCS 21 工程，tiarmclang，当前版本 **V4.3.3_DIRECT / V4.3.3_BATT**）
- `firmware/RMRDebugger` — 上位机调试器（Qt 6.11 + MinGW，支持 XDS110 SWD）
- `RMR_Factory_Tool_V2.0` — 已打包的出厂调试工具（含 Qt 运行库 / OpenOCD / USB 驱动）
- `PCB` / `model` / `image` — 硬件、结构与渲染资料

---

## 更新记录（2026-08-10，V4.3.1 增量 7：1ms 硬件时基 GPTIMER14）
- **根因**：MSPM0C1103/1104 硬件上没有 SysTick（TI E2E 确认，寄存器写入无效、读取恒为 0）——此前用 SysTick_Config 做 1ms 时基实际永不触发，所有毫秒计时（按键 5s 模式切换 / 1.5s 关机 / LVP / ALS 10s 自恢复等）全部卡死或异常
- **修复**：改用空闲的 **GPTIMER14** 周期中断做 1ms 硬件时基（24MHz BUSCLK，period=23999），g_tick_ms 由 TIMG14_IRQHandler 驱动；接口不变，所有基于 g_tick_ms 的计时恢复正常
- **实测**：设备复位后 g_tick_ms 每秒稳定递增，调试信箱实时刷新正常

## 更新记录（2026-08-11 凌晨，V4.3.1 增量 6）

- **虚拟按键与真实按键语义完全一致**：`[+]`/`[-]`/`[+&-]` 改为真实 `pressed`/`released` 路径——按住=注入按下（同时拦截物理键），松开=真正松开（解除拦截）；按下时长=实际按住时长（按钮实时显示 `PRESSED x.x s`）；快速点击 <120ms 补足到最短有效时长（等效真实按键去抖，防止 SWD 写队列延迟吞掉短按）
- **状态与操作合并显示**：监视器条原 `Btn[-]`/`Btn[+]` 两个状态格移除，按键状态直接显示在操作按钮上——注入按住=红底 `PRESSED+秒数`、注入未按住=`OVR`、物理键按下=琥珀底 `PHY`、均无=默认；修复 PHY 状态极性判断（固件 raw_key=1 表示按下，此前 `!raw` 判定正好相反）
- **State 格显示 ALS/手动**：`RUN`/`TEST` 状态后追加 ` ALS`/` MAN`（如 `RUN MAN`、`TEST ALS`），由固件每 tick 同步的 params 位 8 驱动
- **未解锁自动进入测试态**：虚拟按键按下时若设备不在 TEST 态自动执行解锁（ENTER_TEST），保证按下即有响应，与真实按键行为一致
- **松开丢失看门狗**：按键按住期间每 100ms 用系统物理鼠标状态（`GetAsyncKeyState`，而非 Qt 内部可能卡住的状态）检测——若左键已松开但按钮仍标记按住（松开事件丢失/窗口失焦/外部注入），自动强制真正松开，避免虚拟键在固件侧被持续注入、误触发 5s 双键模式切换
- **连接时清空残留覆盖**：切换/激活探针时自动写 0 清空 `ovr_key_minus/plus/block/als_en/led_mode`，防止上次会话残留的覆盖位继续作用于固件
- **固件**：TEST 态 LED 按键输出与真实模式一致（ALS 走 `mode_task()`、手动走 `CFG_BRT_MAP`），TEST 态按键分发补齐 `EVT_BOTH_LONG_5S`


## 更新记录（2026-08-10 深夜，V4.3.1 增量 6：电压降额 + PWM 补偿）
- **根因修复**：`battery_brt_to_pwm` 原来用 `min(i_peak, i_max)` 计算满亮度电流，3.0V 时峰值电流约 2.1mA 恒低于 2.8mA 上限，导致 PWM 占空比恒等于 brt、与电压无关；已改为恒流模型 `i_req = brt*i_max/1000`，`duty = i_req/i_peak`，电压下降时占空比自动升高维持亮度
- **电压线性降额**：新增 `V_DERATE_FULL_MV 3300`；`battery_get_safe_brt` 在 2400mV→30、3300mV→1000 之间按电压线性缩放安全亮度上限，低电压下自动限制档位与亮度（实测 @2.88V safe≈547）
- **刷新修复**：删除 BRT 缓存，电压变化（30mV 以上）不再冻结显示；LVP 分支同样刷新 `g_safe_brt_out`
- **构建根治**：CCS21 工程改用 post-build `tiarmobjcopy -O ihex` 生成 HEX（停用 tiarmhex），根治版本段错误；`.cproject` 已更新
## 更新记录（2026-08-10 晚，V4.3.1 增量 5）
- **固件 brt→PWM 全量程线性修复**：`battery_brt_to_pwm` 新增 `min(i_peak, i_max)` 钳位，`brt 0-1000` 从头到尾线性映射到 0-100%（此前烧录版本在 brt≈980 就提前饱和 100%）；同时修复 WALL 电压下限计算的 uint32 下溢（低电压时功率墙失效问题）
- **LVP_CRIT 迟滞自恢复**：进入 `SYS_LVP_CRIT` 后当电压回升 +100mV（`BATT_STARTUP_HYSTERESIS_MV`）自动恢复 `SYS_RUN`，根治低电压时的 2s 周期債动闪烁（明亮每 2s 瞬间暗一下）和长期卡在 LVP 状态问题
- **调试器 PWM 显示**：监视栏 PWM 改为“当前值 / 2399”，与寄存器原始值对应
- **图表实时刷新修复**：图表数据改用批量 `replace()` 裁剪窗口外点（避免逐个 `remove(0)` 触发 QtCharts 重排异常），并新增 200ms 定时强制重绘；Y 轴自适应新增最小跨度（电压 250mV / 光照 2000 / PWM·BRT 200），避免 raw ADC 噪声被放大成尖峰
- **进入测试态不再闪灭**：调试授权（unlock）进入 TEST 时保持当前亮度（ovr_brt_val 默认取 g_current_brt），修复此前 ovr_brt_val=0 导致进入测试模式瞬间 LED 熄灭闪烁的问题
- **调试邮箱 cfg_params 每 tick 同步**：固件每次主循环刷新邮箱镜像，调试器监视器新增 “Params” 格实时显示 params hex，点虚拟按键 +/- 立即可见挡位/ALS偏移变化（此前只是进入测试态时的快照）
- **虚拟按键功能修复（图形界面）**：修复布局重构时丢失的 `vOvr->addLayout(lKey)`，软件按键 [+]/[-]/[+&-] 与 Tap 版本恢复可见；拆分为两行（按键行 + Hold/Tap 行）避免宽度溢出；修复 `&` 被 Qt 当作快捷键前缀导致按钮显示 [+-] 的问题（改为 [+&&-] 显示 [+&-] ）
- **OpenOCD 端口修复**：Tcl 端口改用 `3334 + 探针序号`，避开 Windows Hyper-V/WinNAT 动态排除端口段（6515-6714 ，含 6666），修复 XDS110 无法绑定 Tcl 端口导致连接失败的问题

## 更新记录（2026-08-10 晚，V4.3.1 增量 4）
- **固件硬件功率估算**：调试邮箱新增 `est_hw_power_uw`（0xB4）= 原始电池电压 x 当前 PWM 反推平均电流，GUI 显示 mW 小数点后三位
- **ALS 最低亮度**：`ALS_MIN_BRT` 50 → 30，无论安装环境多暗亮度不低于 brt=30
- **调试器图表重构**：拆分为三张图一行（电压 / 环境亮度 / PWM+BRT），X 轴自动滚动显示最近400点窗口，Y 轴比例尺自适应（按窗口数据 min/max + 8% 边距）
- **软件按键触发**：GUI 新增 [+&-] 同时按键（长按 1.5s = OFF 开机 / RUN 关机，5s = ALS ↔ 手动切换）；所有软件按键按下时自动拦截物理按键，松开后解除；IPC `key` 支持 plus/minus/both + block 参数

更新记录（2026-08-10 晚，V4.3.1 增量 3）
- **固件 LED 亮度映射修复**：`HW_LED_MAX_CURRENT_UA` 出厂默认由 30000 修正为 **2800 uA**（贴实测峰值电流 ~2744 uA），根治 `brt>=91` 后 PWM 占空比即饱和 100%、ALS 亮度变化不可见的问题；`brt 0-1000` 现在全程线性映射（brt=50→5%、100→10%、300→30%、1000→100%）

更新记录（2026-08-10 下午，V4.3.1 增量 2）
- **调试器布局**：左右两列改为可拖动 QSplitter；所有分栏（全局/右侧日志列/图表与列/双列）支持鼠标拖动调整比例并记忆（归一化比例存储 + 页签切换/窗口缩放时延迟应用，不依赖页签可见性）
- **ALS 亮度映射预览**：“Fake Lux”注入时实时显示预期 Brt%/PWM，参考固件 `als_lux_to_brt` 公式（7*sqrt 曲线 + 5 档偏移 + 低/高量程钳位），依据当前配置计算
- **IPC 调试接口增强**：新增 `tab`（切换页签）、`autotest`、`calib` 命令；`led` 兼容 `value/pwm/brt` 字段；`layout` 支持比例设置/读取
- **AUTO-TEST 修复**：亮环境注入改为 50000 lux（原 5000 在最低亮度 50 时无法区分 dark/bright）；PWM 判定条件反转（寄存器值越小越亮）
- **固件修复**：cmd6 从 TEST→RUN 时补调 `mode_init()`，避免退出测试模式后 ALS 标志未初始化导致无法自动感光

## 更新记录（2026-08-10，V4.3.1 增量）
- **固件**：调试信箱新增 batt_raw_mv（偏移 0xB0，每 tick 镜像原始 ADC 电压，未滤波）
- **调试器**：SEND_SYS_CMD(3) 写配置改为“仅写 map 提供的字段”，空 map 不再清 NVM；cfg_params 只改低 8 位默认档位，保留 ALS(bit8)/offset(bit16-23) 高位
- **调试器 UI**：实时遥测改为固定高度长条（不随窗口缩放）；图表缩小为固定高度并限制 400 点数（防长期运行卡死）；电压显示原始 ADC 值（不平滑）

## 固件 V4.3.1_PROD

### 核心功能
- **亮度控制**：9 档亮度（`CFG_BRT_MAP`），按键短按调档；无限制亮度/PWM 注入（测试模式）
- **ALS 环境光自适应**：OPT3001 光照传感器，滤波 + 高低阈值钳位，亮度下限 `als_min_brt`（默认 30）
- **电池管理**：峰值/平均电流估算（峰值 × 实际 PWM 占空比）、动态内阻、LED 电压/功率估算、安全亮度上限
- **LVP 低电压保护**：临界/扩展两级保护，**5 次计数去抖**，临界进入 `SYS_LVP_CRIT`，扩展触发闪烁提示
- **ALS 故障保护**：连续 **3 次** 读取失败进入 `SYS_ALS_ERR`（LED 闪烁提示），**10 秒后自动恢复**
- **NVM 掉电保存**：配置掉电保存（`nvm_mark_dirty` + 后台写 Flash）
  - FNV-1a CRC32 写入后校验，失败 **3 次重试**
  - **多槽位**（`NVM_MAX_SLOTS`）轮换写入 + 坏块标记 + 磨损均衡
  - 擦写耗时补偿，保存计数 `seq_id`、槽位/扇区地址上报调试信箱
- **状态机**：`SYS_OFF → SYS_RUN → SYS_LVP_CRIT / SYS_FLASH_MODE / SYS_TEST_MODE / SYS_ALS_ERR`，按键长按组合支持 ALS 模式切换、恢复出厂
- **测试模式（Mailbox）**：SWD 写 `0x54455354` 授权进入，支持：
  - `cmd 2` 复位 / `cmd 3` 写配置（mask 过滤）/ `cmd 4` 保存 / `cmd 5` 恢复出厂+保存 / `cmd 6` 开机退出 / `cmd 7` 关机退出
  - 授权丢失 → 立即复位；正常退出 → `SYS_FLASH_MODE`
  - 按键/ALS/亮度/PWM 注入，物理按键屏蔽
- **烧录后清存储区**：配置项 `DEV_CLEAR_NVM_ON_POR`（1 = 烧录后首次上电清空存储区恢复出厂，默认 0）

### 调试信箱（Test Mailbox，SRAM 0x20000000）
每 tick 刷新实时数据供调试器读取：magic/版本、命令与应答、电压电流估算、档位/亮度/PWM、安全亮度、ALS 原始/滤波值、传感器状态与错误计数、按键状态、状态机镜像、NVM 状态（dirty/失败计数/序列/槽位）、全部配置镜像（`cfg_*`）、固件版本串。

### 构建
1. 用 CCS 21 导入 `firmware/RMR`（需 MSPM0 SDK，`ti_msp_dl_config` 由 syscfg 生成）
2. 根目录 `makefile.init` 已加入 post-build 钩子：链接后用 **tiarmobjcopy** 重新生成 Intel HEX，根治 `tiarmhex` 的 `.fw_version` 段内容错误
3. 命令行构建（Debug 目录）：

```bat
set PATH=C:\ti\ccs2100\ccs\utils\bin;C:\ti\ti_cgt_arm_llvm_4.0.2.LTS\bin;%PATH%
cd Debug
gmake -f makefile all
```

产物：`Debug/RMR.hex`（亦同步为仓库根 `firmware/RMR.hex`）。

---

## 调试器 RMRDebugger（Qt 6.11）

### 功能
- **XDS110 / DAPLink 自动扫描**（OpenOCD SWD，默认 100 kHz，可 5 kHz–4 MHz 切换）
- **实时遥测**：150 ms 轮询（可调），显示电压/电流/功率/内阻/档位/亮度/PWM/ALS/NVM 等全部信箱字段，图表实时刷新
- **烧录**：一键擦除+编程+校验+复位（`program verify reset`），进度/日志全显示
- **测试模式**：解锁（授权进入）、读配置、写配置（mask 过滤）、保存、恢复出厂+保存、开机/关机退出、软复位
- **注入调试**：模拟按键（+/- 短按）、ALS 注入（开/关 + lux）、LED 注入（PWM 直驱 / 无限制亮度 / 受限亮度）
- **隐藏 IPC 接口**：`127.0.0.1:7345`，JSON 行协议，供外部脚本/自动化实时读写（见下）

### 构建
```bat
set PATH=C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%
cmake -S firmware\RMRDebugger -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
运行前确保 `openocd.exe` 与 XDS110 驱动可用；程序内可配置 OpenOCD 路径与脚本目录。

### IPC 接口（127.0.0.1:7345）
连接后立即收到 `hello`（含探针列表/当前状态/最近遥测）。请求/响应均为单行 JSON，换行分隔。

命令（`{"cmd": ...}`）：

| cmd | 参数 | 说明 |
|---|---|---|
| `ping` | — | 健康检查，返回 `pong` |
| `state` | — | 重新发送 `hello` |
| `scan` | — | 重新扫描探针 |
| `setfw` | `path` | 设置固件路径 |
| `flash` | `path?` | 烧录（含校验） |
| `unlock` | — | 进入测试模式 + 读配置 |
| `syscmd` | `n` | 系统命令 2/3/4/5/6/7 |
| `cmd` | `type,arg1,arg2,map` | 通用命令（WRITE_8/16/32、READ_CFG、SEND_SYS_CMD…） |
| `write` | `ofs,val,size` | 写信箱偏移 |
| `read` | `ofs,size` | 读信箱偏移，回 `memread` 事件 |
| `poll` | `enabled,intervalMs` | 启停轮询 |
| `speed` | `khz` | 切换 SWD 速度 |
| `key` | `tap(1=+),holdMs` | 模拟按键 |
| `power` | `state(on/off)` | 开机/关机并退出测试模式 |
| `led` | `mode,value` | LED 注入（1=PWM、2=无限制亮度、3=受限亮度） |
| `als` | `enabled,lux` | ALS 注入 |

事件（`{"type": ...}`）：`hello`、`reply`、`status`、`uuid`、`fwver`、`progress`、`log`、`msg`、`config`、`telemetry`、`memread`、`probe_added`、`probe_removed`、`active`。

示例：

```json
{"cmd":"unlock"}
{"cmd":"flash"}
{"cmd":"poll","enabled":true,"intervalMs":150}
```

---

## 烧录方法（XDS110）

```bat
openocd -s <openocd>\scripts -f interface/xds110.cfg -f target/ti_mspm0.cfg ^
  -c "transport select swd" -c "adapter speed 1000" ^
  -c "tcl_port 3334" -c "gdb_port disabled" -c "telnet_port disabled"
```
TCL 端口使用 **3334**（避开 Windows Hyper-V 保留段 6515–6714）。也可直接在调试器 GUI 中一键烧录。

---

## 目录结构
```
tiny-rmr-1-2.05/
├── firmware/
│   ├── RMR/            # MSPM0C1104 固件工程（CCS 21）
│   ├── RMRDebugger/    # Qt 调试器源码
│   └── RMR.hex         # 当前发布固件镜像（V4.3.1）
├── RMR_Factory_Tool_V2.0/  # 打包版调试工具（免编译）
├── PCB/                # 原理图/PCB/打样文件
├── model/  image/      # 结构与渲染资料
└── README.md
```



