#include "ti_msp_dl_config.h"
#include <ti/driverlib/dl_flashctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#define FW_VERSION_STR "V1.0.6_PROD"

#if defined(__clang__) || defined(__GNUC__) || defined(__TI_COMPILER_VERSION__)
__attribute__((used, section(".fw_version")))
const char g_fw_version_flash[16] = FW_VERSION_STR;
#else
#pragma DATA_SECTION(g_fw_version_flash, ".fw_version")
#pragma RETAIN(g_fw_version_flash)
const char g_fw_version_flash[16] = FW_VERSION_STR;
#endif

// --- [0] 出厂默认配置 ---
#define DEV_FORCE_FACTORY_RESET         0       
#define CFG_DEFAULT_ALS_EN              0       
#define CFG_DEFAULT_LEVEL               4       
#define CFG_DEFAULT_ALS_OFFSET          2       

// --- [1] 核心功能编译 ---
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

// Debug
#define FEATURE_SWD_IN_OFF_STATE        1       

// --- [2] 运行时功能标志位 ---
#define FLAG_VOLTAGE_COMPENSATION       (1 << 0)
#define FLAG_ADAPTIVE_GEAR_LIMIT        (1 << 1)
#define FLAG_ALS_MODE                   (1 << 2)
#define FLAG_INACTIVITY_AUTO_DIM        (1 << 3)
#define FLAG_LOWPOWER_STANDBY           (1 << 4)
#define FLAG_LVP_FLASH_WARNING          (1 << 5)
#define FLAG_SWD_IN_OFF_STATE           (1 << 6) 

#define FEATURE_RUNTIME_MASK ( \
    (FEATURE_VOLTAGE_COMPENSATION ? FLAG_VOLTAGE_COMPENSATION : 0) | \
    (FEATURE_ADAPTIVE_GEAR_LIMIT ? FLAG_ADAPTIVE_GEAR_LIMIT : 0) | \
    (FEATURE_ALS_MODE ? FLAG_ALS_MODE : 0) | \
    (FEATURE_INACTIVITY_AUTO_DIM_OFF ? FLAG_INACTIVITY_AUTO_DIM : 0) | \
    (FEATURE_LOWPOWER_STANDBY ? FLAG_LOWPOWER_STANDBY : 0) | \
    (FEATURE_SWD_IN_OFF_STATE ? FLAG_SWD_IN_OFF_STATE : 0) | \
    (FEATURE_LVP_FLASH_WARNING ? FLAG_LVP_FLASH_WARNING : 0))

#define DEFAULT_FEATURE_FLAGS (FEATURE_RUNTIME_MASK)

// --- [3] 硬件引脚分配 ---
#define PORT_OUTPUT                     OUTPUT_PINS_PORT
#define PIN_VCC_EN                      OUTPUT_PINS_VCC_EN_PIN
#define PORT_BTN                        BUTTONS_PORT
#define PIN_BT1                         BUTTONS_BT1_PIN
#define PIN_BT2                         BUTTONS_BT2_PIN

#define KEY_PLUS_PORT                   PORT_BTN
#define KEY_PLUS_PIN                    PIN_BT2
#define KEY_MINUS_PORT                  PORT_BTN
#define KEY_MINUS_PIN                   PIN_BT1

#define PORT_I2C                        SW_I2C_PORT
#define PIN_SW_SDA                      SW_I2C_SW_SDA_PIN
#define PIN_SW_SCL                      SW_I2C_SW_SCL_PIN

#define HW_PWM_INST                     PWM_0_INST
#define HW_PWM_INDEX                    DL_TIMER_CC_0_INDEX
#define HW_ADC_INST                     ADC12_0_INST

// --- [4] 系统与存储 ---
#define MCU_CPU_FREQ_MHZ                24
#define SYS_TICK_PERIOD_MS              1
#define EN_WWDT                         1
#define FLASH_SECTOR_A_ADDR             0x00003800
#define FLASH_SECTOR_B_ADDR             0x00003C00
#define FLASH_SECTOR_SIZE               1024
#define NVM_SLOT_SIZE                   64
#define NVM_MAGIC                       0xAA55AA55
#define EN_AUTO_SAVE                    1
#define FLASH_ERASE_TIME_MS             32
#define FLASH_PROG_TIME_MS              2
#define FLASH_RETRY_COUNT               3

// --- [5] 物理模型参数 ---
#define MCU_VREF_MV                     1400
#define HW_BATTERY_DIVIDER_RATIO        3
#define VBATT_FULL_MV                   (MCU_VREF_MV * HW_BATTERY_DIVIDER_RATIO)
#define ADC_MAX_RESOLUTION              1023
#define ADC_GAIN_CAL                    1000
#define ADC_TIMEOUT_MS                  10
#define ADC_FILTER_SHIFT                3
#define R_DYNAMIC_BASE_MOHM             50
#define R_DYNAMIC_FACTOR                50000000
#define R_DYNAMIC_OFFSET_MV             1900
#define BATT_MIN_WORK_V_MV              2000
#define BATT_LVP_EXTREME_MV             2100
#define BATT_POWER_LOSS_MV              2000
#define BATT_LVP_CRIT_MV                2300
#define POWERLOSS_COUNT                 3
#define LVP_CRIT_COUNT                  5
#define LVP_EXT_COUNT                   5
#define EN_WALLS                        1
#define WALL_MIN_V_BATT_MV              2400
#define BATT_MAX_DISCHARGE_UA           4000
#define BATT_MAX_DISCHARGE_UW           9000
#define LOW_BRT_GUARANTEE               30
#define HW_LED_FORWARD_V_MV             2200
#define HW_LED_MAX_CURRENT_UA           30000
#define HW_SERIES_R_MOHM                360000
#define BRT_SCALE_MAX                   1000
#define PWM_REG_MAX                     2400
#define PWM_FADE_DIV                    16
#define PWM_FADE_MIN_STEP               1
#define PWM_FADE_FINISH                 8
#define CFG_MAX_LEVELS                  9
#define CFG_ENABLED_LEVELS              9
#define SNAP_THRESHOLD_BRT              50

const uint16_t CFG_BRT_MAP[CFG_MAX_LEVELS] = {5, 20, 50, 150, 300, 450, 600, 800, 1000};

// --- [6] 自动感光配置 ---
#define OPT3001_ADDR                    0x44
#define ALS_MAX_SLEW_RATE               20
#define ALS_MIN_BRT                     30

const int16_t AUTO_OFFSET_PCT[5] = {-30, -15, 0, 15, 30};

static inline uint16_t user_als_lux_to_brt_curve(uint32_t lux) {
    if (lux <= 10)   return (lux * 5);
    if (lux <= 100)  return 50 + ((lux - 10) * 2);
    if (lux <= 1000) return 230 + ((lux - 100) / 2);
    if (lux <= 10000)return 680 + ((lux - 1000) / 30);
    return 980;
}

// --- [7] 延时与超时配置 ---
#define TIME_ADC_READ_INTERVAL_MS       100
#define TIME_ALS_POLL_INTERVAL_MS       1000
#define TIME_ALS_POWERUP_DELAY_MS       50
#define TIME_ALS_CONVERT_WAIT_MS        850
#define TIME_I2C_FLIP_DELAY_US          5
#define TIME_AUTO_DIM_S                 3600
#define TIME_AUTO_SHUTDOWN_S            600
#define DIM_LEVEL                       5
#define TIME_NVM_AUTO_SAVE_DELAY_MS     30000
#define TIME_NVM_FORCE_SAVE_MS          600000
#define TIME_UI_FLASH_STEP_MS           50
#define FLASH_TOGGLE_COUNT              60
#define LVP_FLASH_PERIOD_MS             2000
#define LVP_FLASH_ON_TIME_MS            50
#define PWM_FLASH_LEVEL                 50

// --- [8] 按键交互配置 ---
#define KEY_DEBOUNCE_BITS               8
#define KEY_TIME_DEBOUNCE_MS            20
#define KEY_TIME_SHORT_MAX_MS           1000
#define KEY_TIME_LONG_PRESS_MS          1500
#define KEY_TIME_COMBO_TOLERANCE_MS     2000   
#define KEY_TIME_FACTORY_RESET_MS       5000

#define NVM_MAX_SLOTS (FLASH_SECTOR_SIZE / NVM_SLOT_SIZE)
#define POR_MAGIC 0x5AA5C33C

#if defined(__TI_COMPILER_VERSION__) || defined(__clang__)
uint32_t g_por_magic __attribute__((section(".TI.noinit")));
bool g_flash_mode_used __attribute__((section(".TI.noinit")));
#elif defined(__GNUC__)
uint32_t g_por_magic __attribute__((section(".noinit")));
bool g_flash_mode_used __attribute__((section(".noinit")));
#elif defined(__IAR_SYSTEMS_ICC__)
__no_init uint32_t g_por_magic;
__no_init bool g_flash_mode_used;
#else
uint32_t g_por_magic __attribute__((section(".noinit")));
bool g_flash_mode_used __attribute__((section(".noinit")));
#endif

typedef enum { SYS_OFF, SYS_UNLOCKING, SYS_RUN, SYS_LVP_CRIT, SYS_FLASH_MODE, SYS_ALS_ERR, SYS_TEST_MODE } SysState_t;
SysState_t sys_state = SYS_OFF;

// --- [9] 调试信箱 (Test Mode Mailbox) ---
typedef enum {
    TEST_ST_IDLE = 0,
    TEST_ST_BUSY = 1,
    TEST_ST_OK = 2,
    TEST_ST_ERR_VERSION = 3,
    TEST_ST_ERR_FLASH = 4,
    TEST_ST_ERR_INV_CMD = 5
} TestStatus_t;

typedef struct {
    uint32_t magic;           
    uint16_t version;         
    uint16_t host_version;    
    uint32_t cmd;             
    uint32_t cmd_ack;         
    uint32_t status;          

    uint32_t vbatt_mv;
    uint32_t est_i_peak_ua;
    uint32_t est_i_avg_ua;
    uint32_t est_v_led_mv;
    uint32_t est_p_led_uw;
    uint32_t dyn_r_mohm;

    uint16_t current_level;
    uint16_t current_brt_val;
    uint16_t current_pwm_val;
    uint16_t safe_brt_out;

    uint32_t limit_i_led;
    uint32_t limit_v_drop;
    uint32_t limit_i_brt;
    uint32_t limit_p_avg;

    uint32_t als_lux_raw;
    uint32_t als_lux_filtered;

    uint8_t  als_err_cnt;
    uint8_t  sensor_status;
    uint8_t  sys_state_mirror;
    uint8_t  raw_key_minus;     // [只读] 物理按键真实状态 (0或1)

    uint8_t  raw_key_plus;      // [只读] 物理按键真实状态 (0或1)
    uint8_t  state_is_dimmed;
    uint8_t  state_is_overshot;
    uint8_t  state_debug_mode; 

    uint8_t  nvm_is_dirty;
    uint8_t  ovr_block_phys_keys; // [读写] 为1时强制系统屏蔽外部物理按键干扰
    uint8_t  _pad0[2]; 

    uint32_t state_inactivity_sec;
    uint32_t nvm_seq_id;
    uint32_t nvm_sector_addr;
    uint32_t nvm_slot_idx;

    uint8_t  ovr_led_mode;      // 1: 绝对PWM | 2: 安全亮度0-1000 | 3: 绝对亮度0-1000(无视电池保护)
    uint8_t  ovr_key_minus;     // 1: 模拟短路按下
    uint8_t  ovr_key_plus;      // 1: 模拟短路按下
    uint8_t  ovr_als_en;
    
    uint16_t ovr_brt_val;       // 亮度目标参数值 (适用于 ovr_led_mode 2 或 3)
    uint16_t ovr_pwm_val;       // 绝对PWM参数值 (适用于 ovr_led_mode 1)
    uint32_t ovr_als_lux;

    uint32_t cfg_params;           
    uint32_t cfg_features;
    uint32_t cfg_r_base;
    uint32_t cfg_r_series;
    uint32_t cfg_v_led_fw;
    uint32_t cfg_i_max_ua;
    uint32_t cfg_batt_p_uw;
    uint32_t cfg_als_min_brt;
    uint32_t cfg_lvp_crit;
    uint32_t cfg_lvp_ext;
    
    char     fw_version_str[16];
} Test_Mailbox_t;

volatile Test_Mailbox_t g_test_box __attribute__((used)) = {
    .magic = 0,
    .version = 0x0100,
    .fw_version_str = FW_VERSION_STR
};

volatile uint32_t g_tick_ms = 0;
volatile uint32_t g_vbatt_mv_raw = 4200;
volatile uint32_t g_vbatt_mv_filtered = 4200;

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
    uint8_t  reserved[11];
    uint32_t crc32;
} NVM_Data_t;
#pragma pack(pop)

_Static_assert(sizeof(NVM_Data_t) == 64, "NVM_Data_t MUST be exactly 64 bytes.");
_Static_assert(offsetof(NVM_Data_t, crc32) == 60, "CRC offset error, expected 60 bytes.");

NVM_Data_t sys_memory;
NVM_Data_t flash_shadow;

uint32_t current_sector_addr = FLASH_SECTOR_A_ADDR;
uint32_t current_nvm_slot = 0;
bool nvm_dirty = false;
uint32_t nvm_dirty_start_tick = 0;

typedef struct {
    bool active;
    uint8_t count;
    bool led_on;
    uint32_t last_tick;
} FlashAnim_t;
FlashAnim_t g_anim = {0};

uint16_t current_pwm = 0;
uint16_t target_pwm = 0;
uint16_t current_brt_calculated = 0;
uint16_t g_last_applied_pwm = 0xFFFF;
uint32_t inactivity_sec = 0;
uint32_t last_user_action_tick = 0;
uint32_t flash_mode_start_tick = 0;
uint32_t als_err_start_tick = 0;

bool is_dimmed = false;
uint16_t virtual_max_brt = BRT_SCALE_MAX;
bool is_overshot = false;

static uint32_t brt_cache_last_vbatt = 0;
static uint16_t brt_cache_last_req = 0xFFFF;
static uint16_t brt_cache_val = 0;

static uint8_t hist_minus = 0xFF, hist_plus = 0xFF;
static uint32_t t_minus_ms = 0, t_plus_ms = 0, t_both_ms = 0;
static uint32_t max_both_ms = 0;
static bool prev_db_minus = false, prev_db_plus = false;
static bool anim_5s_done = false;

uint16_t calculate_safe_brt(uint16_t req_brt);
void set_brightness_target(uint16_t safe_brt);
void execute_shutdown(void);
void i2c_init(void);

void SysTick_Handler(void) { g_tick_ms++; }
void delay_us(uint32_t us) { delay_cycles((uint32_t)((uint64_t)MCU_CPU_FREQ_MHZ * us)); }

void kick_watchdog(void) {
#if EN_WWDT
    DL_WWDT_restart(WWDT0);
#endif
}

uint32_t calc_fnv1a_32(const uint8_t *data, size_t length) {
    uint32_t hash = 0x811C9DC5;
    for (size_t i = 0; i < length; i++) { hash ^= data[i]; hash *= 0x01000193; }
    return hash;
}

static void mark_nvm_dirty(void) {
    if (memcmp(&flash_shadow, &sys_memory, sizeof(NVM_Data_t)) == 0) {
        nvm_dirty = false;
    } else if (!nvm_dirty) {
        nvm_dirty = true;
        nvm_dirty_start_tick = g_tick_ms;
    }
}

static void invalidate_brt_cache(void) {
    brt_cache_last_req = 0xFFFF;
    brt_cache_last_vbatt = 0;
}

static void reset_key_state(void) {
    hist_minus = 0x00; hist_plus = 0x00;
    t_minus_ms = 0; t_plus_ms = 0; t_both_ms = 0; max_both_ms = 0;
    prev_db_minus = false; prev_db_plus = false;
    anim_5s_done = false;
}

void load_factory_defaults(void) {
    memset(&sys_memory, 0, sizeof(NVM_Data_t)); 
    sys_memory.magic = NVM_MAGIC;
    sys_memory.seq_id = 0;
    sys_memory.default_level = CFG_DEFAULT_LEVEL;
    sys_memory.params = sys_memory.default_level | (CFG_DEFAULT_ALS_EN << 8) | (CFG_DEFAULT_ALS_OFFSET << 16);
    sys_memory.features = DEFAULT_FEATURE_FLAGS;
    sys_memory.r_base = R_DYNAMIC_BASE_MOHM;
    sys_memory.r_series = HW_SERIES_R_MOHM;
    sys_memory.v_led_fw = HW_LED_FORWARD_V_MV;
    sys_memory.i_max_ua = HW_LED_MAX_CURRENT_UA;
    sys_memory.batt_p_uw = BATT_MAX_DISCHARGE_UW;
    sys_memory.als_min_brt = ALS_MIN_BRT;
    sys_memory.lvp_crit = BATT_LVP_CRIT_MV;
    sys_memory.lvp_ext = BATT_LVP_EXTREME_MV;
    invalidate_brt_cache();
}

#if FEATURE_MEMORY_SAVE
void load_memory_from_flash(void) {
    uint32_t max_seq = 0;
    bool found = false;

    for (int sec = 0; sec < 2; sec++) {
        uint32_t sec_addr = (sec == 0) ? FLASH_SECTOR_A_ADDR : FLASH_SECTOR_B_ADDR;
        for (int i = 0; i < NVM_MAX_SLOTS; i++) {
            NVM_Data_t *ptr = (NVM_Data_t *)(sec_addr + i * NVM_SLOT_SIZE);
            if (ptr->magic == NVM_MAGIC) {
                if (calc_fnv1a_32((const uint8_t*)ptr, offsetof(NVM_Data_t, crc32)) == ptr->crc32) {
                    if (!found || ptr->seq_id >= max_seq) {
                        max_seq = ptr->seq_id;
                        sys_memory = *ptr;
                        current_sector_addr = sec_addr;
                        current_nvm_slot = i;
                        found = true;
                    }
                }
            }
        }
    }

    if (found) {
        uint8_t lvl = sys_memory.params & 0xFF;
        if (lvl >= CFG_ENABLED_LEVELS) lvl = sys_memory.default_level;
        sys_memory.params = (sys_memory.params & 0xFFFFFF00) | lvl;
        sys_memory.features &= FEATURE_RUNTIME_MASK;
    } else {
        sys_memory.seq_id = 0;
        current_sector_addr = FLASH_SECTOR_A_ADDR;
        current_nvm_slot = NVM_MAX_SLOTS;
        load_factory_defaults();
    }

    flash_shadow = sys_memory;
    nvm_dirty = false;
    invalidate_brt_cache();
}

bool save_memory_to_flash(void) {
#if FEATURE_SAVE_RAM_SHADOW
    if (memcmp(&flash_shadow, &sys_memory, sizeof(NVM_Data_t)) == 0) {
        nvm_dirty = false;
        return true;
    }
#endif

    sys_memory.seq_id++;
    sys_memory.crc32 = calc_fnv1a_32((const uint8_t*)&sys_memory, offsetof(NVM_Data_t, crc32));

    for (int slot_attempt = 0; slot_attempt < 3; slot_attempt++) {
        uint32_t next_slot = current_nvm_slot + 1;
        uint32_t target_sector = current_sector_addr;

        if (next_slot >= NVM_MAX_SLOTS) {
            target_sector = (current_sector_addr == FLASH_SECTOR_A_ADDR) ? FLASH_SECTOR_B_ADDR : FLASH_SECTOR_A_ADDR;
            next_slot = 0;

            kick_watchdog();
            __disable_irq();
            DL_FlashCTL_unprotectSector(FLASHCTL, target_sector, DL_FLASHCTL_REGION_SELECT_MAIN);
            DL_FlashCTL_eraseMemoryFromRAM(FLASHCTL, target_sector, DL_FLASHCTL_COMMAND_SIZE_SECTOR);
            g_tick_ms += FLASH_ERASE_TIME_MS; 
            __enable_irq();
        }

        uint32_t write_addr = target_sector + next_slot * NVM_SLOT_SIZE;
        bool write_ok = false;

        for (int retry = 0; retry < FLASH_RETRY_COUNT; retry++) {
            kick_watchdog();
            __disable_irq();
            DL_FlashCTL_unprotectSector(FLASHCTL, target_sector, DL_FLASHCTL_REGION_SELECT_MAIN);
            for (int w = 0; w < NVM_SLOT_SIZE; w += 8) {
                DL_FlashCTL_programMemoryFromRAM64(FLASHCTL, write_addr + w, (uint32_t*)((uint8_t*)&sys_memory + w));
                g_tick_ms += FLASH_PROG_TIME_MS;
            }
            __enable_irq();

#if FEATURE_SAVE_VERIFY
            if (memcmp((void*)write_addr, &sys_memory, sizeof(NVM_Data_t)) == 0) {
                write_ok = true;
                break;
            }
#else
            write_ok = true;
            break;
#endif
        }

        if (write_ok) {
            nvm_dirty = false;
            flash_shadow = sys_memory;
            current_sector_addr = target_sector;
            current_nvm_slot = next_slot;
            return true;
        } else {
            current_nvm_slot = next_slot;
            current_sector_addr = target_sector;
        }
    }
    
    nvm_dirty = false; 
    return false;
}

void auto_save_task(void) {
#if EN_AUTO_SAVE
    if (nvm_dirty && sys_state == SYS_RUN) {
        if ((g_tick_ms - last_user_action_tick > TIME_NVM_AUTO_SAVE_DELAY_MS) ||
            (g_tick_ms - nvm_dirty_start_tick  > TIME_NVM_FORCE_SAVE_MS)) {
            save_memory_to_flash();
        }
    }
#endif
}
#else
void load_memory_from_flash(void) { load_factory_defaults(); }
bool save_memory_to_flash(void) { return true; }
void auto_save_task(void) {}
#endif

uint32_t get_dyn_r_mohm(uint32_t vbatt_mv) {
    uint32_t r_base = sys_memory.r_base;
    if (vbatt_mv <= R_DYNAMIC_OFFSET_MV) return r_base + 300000;

    uint32_t extra = R_DYNAMIC_FACTOR / (vbatt_mv - R_DYNAMIC_OFFSET_MV);
    if (extra > 250000) extra = 250000;

    return r_base + extra;
}

#if FEATURE_VOLTAGE_COMPENSATION
static uint32_t calc_limit_i_led(uint32_t i_pulse_ua) {
    if (i_pulse_ua > sys_memory.i_max_ua) {
        if (i_pulse_ua == 0) return BRT_SCALE_MAX;
        return (sys_memory.i_max_ua * BRT_SCALE_MAX) / i_pulse_ua;
    }
    return BRT_SCALE_MAX;
}

static uint32_t calc_limit_v_drop(uint32_t v_cap_mv, uint32_t i_pulse_ua, uint32_t r_dyn_mohm, uint16_t req_brt) {
    uint32_t r_dc_mohm = (r_dyn_mohm * 3) / 10;
    uint32_t v_drop_peak_mv = (uint32_t)(((uint64_t)i_pulse_ua * r_dc_mohm) / 1000000ULL);
    uint32_t v_drop_avg_mv = (uint32_t)(((uint64_t)i_pulse_ua * req_brt * r_dyn_mohm) / 1000000000ULL);
    uint32_t v_drop_total = v_drop_peak_mv + v_drop_avg_mv;

    if ((v_cap_mv > v_drop_total) && (v_cap_mv - v_drop_total < WALL_MIN_V_BATT_MV)) {
        uint32_t allowed_drop_total = v_cap_mv - WALL_MIN_V_BATT_MV;
        if (allowed_drop_total <= v_drop_peak_mv) return LOW_BRT_GUARANTEE;

        uint32_t allowed_drop_avg = allowed_drop_total - v_drop_peak_mv;
        uint32_t d_limit = (uint32_t)(((uint64_t)allowed_drop_avg * 1000000000ULL) / ((uint64_t)i_pulse_ua * r_dyn_mohm));
        return (req_brt <= LOW_BRT_GUARANTEE && d_limit < LOW_BRT_GUARANTEE) ? LOW_BRT_GUARANTEE : d_limit;
    }
    return BRT_SCALE_MAX;
}

static uint32_t calc_limit_i_brt(uint32_t i_pulse_ua) {
    if (i_pulse_ua == 0) return BRT_SCALE_MAX;
    return (BATT_MAX_DISCHARGE_UA * 1000) / i_pulse_ua;
}

static uint32_t calc_limit_p_avg(uint32_t v_cap_mv, uint32_t i_pulse_ua) {
    uint64_t peak_power_uw = ((uint64_t)v_cap_mv * i_pulse_ua) / 1000ULL;
    if (peak_power_uw == 0) return BRT_SCALE_MAX;
    return (uint32_t)(((uint64_t)sys_memory.batt_p_uw * 1000ULL) / peak_power_uw);
}
#endif

static inline bool als_is_enabled(void) {
#if !FEATURE_ALS_MODE
    return false;
#else
    uint8_t auto_en = (sys_memory.params >> 8) & 0xFF;
    return ((sys_memory.features & FLAG_ALS_MODE) &&
            auto_en &&
            (sys_state == SYS_RUN || sys_state == SYS_TEST_MODE) &&
            !is_dimmed);
#endif
}

uint16_t calculate_safe_brt(uint16_t req_brt) {
#if FEATURE_VOLTAGE_COMPENSATION
    if (!(sys_memory.features & FLAG_VOLTAGE_COMPENSATION)) return req_brt;

    uint32_t v_cap_mv = g_vbatt_mv_filtered;
    if (!EN_WALLS) return req_brt;
    if (v_cap_mv <= BATT_MIN_WORK_V_MV) return 0;

    uint32_t diff_v = (v_cap_mv > brt_cache_last_vbatt) ? (v_cap_mv - brt_cache_last_vbatt) : (brt_cache_last_vbatt - v_cap_mv);
    if (diff_v <= 30 && req_brt == brt_cache_last_req) return brt_cache_val;

    uint32_t r_dyn_mohm = get_dyn_r_mohm(v_cap_mv);
    uint32_t r_total_mohm = sys_memory.r_series + r_dyn_mohm;

    uint32_t i_pulse_ua = (uint32_t)(((uint64_t)(v_cap_mv - sys_memory.v_led_fw) * 1000000ULL) / r_total_mohm);
    if (i_pulse_ua == 0) return 0;

    uint32_t min_limit = BRT_SCALE_MAX;

    uint32_t l_i_led = calc_limit_i_led(i_pulse_ua);
    if (l_i_led < min_limit) min_limit = l_i_led;

    uint32_t l_v_drop = calc_limit_v_drop(v_cap_mv, i_pulse_ua, r_dyn_mohm, req_brt);
    if (l_v_drop < min_limit) min_limit = l_v_drop;

    uint32_t l_i_brt = calc_limit_i_brt(i_pulse_ua);
    if (l_i_brt < min_limit) min_limit = l_i_brt;

    uint32_t l_p_avg = calc_limit_p_avg(v_cap_mv, i_pulse_ua);
    if (l_p_avg < min_limit) min_limit = l_p_avg;

    if (min_limit > BRT_SCALE_MAX) min_limit = BRT_SCALE_MAX;

    if (g_test_box.magic == 0x54455354) {
        g_test_box.dyn_r_mohm = r_dyn_mohm;
        g_test_box.limit_i_led = l_i_led;
        g_test_box.limit_v_drop = l_v_drop;
        g_test_box.limit_i_brt = l_i_brt;
        g_test_box.limit_p_avg = l_p_avg;
        g_test_box.safe_brt_out = min_limit;
        g_test_box.est_i_peak_ua = i_pulse_ua;
        g_test_box.est_i_avg_ua = (uint32_t)(((uint64_t)i_pulse_ua * current_pwm) / PWM_REG_MAX);
        g_test_box.est_v_led_mv = sys_memory.v_led_fw;
        g_test_box.est_p_led_uw = (uint32_t)(((uint64_t)g_test_box.est_v_led_mv * g_test_box.est_i_avg_ua) / 1000ULL);
    }

    brt_cache_val = (min_limit > req_brt) ? req_brt : min_limit;
    brt_cache_last_vbatt = v_cap_mv;
    brt_cache_last_req = req_brt;

    return brt_cache_val;
#else
    return req_brt;
#endif
}

void set_brightness_target(uint16_t safe_brt) {
    current_brt_calculated = safe_brt;
    target_pwm = (safe_brt * PWM_REG_MAX) / BRT_SCALE_MAX;
}

void pwm_output_task(void) {
    uint16_t final_pwm = 0;
    uint16_t safe_max_pwm = (calculate_safe_brt(BRT_SCALE_MAX) * PWM_REG_MAX) / BRT_SCALE_MAX;

    if (sys_state == SYS_OFF) final_pwm = 0;
    else if (sys_state == SYS_FLASH_MODE) {
        final_pwm = (PWM_FLASH_LEVEL * PWM_REG_MAX) / BRT_SCALE_MAX; 
    }
    else if (sys_state == SYS_TEST_MODE && g_test_box.ovr_led_mode > 0) {
        // [修改]: 支持完整的独立控制
        if (g_test_box.ovr_led_mode == 1) final_pwm = g_test_box.ovr_pwm_val; // 直接灌入PWM值
        else if (g_test_box.ovr_led_mode == 2) final_pwm = (calculate_safe_brt(g_test_box.ovr_brt_val) * PWM_REG_MAX) / BRT_SCALE_MAX; // 亮度转PWM (带保护)
        else if (g_test_box.ovr_led_mode == 3) final_pwm = (g_test_box.ovr_brt_val * PWM_REG_MAX) / BRT_SCALE_MAX; // 亮度转PWM (无保护强制拉流)
    }
#if FEATURE_LVP_FLASH_WARNING
    else if (sys_state == SYS_LVP_CRIT && (sys_memory.features & FLAG_LVP_FLASH_WARNING))
        final_pwm = (g_tick_ms % LVP_FLASH_PERIOD_MS < LVP_FLASH_ON_TIME_MS) ? ((PWM_FLASH_LEVEL * PWM_REG_MAX) / BRT_SCALE_MAX) : 0;
#endif
    else if (sys_state == SYS_ALS_ERR) {
        uint32_t dt = g_tick_ms - als_err_start_tick;
        if (dt >= 10000) {
            sys_state = SYS_RUN;
            sys_memory.params &= ~(1 << 8);
            mark_nvm_dirty();
            uint8_t lvl = sys_memory.params & 0xFF;
            set_brightness_target(calculate_safe_brt(CFG_BRT_MAP[lvl]));
            final_pwm = current_pwm;
        } else {
            uint32_t mod = dt % 1500;
            if (mod < 600) final_pwm = (PWM_FLASH_LEVEL * PWM_REG_MAX) / BRT_SCALE_MAX;
            else if (mod < 900) final_pwm = 0;
            else if (mod < 1200) final_pwm = (PWM_FLASH_LEVEL * PWM_REG_MAX) / BRT_SCALE_MAX;
            else final_pwm = 0;
        }
    }
#if FEATURE_FLASH_ANIMATION
    else if (g_anim.active) final_pwm = g_anim.led_on ? safe_max_pwm : 0;
#endif
    else final_pwm = current_pwm;

    if (final_pwm > PWM_REG_MAX) final_pwm = PWM_REG_MAX;

    if (final_pwm != g_last_applied_pwm) {
        DL_TimerG_setCaptureCompareValue(HW_PWM_INST, PWM_REG_MAX - final_pwm, HW_PWM_INDEX);
        g_last_applied_pwm = final_pwm;
    }

    if (g_test_box.magic == 0x54455354) g_test_box.current_pwm_val = final_pwm;
}

void pwm_fade_task(void) {
#if FEATURE_FLASH_ANIMATION
    if (g_anim.active) return;
#endif
    if (current_pwm == target_pwm) return;

    int32_t diff = (int32_t)target_pwm - (int32_t)current_pwm;
    if (abs(diff) <= PWM_FADE_FINISH) {
        current_pwm = target_pwm;
        return;
    }

    int16_t step = diff / PWM_FADE_DIV;
    if (step == 0) step = (diff > 0) ? PWM_FADE_MIN_STEP : -PWM_FADE_MIN_STEP;
    current_pwm += step;
}

#if FEATURE_ALS_MODE
#define I2C_SCL_HIGH()  DL_GPIO_disableOutput(PORT_I2C, PIN_SW_SCL)
#define I2C_SCL_LOW()   do { DL_GPIO_clearPins(PORT_I2C, PIN_SW_SCL); DL_GPIO_enableOutput(PORT_I2C, PIN_SW_SCL); } while(0)
#define I2C_SDA_HIGH()  DL_GPIO_disableOutput(PORT_I2C, PIN_SW_SDA)
#define I2C_SDA_LOW()   do { DL_GPIO_clearPins(PORT_I2C, PIN_SW_SDA); DL_GPIO_enableOutput(PORT_I2C, PIN_SW_SDA); } while(0)
#define I2C_SDA_READ()  ((DL_GPIO_readPins(PORT_I2C, PIN_SW_SDA) & PIN_SW_SDA) ? 1 : 0)

void i2c_init(void) {
    DL_GPIO_clearPins(PORT_I2C, PIN_SW_SDA | PIN_SW_SCL);
    I2C_SDA_HIGH();
    I2C_SCL_HIGH();
}

void i2c_stop(void) {
    I2C_SDA_LOW(); delay_us(TIME_I2C_FLIP_DELAY_US);
    I2C_SCL_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US);
    I2C_SDA_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US);
}

bool i2c_start(void) {
    I2C_SDA_HIGH(); I2C_SCL_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US);
    if (I2C_SDA_READ() == 0) { i2c_stop(); return false; }
    I2C_SDA_LOW(); delay_us(TIME_I2C_FLIP_DELAY_US);
    I2C_SCL_LOW(); delay_us(TIME_I2C_FLIP_DELAY_US);
    return true;
}

bool i2c_write_byte(uint8_t data) {
    for (int i = 0; i < 8; i++) {
        if (data & 0x80) I2C_SDA_HIGH(); else I2C_SDA_LOW();
        data <<= 1; delay_us(TIME_I2C_FLIP_DELAY_US); I2C_SCL_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US); I2C_SCL_LOW();
    }
    I2C_SDA_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US); I2C_SCL_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US);
    bool ack = (I2C_SDA_READ() == 0); I2C_SCL_LOW(); delay_us(TIME_I2C_FLIP_DELAY_US);
    return ack;
}

uint8_t i2c_read_byte(bool ack) {
    uint8_t data = 0; I2C_SDA_HIGH();
    for (int i = 0; i < 8; i++) {
        delay_us(TIME_I2C_FLIP_DELAY_US); I2C_SCL_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US);
        data = (data << 1) | I2C_SDA_READ(); I2C_SCL_LOW();
    }
    if (ack) I2C_SDA_LOW(); else I2C_SDA_HIGH();
    delay_us(TIME_I2C_FLIP_DELAY_US); I2C_SCL_HIGH(); delay_us(TIME_I2C_FLIP_DELAY_US); I2C_SCL_LOW(); delay_us(TIME_I2C_FLIP_DELAY_US); I2C_SDA_HIGH();
    return data;
}

void als_task(void) {
    static uint8_t als_st = 0; // 0:Off, 1:PowerUp, 2:Idle, 3:Converting
    static uint32_t als_tick = 0;
    static uint8_t als_err_cnt = 0;
    static uint32_t lux_avg = 0xFFFFFFFF;
#if FEATURE_ALS_SMOOTHING
    static int16_t last_als_brt = -1;
#endif

    if (!als_is_enabled()) {
        if (als_st != 0) {
            i2c_stop();
            DL_GPIO_clearPins(PORT_OUTPUT, PIN_VCC_EN);
            als_st = 0;
            lux_avg = 0xFFFFFFFF;
#if FEATURE_ALS_SMOOTHING
            last_als_brt = -1;
#endif
        }
        if (g_test_box.magic == 0x54455354) g_test_box.sensor_status = 1;
        return;
    }

    uint8_t off_idx = (sys_memory.params >> 16) & 0xFF;
    if (off_idx > 4) { off_idx = 2; }

    #define ALS_FAIL() do { \
        if (g_test_box.magic == 0x54455354) g_test_box.sensor_status = 2; \
        if (++als_err_cnt >= 3) { \
            sys_state = SYS_ALS_ERR; \
            als_err_start_tick = g_tick_ms; \
            als_err_cnt = 0; \
            DL_GPIO_clearPins(PORT_OUTPUT, PIN_VCC_EN); \
            als_st = 0; \
        } else { \
            als_st = 2; \
        } \
        i2c_stop(); \
        return; \
    } while(0)

    if (g_test_box.magic == 0x54455354 && g_test_box.ovr_als_en) {
        if (g_tick_ms - als_tick >= TIME_ALS_POLL_INTERVAL_MS) {
            als_tick = g_tick_ms;
            uint32_t lux = g_test_box.ovr_als_lux;
            if (lux_avg == 0xFFFFFFFF) lux_avg = lux;
            else lux_avg = lux_avg - (lux_avg >> ADC_FILTER_SHIFT) + (lux >> ADC_FILTER_SHIFT);

            g_test_box.als_lux_raw = lux;
            g_test_box.als_lux_filtered = lux_avg;
            g_test_box.sensor_status = 0;

            int16_t base_brt = user_als_lux_to_brt_curve(lux_avg);
            int32_t pct = AUTO_OFFSET_PCT[off_idx];
            int16_t target_final = base_brt + (base_brt * pct) / 100;

            if (target_final < sys_memory.als_min_brt) target_final = sys_memory.als_min_brt;
            if (target_final > BRT_SCALE_MAX) target_final = BRT_SCALE_MAX;

            if (g_test_box.ovr_led_mode == 0) set_brightness_target(calculate_safe_brt((uint16_t)target_final));
        }
        return;
    }

    if (als_st == 0) {
        DL_GPIO_setPins(PORT_OUTPUT, PIN_VCC_EN);
        als_tick = g_tick_ms; als_st = 1;
    }
    else if (als_st == 1 && (g_tick_ms - als_tick >= TIME_ALS_POWERUP_DELAY_MS)) {
        if (!i2c_start()) ALS_FAIL();
        if (!i2c_write_byte(OPT3001_ADDR << 1)) ALS_FAIL();
        i2c_write_byte(0x01); i2c_write_byte(0xCA); i2c_write_byte(0x10);
        i2c_stop();
        als_tick = g_tick_ms; als_st = 3;
    }
    else if (als_st == 2 && (g_tick_ms - als_tick >= (TIME_ALS_POLL_INTERVAL_MS - TIME_ALS_CONVERT_WAIT_MS))) {
        if (!i2c_start()) ALS_FAIL();
        if (!i2c_write_byte(OPT3001_ADDR << 1)) ALS_FAIL();
        i2c_write_byte(0x01); i2c_write_byte(0xCA); i2c_write_byte(0x10);
        i2c_stop();
        als_tick = g_tick_ms; als_st = 3;
    }
    else if (als_st == 3 && (g_tick_ms - als_tick >= TIME_ALS_CONVERT_WAIT_MS)) {
        if (!i2c_start()) ALS_FAIL();
        if (!i2c_write_byte(OPT3001_ADDR << 1)) ALS_FAIL();
        i2c_write_byte(0x00); i2c_stop();

        if (!i2c_start()) ALS_FAIL();
        if (!i2c_write_byte((OPT3001_ADDR << 1) | 0x01)) ALS_FAIL();
        uint8_t msb = i2c_read_byte(1);
        uint8_t lsb = i2c_read_byte(0);
        i2c_stop();

        als_err_cnt = 0;

        uint16_t m = ((msb & 0x0F) << 8) | lsb;
        uint8_t e = (msb >> 4) & 0x0F;
        uint32_t lux = (m * (1UL << e)) / 100;

        if (g_test_box.magic == 0x54455354) {
            g_test_box.als_lux_raw = lux;
            g_test_box.als_err_cnt = als_err_cnt;
            g_test_box.sensor_status = 0;
        }

        if (lux_avg == 0xFFFFFFFF) lux_avg = lux;
        else lux_avg = lux_avg - (lux_avg >> ADC_FILTER_SHIFT) + (lux >> ADC_FILTER_SHIFT);

        if (g_test_box.magic == 0x54455354) g_test_box.als_lux_filtered = lux_avg;

        int16_t base_brt = user_als_lux_to_brt_curve(lux_avg);
        int32_t pct = AUTO_OFFSET_PCT[off_idx];
        int16_t target_final = base_brt + (base_brt * pct) / 100;

#if FEATURE_ALS_SMOOTHING
        if (last_als_brt != -1) {
            int16_t diff = target_final - last_als_brt;
            if (diff > 0) {
                uint16_t slew = ALS_MAX_SLEW_RATE;
                if (last_als_brt < 150) slew = 2; 
                else if (last_als_brt < 400) slew = 5; 
                if (diff > slew) target_final = last_als_brt + slew;
            } else {
                if (diff < -ALS_MAX_SLEW_RATE) target_final = last_als_brt - ALS_MAX_SLEW_RATE;
            }
        }
        last_als_brt = target_final;
#endif

        if (target_final < sys_memory.als_min_brt) target_final = sys_memory.als_min_brt;
        if (target_final > BRT_SCALE_MAX) target_final = BRT_SCALE_MAX;

        if (sys_state != SYS_TEST_MODE || g_test_box.ovr_led_mode == 0) {
            set_brightness_target(calculate_safe_brt((uint16_t)target_final));
        }

        als_tick = g_tick_ms; als_st = 2; // 回到 Idle 等待下一秒
    }
}
#endif

void adc_task(void) {
    static uint32_t adc_tick = 0;
    static bool is_converting = false;
    static bool first_read = true;

    if (!is_converting) {
        if (g_tick_ms - adc_tick >= TIME_ADC_READ_INTERVAL_MS) {
            adc_tick = g_tick_ms;
            DL_ADC12_startConversion(HW_ADC_INST);
            is_converting = true;
        }
    } else {
        if (DL_ADC12_getRawInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED)) {
            DL_ADC12_clearInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
            uint32_t res = DL_ADC12_getMemResult(HW_ADC_INST, DL_ADC12_MEM_IDX_0);

            uint32_t raw_mv = (res * VBATT_FULL_MV * ADC_GAIN_CAL) / (ADC_MAX_RESOLUTION * 1000);
            g_vbatt_mv_raw = raw_mv;

            if (first_read) {
                g_vbatt_mv_filtered = raw_mv;
                first_read = false;
            } else {
                g_vbatt_mv_filtered = g_vbatt_mv_filtered - (g_vbatt_mv_filtered >> ADC_FILTER_SHIFT) + (raw_mv >> ADC_FILTER_SHIFT);
            }
            is_converting = false;
        }
        else if (g_tick_ms - adc_tick > ADC_TIMEOUT_MS) {
            DL_ADC12_clearInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
            is_converting = false;
        }
    }
}

void trigger_flash_anim(void) {
#if FEATURE_FLASH_ANIMATION
    if (g_anim.active) return;
    g_anim.active = true;
    g_anim.count = 0;
    g_anim.led_on = true;
    g_anim.last_tick = g_tick_ms;
#endif
}

void flash_anim_task(void) {
#if FEATURE_FLASH_ANIMATION
    if (!g_anim.active) return;
    if (g_tick_ms - g_anim.last_tick >= TIME_UI_FLASH_STEP_MS) {
        g_anim.last_tick = g_tick_ms;
        g_anim.led_on = !g_anim.led_on;
        if (++g_anim.count >= FLASH_TOGGLE_COUNT) {
            g_anim.active = false;
            uint8_t lvl = sys_memory.params & 0xFF;
            target_pwm = (calculate_safe_brt(CFG_BRT_MAP[lvl]) * PWM_REG_MAX) / BRT_SCALE_MAX;
        }
    }
#endif
}

void protection_task(void) {
    static uint8_t pwr_loss_cnt = 0;
    static uint8_t lvp_crit_cnt = 0;
    static uint8_t lvp_ext_cnt = 0;

    if (g_vbatt_mv_raw < BATT_POWER_LOSS_MV) {
        if (++pwr_loss_cnt >= POWERLOSS_COUNT) {
            sys_state = SYS_OFF;
            pwm_output_task();
            DL_TimerG_stopCounter(HW_PWM_INST);
            DL_TimerG_disableClock(HW_PWM_INST);
            DL_GPIO_clearPins(PORT_OUTPUT, PIN_VCC_EN);
            save_memory_to_flash();
            DL_SYSCTL_setPowerPolicySHUTDOWN();
            while(1) { __WFI(); }
        }
    } else { pwr_loss_cnt = 0; }

    if (sys_state != SYS_OFF && sys_state != SYS_FLASH_MODE && sys_state != SYS_TEST_MODE) {
        if (g_vbatt_mv_raw < sys_memory.lvp_ext) {
            if (++lvp_ext_cnt >= LVP_EXT_COUNT) execute_shutdown();
        } else { lvp_ext_cnt = 0; }

        if (g_vbatt_mv_raw < sys_memory.lvp_crit && (sys_state == SYS_RUN || sys_state == SYS_ALS_ERR)) {
            if (++lvp_crit_cnt >= LVP_CRIT_COUNT) sys_state = SYS_LVP_CRIT;
        } else { lvp_crit_cnt = 0; }
    }

#if FEATURE_INACTIVITY_AUTO_DIM_OFF
    static uint32_t last_1s_tick = 0;
    if (g_tick_ms - last_1s_tick >= 1000) {
        last_1s_tick += 1000;
        if (sys_state == SYS_RUN && (sys_memory.features & FLAG_INACTIVITY_AUTO_DIM)) {
            inactivity_sec++;
            if (!is_dimmed && inactivity_sec > TIME_AUTO_DIM_S) { is_dimmed = true; set_brightness_target(DIM_LEVEL); }
            if (inactivity_sec > TIME_AUTO_DIM_S + TIME_AUTO_SHUTDOWN_S) execute_shutdown();
        } else if (sys_state == SYS_FLASH_MODE) {
            inactivity_sec++;
            if (inactivity_sec >= 300) NVIC_SystemReset();
        } else if (sys_state == SYS_TEST_MODE) {
            inactivity_sec = 0;
        }
    }
#endif
}

void execute_shutdown(void) {
    sys_state = SYS_OFF;
    target_pwm = current_pwm = 0;
    pwm_output_task();
    DL_TimerG_stopCounter(HW_PWM_INST);
    DL_TimerG_disableClock(HW_PWM_INST);
    DL_GPIO_clearPins(PORT_OUTPUT, PIN_VCC_EN);
    save_memory_to_flash(); 
}

static void key_handle_short_plus(void) {
    if (is_dimmed) { is_dimmed = false; return; }
    uint8_t lvl = sys_memory.params & 0xFF;

    if (als_is_enabled()) {
#if FEATURE_ALS_OFFSET_ADJUST
        uint8_t off_idx = (sys_memory.params >> 16) & 0xFF;
        if (off_idx < 4) off_idx++;
        sys_memory.params = (sys_memory.params & 0xFF00FFFF) | (off_idx << 16);
        mark_nvm_dirty();
#endif
        return;
    }

    virtual_max_brt = calculate_safe_brt(BRT_SCALE_MAX);
    if (lvl < CFG_ENABLED_LEVELS - 1) {
        uint16_t req_brt = CFG_BRT_MAP[lvl + 1];
        uint16_t safe_brt = calculate_safe_brt(req_brt);

#if FEATURE_ADAPTIVE_GEAR_LIMIT
        if ((sys_memory.features & FLAG_ADAPTIVE_GEAR_LIMIT) && safe_brt < req_brt) {
            if (req_brt - safe_brt <= SNAP_THRESHOLD_BRT) { lvl++; is_overshot = false; }
            else { if (!is_overshot) { lvl++; is_overshot = true; } safe_brt = virtual_max_brt; }
        } else { lvl++; is_overshot = false; }
#else
        lvl++;
#endif

        sys_memory.params = (sys_memory.params & 0xFFFFFF00) | lvl;
        mark_nvm_dirty();
        if (sys_state != SYS_TEST_MODE || g_test_box.ovr_led_mode == 0) set_brightness_target(safe_brt);
    }
}

static void key_handle_short_minus(void) {
    if (is_dimmed) { is_dimmed = false; return; }
    uint8_t lvl = sys_memory.params & 0xFF;

    if (als_is_enabled()) {
#if FEATURE_ALS_OFFSET_ADJUST
        uint8_t off_idx = (sys_memory.params >> 16) & 0xFF;
        if (off_idx > 0) off_idx--; 
        sys_memory.params = (sys_memory.params & 0xFF00FFFF) | (off_idx << 16);
        mark_nvm_dirty();
#endif
        return;
    }

    if (lvl > 0) { lvl--; is_overshot = false; } 
    sys_memory.params = (sys_memory.params & 0xFFFFFF00) | lvl;
    mark_nvm_dirty();
    if (sys_state != SYS_TEST_MODE || g_test_box.ovr_led_mode == 0) set_brightness_target(calculate_safe_brt(CFG_BRT_MAP[lvl]));
}

static void key_handle_both_long(void) {
    if (sys_state == SYS_OFF || sys_state == SYS_FLASH_MODE) {
        if (g_vbatt_mv_filtered < sys_memory.lvp_ext) return;
        
        sys_state = SYS_RUN; 
        inactivity_sec = 0; is_dimmed = false; is_overshot = false;
        uint8_t lvl = sys_memory.params & 0xFF; 
        set_brightness_target(calculate_safe_brt(CFG_BRT_MAP[lvl]));
    } else {
        execute_shutdown();
    }
}

static void enter_flash_mode(void) {
    if (g_vbatt_mv_filtered < sys_memory.lvp_ext) return;
    g_flash_mode_used = true;
    sys_state = SYS_FLASH_MODE;
    inactivity_sec = 0;
    last_user_action_tick = g_tick_ms;
    flash_mode_start_tick = g_tick_ms;

    __disable_irq();
#if FEATURE_RESTORE_NRST_IN_FLASH
    DL_GPIO_setPins(PORT_OUTPUT, PIN_VCC_EN);
    delay_us(1000);
#endif
    __enable_irq();
}

static bool key_debounce(uint8_t *hist, bool raw_state) {
#if FEATURE_KEY_DEBOUNCE
    *hist = (*hist << 1) | (raw_state ? 0 : 1);
    return (*hist == ((1 << KEY_DEBOUNCE_BITS) - 1));
#else
    return !raw_state;
#endif
}

bool key_is_idle(void) {
    return (hist_minus == 0x00) && (hist_plus == 0x00) &&
           (t_minus_ms == 0) && (t_plus_ms == 0) && (t_both_ms == 0) && (max_both_ms == 0);
}

void key_task(void) {
    uint32_t raw_m_val = DL_GPIO_readPins(KEY_MINUS_PORT, KEY_MINUS_PIN) & KEY_MINUS_PIN;
    uint32_t raw_p_val = DL_GPIO_readPins(KEY_PLUS_PORT, KEY_PLUS_PIN) & KEY_PLUS_PIN;
    
    // [修改]: 支持真实的物理按键和软件拦截
    uint32_t sim_m_val = raw_m_val;
    uint32_t sim_p_val = raw_p_val;

    if (sys_state == SYS_TEST_MODE && g_test_box.magic == 0x54455354) {
        if (g_test_box.ovr_block_phys_keys) {
            sim_m_val = KEY_MINUS_PIN; // 屏蔽时：强制模拟为释放电平 (非0)
            sim_p_val = KEY_PLUS_PIN;  // 屏蔽时：强制模拟为释放电平 (非0)
        }
        
        // 软件虚拟触发为或门逻辑：物理按下或软件触发，都算作按下电平 (0)
        if (g_test_box.ovr_key_minus) sim_m_val = 0; 
        if (g_test_box.ovr_key_plus)  sim_p_val = 0; 
    }

    bool db_minus = key_debounce(&hist_minus, sim_m_val);
    bool db_plus  = key_debounce(&hist_plus, sim_p_val);

    if (db_minus && db_plus) {
        t_both_ms += SYS_TICK_PERIOD_MS;
        if (t_both_ms > max_both_ms) max_both_ms = t_both_ms;

        if (max_both_ms >= KEY_TIME_FACTORY_RESET_MS && !anim_5s_done) {
            anim_5s_done = true;
            trigger_flash_anim(); 
        }
        t_minus_ms = 0; t_plus_ms = 0;
    } else {
        t_both_ms = 0;
    }

    if (prev_db_minus && !db_minus) { 
        if (max_both_ms == 0 && t_minus_ms > KEY_TIME_DEBOUNCE_MS && t_minus_ms < KEY_TIME_SHORT_MAX_MS) {
            if (sys_state == SYS_RUN || sys_state == SYS_TEST_MODE) {
                key_handle_short_minus();
                inactivity_sec = 0; last_user_action_tick = g_tick_ms;
            }
        }
        // [修改]: Flash Mode 短按判定区间设为 50ms 到 1.5s
        if (sys_state == SYS_OFF && !g_flash_mode_used && !(sys_memory.features & FLAG_SWD_IN_OFF_STATE)) {
            if (max_both_ms == 0 && t_minus_ms >= 50 && t_minus_ms < 1500) {
                enter_flash_mode();
            }
        }
        t_minus_ms = 0;
    } else if (db_minus && !db_plus) {
        t_minus_ms += SYS_TICK_PERIOD_MS;
    } else {
        t_minus_ms = 0; 
    }

    if (prev_db_plus && !db_plus) { 
        if (max_both_ms == 0 && t_plus_ms > KEY_TIME_DEBOUNCE_MS && t_plus_ms < KEY_TIME_SHORT_MAX_MS) {
            if (sys_state == SYS_RUN || sys_state == SYS_TEST_MODE) {
                key_handle_short_plus();
                inactivity_sec = 0; last_user_action_tick = g_tick_ms;
            }
        }
        t_plus_ms = 0;
    } else if (db_plus && !db_minus) {
        t_plus_ms += SYS_TICK_PERIOD_MS;
    } else {
        t_plus_ms = 0;
    }

    if (!db_minus && !db_plus) {
        if (max_both_ms > 0) {
            if (max_both_ms >= KEY_TIME_FACTORY_RESET_MS) {
                uint8_t auto_en = (sys_memory.params >> 8) & 0xFF;
                sys_memory.params = (sys_memory.params & 0xFFFF00FF) | ((!auto_en) << 8);
                mark_nvm_dirty();
                if (sys_state == SYS_RUN || sys_state == SYS_TEST_MODE) {
                    uint8_t lvl = sys_memory.params & 0xFF;
                    set_brightness_target(calculate_safe_brt(CFG_BRT_MAP[lvl]));
                }
                inactivity_sec = 0; last_user_action_tick = g_tick_ms;
            } 
            else if (max_both_ms >= KEY_TIME_LONG_PRESS_MS) {
                key_handle_both_long(); 
                inactivity_sec = 0; last_user_action_tick = g_tick_ms;
            } 
            max_both_ms = 0;
            anim_5s_done = false;
        }
    }

    prev_db_minus = db_minus; prev_db_plus = db_plus;
}

void test_mode_task(void) {
    static bool test_mode_active = false;
    bool auth_ok = (g_test_box.magic == 0x54455354 && g_test_box.host_version == g_test_box.version);

    g_test_box.vbatt_mv = g_vbatt_mv_filtered;
    g_test_box.sys_state_mirror = sys_state;
    g_test_box.current_level = sys_memory.params & 0xFF;
    g_test_box.current_brt_val = current_brt_calculated;
    
    // 永远客观反映物理引脚当前是否真实被按下(短路为0返回1)
    g_test_box.raw_key_minus = ((DL_GPIO_readPins(KEY_MINUS_PORT, KEY_MINUS_PIN) & KEY_MINUS_PIN) == 0) ? 1 : 0;
    g_test_box.raw_key_plus  = ((DL_GPIO_readPins(KEY_PLUS_PORT, KEY_PLUS_PIN) & KEY_PLUS_PIN) == 0) ? 1 : 0;
    
    g_test_box.state_is_dimmed = is_dimmed ? 1 : 0;
    g_test_box.state_is_overshot = is_overshot ? 1 : 0;
    g_test_box.state_debug_mode = (sys_memory.features & FLAG_SWD_IN_OFF_STATE) ? 1 : 0;
    
    g_test_box.nvm_is_dirty = nvm_dirty ? 1 : 0;
    g_test_box.state_inactivity_sec = inactivity_sec;
    
    g_test_box.nvm_seq_id = sys_memory.seq_id;
    g_test_box.nvm_sector_addr = current_sector_addr;
    g_test_box.nvm_slot_idx = current_nvm_slot;

    if (auth_ok && sys_state != SYS_TEST_MODE) {
        test_mode_active = true;
        sys_state = SYS_TEST_MODE;
        reset_key_state();
        i2c_init();

        g_test_box.cfg_params = sys_memory.params;
        g_test_box.cfg_features = sys_memory.features;
        g_test_box.cfg_r_base = sys_memory.r_base;
        g_test_box.cfg_r_series = sys_memory.r_series;
        g_test_box.cfg_v_led_fw = sys_memory.v_led_fw;
        g_test_box.cfg_i_max_ua = sys_memory.i_max_ua;
        g_test_box.cfg_batt_p_uw = sys_memory.batt_p_uw;
        g_test_box.cfg_als_min_brt = sys_memory.als_min_brt;
        g_test_box.cfg_lvp_crit = sys_memory.lvp_crit;
        g_test_box.cfg_lvp_ext = sys_memory.lvp_ext;
        g_test_box.status = TEST_ST_IDLE;
        g_test_box.cmd_ack = 0;
    } else if (!auth_ok && sys_state == SYS_TEST_MODE) {
        NVIC_SystemReset(); 
    }

    if (sys_state == SYS_TEST_MODE) {
        calculate_safe_brt(BRT_SCALE_MAX);

        if (g_test_box.cmd != 0) {
            uint32_t c = g_test_box.cmd;
            g_test_box.status = TEST_ST_BUSY;
            g_test_box.cmd = 0;

            if (c == 2) { 
                NVIC_SystemReset();
            } else if (c == 3) { 
                sys_memory.params = g_test_box.cfg_params;
                sys_memory.features = g_test_box.cfg_features & FEATURE_RUNTIME_MASK;
                sys_memory.r_base = g_test_box.cfg_r_base;
                sys_memory.r_series = g_test_box.cfg_r_series;
                sys_memory.v_led_fw = g_test_box.cfg_v_led_fw;
                sys_memory.i_max_ua = g_test_box.cfg_i_max_ua;
                sys_memory.batt_p_uw = g_test_box.cfg_batt_p_uw;
                sys_memory.als_min_brt = g_test_box.cfg_als_min_brt;
                sys_memory.lvp_crit = g_test_box.cfg_lvp_crit;
                sys_memory.lvp_ext = g_test_box.cfg_lvp_ext;
                invalidate_brt_cache();
                g_test_box.status = TEST_ST_OK;
            } else if (c == 4) { 
                mark_nvm_dirty();
                if(save_memory_to_flash()) g_test_box.status = TEST_ST_OK;
                else g_test_box.status = TEST_ST_ERR_FLASH;
            } else if (c == 5) { 
                load_factory_defaults();
                mark_nvm_dirty();
                if(save_memory_to_flash()) g_test_box.status = TEST_ST_OK;
                else g_test_box.status = TEST_ST_ERR_FLASH;

                g_test_box.cfg_params = sys_memory.params;
                g_test_box.cfg_features = sys_memory.features;
                g_test_box.cfg_r_base = sys_memory.r_base;
                g_test_box.cfg_r_series = sys_memory.r_series;
                g_test_box.cfg_v_led_fw = sys_memory.v_led_fw;
                g_test_box.cfg_i_max_ua = sys_memory.i_max_ua;
                g_test_box.cfg_batt_p_uw = sys_memory.batt_p_uw;
                g_test_box.cfg_als_min_brt = sys_memory.als_min_brt;
                g_test_box.cfg_lvp_crit = sys_memory.lvp_crit;
                g_test_box.cfg_lvp_ext = sys_memory.lvp_ext;
                invalidate_brt_cache();
            } else {
                g_test_box.status = TEST_ST_ERR_INV_CMD;
            }
            g_test_box.cmd_ack = c;
        }
    } else {
        if (test_mode_active) {
            test_mode_active = false;
            if (sys_state == SYS_TEST_MODE) sys_state = SYS_FLASH_MODE; 
            g_test_box.ovr_led_mode = 0;
            g_test_box.ovr_key_minus = 0;
            g_test_box.ovr_key_plus = 0;
            g_test_box.ovr_als_en = 0;
            g_test_box.ovr_block_phys_keys = 0; 
            g_test_box.cmd_ack = 0;
        }
    }
}

void GPIOA_IRQHandler(void) {
    DL_GPIO_clearInterruptStatus(PORT_BTN, KEY_MINUS_PIN | KEY_PLUS_PIN);
}

void scheduler_run(void) {
    adc_task();
    protection_task();
    key_task();
    als_task();
    flash_anim_task();
    test_mode_task();
    pwm_fade_task();
    pwm_output_task();
    auto_save_task();
}

int main(void) {
    SYSCFG_DL_init();

    if (g_por_magic != POR_MAGIC) {
        g_por_magic = POR_MAGIC;
        g_flash_mode_used = false;
    }

    DL_SYSTICK_config(24000);
    DL_SYSTICK_enableInterrupt();
    DL_SYSTICK_enable();

#if FEATURE_ALS_MODE
    i2c_init();
#endif

    if (DEV_FORCE_FACTORY_RESET == 1) {
        load_factory_defaults();
        mark_nvm_dirty();
    } else {
        load_memory_from_flash();
    }

    current_pwm = 0;
    target_pwm = 0;
    sys_state = SYS_OFF;

    pwm_output_task();

    DL_TimerG_enableClock(HW_PWM_INST);
    DL_TimerG_startCounter(HW_PWM_INST);

    uint32_t last_tick = 0;

    while (1) {
        kick_watchdog();

        if (sys_state == SYS_OFF &&
           (DL_GPIO_readPins(PORT_BTN, KEY_MINUS_PIN | KEY_PLUS_PIN) == (KEY_MINUS_PIN | KEY_PLUS_PIN)) &&
           key_is_idle() &&
           (g_test_box.magic != 0x54455354) && 
           !(sys_memory.features & FLAG_SWD_IN_OFF_STATE))
        {
            DL_SYSTICK_disable();
            DL_ADC12_disablePower(HW_ADC_INST);
            DL_VREF_disablePower(VREF);

            DL_GPIO_setUpperPinsPolarity(PORT_BTN, DL_GPIO_PIN_27_EDGE_FALL);
            DL_GPIO_setLowerPinsPolarity(PORT_BTN, DL_GPIO_PIN_2_EDGE_FALL);
            DL_GPIO_clearInterruptStatus(PORT_BTN, KEY_MINUS_PIN | KEY_PLUS_PIN);
            DL_GPIO_enableInterrupt(PORT_BTN, KEY_MINUS_PIN | KEY_PLUS_PIN);
            NVIC_EnableIRQ(GPIOA_INT_IRQn);

#if FEATURE_LOWPOWER_STANDBY
            if (sys_memory.features & FLAG_LOWPOWER_STANDBY) {
                DL_SYSCTL_setPowerPolicySTANDBY0(); 
            }
#endif
            __WFI(); 

#if FEATURE_LOWPOWER_STANDBY
            DL_SYSCTL_setPowerPolicyRUN0SLEEP0();
#endif
            DL_GPIO_disableInterrupt(PORT_BTN, KEY_MINUS_PIN | KEY_PLUS_PIN);
            NVIC_DisableIRQ(GPIOA_INT_IRQn);

            DL_VREF_enablePower(VREF);
            DL_ADC12_enablePower(HW_ADC_INST);
            SYSCFG_DL_VREF_init();
            SYSCFG_DL_ADC12_0_init();
            DL_ADC12_enableConversions(HW_ADC_INST);

#if FEATURE_ALS_MODE
            i2c_init();
#endif

            DL_ADC12_startConversion(HW_ADC_INST);
            while (DL_ADC12_getRawInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED) == 0) { kick_watchdog(); }
            uint32_t res = DL_ADC12_getMemResult(HW_ADC_INST, DL_ADC12_MEM_IDX_0);
            DL_ADC12_clearInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);

            uint32_t raw_mv = (res * VBATT_FULL_MV * ADC_GAIN_CAL) / (ADC_MAX_RESOLUTION * 1000);
            g_vbatt_mv_raw = raw_mv;
            g_vbatt_mv_filtered = raw_mv;

            DL_TimerG_enableClock(HW_PWM_INST);
            DL_TimerG_startCounter(HW_PWM_INST);
            g_last_applied_pwm = 0xFFFF;
            pwm_output_task();
            DL_SYSTICK_enable();

            if (g_vbatt_mv_filtered > sys_memory.lvp_ext) {
                inactivity_sec = 0;
            }
        }
        else 
        {
            while (g_tick_ms - last_tick < SYS_TICK_PERIOD_MS) {
                __WFI(); 
            }
            last_tick += SYS_TICK_PERIOD_MS;
            scheduler_run(); 
        }
    }
}