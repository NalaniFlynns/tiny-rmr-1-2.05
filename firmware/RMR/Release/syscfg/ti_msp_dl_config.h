/*
 * Copyright (c) 2023, Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ============ ti_msp_dl_config.h =============
 *  Configured MSPM0 DriverLib module declarations
 *
 *  DO NOT EDIT - This file is generated for the MSPM0C110X
 *  by the SysConfig tool.
 */
#ifndef ti_msp_dl_config_h
#define ti_msp_dl_config_h

#define CONFIG_MSPM0C110X
#define CONFIG_MSPM0C1104

#if defined(__ti_version__) || defined(__TI_COMPILER_VERSION__)
#define SYSCONFIG_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define SYSCONFIG_WEAK __weak
#elif defined(__GNUC__)
#define SYSCONFIG_WEAK __attribute__((weak))
#endif

#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  ======== SYSCFG_DL_init ========
 *  Perform all required MSP DL initialization
 *
 *  This function should be called once at a point before any use of
 *  MSP DL.
 */


/* clang-format off */

#define POWER_STARTUP_DELAY                                                (16)



#define CPUCLK_FREQ                                                     24000000



/* Defines for PWM_0 */
#define PWM_0_INST                                                         TIMA0
#define PWM_0_INST_IRQHandler                                   TIMA0_IRQHandler
#define PWM_0_INST_INT_IRQN                                     (TIMA0_INT_IRQn)
#define PWM_0_INST_CLK_FREQ                                             24000000
/* GPIO defines for channel 1 */
#define GPIO_PWM_0_C1_PORT                                                 GPIOA
#define GPIO_PWM_0_C1_PIN                                          DL_GPIO_PIN_1
#define GPIO_PWM_0_C1_IOMUX                                       (IOMUX_PINCM2)
#define GPIO_PWM_0_C1_IOMUX_FUNC                      IOMUX_PINCM2_PF_TIMA0_CCP1
#define GPIO_PWM_0_C1_IDX                                    DL_TIMER_CC_1_INDEX




/* Defines for ADC12_0 */
#define ADC12_0_INST                                                        ADC0
#define ADC12_0_INST_IRQHandler                                  ADC0_IRQHandler
#define ADC12_0_INST_INT_IRQN                                    (ADC0_INT_IRQn)
#define ADC12_0_ADCMEM_0                                      DL_ADC12_MEM_IDX_0
#define ADC12_0_ADCMEM_0_REF                   DL_ADC12_REFERENCE_VOLTAGE_INTREF
#define ADC12_0_ADCMEM_0_REF_VOLTAGE_V                                      1.40


/* Defines for VREF */
#define VREF_VOLTAGE_MV                                                     1400




/* Port definition for Pin Group OUTPUT_PINS */
#define OUTPUT_PINS_PORT                                                 (GPIOA)

/* Defines for VCC_EN: GPIOA.24 with pinCMx 25 on package pin A1 */
#define OUTPUT_PINS_VCC_EN_PIN                                  (DL_GPIO_PIN_24)
#define OUTPUT_PINS_VCC_EN_IOMUX                                 (IOMUX_PINCM25)
/* Port definition for Pin Group BUTTONS */
#define BUTTONS_PORT                                                     (GPIOA)

/* Defines for BT1: GPIOA.19 with pinCMx 20 on package pin D1 */
#define BUTTONS_BT1_PIN                                         (DL_GPIO_PIN_19)
#define BUTTONS_BT1_IOMUX                                        (IOMUX_PINCM20)
/* Defines for BT2: GPIOA.20 with pinCMx 21 on package pin C1 */
#define BUTTONS_BT2_PIN                                         (DL_GPIO_PIN_20)
#define BUTTONS_BT2_IOMUX                                        (IOMUX_PINCM21)
/* Port definition for Pin Group SW_I2C */
#define SW_I2C_PORT                                                      (GPIOA)

/* Defines for SW_SDA: GPIOA.0 with pinCMx 1 on package pin A2 */
#define SW_I2C_SW_SDA_PIN                                        (DL_GPIO_PIN_0)
#define SW_I2C_SW_SDA_IOMUX                                       (IOMUX_PINCM1)
/* Defines for SW_SCL: GPIOA.27 with pinCMx 28 on package pin B1 */
#define SW_I2C_SW_SCL_PIN                                       (DL_GPIO_PIN_27)
#define SW_I2C_SW_SCL_IOMUX                                      (IOMUX_PINCM28)


/* Defines for WWDT */
#define WWDT0_INST                                                       (WWDT0)
#define WWDT0_INT_IRQN                                          (WWDT0_INT_IRQn)



/* clang-format on */

void SYSCFG_DL_init(void);
void SYSCFG_DL_initPower(void);
void SYSCFG_DL_GPIO_init(void);
void SYSCFG_DL_DEBUG_init(void);
void SYSCFG_DL_SYSCTL_init(void);
void SYSCFG_DL_PWM_0_init(void);
void SYSCFG_DL_ADC12_0_init(void);
void SYSCFG_DL_VREF_init(void);

void SYSCFG_DL_WWDT0_init(void);


#ifdef __cplusplus
}
#endif

#endif /* ti_msp_dl_config_h */
