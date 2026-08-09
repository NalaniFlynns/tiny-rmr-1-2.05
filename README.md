# tiny-rmr-1-2.05 — RMR 智能照明控制项目

基于 **TI MSPM0C1104**（Cortex-M0+）的低功耗智能照明/测试设备完整项目，包含：

- `firmware/RMR` — 设备固件（CCS 21 工程，tiarmclang，当前版本 **V4.3.1_PROD**）
- `firmware/RMRDebugger` — 上位机调试器（Qt 6.11 + MinGW，支持 XDS110 SWD）
- `RMR_Factory_Tool_V2.0` — 已打包的出厂调试工具（含 Qt 运行库 / OpenOCD / USB 驱动）
- `PCB` / `model` / `image` — 硬件、结构与渲染资料

---

## 固件 V4.3.1_PROD

### 核心功能
- **亮度控制**：10 档亮度（`CFG_BRT_MAP`），按键短按调档；无限制亮度/PWM 注入（测试模式）
- **ALS 环境光自适应**：OPT3001 光照传感器，滤波 + 高低阈值钳位，亮度下限 `als_min_brt`（默认 50）
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
