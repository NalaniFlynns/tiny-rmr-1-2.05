#ifndef SYS_LED_PWM_H_
#define SYS_LED_PWM_H_
#include <stdint.h>
#include <stdbool.h>

extern volatile uint16_t g_current_brt;
extern volatile uint16_t g_last_applied_pwm; 

void led_set_target(uint16_t target_brt, bool smooth);
void led_blink_twice(void);
void led_update_task(void);

#endif