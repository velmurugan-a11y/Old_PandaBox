#!/usr/bin/env bash
# Flash build/pandabox_lcr.hex to GD32F305VC over SWD with SEGGER J-Link.
set -e
cd "$(dirname "$0")"
JLINK="${JLINK:-/c/Program Files/SEGGER/JLink/JLink.exe}"
"$JLINK" -device GD32F305VC -if SWD -speed 4000 -autoconnect 1 -NoGui 1 -ExitOnError 1 -CommandFile flash.jlink
