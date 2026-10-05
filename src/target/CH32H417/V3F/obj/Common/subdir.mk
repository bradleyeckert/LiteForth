################################################################################
# MRS Version: 2.5.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/UART.c \
c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/ch32h417_usbfs_device.c \
c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/hardware.c \
c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/usb_desc.c 

C_DEPS += \
./Common/UART.d \
./Common/ch32h417_usbfs_device.d \
./Common/hardware.d \
./Common/usb_desc.d 

OBJS += \
./Common/UART.o \
./Common/ch32h417_usbfs_device.o \
./Common/hardware.o \
./Common/usb_desc.o 

DIR_OBJS += \
./Common/*.o \

DIR_DEPS += \
./Common/*.d \

DIR_EXPANDS += \
./Common/*.253r.expand \


# Each subdirectory must supply rules for building sources it contributes
Common/UART.o: c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/UART.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/V3F/User" -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common" -std=gnu99 -Wa,-adhlns="$@.lst" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/ch32h417_usbfs_device.o: c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/ch32h417_usbfs_device.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/V3F/User" -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common" -std=gnu99 -Wa,-adhlns="$@.lst" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/hardware.o: c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/hardware.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/V3F/User" -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common" -std=gnu99 -Wa,-adhlns="$@.lst" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"
Common/usb_desc.o: c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common/usb_desc.c
	@	riscv-wch-elf-gcc -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -DCore_V3F -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/V3F/User" -I"c:/Users/User/Documents/GitHub/LiteForth/src/target/CH32H417/Common" -std=gnu99 -Wa,-adhlns="$@.lst" -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

