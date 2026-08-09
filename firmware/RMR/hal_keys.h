#ifndef HAL_KEYS_H_
#define HAL_KEYS_H_
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    EVT_NONE = 0,
    EVT_BT1_SHORT_0_8S,
    EVT_BT1_SHORT,
    EVT_BT2_SHORT,
    EVT_BOTH_LONG_1_5S,
    EVT_BOTH_LONG_5S
} KeyEvent_t;

KeyEvent_t keys_task(void);
bool key_is_idle(void);

#endif