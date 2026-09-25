#!/usr/bin/env bash
# Flash a built image with SEGGER J-Link over SWD.
#   ./flash.sh            -> bench image at 0x08000000 (standalone, for debugging)
#   ./flash.sh app1       -> app1 slot at 0x08008000 (needs Leo's bootloader present)
#   ./flash.sh app2       -> app2 slot at 0x08021000
#   ./flash.sh restore    -> restore backup/original_flash_256K.bin at 0x08000000
set -e
cd "$(dirname "$0")"
JLINK="${JLINK:-/c/Program Files/SEGGER/JLink/JLink.exe}"
target=${1:-bench}

case $target in
  bench) bin=build/bench/pandabox_bench.bin; addr=0x08000000 ;;
  app1)  bin=build/app1/pandabox_app1.bin;   addr=0x08008000 ;;
  app2)  bin=build/app2/pandabox_app2.bin;   addr=0x08021000 ;;
  restore) bin=../../backup/original_flash_256K.bin; addr=0x08000000 ;;
  *) echo "unknown target $target"; exit 1 ;;
esac

script=$(mktemp)
cat > "$script" <<EOF
connect
h
loadbin $bin $addr
verifybin $bin $addr
r
g
exit
EOF
"$JLINK" -NoGui 1 -device GD32F305VC -if SWD -speed 4000 -autoconnect 1 -ExitOnError 1 -CommandFile "$(cygpath -w "$script")"
rm -f "$script"
