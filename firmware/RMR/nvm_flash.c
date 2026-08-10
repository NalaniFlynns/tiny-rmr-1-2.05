#include "nvm_flash.h"
#include "app_config.h"
#include <ti/driverlib/dl_flashctl.h>
#include <ti/driverlib/dl_wwdt.h>
#include <string.h>
#include <stddef.h>

_Static_assert(sizeof(NVM_Data_t) == 64, "NVM_Data_t MUST be exactly 64 bytes.");
_Static_assert(offsetof(NVM_Data_t, crc32) == 60, "CRC offset error, expected 60 bytes.");

#define NVM_MAX_SLOTS (FLASH_SECTOR_SIZE / NVM_SLOT_SIZE)

NVM_Data_t sys_memory;       
static NVM_Data_t flash_shadow;     
static uint32_t current_sector_addr = FLASH_SECTOR_A_ADDR;
static uint32_t current_nvm_slot = 0;
static bool nvm_dirty = false;
static uint32_t nvm_dirty_start_tick = 0;
static bool force_factory = false;
static uint8_t nvm_save_fail_cnt = 0;
static uint32_t nvm_last_attempt_tick = 0;
static bool bad_slot[2][NVM_MAX_SLOTS];   /* ????(???????) */

static uint32_t calc_fnv1a_32(const uint8_t *data, size_t length) {
    uint32_t hash = 0x811C9DC5;
    for (size_t i = 0; i < length; i++) { hash ^= data[i]; hash *= 0x01000193; }
    return hash;
}

static void load_factory_defaults(void) {
    memset(&sys_memory, 0, sizeof(NVM_Data_t)); 
    sys_memory.magic = NVM_MAGIC;
    sys_memory.seq_id = 0;
    sys_memory.features = DEFAULT_FEATURE_FLAGS;
    sys_memory.r_base = CFG_DEFAULT_R_BASE_MOHM;                 
    sys_memory.r_series = CFG_DEFAULT_R_SERIES_MOHM;           
    sys_memory.v_led_fw = HW_LED_FORWARD_V_MV;             
    sys_memory.i_max_ua = HW_LED_MAX_CURRENT_UA;            
    sys_memory.batt_p_uw = BATT_MAX_DISCHARGE_UW;            
    sys_memory.als_min_brt = ALS_MIN_BRT;            
    sys_memory.als_sqrt_factor = ALS_SQRT_FACTOR;       
    sys_memory.als_cap_low_x100 = ALS_CAP_BRT_LOW_LUX / 100;   
    sys_memory.als_cap_high_x100 = ALS_CAP_BRT_HIGH_LUX / 100;     sys_memory.lvp_crit = BATT_LVP_CRIT_MV;             
    sys_memory.lvp_ext = BATT_LVP_EXTREME_MV;              
    sys_memory.default_level = CFG_DEFAULT_LEVEL;           
    sys_memory.params = CFG_DEFAULT_LEVEL | (CFG_DEFAULT_ALS_EN << 8) | (CFG_DEFAULT_ALS_OFFSET << 16); 
}

#if FEATURE_MEMORY_SAVE
void nvm_init_and_load(void) {
    uint32_t max_seq = 0;
    bool found = false;
    for (int sec = 0; sec < 2; sec++) {
        uint32_t sec_addr = (sec == 0) ? FLASH_SECTOR_A_ADDR : FLASH_SECTOR_B_ADDR;
        for (int i = 0; i < NVM_MAX_SLOTS; i++) {
            NVM_Data_t *ptr = (NVM_Data_t *)(sec_addr + i * NVM_SLOT_SIZE);
            if (ptr->magic == NVM_MAGIC) {
                if (calc_fnv1a_32((const uint8_t*)ptr, offsetof(NVM_Data_t, crc32)) == ptr->crc32) {
                    if (!found || ptr->seq_id >= max_seq) {
                        max_seq = ptr->seq_id;
                        sys_memory = *ptr;
                        current_sector_addr = sec_addr;
                        current_nvm_slot = i;
                        found = true;
                    }
                } else {
                    bad_slot[sec][i] = true;   /* magic ?? CRC ? -> ??, ???? */
                }
            }
        }
    }

    bool need_save = (!found || DEV_FORCE_FACTORY_RESET || force_factory);
    if (need_save) {
        current_sector_addr = FLASH_SECTOR_A_ADDR;
        current_nvm_slot = NVM_MAX_SLOTS; 
        load_factory_defaults();
    } else {
        /* 加载后数据修正: level 越界回退默认 + 特性按编译期掩码过滤 */
        uint8_t lvl = sys_memory.params & 0xFF;
        if (lvl >= CFG_MAX_LEVELS) lvl = sys_memory.default_level;
        sys_memory.params = (sys_memory.params & 0xFFFFFF00) | lvl;
        sys_memory.features &= FEATURE_RUNTIME_MASK;
        /* ?????: 0 ????????? */
        if (sys_memory.als_sqrt_factor == 0)  sys_memory.als_sqrt_factor = ALS_SQRT_FACTOR;
        if (sys_memory.als_cap_low_x100 == 0) sys_memory.als_cap_low_x100 = ALS_CAP_BRT_LOW_LUX / 100;
        if (sys_memory.als_cap_high_x100 == 0) sys_memory.als_cap_high_x100 = ALS_CAP_BRT_HIGH_LUX / 100;
    }
    force_factory = false;

    flash_shadow = sys_memory;
    nvm_dirty = false;
    if (need_save) {
        memset(&flash_shadow, 0, sizeof(NVM_Data_t));   /* 强制实际写入, 清掉旧数据 */
        nvm_dirty = true;
        nvm_save_dirty();
    }
}

void nvm_mark_dirty(void) {
    if (memcmp(&flash_shadow, &sys_memory, sizeof(NVM_Data_t)) == 0) {
        nvm_dirty = false;
    } else {
        nvm_dirty = true;
        nvm_save_fail_cnt = 0;   /* ???: ?????????? */
        nvm_dirty_start_tick = g_tick_ms;
    }
}

static bool perform_flash_save(void) {
#if FEATURE_SAVE_RAM_SHADOW
    if (memcmp(&flash_shadow, &sys_memory, sizeof(NVM_Data_t)) == 0) { nvm_dirty = false; return true; }
#endif
    sys_memory.seq_id++;
    sys_memory.crc32 = calc_fnv1a_32((const uint8_t*)&sys_memory, offsetof(NVM_Data_t, crc32));

    /* ????: ????????????????(????);
       ??: ?????????? CRC ????? bad_slot ?, ???? */
    int attempted = 0;             /* ???????????(?? FLASH_SLOT_ATTEMPTS) */
    uint32_t probe_cnt = 0;        /* ???????, ???????? */
    uint32_t next_slot = current_nvm_slot;
    uint32_t target_sector = current_sector_addr;

    while (attempted < FLASH_SLOT_ATTEMPTS && probe_cnt < (uint32_t)NVM_MAX_SLOTS * 2) {
        next_slot++;
        probe_cnt++;
        if (next_slot >= NVM_MAX_SLOTS) {
            target_sector = (current_sector_addr == FLASH_SECTOR_A_ADDR) ? FLASH_SECTOR_B_ADDR : FLASH_SECTOR_A_ADDR;
            next_slot = 0;
            DL_FlashCTL_clearInterruptStatus(FLASHCTL);
            DL_WWDT_restart(WWDT0_INST);
            __disable_irq();
            DL_FlashCTL_unprotectSector(FLASHCTL, target_sector, DL_FLASHCTL_REGION_SELECT_MAIN);
            DL_FlashCTL_eraseMemoryFromRAM(FLASHCTL, target_sector, DL_FLASHCTL_COMMAND_SIZE_SECTOR);
            __enable_irq();
            g_tick_ms += FLASH_ERASE_TIME_MS;   /* ?????? */
            int sec_idx = (target_sector == FLASH_SECTOR_B_ADDR) ? 1 : 0;
            memset(bad_slot[sec_idx], 0, sizeof(bad_slot[sec_idx]));   /* ?????????? */
        }

        int sec_idx = (target_sector == FLASH_SECTOR_B_ADDR) ? 1 : 0;
        if (bad_slot[sec_idx][next_slot]) continue;   /* ????, ??????? */

        attempted++;
        uint32_t write_addr = target_sector + next_slot * NVM_SLOT_SIZE;
        bool write_ok = false;

        /* ??????? FLASH_RETRY_COUNT ?, ???? */
        for (int retry = 0; retry < FLASH_RETRY_COUNT; retry++) {
            DL_FlashCTL_clearInterruptStatus(FLASHCTL);
            uint32_t safe_buffer[2];
            DL_WWDT_restart(WWDT0_INST);
            for (int w = 0; w < NVM_SLOT_SIZE; w += 8) {
                memcpy(safe_buffer, ((uint8_t*)&sys_memory) + w, 8);
                DL_WWDT_restart(WWDT0_INST);
                __disable_irq();
                DL_FlashCTL_unprotectSector(FLASHCTL, write_addr + w, DL_FLASHCTL_REGION_SELECT_MAIN);
                DL_FlashCTL_programMemoryFromRAM64(FLASHCTL, write_addr + w, safe_buffer);
                __enable_irq();
                g_tick_ms += FLASH_PROG_TIME_MS;   /* ?????? */
            }
#if FEATURE_SAVE_VERIFY
            if (memcmp((const void*)write_addr, &sys_memory, sizeof(NVM_Data_t)) == 0) {
                write_ok = true;
                break;
            }
#else
            write_ok = true;
            break;
#endif
        }

        if (write_ok) {
            flash_shadow = sys_memory;
            current_sector_addr = target_sector;
            current_nvm_slot = next_slot;
            nvm_dirty = false;
            nvm_save_fail_cnt = 0;
            nvm_last_attempt_tick = g_tick_ms;
            return true;
        }

        /* ???: ????, ?????? */
        bad_slot[sec_idx][next_slot] = true;
        current_sector_addr = target_sector;
        current_nvm_slot = next_slot;
    }

    /* ??????: ?? dirty(RAM ???????), ?????? */
    nvm_save_fail_cnt++;
    nvm_last_attempt_tick = g_tick_ms;
    nvm_dirty = true;
    return false;
}

bool nvm_save_dirty(void) {
    if (nvm_dirty) return perform_flash_save();
    return true;
}

bool nvm_is_dirty(void) { return nvm_dirty; }

void nvm_background_task(void) {
#if EN_AUTO_SAVE
    if (nvm_dirty && sys_state == SYS_RUN) {
        if (nvm_save_fail_cnt == 0) {
            /* ?????: ????????? */
            if (g_tick_ms - nvm_dirty_start_tick > TIME_NVM_AUTO_SAVE_DELAY_MS) perform_flash_save();
        } else if (nvm_save_fail_cnt < NVM_SAVE_MAX_RETRIES) {
            /* ????: ?? dirty, ? TIME_NVM_AUTO_SAVE_DELAY_MS ??, ?????????? */
            if (g_tick_ms - nvm_last_attempt_tick > TIME_NVM_AUTO_SAVE_DELAY_MS) perform_flash_save();
        }
        /* ?? NVM_SAVE_MAX_RETRIES: ??????, ??????? RAM(nvm_is_dirty=1 ???), ??????/??????? */
    }
#endif
}

uint32_t nvm_get_sector_addr(void) { return current_sector_addr; }
uint32_t nvm_get_slot_idx(void) { return current_nvm_slot; }
uint8_t nvm_get_save_fail_cnt(void) { return nvm_save_fail_cnt; }

#else /* !FEATURE_MEMORY_SAVE */
void nvm_init_and_load(void) { load_factory_defaults(); }
void nvm_mark_dirty(void) {}
bool nvm_save_dirty(void) { return true; }
void nvm_background_task(void) {}
bool nvm_is_dirty(void) { return false; }
uint32_t nvm_get_sector_addr(void) { return 0; }
uint32_t nvm_get_slot_idx(void) { return 0; }
uint8_t nvm_get_save_fail_cnt(void) { return 0; }
#endif

void nvm_force_factory_reset(void) {
    if (sys_memory.magic == NVM_MAGIC) {
        /* 运行时(已初始化): 立即恢复出厂并标记保存 */
        load_factory_defaults();
        nvm_mark_dirty();
    } else {
        /* 初始化前(POR 清空): 由 nvm_init_and_load 消费 */
        force_factory = true;
    }
}