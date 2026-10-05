################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################
-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include User/subdir.mk
-include Common/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(S_DEPS)),)
-include $(S_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
ifneq ($(strip $(ASM_DEPS)),)
-include $(ASM_DEPS)
endif
ifneq ($(strip $(ASM_UPPER_DEPS)),)
-include $(ASM_UPPER_DEPS)
endif
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
SECONDARY_FLASH += \
SimulateCDC_V5F.bin \
SimulateCDC_V5F.hex \

SECONDARY_LIST += \
SimulateCDC_V5F.lst \

SECONDARY_SIZE += \
SimulateCDC_V5F.siz \


# All Target
all: 
	$(MAKE) --no-print-directory main-build 

main-build: SimulateCDC_V5F.elf secondary-outputs

# Tool invocations
SimulateCDC_V5F.elf: $(OBJS) $(USER_OBJS_ESCAPE)
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -T "${project}/Ld/V5F/Link_v5f.ld" -nostartfiles -Xlinker --gc-sections -Wl,-Map,"SimulateCDC_V5F.map" --specs=nano.specs --specs=nosys.specs -o "SimulateCDC_V5F.elf" $(OBJS) $(LIBS)
SimulateCDC_V5F.bin: SimulateCDC_V5F.elf
	@	riscv-wch-elf-objcopy -O binary "SimulateCDC_V5F.elf" "SimulateCDC_V5F.bin"
SimulateCDC_V5F.hex: SimulateCDC_V5F.elf
	@	riscv-wch-elf-objcopy -O ihex "SimulateCDC_V5F.elf" "SimulateCDC_V5F.hex"
SimulateCDC_V5F.lst: SimulateCDC_V5F.elf
	@	riscv-wch-elf-objdump --all-headers --demangle --disassemble -M xw "SimulateCDC_V5F.elf" > "SimulateCDC_V5F.lst"
SimulateCDC_V5F.siz: SimulateCDC_V5F.elf
	riscv-wch-elf-size --format=berkeley "SimulateCDC_V5F.elf"

# Other Targets
clean:
	-$(RM) $(DIR_OBJS) $(SECONDARY_FLASH)$(SECONDARY_LIST)$(SECONDARY_SIZE) SimulateCDC_V5F.elf
	-$(RM) $(DIR_EXPANDS) $(CALLGRAPH_DOT)
	-$(RM) $(DIR_DEPS)
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE)

.PHONY: all clean dependents

-include ../makefile.targets