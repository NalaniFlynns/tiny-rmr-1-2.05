#include "sys_battery.h"
#include "app_config.h"

volatile uint32_t g_vbatt_mv_raw = 3100;
volatile uint32_t g_vbatt_mv_filtered = 3100;
static uint32_t adc_tick = 0;
static bool is_converting = false;
static bool first_read = true;

volatile uint32_t g_est_i_peak_ua = 0;
volatile uint32_t g_dyn_r_mohm = 0;
volatile uint32_t g_limit_i_led = 0;
volatile uint32_t g_limit_v_drop = 0;
volatile uint32_t g_limit_i_brt = 0;
volatile uint32_t g_limit_p_avg = 0;
volatile uint32_t g_safe_brt_out = 0;

static uint32_t brt_cache_last_vbatt = 0;
static uint16_t brt_cache_last_req = 0xFFFF;
static uint16_t brt_cache_val = 0;

void battery_task(void) {
    if (!is_converting) {
        if (g_tick_ms - adc_tick >= TIME_ADC_READ_INTERVAL_MS) { 
            DL_ADC12_clearInterruptStatus(HW_ADC_INST, 0xFFFFFFFF);
            DL_ADC12_enableConversions(HW_ADC_INST);
            DL_ADC12_startConversion(HW_ADC_INST);
            is_converting = true;
            adc_tick = g_tick_ms;
        }
    } else {
        if (DL_ADC12_getRawInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED)) {
            DL_ADC12_clearInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
            uint32_t res = DL_ADC12_getMemResult(HW_ADC_INST, DL_ADC12_MEM_IDX_0);
            if (res > 1023) res >>= 2;
            uint32_t mv = (res * VBATT_FULL_MV * ADC_GAIN_CAL) / (ADC_MAX_RESOLUTION * 1000);
            g_vbatt_mv_raw = mv;
            if (first_read) { g_vbatt_mv_filtered = mv; first_read = false; } 
            else { g_vbatt_mv_filtered = g_vbatt_mv_filtered - (g_vbatt_mv_filtered >> ADC_FILTER_SHIFT) + (mv >> ADC_FILTER_SHIFT); }
            is_converting = false;
        } else if (g_tick_ms - adc_tick > ADC_TIMEOUT_MS) {
            DL_ADC12_clearInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
            is_converting = false;
        }
    }
}

uint16_t battery_get_safe_brt(uint16_t req_brt) {
    if (!(sys_memory.features & FLAG_VOLTAGE_COMPENSATION)) return req_brt;
    uint32_t v_cap_mv = g_vbatt_mv_filtered;
    if (v_cap_mv <= sys_memory.lvp_ext) return 0;
    uint32_t diff_v = (v_cap_mv > brt_cache_last_vbatt) ? (v_cap_mv - brt_cache_last_vbatt) : (brt_cache_last_vbatt - v_cap_mv);
    if (diff_v <= BRT_CACHE_DELTA_MV && req_brt == brt_cache_last_req) return brt_cache_val;

    uint32_t r_dyn_mohm = sys_memory.r_base;
    if (v_cap_mv <= R_DYNAMIC_OFFSET_MV) r_dyn_mohm += R_DYNAMIC_EXTRA_LOWV_MOHM;
    else {
        uint32_t extra = R_DYNAMIC_FACTOR / (v_cap_mv - R_DYNAMIC_OFFSET_MV);
        r_dyn_mohm += (extra > R_DYNAMIC_EXTRA_CAP_MOHM) ? R_DYNAMIC_EXTRA_CAP_MOHM : extra;
    }
    uint32_t r_total_mohm = sys_memory.r_series + r_dyn_mohm;
    uint32_t i_pulse_ua = (uint32_t)(((uint64_t)(v_cap_mv - sys_memory.v_led_fw) * 1000000ULL) / r_total_mohm);
    
    g_dyn_r_mohm = r_dyn_mohm;
    g_est_i_peak_ua = i_pulse_ua;

    if (i_pulse_ua == 0) return 0;

    uint32_t min_limit = BRT_SCALE_MAX;
    uint32_t l_i_led = (i_pulse_ua > sys_memory.i_max_ua) ? ((sys_memory.i_max_ua * BRT_SCALE_MAX) / i_pulse_ua) : BRT_SCALE_MAX;
    
    uint32_t r_dc_mohm = (r_dyn_mohm * R_DYNAMIC_DC_FACTOR_PCT) / 100;
    uint32_t v_drop_peak = (uint32_t)(((uint64_t)i_pulse_ua * r_dc_mohm) / 1000000ULL);
    uint32_t v_drop_avg = (uint32_t)(((uint64_t)i_pulse_ua * req_brt * r_dyn_mohm) / 1000000000ULL);
    uint32_t v_drop_total = v_drop_peak + v_drop_avg;
    uint32_t l_v_drop = BRT_SCALE_MAX;
    
    if (EN_WALLS && (v_cap_mv > v_drop_total) && (v_cap_mv - v_drop_total < WALL_MIN_V_BATT_MV)) {
        uint32_t allowed_drop_total = v_cap_mv - WALL_MIN_V_BATT_MV;
        if (allowed_drop_total <= v_drop_peak) l_v_drop = LOW_BRT_GUARANTEE;
        else {
            uint32_t allowed_drop_avg = allowed_drop_total - v_drop_peak;
            l_v_drop = (uint32_t)(((uint64_t)allowed_drop_avg * 1000000000ULL) / ((uint64_t)i_pulse_ua * r_dyn_mohm));
            if (l_v_drop < LOW_BRT_GUARANTEE) l_v_drop = LOW_BRT_GUARANTEE;
        }
    }

    uint32_t l_i_brt = (BATT_MAX_DISCHARGE_UA * 1000) / i_pulse_ua; 
    uint64_t peak_power_uw = ((uint64_t)v_cap_mv * i_pulse_ua) / 1000ULL;
    uint32_t l_p_avg = (peak_power_uw == 0) ? BRT_SCALE_MAX : (uint32_t)(((uint64_t)sys_memory.batt_p_uw * 1000ULL) / peak_power_uw);

    if (l_i_led < min_limit) min_limit = l_i_led;
    if (l_v_drop < min_limit) min_limit = l_v_drop;
    if (l_i_brt < min_limit) min_limit = l_i_brt;
    if (l_p_avg < min_limit) min_limit = l_p_avg;

    uint16_t final_safe = (min_limit > BRT_SCALE_MAX) ? BRT_SCALE_MAX : min_limit;

    g_limit_i_led = l_i_led;
    g_limit_v_drop = l_v_drop;
    g_limit_i_brt = l_i_brt;
    g_limit_p_avg = l_p_avg;
    g_safe_brt_out = final_safe;

    brt_cache_val = final_safe > req_brt ? req_brt : final_safe;
    brt_cache_last_vbatt = v_cap_mv;
    brt_cache_last_req = req_brt;
    return brt_cache_val;
}

uint16_t battery_brt_to_pwm(uint16_t brt) {
    if (brt == 0) return PWM_REG_MAX;
    uint32_t v_cap_mv = g_vbatt_mv_filtered;
    if (v_cap_mv <= sys_memory.v_led_fw) return PWM_REG_MAX; 

    uint32_t r_dyn_mohm = sys_memory.r_base;
    if (v_cap_mv <= R_DYNAMIC_OFFSET_MV) r_dyn_mohm += R_DYNAMIC_EXTRA_LOWV_MOHM;
    else {
        uint32_t extra = R_DYNAMIC_FACTOR / (v_cap_mv - R_DYNAMIC_OFFSET_MV);
        r_dyn_mohm += (extra > R_DYNAMIC_EXTRA_CAP_MOHM) ? R_DYNAMIC_EXTRA_CAP_MOHM : extra;
    }
    uint32_t r_total_mohm = sys_memory.r_series + r_dyn_mohm;
    uint32_t i_peak_ua = (uint32_t)(((uint64_t)(v_cap_mv - sys_memory.v_led_fw) * 1000000ULL) / r_total_mohm);
    if (i_peak_ua == 0) return PWM_REG_MAX;

    uint32_t i_req_avg = (brt * sys_memory.i_max_ua) / BRT_SCALE_MAX;
    uint32_t duty_mille = (i_req_avg * 1000) / i_peak_ua;
    if (duty_mille > 1000) duty_mille = 1000;
    return (uint16_t)(PWM_REG_MAX - (duty_mille * PWM_REG_MAX / 1000));
}

bool battery_startup_check(void) {
    DL_VREF_enablePower(VREF);
    DL_ADC12_enablePower(HW_ADC_INST);
    delay_cycles(CPU_CYCLES_PER_MS * 5); 
    DL_ADC12_clearInterruptStatus(HW_ADC_INST, 0xFFFFFFFF);
    DL_ADC12_enableConversions(HW_ADC_INST);
    DL_ADC12_startConversion(HW_ADC_INST);
    uint32_t timeout = 50000;
    while((DL_ADC12_getRawInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED) == 0) && (timeout > 0)) timeout--;
    if(timeout == 0) return false;
    uint32_t res = DL_ADC12_getMemResult(HW_ADC_INST, DL_ADC12_MEM_IDX_0);
    DL_ADC12_clearInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
    if (res > 1023) res >>= 2;
    g_vbatt_mv_raw = (res * VBATT_FULL_MV * ADC_GAIN_CAL) / (ADC_MAX_RESOLUTION * 1000);
    g_vbatt_mv_filtered = g_vbatt_mv_raw; 
    first_read = false; 
    return (g_vbatt_mv_filtered > (sys_memory.lvp_ext + BATT_STARTUP_HYSTERESIS_MV));
}

void battery_resume(void) {
    DL_VREF_enablePower(VREF);
    DL_ADC12_enablePower(HW_ADC_INST);
    delay_cycles(CPU_CYCLES_PER_MS * 5); 
    DL_ADC12_clearInterruptStatus(HW_ADC_INST, 0xFFFFFFFF);
    DL_ADC12_enableConversions(HW_ADC_INST);
    DL_ADC12_startConversion(HW_ADC_INST);
    uint32_t timeout = 50000;
    while((DL_ADC12_getRawInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED) == 0) && (timeout > 0)) timeout--;
    if(timeout > 0) {
        uint32_t res = DL_ADC12_getMemResult(HW_ADC_INST, DL_ADC12_MEM_IDX_0);
        DL_ADC12_clearInterruptStatus(HW_ADC_INST, DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
        if (res > 1023) res >>= 2;
        g_vbatt_mv_raw = (res * VBATT_FULL_MV * ADC_GAIN_CAL) / (ADC_MAX_RESOLUTION * 1000);
        g_vbatt_mv_filtered = g_vbatt_mv_filtered - (g_vbatt_mv_filtered >> ADC_FILTER_SHIFT) + (g_vbatt_mv_raw >> ADC_FILTER_SHIFT); 
    }
}