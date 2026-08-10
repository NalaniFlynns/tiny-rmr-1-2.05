#include "test_mailbox.h"
#include "app_config.h"
#include "nvm_flash.h"
#include "sys_battery.h"
#include "sys_led_pwm.h"
#include "sys_mode.h"
#include "hal_opt3001.h"
#include <ti/devices/msp/msp.h>

volatile Test_Mailbox_t g_test_box __attribute__((used)) = {
    .magic = 0, .version = 0x0100, .fw_version_str = FW_VERSION_STR
};

static bool test_mode_active = false;

static void test_box_sync_cfg(void) {
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
    g_test_box.cfg_als_sqrt_factor = sys_memory.als_sqrt_factor;
    g_test_box.cfg_als_cap_low_x100 = sys_memory.als_cap_low_x100;
    g_test_box.cfg_als_cap_high_x100 = sys_memory.als_cap_high_x100;
}
void test_mailbox_task(void) {
    bool auth_ok = (g_test_box.magic == TEST_MAGIC && g_test_box.host_version == g_test_box.version);

    /* ===== 实时监视器刷新(每 tick) ===== */
    g_test_box.vbatt_mv = g_vbatt_mv_filtered;
    g_test_box.vbatt_raw_mv = g_vbatt_mv_raw;
    g_test_box.sys_state_mirror = sys_state;
    g_test_box.current_level = sys_memory.params & 0xFF;
    g_test_box.current_brt_val = g_current_brt; 
    g_test_box.current_pwm_val = g_last_applied_pwm; 
    g_test_box.est_i_peak_ua = g_est_i_peak_ua;
        /* 平均电流 = 实测峰值电流 × 实际 PWM 占空比(0..1000‰), 反映真实负载而非设定值 */
    uint32_t duty_mille = ((uint32_t)(PWM_REG_MAX - g_last_applied_pwm) * 1000u) / (uint32_t)PWM_REG_MAX;
    g_test_box.est_i_avg_ua = (uint32_t)(((uint64_t)g_est_i_peak_ua * duty_mille) / 1000u);
    g_test_box.est_v_led_mv = sys_memory.v_led_fw;
    g_test_box.est_p_led_uw = (uint32_t)(((uint64_t)sys_memory.v_led_fw * g_test_box.est_i_avg_ua) / 1000ULL);
    g_test_box.dyn_r_mohm = g_dyn_r_mohm;
    g_test_box.safe_brt_out = g_safe_brt_out;
    g_test_box.limit_i_led = g_limit_i_led;
    g_test_box.limit_v_drop = g_limit_v_drop;
    g_test_box.limit_i_brt = g_limit_i_brt;
    g_test_box.limit_p_avg = g_limit_p_avg;
    g_test_box.als_lux_raw = g_als_lux_raw;
    g_test_box.als_lux_filtered = g_als_lux_filtered;
    g_test_box.als_err_cnt = g_als_err_cnt;
    g_test_box.sensor_status = g_als_sensor_status;
    g_test_box.raw_key_minus = ((DL_GPIO_readPins(PORT_BTN, PIN_BT1) & PIN_BT1) == 0) ? 1 : 0;
    g_test_box.raw_key_plus  = ((DL_GPIO_readPins(PORT_BTN, PIN_BT2) & PIN_BT2) == 0) ? 1 : 0;
    g_test_box.state_is_dimmed = g_is_dimmed ? 1 : 0;
    g_test_box.state_is_overshot = g_is_overshot ? 1 : 0;
    g_test_box.state_debug_mode = (sys_memory.features & FLAG_SWD_IN_OFF_STATE) ? 1 : 0;
    g_test_box.nvm_is_dirty = nvm_is_dirty() ? 1 : 0;
    g_test_box.nvm_save_fail_cnt = nvm_get_save_fail_cnt();
    g_test_box.state_inactivity_sec = g_inactivity_sec;
    g_test_box.nvm_seq_id = sys_memory.seq_id;
    g_test_box.nvm_sector_addr = nvm_get_sector_addr();
    g_test_box.nvm_slot_idx = nvm_get_slot_idx();

    if (auth_ok && sys_state != SYS_TEST_MODE) {
        test_mode_active = true;
        sys_state = SYS_TEST_MODE;
        opt3001_init();
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
        g_test_box.cfg_als_sqrt_factor = sys_memory.als_sqrt_factor;
        g_test_box.cfg_als_cap_low_x100 = sys_memory.als_cap_low_x100;
        g_test_box.cfg_als_cap_high_x100 = sys_memory.als_cap_high_x100;
        g_test_box.status = TEST_ST_IDLE;
        g_test_box.cmd_ack = 0;
    } else if (!auth_ok && sys_state == SYS_TEST_MODE) {
        NVIC_SystemReset();   /* 授权丢失 -> 复位 */
    }

    if (sys_state == SYS_TEST_MODE && g_test_box.cmd != 0) {
        uint32_t c = g_test_box.cmd;
        g_test_box.status = TEST_ST_BUSY;
        g_test_box.cmd = 0;
        if (c == 2) {
            NVIC_SystemReset();
        } else if (c == 3) {   /* 写配置(mask 过滤) */
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
            /* ALS ????: 1..20, 0 ????????? */
            sys_memory.als_sqrt_factor = (g_test_box.cfg_als_sqrt_factor > 20) ? 20 : g_test_box.cfg_als_sqrt_factor;
            sys_memory.als_cap_low_x100 = (g_test_box.cfg_als_cap_low_x100 > 20) ? 20 : g_test_box.cfg_als_cap_low_x100;
            sys_memory.als_cap_high_x100 = (g_test_box.cfg_als_cap_high_x100 > 20) ? 20 : g_test_box.cfg_als_cap_high_x100;
            g_test_box.status = TEST_ST_OK;  test_box_sync_cfg();  /* 同步镜像, 供 GUI 读回 */
        } else if (c == 4) {   /* 保存 */
            nvm_mark_dirty();
            if (nvm_save_dirty()) g_test_box.status = TEST_ST_OK;
            else g_test_box.status = TEST_ST_ERR_FLASH;
            test_box_sync_cfg();  /* 保存后同步镜像, 供 GUI 读回 */
        } else if (c == 5) {   /* 恢复出厂 + 保存 */
            nvm_force_factory_reset();
            if (nvm_save_dirty()) g_test_box.status = TEST_ST_OK;
            else g_test_box.status = TEST_ST_ERR_FLASH;
            test_box_sync_cfg();  /* 保存后同步镜像, 供 GUI 读回 */
        } else if (c == 6) {   /* 开机(测压达标)并退出测试模式 */
            bool power_on = true;
            if (sys_state == SYS_OFF) {
                power_on = battery_startup_check();
                if (power_on) {
                    sys_state = SYS_RUN;
                    g_inactivity_sec = 0;
                    g_is_dimmed = false;
                    mode_init();
                }
            } else {
                sys_state = SYS_RUN;   /* 已在运行/测试态: 直接保持运行 */
            }
            test_mode_active = false;
            g_test_box.magic = 0;   /* 退出测试模式, 保持 RUN */
            g_test_box.ovr_led_mode = 0;
            g_test_box.ovr_key_minus = 0;
            g_test_box.ovr_key_plus = 0;
            g_test_box.ovr_als_en = 0;
            g_test_box.ovr_block_phys_keys = 0;
            g_test_box.status = power_on ? TEST_ST_OK : TEST_ST_ERR_INV_CMD;
        } else if (c == 7) {   /* 关机并退出测试模式 */
            if (sys_state != SYS_OFF) {
                sys_state = SYS_OFF;
                nvm_mark_dirty();
                nvm_save_dirty();
            }
            test_mode_active = false;
            g_test_box.magic = 0;
            g_test_box.ovr_led_mode = 0;
            g_test_box.ovr_key_minus = 0;
            g_test_box.ovr_key_plus = 0;
            g_test_box.ovr_als_en = 0;
            g_test_box.ovr_block_phys_keys = 0;
            g_test_box.status = TEST_ST_OK;
        } else {
            g_test_box.status = TEST_ST_ERR_INV_CMD;
        }
        g_test_box.cmd_ack = c;
    }

    /* 测试态: 每 tick 刷新诊断计算数据 */
    if (sys_state == SYS_TEST_MODE) {
        battery_get_safe_brt(BRT_SCALE_MAX);
    }

    /* 正常退出测试模式 -> SYS_FLASH_MODE */
    if (test_mode_active && sys_state != SYS_TEST_MODE) {
        test_mode_active = false;
        sys_state = SYS_FLASH_MODE;
        g_test_box.ovr_led_mode = 0;
        g_test_box.ovr_key_minus = 0;
        g_test_box.ovr_key_plus = 0;
        g_test_box.ovr_als_en = 0;
        g_test_box.ovr_block_phys_keys = 0;
        g_test_box.cmd_ack = 0;
    }
}
