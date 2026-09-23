#!/usr/bin/env bash
# Build 02_HW_Bringup with the Arm GNU toolchain (arm-none-eabi-gcc must be on PATH).
set -e
# Use the Arm GNU toolchain from its default install folder if it is not on PATH
command -v arm-none-eabi-gcc >/dev/null || PATH="$(ls -d "/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/"*/bin | tail -1):$PATH"
cd "$(dirname "$0")"

LIB=../../GD32F30x_Firmware_Library
CMSIS=$LIB/CMSIS/GD/GD32F30x
SPL=$LIB/GD32F30x_standard_peripheral
OUT=build
NAME=hw_bringup

CFLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \
  -DGD32F30X_CL -DUSE_STDPERIPH_DRIVER -DHXTAL_VALUE=12000000U \
  -Isrc -I$CMSIS/Include -I$LIB/CMSIS -I$SPL/Include \
  -Os -g3 -Wall -ffunction-sections -fdata-sections"

SRCS="src/main.c src/board.c src/uart.c src/tests.c src/system_gd32f30x.c \
  $SPL/Source/gd32f30x_rcu.c $SPL/Source/gd32f30x_gpio.c $SPL/Source/gd32f30x_misc.c $SPL/Source/gd32f30x_usart.c $SPL/Source/gd32f30x_spi.c $SPL/Source/gd32f30x_adc.c $SPL/Source/gd32f30x_rtc.c $SPL/Source/gd32f30x_pmu.c \
  $CMSIS/Source/GCC/startup_gd32f30x_cl.S"

mkdir -p $OUT
OBJS=""
for s in $SRCS; do
  o=$OUT/$(basename "${s%.*}").o
  arm-none-eabi-gcc $CFLAGS -c "$s" -o "$o"
  OBJS="$OBJS $o"
done

arm-none-eabi-gcc $CFLAGS -T$CMSIS/Source/GCC/Ld/gd32f305xC_flash.ld \
  -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$OUT/$NAME.map --specs=nano.specs --specs=nosys.specs -Wl,--no-warn-mismatch \
  $OBJS -o $OUT/$NAME.elf
arm-none-eabi-objcopy -O ihex $OUT/$NAME.elf $OUT/$NAME.hex
arm-none-eabi-objcopy -O binary $OUT/$NAME.elf $OUT/$NAME.bin
arm-none-eabi-size $OUT/$NAME.elf
