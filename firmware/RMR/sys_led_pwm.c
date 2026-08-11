#include "sys_led_pwm.h"
#include "app_config.h"
#include "sys_battery.h"

volatile uint16_t g_current_brt = 0;
volatile uint16_t g_last_applied_pwm = PWM_REG_MAX; 
static uint16_t target_brt = 0;
static uint32_t fade_tick = 0;
static bool smooth_mode = false;

void led_set_target(uint16_t brt, bool smooth) {
    target_brt = brt;
    smooth_mode = smooth;
}

void led_update_task(void) {
    if (sys_state == SYS_OFF) {
        g_last_applied_pwm = PWM_REG_MAX;
        DL_TimerG_setCaptureCompareValue(HW_PWM_INST, PWM_REG_MAX, HW_PWM_INDEX);
        return;
    }

    if (g_is_dimmed) target_brt = DIM_LEVEL;

    if (smooth_mode && g_current_brt != target_brt) {
        if (g_tick_ms - fade_tick >= PWM_FADE_INTERVAL_MS) {
            fade_tick = g_tick_ms;
            int16_t diff = (int16_t)target_brt - (int16_t)g_current_brt;
            if (diff > 0) g_current_brt += (diff > (int16_t)PWM_FADE_STEP_DIV) ? (uint16_t)(diff / PWM_FADE_STEP_DIV) : 1;
            else g_current_brt += (diff < -(int16_t)PWM_FADE_STEP_DIV) ? (int16_t)(diff / PWM_FADE_STEP_DIV) : -1;
        }
    } else {
        g_current_brt = target_brt;
    }

    uint16_t final_brt = g_current_brt;

    bool lvp_flash = false;
    if (sys_state == SYS_LVP_CRIT) {
#if FEATURE_LVP_FLASH_WARNING
        if ((sys_memory.features & FLAG_LVP_FLASH_WARNING) &&
            ((g_tick_ms % LVP_FLASH_PERIOD_MS) < LVP_FLASH_ON_TIME_MS)) {
            final_brt = final_brt / LVP_FLASH_LEVEL_DIV;
            lvp_flash = true;
        }
#else
        if ((g_tick_ms % LVP_FLASH_PERIOD_MS) < LVP_FLASH_ON_TIME_MS) {
            final_brt = final_brt / LVP_FLASH_LEVEL_DIV;
            lvp_flash = true;
        }
#endif
    }

#if POWER_SAVE_BUILD
    /* 省电版: 亮度/状态/闪烁相位未变化时不重算 PWM(原每 tick 64 位乘除+写寄存器) */
    static uint16_t last_brt = 0xFFFF;
    static uint8_t  last_state = 0xFF;
    static bool     last_lvp_flash = false;
    if (final_brt != last_brt || (uint8_t)sys_state != last_state || lvp_flash != last_lvp_flash) {
        last_brt = final_brt; last_state = (uint8_t)sys_state; last_lvp_flash = lvp_flash;
        uint16_t pwm_val = battery_brt_to_pwm(final_brt);
        g_last_applied_pwm = pwm_val;
        DL_TimerG_setCaptureCompareValue(HW_PWM_INST, pwm_val, HW_PWM_INDEX);
    }
#else
    uint16_t pwm_val = battery_brt_to_pwm(final_brt);
    g_last_applied_pwm = pwm_val; 
    
    DL_TimerG_setCaptureCompareValue(HW_PWM_INST, pwm_val, HW_PWM_INDEX);
#endif
}

void led_blink_twice(void) {
#if FEATURE_FLASH_ANIMATION
    for(int i=0; i<2; i++) {
        uint16_t flash_pwm = PWM_REG_MAX - (LED_BLINK_BRT * PWM_REG_MAX / BRT_SCALE_MAX);
        g_last_applied_pwm = flash_pwm;
        DL_TimerG_setCaptureCompareValue(HW_PWM_INST, flash_pwm, HW_PWM_INDEX);
        delay_cycles(CPU_CYCLES_PER_MS * 100);
        
        g_last_applied_pwm = PWM_REG_MAX;
        DL_TimerG_setCaptureCompareValue(HW_PWM_INST, PWM_REG_MAX, HW_PWM_INDEX);
        delay_cycles(CPU_CYCLES_PER_MS * 100);
    }
#endif
}