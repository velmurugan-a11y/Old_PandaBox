# GD32F305VCT6 native bring-up

A fresh, from-scratch firmware project for the actual confirmed hardware
(schematic `XBOX_V2.5`, BOM `PandaBox BOM V2.5.xlsx`) — separate from the
sibling `pandabox-firmware/` reverse-engineering project. Reuses the
already-worked-out Lx protocol/BoxStatus wire format conceptually (not
yet ported here — this phase is hardware bring-up only); replaces all
drivers with real code written against this board's actual schematic.

**Flashing this OVERWRITES whatever is currently on the chip**, including
the old X-Box bootloader if present — this image is standalone, with its
own reset vector at `0x08000000`, not a slot inside the old APP1/APP2
scheme. Back up the chip first (see
`../pandabox-firmware/scripts/jlink_backup.jlink`) if you haven't already.

## ⚠ Hard-learned: the YC1021 can be wedged into a non-advertising state

Sending `yc1021.c`'s reconstructed provisioning sequence to a real board
put the YC1021 into a state where it stopped advertising over BLE
entirely (not just under its usual name — invisible to an unfiltered
scan). This **survived a normal MCU reset and a simple main-power
cycle**. The only thing that recovered it was a **full cold boot**:
disconnecting both main power and the board's `CR1220` backup coin cell,
leaving it fully unpowered for 30+ seconds, then reconnecting — after
which the chip came back with its real persisted identity intact.

Before sending anything from `yc1021.c` to real hardware again:
- Know where the coin cell is and have physical access to it, in case
  you need that recovery procedure again.
- Prefer testing RX-only (skip the `yc1021_provision()` call) to confirm
  passive listening alone is safe before re-enabling TX.
- Treat the provisioning frame bytes as genuinely unverified, not just
  "might not do anything" — get authoritative Yichip protocol
  documentation before trusting them further.

## Confirmed hardware (schematic + BOM, not assumed)

- MCU: **GD32F305VCT6**, LQFP100 (settles an earlier "Z package" mix-up —
  the schematic's own MCU designator and the BOM both say VCT6).
- MCU crystal: 12MHz (X101). YC1021 crystal: 24MHz (X2, matches its
  datasheet's recommended default).
- 4G: Quectel **EC25-AF**, exact part `EC25AFA-512-STD`.
- BT/BLE: **YC1021** (QFN32).
- This board also has a **real WiFi module** (`U801`, `FC20N-Q93`, SDIO-
  attached) and a separate BT RF amplifier (`U10`, `GSR2401`, not the
  RF5745 from an earlier datasheet) — neither is wired into this project
  yet; both are real, separate scopes of work.
- Full pin map: see `src/board_config.h`, sourced directly from the
  schematic's net labels.

## What's implemented (Phase 1: bring-up only)

- `src/clock.c` — peripheral clock enables + the one AFIO remap the debug
  UART needs. Deliberately still runs on the chip's default internal 8MHz
  oscillator, no PLL, no HXTAL dependency (see `board_config.h` for why
  and for the confirmed real 12MHz crystal value to use in Phase 2).
- `src/uart.c` — generic polled GD32/STM32F1-compatible USART driver.
- `src/rtt.c` / `src/log.c` — SEGGER RTT (log output read live over the
  same SWD connection used to flash, no UART/USB needed) plus a shared
  `log_line()`/`log_hex()`/`log_uint()` helper that also mirrors to the
  debug UART. This is how everything below is actually observed on real
  hardware.
- `src/yc1021.c` — **not standard Bluetooth HCI-H5** (an earlier revision
  assumed that from the datasheet's generic capability claim; real
  hardware testing disproved it — total silence, and the real working
  firmware doesn't use it either). Uses the actual proprietary Yichip
  framing recovered from this project's own disassembled `app_bt.c` /
  `app_transport.c`: TX `[0x01][cmd_type][len][payload]`, RX
  `[0x02][event_type][...]`, including the length-prefixed `{7,8}` event
  types that carry the Lx command text. Implements the provisioning
  sequence (name/pin/enable) and a first real command (`BoxInfo`) end to
  end. **Read the hard-learned warning above before touching TX here.**
- `src/ec25.c` — PWRKEY power-on pulse (polarity assumption flagged in
  the file, unconfirmed) + a plain `AT` -> `OK` handshake loop.
- `src/main.c` — ties it together: asserts `PWRHOLD` immediately (before
  anything else, per the schematic's power-latch net), brings up
  RTT+UART logging, runs a full LED test sequence (all 6 LEDs, 3 cycles,
  each logged by name), then polls the YC1021 and EC25 state machines.

## Bugs found and fixed by static review (before any hardware test)

No hardware/peripheral simulator is available in this environment, so
verification so far is: (1) independently re-implementing the SLIP/H5
framing and UART baud-divisor math in Python and cross-checking the
output against the C logic byte-for-byte, and (2) a careful manual
read-through of the boot sequence. That caught two real issues:

1. **`PWRHOLD` was set before `clock_init()`.** GPIOC's peripheral clock
   is only enabled inside `clock_init()`; a GPIO register write before
   its port's RCU clock bit is set is a no-op on this family. The one
   pin explicitly commented "must happen before anything else" would have
   silently done nothing. Fixed: `clock_init()` now runs first.
2. **PD0/PD1 (originally used for an external-watchdog kick) conflict
   with the confirmed-populated HXTAL crystal (X101)** on the same
   physical pins (schematic page 1: "OSC_IN/PD0" / "OSC_OUT/PD1"). A
   separate net-label block on page 2 also calls them `PD0_WDI`/
   `PD1_WDI`, which cannot both be true. Removed WDI handling entirely
   rather than guess a polarity/pin on a genuinely contradictory reading
   of the schematic -- see the note in `src/board_config.h`. If this
   board really has an external watchdog supervisor, find out what
   actually drives its input before adding this back.

## What's NOT implemented yet (real, separate work)

- Full HCI-H5 reliable data channel (ACK/SEQ tracking) — needed to
  actually exchange HCI commands/events with the YC1021 past the sync
  handshake.
- RS485 ports (LCR/meter node buses) — pins are confirmed in
  `board_config.h`, drivers not written.
- WiFi (`FC20N-Q93`, SDIO) and the `GSR2401` BT amplifier.
- Phase 2 clock config (HXTAL 12MHz -> PLL for full 120MHz operation).
- The Lx command protocol / BoxStatus wire format from the sibling
  project — this phase proves the hardware chain works before any of
  that gets layered on top, per the agreed scope.

## Building

```
make
```

Output: `build/bringup.bin` (and `.elf` for debugging with the same
J-Link).

## Flashing and watching it run

```
JLink -device GD32F305VC -if SWD -speed 4000 -CommandFile scripts\jlink_flash_and_run.jlink
```

Then read the log live over the same J-Link connection with SEGGER's
`JLinkRTTViewer.exe` (Device `GD32F305VC`, SWD, 4000 kHz — it
auto-detects the RTT control block), or capture a window of it with
`JLinkRTTLogger.exe -Device GD32F305VC -If SWD -Speed 4000 -RTTChannel 0
<outfile>`. Expect the boot banner, a full LED test sequence naming each
LED as it lights, then the YC1021/EC25 status lines. The debug UART
(DB9/J3) carries the identical log if you have a proper RS232-capable
adapter for it — see the note in `board_config.h` about why a plain
USB-TTL adapter won't work there.

If EC25 never responds: the PWRKEY pulse polarity assumption in
`src/ec25.c` is the first thing to check (scope the PWRKEY line during
`ec25_init()`), not the UART wiring. If YC1021 never shows RX activity:
see the hard-learned warning above before sending it anything further.
