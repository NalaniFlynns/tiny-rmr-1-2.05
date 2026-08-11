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
} Test_Mailbox_t;

extern volatile Test_Mailbox_t g_test_box;
void test_mailbox_task(void);

#endif