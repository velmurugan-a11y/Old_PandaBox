#!/usr/bin/env bash
# Build the PandaBox V2.89-compatible firmware with the Arm GNU toolchain.
#
#   ./build.sh            -> app1 (0x08008000, booted by Leo's bootloader)
#   ./build.sh app2       -> app2 (0x08021000)
#   ./build.sh bench      -> standalone image at 0x08000000 (no bootloader, J-Link debugging)
#   ./build.sh all        -> app1 + app2 + bench
#
# Output: build/<target>/pandabox_<target>.{elf,hex,bin,map}
set -e
command -v arm-none-eabi-gcc >/dev/null || PATH="$(ls -d "/c/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/"*/bin | tail -1):$PATH"
cd "$(dirname "$0")"

LIB=../../GD32F30x_Firmware_Library
CMSIS=$LIB/CMSIS/GD/GD32F30x
SPL=$LIB/GD32F30x_standard_peripheral

build_one() {
  local target=$1 origin length
  case $target in
    app1)  origin=0x08008000; length=98K ;;
    app2)  origin=0x08021000; length=98K ;;
    bench) origin=0x08000000; length=254K ;;
    *) echo "unknown target $target"; exit 1 ;;
  esac
  local out=build/$target name=pandabox_$target
  mkdir -p $out
  sed -e "s/@ORIGIN@/$origin/" -e "s/@LENGTH@/$length/" linker/app.ld.in > $out/link.ld

  local CFLAGS="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft \
    -DGD32F30X_CL -DUSE_STDPERIPH_DRIVER -DHXTAL_VALUE=12000000U -DAPP_BASE=$origin \
    -Isrc -Isrc/hal -Isrc/sys -Isrc/drv -Isrc/app -Isrc/app/update -Isrc/main -Isrc/util -Iconfig \
    -I$CMSIS/Include -I$LIB/CMSIS -I$SPL/Include \
    -Os -g3 -std=gnu99 -Wall -Wno-unused-function -ffunction-sections -fdata-sections -fno-common $EXTRA_CFLAGS"

  local SRCS="$(ls src/*/*.c src/app/update/*.c) \
    $SPL/Source/gd32f30x_rcu.c $SPL/Source/gd32f30x_gpio.c $SPL/Source/gd32f30x_misc.c \
    $SPL/Source/gd32f30x_usart.c $SPL/Source/gd32f30x_spi.c $SPL/Source/gd32f30x_fmc.c \
    $SPL/Source/gd32f30x_rtc.c $SPL/Source/gd32f30x_bkp.c $SPL/Source/gd32f30x_pmu.c \
    $SPL/Source/gd32f30x_adc.c $SPL/Source/gd32f30x_fwdgt.c \
    $CMSIS/Source/GCC/startup_gd32f30x_cl.S"

  local OBJS=""
  for s in $SRCS; do
    local o=$out/obj/$(echo "${s%.*}" | sed 's#^\.\./\.\./##; s#/#_#g').o
    mkdir -p $out/obj
    arm-none-eabi-gcc $CFLAGS -c "$s" -o "$o"
    OBJS="$OBJS $o"
  done
  arm-none-eabi-gcc $CFLAGS -T$out/link.ld -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
    -Wl,-Map=$out/$name.map --specs=nano.specs --specs=nosys.specs $OBJS -o $out/$name.elf
  arm-none-eabi-objcopy -O ihex $out/$name.elf $out/$name.hex
  arm-none-eabi-objcopy -O binary $out/$name.elf $out/$name.bin
  echo "== $target @ $origin"
  arm-none-eabi-size $out/$name.elf
}

targets=${1:-app1}
[ "$targets" = all ] && targets="app1 app2 bench"
for t in $targets; do build_one $t; done
