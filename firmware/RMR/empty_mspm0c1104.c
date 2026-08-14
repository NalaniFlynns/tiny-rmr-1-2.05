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
bool g_off_intent __attribute__((section(".TI.noinit")));
#elif defined(__GNUC__)
uint32_t g_por_magic __attribute__((section(".noinit")));
bool g_flash_mode_used __attribute__((section(".noinit")));
bool g_off_intent __attribute__((section(".noinit")));
#else
uint32_t g_por_magic __attribute__((section(".noinit")));
bool g_flash_mode_used __attribute__((section(".noinit")));
bool g_off_intent __attribute__((section(".noinit")));
#endif

volatile uint32_t g_tick_ms = 0;
volatile uint32_t g_rst_cause = 0;   /* SYSCTL RSTCAUSE ID, captured once at boot (read-to-clear) */

/* 1ms ????: MSPM0C1103/1104 ??? SysTick(TI E2E ??, ???????? 0),
   ????? GPTIMER14 ?????????????????? >1ms(????),
   ?????????????"??"??(?? 5s ????/1.5s ??/LVP/ALS ???)???? */
static void tick_timer_init(void) {
    const DL_TimerG_ClockConfig tickClockConfig = {
        .clockSel = DL_TIMER_CLOCK_BUSCLK,
        .divideRatio = DL_TIMER_CLOCK_DIVIDE_1,
        .prescale = 0U
    };
    const DL_TimerG_TimerConfig tickTimerConfig = {
        .timerMode = DL_TIMER_TIMER_MODE_PERIODIC_UP,
        .period = CPUCLK_FREQ / 1000u - 1u,   /* 24MHz BUSCLK -> 1ms */
        .startTimer = DL_TIMER_STOP,
        .genIntermInt = DL_TIMER_INTERM_INT_DISABLED,
        .counterVal = 0U
    };
    DL_TimerG_enablePower(TIMG14);
    delay_cycles(POWER_STARTUP_DELAY);
    DL_TimerG_setClockConfig(TIMG14, (DL_TimerG_ClockConfig *) &tickClockConfig);
    DL_TimerG_initTimerMode(TIMG14, (DL_TimerG_TimerConfig *) &tickTimerConfig);
    DL_TimerG_enableInterrupt(TIMG14, DL_TIMER_INTERRUPT_ZERO_EVENT);
    NVIC_EnableIRQ(TIMG14_INT_IRQn);
    DL_TimerG_startCounter(TIMG14);
}

#if POWER_SAVE_BUILD
/* ECO 动态降频: 非运行态(关机/FLASH) SYSOSC 切 4MHz 低功耗模式, 运行态恢复 24MHz;
   GPTIMER14 同步重配 LOAD, g_tick_ms 恒为真实 1ms(按键/超时/轮询计时不变) */
static void sysclk_set_freq(bool high) {
    DL_TimerG_stopCounter(TIMG14);
    if (high) {
        DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_BASE);
        DL_TimerG_setLoadValue(TIMG14, CPUCLK_FREQ / 1000u - 1u);
    } else {
        DL_SYSCTL_setSYSOSCFreq(DL_SYSCTL_SYSOSC_FREQ_4M);
        DL_TimerG_setLoadValue(TIMG14, 4000000u / 1000u - 1u);
    }
    delay_cycles(1000);   /* 等 SYSOSC gear shift 稳定 */
    DL_TimerG_startCounter(TIMG14);
}
#endif

void TIMG14_IRQHandler(void) {
    DL_TimerG_clearInterruptStatus(TIMG14, DL_TIMER_INTERRUPT_ZERO_EVENT);
    g_tick_ms++;
}
volatile uint32_t g_inactivity_sec = 0;
volatile bool g_is_dimmed = false;

SysState_t sys_state = SYS_OFF;
NVM_Data_t sys_memory;

volatile char g_debug_str[64] = "Initializing..."; 
static uint32_t str_idx = 0;

void GPIOA_IRQHandler(void) {
    DL_GPIO_clearInterruptStatus(PORT_BTN, PIN_BT1 | PIN_BT2);
}

#if !POWER_SAVE_BUILD
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
    append_str(" Rst:"); append_int(g_rst_cause, 2);

    while (str_idx < 63) { g_debug_str[str_idx++] = ' '; } g_debug_str[63] = '\0';
}
#endif

int main(void) {
    SYSCFG_DL_init();
    __enable_irq();
    /* Reset cause (read-to-clear, so capture before anything else reads it):
       distinguish cold boot (POR/BOR/SHUTDOWN-exit) from soft resets
       (WWDT violation, SYSRST, debugger, etc.). */
    g_rst_cause = SYSCTL->SOCLOCK.RSTCAUSE & SYSCTL_RSTCAUSE_ID_MASK;
    /* Cold boot = real power-on. Two independent signals, OR-ed:
       - RSTCAUSE reports POR/BOR/SHUTDOWN-exit (may be consumed by the boot
         ROM before main(), in which case it reads NORST=0);
       - retained SRAM magic is lost on a true power-off (SRAM dies), so a
         fresh magic proves power was actually removed. */
    bool cold_boot =
        (g_rst_cause == SYSCTL_RSTCAUSE_ID_PORHWFAIL) ||
        (g_rst_cause == SYSCTL_RSTCAUSE_ID_POREXNRST) ||
        (g_rst_cause == SYSCTL_RSTCAUSE_ID_PORSW) ||
        (g_rst_cause == SYSCTL_RSTCAUSE_ID_BORSUPPLY) ||
        (g_rst_cause == SYSCTL_RSTCAUSE_ID_BORWAKESHUTDN);
    if (g_por_magic != POR_MAGIC) cold_boot = true;
#if POWER_SAVE_BUILD
    /* ECO: 启动后先断 ADC/VREF 寄存器电源域, 首次采样/开机测压前再上电(配置保持, 仅门控) */
    DL_ADC12_disablePower(HW_ADC_INST);
    DL_VREF_disablePower(VREF);
#endif

    tick_timer_init();  /* GPTIMER14 1ms 硬件时基 (MSPM0C1104 无硬件 SysTick) */
    
    if (g_por_magic != POR_MAGIC) {
        g_por_magic = POR_MAGIC;
        g_flash_mode_used = false;
        g_off_intent = false;   /* fresh power-on: AUTO_POWER_ON is allowed */
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
    static bool g_off_pending = false;   /* 1.5s ?????????, ???? standby */

#if FEATURE_AUTO_POWER_ON
    /* Cold boot always honors AUTO_POWER_ON. Warm resets only auto-boot when
       the device was NOT intentionally off before the reset (e.g. debugger /
       flash reset while running); WWDT/SYSRST while OFF stay off - this is
       what keeps the LED dark after a 1.5s dual-key shutdown. */
    if (cold_boot || !g_off_intent) {
        if (sys_memory.features & FLAG_AUTO_POWER_ON) {
            if (battery_startup_check()) {
                sys_state = SYS_RUN;
                g_inactivity_sec = 0;
                g_is_dimmed = false;
                g_off_intent = false;
                mode_init();
            }
        }
    }
#endif

    while (1) {
#if POWER_SAVE_BUILD || DEBUG_LP_BUILD
        __WFI();   /* 省电版: 睡眠等待 GPTIMER14 1ms tick 中断唤醒, 替代 24MHz 忙等 */
#else
        delay_cycles(CPU_CYCLES_PER_MS);
#endif

#if POWER_SAVE_BUILD
        /* ECO 动态降频: 运行态 24MHz, 关机/FLASH 态 SYSOSC 4MHz */
        {   static uint8_t sysclk_high = 1;
            bool want_high = (sys_state == SYS_RUN || sys_state == SYS_LVP_CRIT ||
                              sys_state == SYS_ALS_ERR || sys_state == SYS_TEST_MODE);
            if (want_high != (sysclk_high != 0)) {
                sysclk_high = want_high ? 1 : 0;
                sysclk_set_freq(want_high);
            }
        }
#endif

        DL_WWDT_restart(WWDT0_INST);

        KeyEvent_t key = keys_task();
        battery_task();
        test_mailbox_task(); 
        
#if !POWER_SAVE_BUILD
        if (g_tick_ms % 100 == 0) update_debug_string();
#endif
        
        /* 掉电保护: 保存配置后进 SHUTDOWN */
#if DEBUG_BUILD || DEBUG_LP_BUILD
        /* 调试版: 直供不掉电 SHUTDOWN, 保证 SWD 全程可连; 仍保留脏配置落盘 */
        if (g_vbatt_mv_raw < BATT_POWER_LOSS_MV) {
            if (++pwr_loss_cnt >= POWERLOSS_COUNT) {
                pwr_loss_cnt = 0;
                nvm_save_dirty(); 
            }
        } else { pwr_loss_cnt = 0; }
#else
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

#endif
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
                    g_off_intent = true;
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
                    g_off_intent = true;
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
                g_off_intent = false;
                mode_init();
            }
        }

        if (sys_state == SYS_OFF) {
            led_set_target(0, false);
#if POWER_SAVE_BUILD || DEBUG_LP_BUILD
            opt3001_set_enabled(false);   /* 省电版: OFF 态关 ALS 传感器(shutdown, 幂等) */
#endif
            
            if (key == EVT_BT1_SHORT_0_8S && !g_flash_mode_used) {
                if (g_vbatt_mv_filtered >= sys_memory.lvp_ext) {   /* 测压达标才进入 Flash 模式 */
                    g_flash_mode_used = true;
                    flash_mode_tick = g_tick_ms;   /* 起始 tick(差值比较防回绕) */
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
                    g_off_intent = false;
                    mode_init();
                } else {
                    led_set_target(100, false); led_update_task(); delay_cycles(CPU_CYCLES_PER_MS * STARTUP_FAIL_BLINK_DELAY_MS);
                    led_set_target(0, false); led_update_task();
                }
            }
            else if (key == EVT_BOTH_RELEASE_1_5S) {
                /* 1.5s ?????? 5s ?????: ????, ??? standby */
                g_off_pending = false;
            }
            else if (key == EVT_BOTH_LONG_5S) {
                /* RUN 1.5s 熄灯后继续按满 5s: 闪烁提示并切换 ALS<->MAN 亮灯(测压达标);
                   OFF 直接长按(开机失败重试): 以 ALS 模式开机;
                   1.5s~5s 之间松开则保持关机 */
                bool from_run_hold = g_off_pending;   /* RUN 熄灯路径: 本次为模式切换 */
                g_off_pending = false;
                led_blink_twice();
                if (battery_startup_check()) {
                    sys_state = SYS_RUN;
                    g_inactivity_sec = 0;
                    g_is_dimmed = false;
                    g_off_intent = false;
                    if (from_run_hold) {
                        g_is_als_mode = !g_is_als_mode;   /* ALS<->MAN 互切 */
                    } else {
                        g_is_als_mode = true;             /* 快捷: ALS 开机 */
                    }
                    sys_memory.params = (sys_memory.params & ~(1u << 8)) | ((g_is_als_mode ? 1u : 0u) << 8);
                    nvm_mark_dirty();
                    nvm_save_dirty();
                    mode_init();
                }
            }

#if DEBUG_BUILD
            /* DEBUG build: skip OFF deep sleep (busy loop), SWD always on */
#else
            if (sys_state == SYS_OFF && !g_off_pending && key_is_idle()
#if DEBUG_LP_BUILD
               ) {   /* LP debug: force OFF deep sleep (WFI+STANDBY0), SWD may drop */
#else
               && !(sys_memory.features & FLAG_SWD_IN_OFF_STATE)) {
#endif
                DL_ADC12_disablePower(HW_ADC_INST);
                DL_VREF_disablePower(VREF);

                DL_GPIO_setUpperPinsPolarity(PORT_BTN, DL_GPIO_PIN_27_EDGE_FALL);
                DL_GPIO_setLowerPinsPolarity(PORT_BTN, DL_GPIO_PIN_2_EDGE_FALL);
                DL_GPIO_clearInterruptStatus(PORT_BTN, PIN_BT1 | PIN_BT2);
                /* STANDBY0: IOMUX IO wakeup (WUEN/WCOMP async level compare) is
                   the primary wake source; GPIO edge IRQ stays as a backup.
                   WCOMP must be configured before WUEN is enabled. */
                /* Buttons are active-low (idle=1, pressed=0): compare for 0 so
                   a press wakes the device. Compare for 1 would only wake on
                   release and also match immediately on standby entry. */
                DL_GPIO_setWakeupCompareValue(BUTTONS_BT1_IOMUX, DL_GPIO_WAKEUP_COMPARE_VALUE_0);
                DL_GPIO_setWakeupCompareValue(BUTTONS_BT2_IOMUX, DL_GPIO_WAKEUP_COMPARE_VALUE_0);
                DL_GPIO_enableWakeUp(BUTTONS_BT1_IOMUX);
                DL_GPIO_enableWakeUp(BUTTONS_BT2_IOMUX);
                DL_GPIO_enableInterrupt(PORT_BTN, PIN_BT1 | PIN_BT2);
                NVIC_ClearPendingIRQ(GPIOA_INT_IRQn);
                NVIC_EnableIRQ(GPIOA_INT_IRQn);
                NVIC_ClearPendingIRQ(TIMG14_INT_IRQn);

                /* WWDT keeps counting in STANDBY0 (STISM only covers SLEEP), so
                   with the main loop halted it would reset the device every
                   500ms. A new WWDT period only loads on the next counter
                   restart: write the stretch config FIRST, then restart, so
                   the ~8192s period is active before STANDBY0 is entered.
                   (Restart-then-write leaves the old 500ms period active and
                   the WWDT would reset ~500ms into deep sleep.)
                   write the stretch config FIRST, then restart, so the ~8192s
                   period is active before STANDBY0 is entered. (Restart-then-
                   write leaves the old 500ms period active and the WWDT would
                   reset the device ~500ms into deep sleep.) */
                WWDT0->WWDTCTL0 = (WWDT_WWDTCTL0_KEY_UNLOCK_W |
                                   WWDT_WWDTCTL0_PER_EN_25 |
                                   WWDT_WWDTCTL0_CLKDIV_MAXIMUM |
                                   WWDT_WWDTCTL0_STISM_STOP);
                DL_WWDT_restart(WWDT0_INST);
#if FEATURE_LOWPOWER_STANDBY
                if (sys_memory.features & FLAG_LOWPOWER_STANDBY) {
                    DL_SYSCTL_setPowerPolicySTANDBY0();
                }
#endif
                __disable_irq();   /* controlled WFI: 1ms tick cannot abort it */
                __WFI();
                __enable_irq();
#if FEATURE_LOWPOWER_STANDBY
                DL_SYSCTL_setPowerPolicyRUN0SLEEP0();
#endif
                DL_GPIO_disableWakeUp(BUTTONS_BT1_IOMUX);
                DL_GPIO_disableWakeUp(BUTTONS_BT2_IOMUX);
                DL_GPIO_disableInterrupt(PORT_BTN, PIN_BT1 | PIN_BT2);
                NVIC_DisableIRQ(GPIOA_INT_IRQn);
                /* restore normal 500ms watchdog service */
                SYSCFG_DL_WWDT0_init();
                DL_WWDT_restart(WWDT0_INST);

                battery_resume();   /* wakeup: re-measure battery */
                if (g_vbatt_mv_filtered > sys_memory.lvp_ext) {
                    g_inactivity_sec = 0;
                }
            }
#endif
        } 
        else if (sys_state == SYS_RUN || sys_state == SYS_LVP_CRIT) {
            if (key == EVT_BOTH_LONG_1_5S) {
                sys_state = SYS_OFF;
                g_off_intent = true;
                g_off_pending = true;       /* ???????: ?????(???5s)?? standby */
                led_set_target(0, false);   /* ? 1.5s ????, ???????? */
                led_update_task();
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
#if FEATURE_ALS_MODE
                /* 连续多次自恢复后锁定 ALS: 防传感器持续损坏时每 10s 闪一次循环 */
                if (++g_als_err_recover_cnt >= ALS_ERR_LOCKOUT_COUNT) {
                    g_als_err_recover_cnt = 0;
                    g_als_err_lockout = true;
                }
#endif
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
            if (key == EVT_BOTH_LONG_1_5S) {
                /* FLASH 模式长按双键 1.5s 也可正常开机(测压达标才启动, 与 OFF 态一致) */
                if (battery_startup_check()) {
                    sys_state = SYS_RUN;
                    g_inactivity_sec = 0;
                    g_is_dimmed = false;
                    g_off_intent = false;
                    mode_init();
                } else {
                    led_set_target(100, false); led_update_task(); delay_cycles(CPU_CYCLES_PER_MS * STARTUP_FAIL_BLINK_DELAY_MS);
                    led_set_target(0, false); led_update_task();
                }
            } else {
                if (g_tick_ms - flash_mode_tick > TIME_FLASH_MODE_TIMEOUT_MS) NVIC_SystemReset();
            }
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
