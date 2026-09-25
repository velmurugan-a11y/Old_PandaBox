# PandaBox Development — 3-Session Activity Log

**Repo:** `velmurugan-a11y/Old_PandaBox`
**Local:** `C:\GD32\Old_PandaBox`
**Board:** PandaBox XBOX_V2.5 · GD32F305VCT6 · LQFP100
**Firmware project:** `Projects/04_Phase3_TCS_BLE_Mirror`
**Branch:** `feature/tcs-port2-happy-flow`

---

## User Inputs & Goal

> "Run the full happy flow end-to-end using pandabox-tester via BLE. Fix any errors found
> in serial print or code in both PandaBox firmware and LCR simulator."
>
> "PE5=HIGH, PE6=HIGH confirmed. USB-RS232 connected to PandaBox Port 2."
>
> "Create a new branch with updated code. Local folder is C:\GD32."

---

## Session 1 — Firmware Survey & Hardware Mapping

### What was done
- Full read of `Projects/04_Phase3_TCS_BLE_Mirror/` source tree
- Confirmed MCU from schematic + BOM: **GD32F305VCT6** (LQFP100, NOT the Z/LQFP144 package)
- Confirmed all peripheral pin assignments from `src/board_config.h` and XBOX_V2.5 schematic

### Key hardware facts confirmed from schematic

| Peripheral | Pins | Connector | Notes |
|---|---|---|---|
| Debug UART (USART0) | PB6(TX)/PB7(RX) remapped | J3 DB9 | 115200 8N1 via BL13232ETS U9 |
| TCS Port 1 (USART1) | PA2(TX)/PA3(RX) | J1 DB25 | 19200 8N1, RS232 via U504 |
| TCS Port 2 (USART2) | PD8(TX)/PD9(RX) full remap | J5 DB25 | 19200 8N1, RS232 via U504 |
| YC1021 BLE (UART3) | PC10(TX)/PC11(RX) | — | 115200, BT_RST=PD4, BT_EN=PD5 |
| EC25 4G (UART4) | PC12(TX)/PD2(RX) | — | PWRKEY=PB15, VGSM_EN=PE2 |
| RS232 power enable | PE5 | — | RT9080 LDO CE → U504 (both J1+J5) |
| RS485 power enable | PE6 | — | RT9080 LDO CE → U104/U4 RS485 |
| SPI0 Flash | PA4-PA7 | — | UNCONFIRMED — needs JEDEC ID verify |
| WiFi (SDIO) | U801 FC20N-Q93 | — | Out of scope, separate SDIO driver needed |

### Key findings
- `task_meter.c` boot: scans TCS nodes 1–10 via `tcs_scan_node()` (NOT LCP)
- Main loop: polls TCS + BLE every 5 ms
- `tcs_init()` asserts PE5 HIGH (RS232_EN) and PE6 HIGH (RS485_EN) — both rails powered
- PD0/PD1 = **CONFLICT**: used as 12 MHz crystal (HXTAL) AND labelled WDI — do NOT configure as GPIO
- Separate PIC10F200 housekeeping IC on schematic page 2 may be the real WDI driver

---

## Session 2 — Protocol Mismatch Discovery & Fixes

### Problem identified
The `LCR Meter\LCR-meter-simulator\lcr-meter-simulator_5\app.py` (LCP simulator) was
used to talk to PandaBox. Phase 3 firmware uses **TCS protocol** (not LCP). Zero bytes
were received on COM7 — double failure:

1. **Wrong protocol**: LCP uses double `0x7E` sync + CRC-16; TCS uses single `0x7E`
   sync + CRC-8 (Dallas/Maxim) with different opcodes entirely
2. **Wrong physical port**: `TCS_USE_RS485_2` defaulted to 0 → firmware sent frames on
   USART1/PA2/PA3/J1 (Port 1), but USB-RS232 adapter was wired to J5 (Port 2)

### TCS Protocol (Phase 3 firmware)
```
Frame: [0x7E][dest][src=0x41][flag][cmd][len][data...][crc8]
Escaping: 0x7E → 0x7D 0x5E, 0x7D → 0x7D 0x5D
Host address: 0x41 (PandaBox sends as src)

Opcodes:
  0xA9 = Scan/ClearState
  0x0C = Status
  0x37 = ConfigDeliveryA
  0x38 = ConfigDeliveryB
  0x3C = StartDelivery
  0x3D = Stop
  0x39 = Pause
  0x3A = Resume
  0x3E = Print
  0x42 = GetData(flow)        → flag=0x40
  0x2B = GetData(gross)       → flag=0x40
  0x1E = GetData(sysGross)    → flag=0x40
  0x1C = GetData(netTotal)    → flag=0x40

GetData response payload: [0x00][0x00][8-byte big-endian IEEE-754 double]
  (firmware reads double from rx_buf[8] in tcs_get_data())
```

### Fixes made

**Fix 1 — `src/tcs.c`**: Switched `TCS_USE_RS485_2` default from 0 to 1
```c
// BEFORE:
#define TCS_USE_RS485_2 0

// AFTER:
#define TCS_USE_RS485_2 1   /* USB-RS232 adapter is on Port 2 (PD8/PD9, J5) */
```

**Fix 2 — `scripts/tcs_simulator.py`**: Complete rewrite with correct Phase-3 opcodes.
Previous mock used guessed opcodes (0x10/0x11/0x12/0x13/0x20). Rewritten to handle
the real firmware opcode set above. MeterState simulates 5.0 gal/min flow when
`measuring=True` and not paused. Field responses carry the `[0x00][0x00][8-byte double]`
payload the firmware expects at `rx_buf[8]`.

**Fix 3 — Rebuild `build/bringup.bin`**: Recompiled manually with arm-none-eabi-gcc
(no `make` available on PATH). Build: cortex-m4 soft-float, -Os, 120 MHz SYSCLK.
Output: text=41884 data=168 bss=29640.

### BLE happy flow steps 1–4 PASS (before firmware reflash)
| Step | Command | Result |
|---|---|---|
| 1 | `SetPortLcrNode 1,2,` | PASS |
| 2 | `RdPortLcrNode` | PASS |
| 3 | `BoxStatus` | PASS |
| 4 | `BoxInfo` | PASS |
| 5–13 | GetData/Start/Pause/Stop | BLOCKED (wrong port, no TCS response) |

---

## Session 3 — Branch, Flash & Happy Flow Run

### Branch created
```
feature/tcs-port2-happy-flow
commit 42a2086
```
Contains: `src/tcs.c` (port fix) + `scripts/tcs_simulator.py` (full rewrite)

### JLink flash issue
- JLink probe showed `IsPresent=False` in Windows PnP — probe USB not connected to PC
- J-Flash Lite (Start Menu → SEGGER J-Link V9.58 → JFlashLite.exe) used instead
- Flashed `build/bringup.bin` at `0x08000000` successfully

**J-Flash Lite settings used:**
```
Device:    GD32F305VC
Interface: SWD
Speed:     4000 kHz
File:      C:\GD32\Old_PandaBox\Projects\04_Phase3_TCS_BLE_Mirror\build\bringup.bin
Addr:      0x08000000
```

### Happy flow run result
```
LCR bridge stopped: POST http://127.0.0.1:5000/api/serial/stop  → ok
TCS simulator started: python scripts\tcs_simulator.py COM7 --node 1 --baud 19200

14:09:55  Scanning for PandaBrainBLE ...
14:10:02  Found PandaBrainBLE @ E7:11:33:32:09:7B (7.2s)
14:10:43  ERROR: TimeoutError:
14:10:43  Disconnected
```

### Root cause: BLE reconnect limitation
YC1021 (U5) only accepts the **first BLE connection per power cycle** from a given PC.
Once the bleak scanner found the device and attempted `BleakClient.connect(timeout=40)`,
the connection timed out — device was either already connected from a prior session or
in a state that refuses a new connection from the same BLE central address.

**Known workaround**: Power-cycle the PandaBox (full board power off/on), then
immediately run `happy_flow.py` before any other BLE tool connects.

---

## Pending Items

| # | Item | Status |
|---|---|---|
| 1 | TCS_USE_RS485_2=1 fix | ✅ Done & flashed |
| 2 | tcs_simulator.py rewrite | ✅ Done |
| 3 | bringup.bin rebuilt & flashed | ✅ Done |
| 4 | BLE happy flow full run | ❌ Blocked by BLE reconnect timeout |
| 5 | Verify TCS frames on COM7 (simulator logs) | ❌ Not yet — needs power-cycle first |
| 6 | SPI flash JEDEC ID readback (confirm PA4–PA7) | ❌ Not done |
| 7 | PD0/PD1 WDI vs HXTAL crystal conflict | ❌ Needs schematic visual check |
| 8 | EC25 4G bring-up (VGSM_EN=PE2, PWRKEY=PB15) | ❌ Out of scope this session |
| 9 | WiFi U801 FC20N-Q93 SDIO driver | ❌ Out of scope |

## To resume next session

1. **Power-cycle the PandaBox** (unplug USB power, wait 3s, replug)
2. **Start TCS simulator**: `python scripts\tcs_simulator.py COM7 --node 1 --baud 19200`
3. **Immediately run**: `python happy_flow.py`
4. Check simulator output for TCS frames (Scan, Status, GetData, Start, etc.)
5. If no frames on COM7 → check J1 vs J5 null-modem wiring (TXD/RXD crossover + GND)

## Working Rules (must remain in effect)
- Tester/LCR folders (`LCR Meter\`, pandabox-tester): **read-only**
- No external flash writes (do not call flash_store write functions)
- **Push only when explicitly told**
- COM9 held by tester — do not open or close
- BLE address MSB currently: **E7** (E7:11:33:32:09:7B)
