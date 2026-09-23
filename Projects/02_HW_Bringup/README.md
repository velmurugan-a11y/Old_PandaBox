# 02_HW_Bringup

Hardware bring-up firmware for the PandaBox (XBOX V2.5, GD32F305VCT6). A serial command line on **USART0 (PB6/PB7, 115200 8N1)** runs one handshake test per peripheral. The plan behind it is [docs/PandaBox_Peripheral_Bringup.md](../../docs/PandaBox_Peripheral_Bringup.md), and the results are in [docs/Bringup_Log.md](../../docs/Bringup_Log.md).

Nothing in this firmware erases or writes the external SPI flash. It holds Leo's delivery history, and the firmware only ever reads it.

## Build, flash, run

```bash
./build.sh                       # GCC, HXTAL_VALUE=12 MHz, 120 MHz PLL
./flash.sh                       # J-Link SWD, program + verify at 0x08000000
python tools/bringup.py info adc "flash id" "gsm test" bt
python tools/decode_flash.py     # rebuild the flash dump + decode LCR records from the logs
```

The console is a CH340 USB-serial adapter on **COM9**, wired to PB6 (TX) and PB7 (RX) at 3.3 V TTL. Tera Term works too: 115200 8N1, commands are echoed, and each prompt is `> `.

## Commands

| Command | Test |
|---|---|
| `info` | Clocks (12 MHz crystal → 120 MHz), reset reason, flash size, UID |
| `led [n on]` | LED chase, or one LED |
| `adc` | 12 V input (PC0 ×11), coin cell (PC1 ×2 while PE1 = 1) |
| `inputs` | 12 V detect on LCR1/LCR2/DB9, USB VBUS |
| `rtc` | LXTAL / RTC status, read only |
| `rs232 <1\|2>`, `rs485 <1\|2>`, `rsdiag [ms]` | LCR transceiver acknowledge / loopback / power-control diagnostic |
| `flash id`, `flash rd <addr> <n>`, `flash map` | GD25Q256E ID, status, SFDP; read-only dump; used-sector map |
| `gsm test` | EC25 with PE2 = 0 (expected silent), then PE2 = 1 (expect RDY, then an AT session); modem off at the end |
| `gsm on\|off`, `gsm pe2 <0\|1>`, `at <cmd>` | Manual modem control |
| `bt` | YC1021 reset + HCI Reset / Read Version / Read BD_ADDR |
| `reboot` | Software reset |

Every test prints a `RESULT <test> PASS|FAIL|INFO <details>` line. `tools/bringup.py` appends each exchange to `logs/session_<date>.log` and each RESULT line to `logs/results.csv`.

## Files

| Path | What |
|---|---|
| `src/board.h` | Pin map (LQFP100 pin numbers in the comments) |
| `src/board.c` | Safe power-on state for every control pin |
| `src/system_gd32f30x.c` | GD library clock file, patched for the 12 MHz crystal (÷1 ×10 = 120 MHz). Falls back to the internal 8 MHz oscillator if the crystal fails. |
| `src/uart.c` | Interrupt RX ring buffers for USART0/1/2, UART3/4 |
| `src/tests.c` | All test commands |
| `tools/bringup.py` | Host script: send commands, log replies |
| `tools/decode_flash.py` | Rebuild flash dumps and decode 64-byte LCR records |
| `logs/` | Session logs, results, flash dumps, decoded records |
