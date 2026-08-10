#include "app_config.h"
#include "hal_keys.h"
#include "hal_opt3001.h"
#include "sys_battery.h"
#include "sys_led_pwm.h"
#include "sys_mode.h"
#include "nvm_flash.h"
#include "test_mailbox.h"

#if defined(__clang__) || defined(__GNUC__) || defined(__TI_COMPILER_VERSION__)
__attribute__((used, section(".fw_version")))
const char g_fw_version_flash[16] = FW_VERSION_STR;
#else
#pragma DATA_SECTION(g_fw_version_flash, ".fw_version")
#pragma RETAIN(g_fw_version_flash)
const char g_fw_version_flash[16] = FW_VERSION_STR;
#endif

#define POR_MAGIC 0x5AA5C33C

#if defined(__clang__) || defined(__GNUC__) || defined(__TI_COMPILER_VERSION__)
uint32_t g_por_magic __attribute__((section(".TI.noinit")));
bool g_flash_mode_used __attribute__((section(".TI.noinit")));
#elif defined(__GNUC__)
uint32_t g_por_magic __attribute__((section(".noinit")));
bool g_flash_mode_used __attribute__((section(".noinit")));
#else
uint32_t g_por_magic __attribute__((section(".noinit")));
bool g_flash_mode_used __attribute__((section(".noinit")));
#endif

volatile uint32_t g_tick_ms = 0;
volatile uint32_t g_inactivity_sec = 0;
volatile bool g_is_dimmed = false;

SysState_t sys_state = SYS_OFF;
NVM_Data_t sys_memory;

volatile char g_debug_str[64] = "Initializing..."; 
static uint32_t str_idx = 0;

void GPIOA_IRQHandler(void) {
    DL_GPIO_clearInterruptStatus(PORT_BTN, PIN_BT1 | PIN_BT2);
}

static void append_str(const char* s) { while (*s && str_idx < 63) { g_debug_str[str_idx++] = *s++; } }
static void append_int(uint32_t val, uint8_t min_digits) {
    char temp[12]; int i = 0;
    if (val == 0) { temp[i++] = '0'; } 
    else { while (val > 0) { temp[i++] = (val % 10) + '0'; val /= 10; } }
    while (i < min_digits) { temp[i++] = '0'; }
    while (i > 0 && str_idx < 63) { g_debug_str[str_idx++] = temp[--i]; }
}
static void update_debug_string(void) {
    str_idx = 0;
    if (sys_state == SYS_OFF) append_str("[OFF] ");
    else if (sys_state == SYS_RUN) append_str(g_is_als_mode ? "[ALS] " : "[MAN] ");
    else if (sys_state == SYS_TEST_MODE) append_str("[TST] ");
    else if (sys_state == SYS_FLASH_MODE) append_str("[FLS] ");
    else if (sys_state == SYS_ALS_ERR) append_str("[ERR] ");
    else append_str("[LVP] ");
    
    append_str("Lvl:"); append_int(sys_memory.params & 0xFF, 1);
    append_str(" Ofs:"); append_int((sys_memory.params >> 16) & 0xFF, 1);
    append_str(" V:"); append_int(g_vbatt_mv_filtered, 4);
    append_str("mV Max:"); append_int(battery_get_safe_brt(BRT_SCALE_MAX), 1);
    append_str(" P:"); append_int(g_last_applied_pwm, 4);

    while (str_idx < 63) { g_debug_str[str_idx++] = ' '; } g_debug_str[63] = '\0';
}

int main(void) {
    SYSCFG_DL_init();
    __enable_irq();
    
    if (g_por_magic != POR_MAGIC) {
        g_por_magic = POR_MAGIC;
        g_flash_mode_used = false;
#if DEV_CLEAR_NVM_ON_POR
        /* 烧录后首次上电(POR): 清空存储区恢复出厂 */
        nvm_force_factory_reset();
#endif
    }

    nvm_init_and_load(); 
    opt3001_init();
    DL_GPIO_setPins(PORT_OUTPUT, PIN_VCC_EN);
    DL_TimerG_startCounter(HW_PWM_INST);

    sys_state = SYS_OFF;
    static uint8_t pwr_loss_cnt = 0;
    static uint8_t lvp_ext_cnt = 0;
    static uint8_t lvp_crit_cnt = 0;
    static uint32_t flash_mode_tick = 0;

#if FEATURE_AUTO_POWER_ON
    if (sys_memory.features & FLAG_AUTO_POWER_ON) {
        if (battery_startup_check()) {
            sys_state = SYS_RUN;
            g_inactivity_sec = 0;
            g_is_dimmed = false;
            mode_init();
        }
    }
#endif

    while (1) {
        delay_cycles(CPU_CYCLES_PER_MS);
        g_tick_ms++;

        DL_WWDT_restart(WWDT0_INST);

        KeyEvent_t key = keys_task();
        battery_task();
        test_mailbox_task(); 
        
        if (g_tick_ms % 100 == 0) update_debug_string();
        
        /* 掉电保护: 保存配置后进 SHUTDOWN */
        if (g_vbatt_mv_raw < BATT_POWER_LOSS_MV) {
            if (++pwr_loss_cnt >= POWERLOSS_COUNT) {
                led_set_target(0, false); led_update_task();
                DL_TimerG_stopCounter(HW_PWM_INST);
                DL_GPIO_clearPins(PORT_OUTPUT, PIN_VCC_EN);
                nvm_save_dirty(); 
                DL_SYSCTL_setPowerPolicySHUTDOWN();
                while(1) { __WFI(); } 
            }
        } else { pwr_loss_cnt = 0; }

        /* 无操作自动调暗/关机 */
#if FEATURE_INACTIVITY_AUTO_DIM_OFF
        if (g_tick_ms % TIME_SEC_MS == 0) {
            if (sys_state == SYS_RUN && (sys_memory.features & FLAG_INACTIVITY_AUTO_DIM)) {
                g_inactivity_sec++;
                if (!g_is_dimmed && g_inactivity_sec > TIME_AUTO_DIM_S) { 
                    g_is_dimmed = true; 
                }
                if (g_inactivity_sec > TIME_AUTO_DIM_S + TIME_AUTO_SHUTDOWN_S) { 
                    sys_state = SYS_OFF; 
                    nvm_save_dirty(); 
                }
            }
        }
#endif

        /* LVP 低电压保护(5 次计数去抖) */
        if (sys_state != SYS_OFF && sys_state != SYS_TEST_MODE) {
            if (g_vbatt_mv_filtered < sys_memory.lvp_ext) {
                if (++lvp_ext_cnt >= LVP_EXT_COUNT) {
                    lvp_ext_cnt = 0;
                    sys_state = SYS_OFF;
                    nvm_save_dirty(); 
                }
            } else { lvp_ext_cnt = 0; }

            if (g_vbatt_mv_filtered < sys_memory.lvp_crit) {
                if (++lvp_crit_cnt >= LVP_CRIT_COUNT) {
                    lvp_crit_cnt = 0;
                    if (sys_state == SYS_RUN || sys_state == SYS_ALS_ERR) sys_state = SYS_LVP_CRIT;
                }
            } else { lvp_crit_cnt = 0; }

            /* ????(???)???? LVP_CRIT, ???????????? LED ?? */
            if (sys_state == SYS_LVP_CRIT && g_vbatt_mv_filtered >= (sys_memory.lvp_crit + BATT_STARTUP_HYSTERESIS_MV)) {
                sys_state = SYS_RUN;
                g_inactivity_sec = 0;
                g_is_dimmed = false;
                mode_init();
            }
        }

        if (sys_state == SYS_OFF) {
            led_set_target(0, false);
            
            if (key == EVT_BT1_SHORT_0_8S && !g_flash_mode_used) {
                if (g_vbatt_mv_filtered >= sys_memory.lvp_ext) {   /* 测压达标才进入 Flash 模式 */
                    g_flash_mode_used = true;
                    flash_mode_tick = 0;
                    sys_state = SYS_FLASH_MODE;
                    led_blink_twice(); 
#if FEATURE_RESTORE_NRST_IN_FLASH
                    DL_GPIO_setPins(PORT_OUTPUT, PIN_VCC_EN);
                    delay_cycles(CPU_CYCLES_PER_MS);
#endif
                }
            }
            else if (key == EVT_BOTH_LONG_1_5S) {
                if (battery_startup_check()) {   /* 测压达标才启动 */
                    sys_state = SYS_RUN;
                    g_inactivity_sec = 0;
                    g_is_dimmed = false;
                    mode_init();
                } else {
                    led_set_target(100, false); led_update_task(); delay_cycles(CPU_CYCLES_PER_MS * STARTUP_FAIL_BLINK_DELAY_MS);
                    led_set_target(0, false); led_update_task();
                }
            }

            if (sys_state == SYS_OFF && key_is_idle() && !(sys_memory.features & FLAG_SWD_IN_OFF_STATE)) {
                DL_ADC12_disablePower(HW_ADC_INST);
                DL_VREF_disablePower(VREF);

                DL_GPIO_setUpperPinsPolarity(PORT_BTN, DL_GPIO_PIN_27_EDGE_FALL);
                DL_GPIO_setLowerPinsPolarity(PORT_BTN, DL_GPIO_PIN_2_EDGE_FALL);
                DL_GPIO_clearInterruptStatus(PORT_BTN, PIN_BT1 | PIN_BT2);
                DL_GPIO_enableInterrupt(PORT_BTN, PIN_BT1 | PIN_BT2);
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
                DL_GPIO_disableInterrupt(PORT_BTN, PIN_BT1 | PIN_BT2);
                NVIC_DisableIRQ(GPIOA_INT_IRQn);
                
                battery_resume();   /* 唤醒后测压 */
                if (g_vbatt_mv_filtered > sys_memory.lvp_ext) {
                    g_inactivity_sec = 0;
                }
            }
        } 
        else if (sys_state == SYS_RUN || sys_state == SYS_LVP_CRIT) {
            if (key == EVT_BOTH_LONG_1_5S) {
                sys_state = SYS_OFF;
                nvm_save_dirty();
            } else {
                mode_handle_key(key);
                mode_task();
            }
        }
        else if (sys_state == SYS_ALS_ERR) {
            uint32_t dt = g_tick_ms - g_als_err_start_tick;
            if (dt >= TIME_ALS_ERR_RECOVER_MS) {
                /* 10s 后自恢复: 关闭 ALS 使能回到手动模式 */
                sys_state = SYS_RUN;
                g_is_als_mode = false;
                sys_memory.params &= ~(1 << 8);
                nvm_mark_dirty();
                nvm_save_dirty();
                uint8_t lvl = sys_memory.params & 0xFF;
                led_set_target(battery_get_safe_brt(CFG_BRT_MAP[lvl]), false);
            } else {
                uint32_t mod = dt % ALS_ERR_BLINK_PERIOD_MS;
                uint16_t pwm = (mod < ALS_ERR_BLINK_ON_MS) ?
                    (uint16_t)(PWM_REG_MAX - (PWM_FLASH_LEVEL * PWM_REG_MAX) / BRT_SCALE_MAX) : PWM_REG_MAX;
                g_last_applied_pwm = pwm;
                DL_TimerG_setCaptureCompareValue(HW_PWM_INST, pwm, HW_PWM_INDEX);
            }
        }
        else if (sys_state == SYS_FLASH_MODE) {
            if (++flash_mode_tick > TIME_FLASH_MODE_TIMEOUT_MS) NVIC_SystemReset();
        }
        else if (sys_state == SYS_TEST_MODE && g_test_box.magic == 0x54455354) {
            if (g_test_box.ovr_led_mode == 1) {
                DL_TimerG_setCaptureCompareValue(HW_PWM_INST, PWM_REG_MAX - g_test_box.ovr_pwm_val, HW_PWM_INDEX);
                g_last_applied_pwm = PWM_REG_MAX - g_test_box.ovr_pwm_val;
            } else if (g_test_box.ovr_led_mode == 2) {
                /* 无限制亮度注入: brt->PWM 映射, 输出钳位 0..PWM_REG_MAX */
                uint32_t pwm = battery_brt_to_pwm(g_test_box.ovr_brt_val);
                if (pwm > PWM_REG_MAX) pwm = PWM_REG_MAX;
                DL_TimerG_setCaptureCompareValue(HW_PWM_INST, (uint16_t)pwm, HW_PWM_INDEX);
                g_last_applied_pwm = (uint16_t)pwm;
            } else if (g_test_box.ovr_led_mode == 3) {
                led_set_target(g_test_box.ovr_brt_val, false);
            } else {
#if FEATURE_ALS_MODE
                if (g_test_box.ovr_als_en || g_is_als_mode) {
                    mode_task();   /* ALS 注入/真实 ALS 模式: 按键偏移后立即重算亮度 */
                } else
#endif
                {
                    uint8_t lvl = sys_memory.params & 0xFF;
                    led_set_target(battery_get_safe_brt(CFG_BRT_MAP[lvl]), false);   /* 手动模式: 挡位亮度, 与真实按键一致 */
                }
            }
            /* 测试态响应模拟键: 短按调挡位/偏移 + 5s 长按切换 ALS<->手动, 与真实按键一致 */
            if (key == EVT_BT1_SHORT || key == EVT_BT1_SHORT_0_8S || key == EVT_BT2_SHORT ||
                key == EVT_BOTH_LONG_5S) {
                mode_handle_key(key);
            }
        }

        if (sys_state != SYS_ALS_ERR && (sys_state != SYS_TEST_MODE || (g_test_box.ovr_led_mode != 1 && g_test_box.ovr_led_mode != 2))) {
            led_update_task(); 
        }
        nvm_background_task();
    }
}