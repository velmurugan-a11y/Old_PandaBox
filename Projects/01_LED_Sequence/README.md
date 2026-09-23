# 01_LED_Sequence

Six PandaBox LEDs blink one after another (a chase). Each LED stays on for 300 ms (`LED_ON_TIME_MS` in `src/main.c`).

| # | LED | Pin |
|---|---|---|
| 1 | PWR red | PB1 |
| 2 | GPS green | PB0 |
| 3 | WiFi orange | PC4 |
| 4 | BT blue | PC5 |
| 5 | LCP1 | PC6 |
| 6 | LCP2 | PC7 |

Each LED is switched by an S9014 NPN transistor, so the LEDs are **active high**: pin high turns the LED on.

The clock is 120 MHz from the internal 8 MHz oscillator through the PLL (`src/system_gd32f30x.c`), so the code doesn't depend on the board crystal.

## Build and flash

Needs the Arm GNU Toolchain (`arm-none-eabi-gcc`) and SEGGER J-Link, connected over SWD.

```bash
./build.sh    # -> build/led_sequence.elf / .hex / .bin
./flash.sh    # program + verify at 0x08000000, then reset and run
```

This image goes at `0x08000000`, where the PandaBox bootloader normally sits. It overwrites the start of the original firmware.
