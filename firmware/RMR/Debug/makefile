################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

CG_TOOL_ROOT := C:/ti/ti_cgt_arm_llvm_4.0.2.LTS

GEN_OPTS__FLAG := @"syscfg/device.opt" 
GEN_CMDS__FLAG := 

ORDERED_OBJS += \
"./empty_mspm0c1104.o" \
"./syscfg/ti_msp_dl_config.o" \
"./startup_mspm0c110x_ticlang.o" \
"./hal_keys.o" \
"./hal_opt3001.o" \
"./nvm_flash.o" \
"./sys_battery.o" \
"./sys_led_pwm.o" \
"./sys_mode.o" \
"./test_mailbox.o" \
"../device_linker.cmd" \
$(GEN_CMDS__FLAG) \
-Wl,-ldevice.cmd.genlibs \
-Wl,-llibc.a \

-include ../makefile.init

RM := DEL /F
RMDIR := RMDIR /S/Q

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir_vars.mk
-include subdir_rules.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C55_DEPS)),)
-include $(C55_DEPS)
endif
ifneq ($(strip $(C_UPPER_DEPS)),)
-include $(C_UPPER_DEPS)
endif
ifneq ($(strip $(S67_DEPS)),)
-include $(S67_DEPS)
endif
ifneq ($(strip $(S62_DEPS)),)
-include $(S62_DEPS)
endif
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(OPT_DEPS)),)
-include $(OPT_DEPS)
endif
ifneq ($(strip $(C??_DEPS)),)
-include $(C??_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(S??_DEPS)),)
-include $(S??_DEPS)
endif
ifneq ($(strip $(C64_DEPS)),)
-include $(C64_DEPS)
endif
ifneq ($(strip $(CXX_DEPS)),)
-include $(CXX_DEPS)
endif
ifneq ($(strip $(S64_DEPS)),)
-include $(S64_DEPS)
endif
ifneq ($(strip $(CLA_DEPS)),)
-include $(CLA_DEPS)
endif
ifneq ($(strip $(S55_DEPS)),)
-include $(S55_DEPS)
endif
ifneq ($(strip $(SV7A_DEPS)),)
-include $(SV7A_DEPS)
endif
ifneq ($(strip $(C62_DEPS)),)
-include $(C62_DEPS)
endif
ifneq ($(strip $(C67_DEPS)),)
-include $(C67_DEPS)
endif
ifneq ($(strip $(K_DEPS)),)
-include $(K_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(CC_DEPS)),)
-include $(CC_DEPS)
endif
ifneq ($(strip $(C++_DEPS)),)
-include $(C++_DEPS)
endif
ifneq ($(strip $(C43_DEPS)),)
-include $(C43_DEPS)
endif
ifneq ($(strip $(S43_DEPS)),)
-include $(S43_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(CPP_DEPS)),)
-include $(CPP_DEPS)
endif
ifneq ($(strip $(SA_DEPS)),)
-include $(SA_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
EXE_OUTPUTS += \
RMR.out 

EXE_OUTPUTS__QUOTED += \
"RMR.out" 

BIN_OUTPUTS += \
RMR.hex 

BIN_OUTPUTS__QUOTED += \
"RMR.hex" 


# All Target
all: $(OBJS) $(CMD_SRCS) $(GEN_CMDS)
	@$(MAKE) --no-print-directory -Onone "RMR.out" secondary-outputs

# Tool invocations
RMR.out: $(OBJS) $(CMD_SRCS) $(GEN_CMDS)
	@echo 'Arm Linker - building target: "$@"'
	"C:/ti/ti_cgt_arm_llvm_4.0.2.LTS/bin/tiarmclang.exe" @"syscfg/device.opt"  -march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -Oz -gdwarf-3 -Wl,-m"RMR.map" -Wl,-i"C:/ti/mspm0_sdk_2_11_00_07/source" -Wl,-i"C:/Users/Nalani/workspace_v12/RMR/Debug/syscfg" -Wl,-i"C:/ti/ti_cgt_arm_llvm_4.0.2.LTS/lib" -Wl,--diag_wrap=off -Wl,--display_error_number -Wl,--warn_sections -Wl,--xml_link_info="RMR_linkInfo.xml" -Wl,--rom_model -o "RMR.out" $(ORDERED_OBJS)
	@echo 'Finished building target: "$@"'
	@echo ' '

RMR.hex: $(EXE_OUTPUTS)
	@echo 'Arm Objcopy Utility - building secondary target: "$@"'
	"C:/ti/ti_cgt_arm_llvm_4.0.2.LTS/bin/tiarmobjcopy.exe" --output-target=ihex $(EXE_OUTPUTS__QUOTED) "RMR.hex"
	@echo 'Finished building secondary target: "$@"'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(GEN_MISC_FILES__QUOTED)$(BIN_OUTPUTS__QUOTED)$(GEN_FILES__QUOTED)$(EXE_OUTPUTS__QUOTED)
	-$(RM) "empty_mspm0c1104.o" "syscfg\ti_msp_dl_config.o" "startup_mspm0c110x_ticlang.o" "hal_keys.o" "hal_opt3001.o" "nvm_flash.o" "sys_battery.o" "sys_led_pwm.o" "sys_mode.o" "test_mailbox.o" 
	-$(RM) "empty_mspm0c1104.d" "syscfg\ti_msp_dl_config.d" "startup_mspm0c110x_ticlang.d" "hal_keys.d" "hal_opt3001.d" "nvm_flash.d" "sys_battery.d" "sys_led_pwm.d" "sys_mode.d" "test_mailbox.d" 
	-$(RMDIR) $(GEN_MISC_DIRS__QUOTED)
	-@echo ' '

secondary-outputs: $(BIN_OUTPUTS)

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets

