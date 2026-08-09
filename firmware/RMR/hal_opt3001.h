#ifndef HAL_OPT3001_H_
#define HAL_OPT3001_H_
#include <stdint.h>

extern volatile uint32_t g_als_lux_raw;
extern volatile uint32_t g_als_lux_filtered;
extern volatile uint8_t  g_als_sensor_status;   /* 0=正常 1=未启用 2=故障 */
extern volatile uint8_t  g_als_err_cnt;
extern volatile uint32_t g_als_err_start_tick;

void opt3001_init(void);
void opt3001_trigger_conversion(void);
uint32_t opt3001_read_lux(void);

#endif