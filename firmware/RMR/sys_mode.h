#ifndef SYS_MODE_H_
#define SYS_MODE_H_
#include "hal_keys.h"
#include <stdbool.h>

extern bool g_is_als_mode;

void mode_init(void);
void mode_task(void);
void mode_handle_key(KeyEvent_t evt);

#endif