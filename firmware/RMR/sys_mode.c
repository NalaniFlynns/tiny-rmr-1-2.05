#include "sys_mode.h"
#include "sys_led_pwm.h"
#include "sys_battery.h"
#include "hal_opt3001.h"
#include "nvm_flash.h"
#include "app_config.h"
#include "test_mailbox.h"

const uint16_t CFG_BRT_MAP[CFG_MAX_LEVELS] = {5, 20, 50, 150, 300, 450, 600, 800, 1000};
const int16_t AUTO_OFFSET_PCT[5] = {-50, -30, 0, 30, 50};

static uint32_t als_tick = 0;
bool g_is_als_mode = false;
volatile bool g_is_overshot = false; 

#if FEATURE_ALS_SMOOTHING
static uint32_t lux_avg = 0xFFFFFFFF;
static int16_t last_als_brt = -1;
#endif

static uint32_t fast_isqrt(uint32_t n) {
    uint32_t root = 0, bit = 1UL << 30; 
    while (bit > n) bit >>= 2;
    while (bit != 0) { if (n >= root + bit) { n -= root + bit; root = (root >> 1) + bit; } else { root >>= 1; } bit >>= 2; }
    return root;
}

void mode_init(void) {
    g_is_als_mode = ((sys_memory.params >> 8) & 0xFF) == 1;
#if FEATURE_ALS_MODE
    if (g_is_als_mode) { 
        opt3001_trigger_conversion(); 
        als_tick = g_tick_ms; 
#if FEATURE_ALS_SMOOTHING
        lux_avg = 0xFFFFFFFF;
        last_als_brt = -1;
#endif
    }
#endif
}

#if FEATURE_ALS_MODE
/* ALS 亮度曲线(已下调): base = ALS_SQRT_FACTOR * sqrt(lux/100), 最低 ALS_MIN_BRT */
static uint16_t als_lux_to_brt(uint32_t lux) {
    uint32_t lux_int = lux / 100;
    /* ?????? NVM(0 ???????) */
    uint8_t factor = sys_memory.als_sqrt_factor ? sys_memory.als_sqrt_factor : ALS_SQRT_FACTOR;
    uint32_t cap_low  = sys_memory.als_cap_low_x100  ? (uint32_t)sys_memory.als_cap_low_x100 * 100  : ALS_CAP_BRT_LOW_LUX;
    uint32_t cap_high = sys_memory.als_cap_high_x100 ? (uint32_t)sys_memory.als_cap_high_x100 * 100 : ALS_CAP_BRT_HIGH_LUX;
    int32_t base = (int32_t)factor * (int32_t)fast_isqrt(lux_int);
    uint8_t off_idx = (sys_memory.params >> 16) & 0xFF;
    if (off_idx > 4) off_idx = 2;
    int32_t target = base + (base * AUTO_OFFSET_PCT[off_idx]) / 100;
    if (target < (int32_t)sys_memory.als_min_brt) target = sys_memory.als_min_brt;
    if (lux_int <= 10000) { if (target > (int32_t)cap_low) target = (int32_t)cap_low; } 
    else { if (target > (int32_t)cap_high) target = (int32_t)cap_high; }
    if (target > BRT_SCALE_MAX) target = BRT_SCALE_MAX;
    if (target < 0) target = 0;
    return (uint16_t)target;
}
#endif

void mode_task(void) {
    if (g_is_dimmed) return; 

#if FEATURE_ALS_MODE
    bool test_als_ovr = (sys_state == SYS_TEST_MODE && g_test_box.magic == 0x54455354 && g_test_box.ovr_als_en);
    bool auth = (g_test_box.magic == 0x54455354 && g_test_box.host_version == g_test_box.version);
    /* TEST 态保持当前模式: ALS 就是 ALS(走真实传感器曲线), MAN 就是 MAN(走档位), 与真实按键语义一致 */
    bool als_active = test_als_ovr || (g_is_als_mode && (sys_state == SYS_RUN || sys_state == SYS_LVP_CRIT || (sys_state == SYS_TEST_MODE && auth)));
    if (als_active) {
        if (g_tick_ms - als_tick >= TIME_ALS_POLL_INTERVAL_MS) { 
            uint32_t lux = 0;
            if (test_als_ovr) {
                lux = g_test_box.ovr_als_lux;
                g_als_sensor_status = 0;
            } else {
                lux = opt3001_read_lux();
                opt3001_trigger_conversion();
            }
            als_tick = g_tick_ms;

            /* 连续 ALS_ERR_FAIL_COUNT 次失败 -> SYS_ALS_ERR(主循环闪烁提示, 10s 后自恢复) */
            if (lux == 0xFFFFFFFF) {
                g_als_sensor_status = 2;
                if (++g_als_err_cnt >= ALS_ERR_FAIL_COUNT) {
                    g_als_err_cnt = 0;
                    if (sys_state != SYS_TEST_MODE) {
                        sys_state = SYS_ALS_ERR;
                        g_als_err_start_tick = g_tick_ms;
                    }
                }
                return;
            }

            g_als_err_cnt = 0;
            g_als_sensor_status = 0;
            g_als_lux_raw = lux;

#if FEATURE_ALS_SMOOTHING
            if (lux_avg == 0xFFFFFFFF) lux_avg = lux;
            else lux_avg = lux_avg - (lux_avg >> ADC_FILTER_SHIFT) + (lux >> ADC_FILTER_SHIFT);
            uint32_t use_lux = lux_avg;
#else
            uint32_t use_lux = lux;
#endif
            g_als_lux_filtered = use_lux;

            uint16_t target = als_lux_to_brt(use_lux);

#if FEATURE_ALS_SMOOTHING
            if (last_als_brt != -1) {
                int16_t diff = (int16_t)target - last_als_brt;
                if (diff > 0) {
                    uint16_t slew = ALS_MAX_SLEW_RATE;
                    if (last_als_brt < 150) slew = ALS_SLEW_LOW; 
                    else if (last_als_brt < 400) slew = ALS_SLEW_MID; 
                    if (diff > (int16_t)slew) target = (uint16_t)(last_als_brt + slew);
                } else {
                    if (diff < -(int16_t)ALS_MAX_SLEW_RATE) target = (uint16_t)(last_als_brt - ALS_MAX_SLEW_RATE);
                }
            }
            last_als_brt = (int16_t)target;
#endif

            target = battery_get_safe_brt(target);
            led_set_target(target, true);
        }
        return;
    }
#endif /* FEATURE_ALS_MODE */

    if (sys_state != SYS_RUN && sys_state != SYS_LVP_CRIT && sys_state != SYS_TEST_MODE) return;

    uint8_t lvl = sys_memory.params & 0xFF;
    uint16_t target = CFG_BRT_MAP[lvl];
    target = battery_get_safe_brt(target); 
    led_set_target(target, false); 
}

void mode_handle_key(KeyEvent_t evt) {
    if (sys_state != SYS_RUN && sys_state != SYS_LVP_CRIT && sys_state != SYS_TEST_MODE) return;
    
    uint8_t lvl = sys_memory.params & 0xFF;
    uint8_t off_idx = (sys_memory.params >> 16) & 0xFF;
    uint16_t max_brt = battery_get_safe_brt(BRT_SCALE_MAX);

    if (evt == EVT_BT2_SHORT) { 
        if (g_is_als_mode) { 
#if FEATURE_ALS_OFFSET_ADJUST
            if (off_idx < 4) off_idx++; 
#endif
        } else {
            if (lvl < CFG_MAX_LEVELS - 1) {
#if FEATURE_ADAPTIVE_GEAR_LIMIT
                if (CFG_BRT_MAP[lvl+1] <= max_brt) { lvl++; g_is_overshot = false; }
                else if (CFG_BRT_MAP[lvl+1] - max_brt <= SNAP_THRESHOLD_BRT) { lvl++; g_is_overshot = false; } 
                else { lvl++; g_is_overshot = true; } 
#else
                lvl++; g_is_overshot = false;
#endif
            }
        }
    } 
    else if (evt == EVT_BT1_SHORT || evt == EVT_BT1_SHORT_0_8S) { 
        if (g_is_als_mode) { 
#if FEATURE_ALS_OFFSET_ADJUST
            if (off_idx > 0) off_idx--; 
#endif
        } else {
            if (CFG_BRT_MAP[lvl] > max_brt) { 
                while (lvl > 0 && CFG_BRT_MAP[lvl] > max_brt) lvl--;
            } else {
                if (lvl > 0) lvl--; 
            }
            g_is_overshot = false;
        }
    }
    else if (evt == EVT_BOTH_LONG_5S) { 
#if FEATURE_ALS_MODE
        if (g_als_err_lockout) {
            /* 传感器持续故障已锁定: 拒绝切回 ALS, 闪灯提示 */
            led_blink_twice();
        } else {
            g_is_als_mode = !g_is_als_mode;
            led_blink_twice();
            if (g_is_als_mode) { opt3001_trigger_conversion(); als_tick = g_tick_ms; }
        }
#endif
    }

    if (evt != EVT_NONE) {
        sys_memory.params = (off_idx << 16) | (g_is_als_mode << 8) | lvl;
        /* 延迟保存: 只置 dirty 交给后台 30s 自动保存(省 FLASH 磨损);
           关机/LVP/掉电路径仍强制 nvm_save_dirty() */
        nvm_mark_dirty();
    }
}