#ifndef TEST_MAILBOX_H_
#define TEST_MAILBOX_H_
#include <stdint.h>

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
    uint8_t  raw_key_minus;     

    uint8_t  raw_key_plus;      
    uint8_t  state_is_dimmed;
    uint8_t  state_is_overshot;
    uint8_t  state_debug_mode; 

    uint8_t  nvm_is_dirty;
    uint8_t  nvm_save_fail_cnt;
    uint8_t  ovr_block_phys_keys; 
    uint8_t  rst_cause;         /* RSTCAUSE ID of last boot (same offset as old _pad0) */

    uint32_t state_inactivity_sec;
    uint32_t nvm_seq_id;
    uint32_t nvm_sector_addr;
    uint32_t nvm_slot_idx;

    uint8_t  ovr_led_mode;      /* 1=PWM??(0-2399) 2=????????(???PWM???0-2399) 3=?????? */
    uint8_t  ovr_key_minus;     
    uint8_t  ovr_key_plus;      
    uint8_t  ovr_als_en;
    
    uint16_t ovr_brt_val;       
    uint16_t ovr_pwm_val;       
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
    uint8_t  cfg_als_sqrt_factor;   /* 0=??????? */
    uint8_t  cfg_als_cap_low_x100;  /* 0=??????? */
    uint8_t  cfg_als_cap_high_x100; /* 0=??????? */
    uint8_t  _pad1[1];
    
    char     fw_version_str[16];
    uint32_t vbatt_raw_mv;      /* 原始 ADC 电压(未滤波), 0xB0 */
    uint32_t est_hw_power_uw;   /* hw power (batt side) = vbatt_raw x i_avg, 0xB4 */
    uint32_t sys_clk_khz;       /* 恒定 24000: MSPM0C110x SYSOSC 无法真正降频, 0xB8 */
    uint32_t boot_refuse_reason;/* 最近一次开机被拒绝的原因, 0xBC */
    uint32_t standby_entry_cnt; /* 本次上电后进入 STANDBY1 深睡的次数(SRAM 保留, 唤醒后可读), 0xC0 */
} Test_Mailbox_t;

/* boot_refuse_reason 取值 */
#define BOOT_REFUSE_NONE        0   /* 无拒绝/最近一次开机成功 */
#define BOOT_REFUSE_VOLT        1   /* 测压门未达标: V <= lvp_ext + 100mV */
#define BOOT_REFUSE_OFF_INTENT  2   /* 热复位且 off-intent: 保持关机(仅影响自动开机, 可按键开机) */
#define BOOT_REFUSE_AUTO_FLAG   3   /* FLAG_AUTO_POWER_ON 未使能(仍可按键开机) */

extern volatile Test_Mailbox_t g_test_box;
void test_mailbox_task(void);
/* 强制退出测试态: 清除授权/覆盖/本地标志, 用于自动关机等路径 */
void test_box_exit_test_mode(void);

#endif