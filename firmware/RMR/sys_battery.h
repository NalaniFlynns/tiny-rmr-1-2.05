#ifndef SYS_BATTERY_H_
#define SYS_BATTERY_H_
#include <stdint.h>
#include <stdbool.h>

extern volatile uint32_t g_est_i_peak_ua;
extern volatile uint32_t g_dyn_r_mohm;
extern volatile uint32_t g_limit_i_led;
extern volatile uint32_t g_limit_v_drop;
extern volatile uint32_t g_limit_i_brt;
extern volatile uint32_t g_limit_p_avg;
extern volatile uint32_t g_safe_brt_out;

bool battery_startup_check(void);
void battery_task(void);
void battery_resume(void);
uint16_t battery_get_safe_brt(uint16_t req_brt);
uint16_t battery_brt_to_pwm(uint16_t brt);

#endif