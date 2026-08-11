#ifndef HAL_OPT3001_H_
#define HAL_OPT3001_H_
#include <stdint.h>
#include <stdbool.h>

extern volatile uint32_t g_als_lux_raw;
extern volatile uint32_t g_als_lux_filtered;
extern volatile uint8_t  g_als_sensor_status;   /* 0=正常 1=未启用 2=故障 */
extern volatile uint8_t  g_als_err_cnt;
extern volatile uint32_t g_als_err_start_tick;

void opt3001_init(void);
void opt3001_trigger_conversion(void);
void opt3001_set_enabled(bool on);  /* 省电版: true=连续转换(0xC210) false=shutdown(0x0000, 0.4uA 级), 幂等 */
uint32_t opt3001_read_lux(void);

#endif