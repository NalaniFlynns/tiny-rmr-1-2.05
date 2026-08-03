/*****************************************************************************
  Copyright (C) 2023 Texas Instruments Incorporated - http://www.ti.com/
*****************************************************************************/
-uinterruptVectors
--stack_size=256

MEMORY
{
    /* 
     * 1. [应用代码区] (14KB - 16字节 = 0x37F0)
     */
    FLASH           (RX)  : origin = 0x00000000, length = 0x000037F0
    
    /* 
     * 2. [固件版本与设备信息区] (16 字节)
     */
    FLASH_VERSION   (R)   : origin = 0x000037F0, length = 0x00000010, fill = 0xFFFFFFFF

    /* 
     * 3. [NVM 数据存储区 A & B]
     */
    FLASH_NVM_A     (R)   : origin = 0x00003800, length = 0x00000400, fill = 0xFFFFFFFF
    FLASH_NVM_B     (R)   : origin = 0x00003C00, length = 0x00000400, fill = 0xFFFFFFFF

    /* 1KB SRAM */
    SRAM            (RWX) : origin = 0x20000000, length = 0x00000400
    
    BCR_CONFIG      (R)   : origin = 0x41C00000, length = 0x000000FF
}

SECTIONS
{
    .intvecs:   > 0x00000000
    .text   : palign(8) {} > FLASH
    .const  : palign(8) {} > FLASH
    .cinit  : palign(8) {} > FLASH
    .pinit  : palign(8) {} > FLASH
    .rodata : palign(8) {} > FLASH
    .ARM.exidx    : palign(8) {} > FLASH
    .init_array   : palign(8) {} > FLASH
    .binit        : palign(8) {} > FLASH
    .TI.ramfunc   : load = FLASH, palign(8), run=SRAM, table(BINIT)
    
    .fw_version : palign(4) {} > FLASH_VERSION

    .nvm_dummy (NOLOAD) : {} > FLASH_NVM_A

    .TI.noinit : {} > SRAM

    .vtable :   > SRAM
    .args   :   > SRAM
    .data   :   > SRAM
    .bss    :   > SRAM
    .sysmem :   > SRAM
    .stack  :   > SRAM (HIGH)

    .BCRConfig  : {} > BCR_CONFIG
}