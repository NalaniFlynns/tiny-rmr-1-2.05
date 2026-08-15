#include "hal_keys.h"
#include "app_config.h"
#include "test_mailbox.h"

static uint32_t bt1_down_tick = 0, bt2_down_tick = 0;
static uint32_t both_down_tick = 0;
static bool bt1_was_down = false, bt2_was_down = false;
static bool both_was_down = false;
static bool both_handled_1_5s = false; 
static bool both_handled_5s = false; 
static bool lock_single_key = false;
static bool both_released = true;
static bool both_release_handled = false;   /* ?????????? */   /* ?????????????????????(?????) */

#if FEATURE_KEY_DEBOUNCE
static uint8_t hist_bt1 = 0xFF, hist_bt2 = 0xFF;
static bool key_debounced(bool raw_pressed, uint8_t *hist) {
    *hist = (uint8_t)((*hist << 1) | (raw_pressed ? 1u : 0u));
    return (*hist == (uint8_t)((1u << KEY_DEBOUNCE_BITS) - 1u));
}
#endif

KeyEvent_t keys_task(void) {
    uint32_t b1_val = (DL_GPIO_readPins(PORT_BTN, PIN_BT1) & PIN_BT1);
    uint32_t b2_val = (DL_GPIO_readPins(PORT_BTN, PIN_BT2) & PIN_BT2);

    /* 虚拟按键注入: 调试器授权(magic+host_version)后任意状态生效, 与真实按键走同一套去抖/事件状态机 */
    if (g_test_box.magic == TEST_MAGIC && g_test_box.host_version == g_test_box.version) {
        if (g_test_box.ovr_block_phys_keys) { b1_val = PIN_BT1; b2_val = PIN_BT2; }
        if (g_test_box.ovr_key_minus) b1_val = 0;
        if (g_test_box.ovr_key_plus) b2_val = 0;
    }

    bool b1 = (b1_val == 0);
    bool b2 = (b2_val == 0);
#if FEATURE_KEY_DEBOUNCE
    b1 = key_debounced(b1, &hist_bt1);
    b2 = key_debounced(b2, &hist_bt2);
#endif
    bool both_now = (b1 && b2);
    KeyEvent_t evt = EVT_NONE;

    if (!b1 && !b2) {
        lock_single_key = false;
        both_released = true;
    }

    if (both_now) {
        lock_single_key = true;
        bt1_down_tick = 0;
        bt2_down_tick = 0;
        
        if (both_released) {
            both_released = false;
            both_down_tick = g_tick_ms; 
            both_handled_1_5s = false;
            both_handled_5s = false;
            both_release_handled = false;
        } else {
            uint32_t dur = g_tick_ms - both_down_tick;
            if (dur >= KEY_TIME_LONG_PRESS_MS && !both_handled_1_5s) {
                both_handled_1_5s = true;
                evt = EVT_BOTH_LONG_1_5S;
            }
            if (dur >= KEY_TIME_FACTORY_RESET_MS && !both_handled_5s) {
                both_handled_5s = true;   
                evt = EVT_BOTH_LONG_5S; 
            }
        }
    } else {
        if (both_was_down) {
            uint32_t dur = g_tick_ms - both_down_tick;
            if (dur >= KEY_TIME_LONG_PRESS_MS && !both_handled_5s && !both_handled_1_5s) {
                /* ??: 1.5s ????????????, ??????????;
                   OFF/ALS_ERR/FLASH ?????????("??5s??=????") */
                if (sys_state != SYS_OFF && sys_state != SYS_ALS_ERR && sys_state != SYS_FLASH_MODE) {
                    evt = EVT_BOTH_LONG_1_5S;
                }
            } else if (!b1 && !b2 && both_handled_1_5s && !both_handled_5s && !both_release_handled) {
                /* 1.5s ?????? 5s ???????: ????(??? standby) */
                both_release_handled = true;
                evt = EVT_BOTH_RELEASE_1_5S;
            }
        } else {
            if (!lock_single_key) {
                if (bt1_was_down && !b1) {
                    if (bt1_down_tick != 0) {
                        uint32_t dur = g_tick_ms - bt1_down_tick;
                        if (dur > KEY_TIME_DEBOUNCE_MS && dur <= KEY_TIME_SHORT_0_8S_MS) evt = EVT_BT1_SHORT_0_8S;
                        else if (dur > KEY_TIME_SHORT_0_8S_MS && dur < KEY_TIME_SHORT_MAX_MS) evt = EVT_BT1_SHORT;
                    }
                } else if (!bt1_was_down && b1) { 
                    bt1_down_tick = g_tick_ms; 
                }

                if (bt2_was_down && !b2) {
                    if (bt2_down_tick != 0) {
                        uint32_t dur = g_tick_ms - bt2_down_tick;
                        if (dur > KEY_TIME_DEBOUNCE_MS && dur < KEY_TIME_SHORT_MAX_MS) evt = EVT_BT2_SHORT;
                    }
                } else if (!bt2_was_down && b2) { 
                    bt2_down_tick = g_tick_ms; 
                }
            }
        }
    }
    
    bt1_was_down = b1; 
    bt2_was_down = b2;
    both_was_down = both_now;

    /* 干净松开(双键均未按下且上一拍也非双键): 复位双键状态机, 防止 5s 事件/
       异常中断后 both_released/both_handled_5s 残留, 导致下一次双按瞬间判定
       超时长按(表现为"一按就开机") */
    if (!b1 && !b2 && !both_was_down) {
        both_released = true;
        both_handled_1_5s = false;
        both_handled_5s = false;
        both_release_handled = false;
        both_down_tick = 0;
    }

    if (evt != EVT_NONE) {
        g_inactivity_sec = 0;
        if (g_is_dimmed) { 
            g_is_dimmed = false; 
            evt = EVT_NONE; 
        } 
    }

    return evt;
}

bool key_is_idle(void) {
    bool b1 = (DL_GPIO_readPins(PORT_BTN, PIN_BT1) & PIN_BT1) == 0;
    bool b2 = (DL_GPIO_readPins(PORT_BTN, PIN_BT2) & PIN_BT2) == 0;
    return (!b1 && !b2 && !bt1_was_down && !bt2_was_down);
}