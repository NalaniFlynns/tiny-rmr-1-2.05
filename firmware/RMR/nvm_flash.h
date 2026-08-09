#ifndef NVM_FLASH_H_
#define NVM_FLASH_H_
#include <stdbool.h>
#include <stdint.h>

void nvm_init_and_load(void);
void nvm_mark_dirty(void);
bool nvm_save_dirty(void);
void nvm_background_task(void);
bool nvm_is_dirty(void);
void nvm_force_factory_reset(void);
uint32_t nvm_get_sector_addr(void);
uint32_t nvm_get_slot_idx(void);
uint8_t nvm_get_save_fail_cnt(void);

#endif