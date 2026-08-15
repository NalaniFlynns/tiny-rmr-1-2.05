#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_
#include "ti_msp_dl_config.h"
#include <stdint.h>
#include <stdbool.h>

/* ==================== 固件信息 ==================== */
/* ==================== 固件信息 ==================== */
/* 电源模式(编译期):
   POWER_SOURCE_DIRECT = 1 -> 稳压电源直连版: 1.6-3.6V 可调电源 + 120-635Ω 可调限流电阻, 不算电池内阻
   POWER_SOURCE_DIRECT = 0 -> 电池版: 2x SR516SW 串联(3.1V 标称, 12.5mAh/节), 计入电池内阻 */
#ifndef POWER_SOURCE_DIRECT
#define POWER_SOURCE_DIRECT 1
#endif
/* 省电版(ECO): 1=关闭一切非必要开销(生产电池供电):
   WFI 睡眠替代 24MHz 忙等 / 关闭调试箱每 tick 实时刷新 / ADC+VREF 采样间隙断电 /
   OPT3001 手动模式与 OFF 态 shutdown / ALS 与 ADC 轮询降频 / 出厂默认 OFF 态进 STANDBY1 深睡 */
#ifndef POWER_SAVE_BUILD
#define POWER_SAVE_BUILD 0
#endif
/* 调试版: 1=测试板直供调试专用, 全程 SWD 可访问(不进 STANDBY1 深睡/不执行掉电 SHUTDOWN), 其余特性(ECO 降频/NVM 等)不变 */
#ifndef DEBUG_BUILD
#define DEBUG_BUILD 0
#endif
/* 低功耗调试版: 1=OFF 态进 WFI/STANDBY1 彻底低功耗(忽略 NVM SWD 位, SWD 自然断开), 开机/运行态 SWD 保持, 其余特性不变 */
#ifndef DEBUG_LP_BUILD
#define DEBUG_LP_BUILD 1
#endif

/* 量产正式版: 默认禁用 SWD + NRST(寄存器写后仅 POR 可恢复).
   每次复位后 PROD_SWD_BOOT_WINDOW_MS 窗口内保持 SWD/NRST 开放, 供 BT1 0.8s 进 FLASH_MODE 烧录;
   窗口结束且不在 FLASH_MODE 即一次性禁用, 之后只有重新上电才能再连 SWD.
   DBG/DBGL 调试版不启用, 保证调试期 SWD 全程可连. */
#if !DEBUG_BUILD && !DEBUG_LP_BUILD
#ifndef FEATURE_PROD_SWD_DISABLE
#define FEATURE_PROD_SWD_DISABLE 1
#endif
#else
#define FEATURE_PROD_SWD_DISABLE 0
#endif
#define PROD_SWD_BOOT_WINDOW_MS     5000

/* SR516SW 单节内阻(mΩ): 规格书未标注, 取氧化银纽扣电池典型值, 可经调试器写 NVM r_series 校准 */
#define BATT_SR516SW_SINGLE_R_MOHM  25000

#if DEBUG_BUILD
#define FW_VERSION_STR "V4.3.5_DBG"
#define CFG_DEFAULT_R_SERIES_MOHM   HW_SERIES_R_MOHM
#elif DEBUG_LP_BUILD
#define FW_VERSION_STR "V4.3.5_DBGL"
#define CFG_DEFAULT_R_SERIES_MOHM   HW_SERIES_R_MOHM
#elif POWER_SAVE_BUILD
#if POWER_SOURCE_DIRECT
#define FW_VERSION_STR "V4.3.5_ECO_D"
#define CFG_DEFAULT_R_SERIES_MOHM   HW_SERIES_R_MOHM
#else
#define FW_VERSION_STR "V4.3.5_ECO_B"
#define CFG_DEFAULT_R_SERIES_MOHM   (HW_SERIES_R_MOHM + 2 * BATT_SR516SW_SINGLE_R_MOHM)
#endif
#else
#if POWER_SOURCE_DIRECT
#define FW_VERSION_STR "V4.3.5_DIRECT"
#define CFG_DEFAULT_R_SERIES_MOHM   HW_SERIES_R_MOHM
#else
#define FW_VERSION_STR "V4.3.5_BATT"
#define CFG_DEFAULT_R_SERIES_MOHM   (HW_SERIES_R_MOHM + 2 * BATT_SR516SW_SINGLE_R_MOHM)
#endif
#endif
#define CFG_DEFAULT_R_BASE_MOHM     R_DYNAMIC_BASE_MOHM

/* ==================== [0] 出厂默认配置 ==================== */
#define DEV_FORCE_FACTORY_RESET         0       /* 1: 每次上电强制恢复出厂并保存 */
#define DEV_CLEAR_NVM_ON_POR            0       /* 1: 烧录后首次上电(POR)清空存储区恢复出厂 */
#define CFG_DEFAULT_ALS_EN              0
#define CFG_DEFAULT_LEVEL               4
#define CFG_DEFAULT_ALS_OFFSET          2

/* ==================== [1] 编译期功能开关 ==================== */
#define FEATURE_VOLTAGE_COMPENSATION    1
#define FEATURE_ADAPTIVE_GEAR_LIMIT     1
#define FEATURE_KEY_DEBOUNCE            1
#define FEATURE_ALS_MODE                1
#define FEATURE_ALS_OFFSET_ADJUST       1
#define FEATURE_ALS_SMOOTHING           1
#define FEATURE_INACTIVITY_AUTO_DIM_OFF 1
#define FEATURE_MEMORY_SAVE             1
#define FEATURE_SAVE_VERIFY             1
#define FEATURE_SAVE_RAM_SHADOW         1
#define FEATURE_LOWPOWER_STANDBY        1
#define FEATURE_LVP_FLASH_WARNING       1
#define FEATURE_FLASH_ANIMATION         1
#define FEATURE_RESTORE_NRST_IN_FLASH   1
#define FEATURE_AUTO_POWER_ON           1
#define FEATURE_SWD_IN_OFF_STATE        1

/* ==================== [2] 运行时功能标志位 ==================== */
#define FLAG_VOLTAGE_COMPENSATION       (1 << 0)
#define FLAG_ADAPTIVE_GEAR_LIMIT        (1 << 1)
#define FLAG_ALS_MODE                   (1 << 2)
#define FLAG_INACTIVITY_AUTO_DIM        (1 << 3)
#define FLAG_LOWPOWER_STANDBY           (1 << 4)
#define FLAG_LVP_FLASH_WARNING          (1 << 5)
#define FLAG_SWD_IN_OFF_STATE           (1 << 6) 
#define FLAG_AUTO_POWER_ON              (1 << 7) 

#define FEATURE_RUNTIME_MASK ( \
    (FEATURE_VOLTAGE_COMPENSATION ? FLAG_VOLTAGE_COMPENSATION : 0) | \
    (FEATURE_ADAPTIVE_GEAR_LIMIT ? FLAG_ADAPTIVE_GEAR_LIMIT : 0) | \
    (FEATURE_ALS_MODE ? FLAG_ALS_MODE : 0) | \
    (FEATURE_INACTIVITY_AUTO_DIM_OFF ? FLAG_INACTIVITY_AUTO_DIM : 0) | \
    (FEATURE_LOWPOWER_STANDBY ? FLAG_LOWPOWER_STANDBY : 0) | \
    (FEATURE_LVP_FLASH_WARNING ? FLAG_LVP_FLASH_WARNING : 0) | \
    (FEATURE_SWD_IN_OFF_STATE ? FLAG_SWD_IN_OFF_STATE : 0) | \
    (FEATURE_AUTO_POWER_ON ? FLAG_AUTO_POWER_ON : 0))

#if DEBUG_BUILD
/* 调试版: 出厂默认 OFF 态保持 SWD 可访问(与代码级 SWD 保活一致) */
#define DEFAULT_FEATURE_FLAGS (FEATURE_RUNTIME_MASK | FLAG_SWD_IN_OFF_STATE)
#elif DEBUG_LP_BUILD
/* 浣庡姛鑰?璋冭瘯鐗? 鍑哄巶榛樿?娓?SWD 浣?-> OFF 鎬佽繘 WFI/STANDBY1 娣辩潯(鏈€鐪佺數, SWD 鏂紑鍙帴鍙? */
#define DEFAULT_FEATURE_FLAGS (FEATURE_RUNTIME_MASK & ~FLAG_SWD_IN_OFF_STATE)
#elif POWER_SAVE_BUILD
/* 省电版: 出厂默认清 SWD 保活位 -> OFF 态进 STANDBY1 深睡(µA 级), 代价是 OFF 态 SWD 不可访问 */
#define DEFAULT_FEATURE_FLAGS (FEATURE_RUNTIME_MASK & ~FLAG_SWD_IN_OFF_STATE)
#else
/* 量产版: 清 SWD 保活位 -> OFF 态进 STANDBY1 深睡(即使编译为非省电版; SWD 由 FEATURE_PROD_SWD_DISABLE 统一门控) */
#define DEFAULT_FEATURE_FLAGS (FEATURE_RUNTIME_MASK & ~FLAG_SWD_IN_OFF_STATE)
#endif

/* ==================== [3] 硬件引脚分配 ==================== */
#define PORT_I2C                SW_I2C_PORT
#define PIN_SW_SDA              SW_I2C_SW_SDA_PIN
#define PIN_SW_SCL              SW_I2C_SW_SCL_PIN
#define PORT_OUTPUT             OUTPUT_PINS_PORT
#define PIN_VCC_EN              OUTPUT_PINS_VCC_EN_PIN
#define PORT_BTN                BUTTONS_PORT
#define PIN_BT1                 BUTTONS_BT1_PIN
#define PIN_BT2                 BUTTONS_BT2_PIN
#define HW_PWM_INST             PWM_0_INST
#define HW_PWM_INDEX            DL_TIMER_CC_0_INDEX
#define HW_ADC_INST             ADC12_0_INST

/* ==================== [4] 系统与存储 ==================== */
#define MCU_CPU_FREQ_MHZ        24
#define CPU_CYCLES_PER_MS       (24000)
#define SYS_TICK_PERIOD_MS      1
#define EN_WWDT                 1
#define FLASH_SECTOR_A_ADDR     0x00003800
#define FLASH_SECTOR_B_ADDR     0x00003C00
#define FLASH_SECTOR_SIZE       1024
#define NVM_SLOT_SIZE           64
#define NVM_MAGIC               0xAA55AA5C 
#define NVM_OFF_INTENT_MARK      0x5A      /* reserved[0] shutdown intent sentinel: 0x5A=off, 0x00=running */
#define EN_AUTO_SAVE            1
#define FLASH_ERASE_TIME_MS     32
#define FLASH_PROG_TIME_MS      2
#define FLASH_RETRY_COUNT       3
#define FLASH_SLOT_ATTEMPTS     3
#define NVM_SAVE_MAX_RETRIES      5
#define TIME_NVM_AUTO_SAVE_DELAY_MS     30000
#define TIME_NVM_FORCE_SAVE_MS          600000
#define TIME_FLASH_MODE_TIMEOUT_MS      300000

/* STANDBY1 唤醒: 仅 WUEN/WCOMP 异步 IO 电平比较 + GPIO 边沿中断(实测可靠).
   曾加 TIMG14 32k 周期轮询(LOAD=24000, ~750ms)兜底, 每 750ms 产生 ~20uA 短尖峰;
   实测 WUEN 唤醒无失败且重新上电可恢复, 量产省电移除: 深睡前停 TIMG14 表. */

/* ==================== [5] 物理模型参数 ==================== */
#define PWM_REG_MAX             2399 
#define BRT_SCALE_MAX           1000
#define MCU_VREF_MV             1400
#define HW_BATTERY_DIV_RATIO    3
#define VBATT_FULL_MV           (MCU_VREF_MV * HW_BATTERY_DIV_RATIO)
#define ADC_MAX_RESOLUTION      1023
#define ADC_GAIN_CAL            1000
#define ADC_TIMEOUT_MS          10
#define ADC_FILTER_SHIFT        3
#if POWER_SAVE_BUILD
#define TIME_ADC_READ_INTERVAL_MS   500   /* 省电版: 500ms(原 100ms), LVP 去抖 5 次 -> ~2.5s 响应 */
#else
#define TIME_ADC_READ_INTERVAL_MS   100
#endif
#define OPT3001_ADDR            0x44
#define OPT3001_REG_RESULT       0x00
#define OPT3001_REG_CONFIG       0x01
#define OPT3001_CFG_HI           0xC2
#define OPT3001_CFG_LO           0x10
#define OPT3001_MAX_LUX          8386500

#define R_DYNAMIC_BASE_MOHM     50
#define R_DYNAMIC_EXTRA_LOWV_MOHM 300000
#define R_DYNAMIC_EXTRA_CAP_MOHM  250000
#define R_DYNAMIC_DC_FACTOR_PCT   30
#define BRT_CACHE_DELTA_MV        30
#define BATT_STARTUP_HYSTERESIS_MV 100
#define R_DYNAMIC_FACTOR        50000000
#define R_DYNAMIC_OFFSET_MV     1900
#define BATT_MIN_WORK_V_MV      2000
#define BATT_LVP_EXTREME_MV     2100
#define BATT_POWER_LOSS_MV      2000
#define BATT_LVP_CRIT_MV        2300
#define POWERLOSS_COUNT         3
#define LVP_CRIT_COUNT          5
#define LVP_EXT_COUNT           5
#define EN_WALLS                1
#define WALL_MIN_V_BATT_MV      2400
#define BATT_MAX_DISCHARGE_UA   4000
#define BATT_MAX_DISCHARGE_UW   9000
#define LOW_BRT_GUARANTEE       30
#define V_DERATE_FULL_MV       3300      /* 电压 ≥ 此值时无电压降额, 低于则线性降至墙电压 */
#define HW_LED_FORWARD_V_MV     2200
#define HW_LED_MAX_CURRENT_UA   2800
#define HW_SERIES_R_MOHM        360000

/* ==================== [6] 自动感光配置 ==================== */
#define ALS_MAX_SLEW_RATE       20
#define ALS_SLEW_LOW             2
#define ALS_SLEW_MID             5
#define ALS_MIN_BRT             30
#define ALS_SQRT_FACTOR         5
#define ALS_CAP_BRT_LOW_LUX     600
#define ALS_CAP_BRT_HIGH_LUX    800
#if POWER_SAVE_BUILD
#define TIME_ALS_POLL_INTERVAL_MS   1000  /* 省电版: 1s(原 120ms), OPT3001 转换占空比 ~10% */
#else
#define TIME_ALS_POLL_INTERVAL_MS   120
#endif
#define ALS_ERR_FAIL_COUNT      3
#define ALS_ERR_LOCKOUT_COUNT  3    /* 连续 N 次 ALS 故障自恢复后锁定 ALS, 防传感器持续损坏时每 10s 闪一次循环 */
#define TIME_ALS_ERR_RECOVER_MS     10000
#define ALS_ERR_BLINK_PERIOD_MS     1500
#define ALS_ERR_BLINK_ON_MS         600

/* ==================== [7] 延时与超时配置 ==================== */
#define CFG_MAX_LEVELS          9
#define SNAP_THRESHOLD_BRT      50
#define TIME_AUTO_DIM_S         2400
#define TIME_AUTO_SHUTDOWN_S    3000   /* 40min 调暗后再无操作 50min 关机(总 90min) */
#define DIM_LEVEL               5
#define LVP_FLASH_PERIOD_MS     2000
#define LVP_FLASH_ON_TIME_MS    50
#define LVP_FLASH_LEVEL_DIV     4
#define PWM_FLASH_LEVEL         50
#define PWM_FADE_INTERVAL_MS    12
#define PWM_FADE_STEP_DIV       10

/* ==================== [8] 按键交互配置 ==================== */
#define KEY_DEBOUNCE_BITS       8
#define KEY_TIME_DEBOUNCE_MS    20
#define KEY_TIME_SHORT_0_8S_MS  800
#define KEY_TIME_SHORT_MAX_MS   1500
#define KEY_TIME_LONG_PRESS_MS  1500
#define KEY_TIME_FACTORY_RESET_MS 5000
#define TIME_SEC_MS               1000
#define STARTUP_FAIL_BLINK_DELAY_MS 100
#define LED_BLINK_BRT             300
#define TEST_MAGIC                0x54455354

/* ==================== [9] NVM 数据结构 ==================== */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t seq_id;
    uint32_t params;      
    uint32_t features;    
    uint32_t r_base;
    uint32_t r_series;
    uint32_t v_led_fw;
    uint32_t i_max_ua;
    uint32_t batt_p_uw;
    uint32_t als_min_brt;
    uint32_t lvp_crit;
    uint32_t lvp_ext;
    uint8_t  default_level;
    uint8_t  als_sqrt_factor;   /* 0 -> ALS_SQRT_FACTOR */
    uint8_t  als_cap_low_x100;  /* 0 -> ALS_CAP_BRT_LOW_LUX/100 */
    uint8_t  als_cap_high_x100; /* 0 -> ALS_CAP_BRT_HIGH_LUX/100 */
    uint8_t  reserved[8];
    uint32_t crc32;
} __attribute__((aligned(8))) NVM_Data_t;
#pragma pack(pop)

typedef enum { SYS_OFF = 0, SYS_RUN, SYS_LVP_CRIT, SYS_FLASH_MODE, SYS_TEST_MODE, SYS_ALS_ERR } SysState_t;

extern volatile uint32_t g_tick_ms;
extern volatile uint32_t g_rst_cause;
extern SysState_t sys_state;
extern NVM_Data_t sys_memory;
extern volatile uint32_t g_vbatt_mv_raw;
extern volatile uint32_t g_vbatt_mv_filtered;
extern volatile uint32_t g_inactivity_sec;
extern volatile bool g_is_dimmed;
extern volatile bool g_is_overshot;
extern const uint16_t CFG_BRT_MAP[CFG_MAX_LEVELS];
extern const int16_t AUTO_OFFSET_PCT[5];
#endif
