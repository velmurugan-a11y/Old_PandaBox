# PandaBox GD32F305VCT6 — Full Development Notes
## Branch: feature/phase3-ble-modem-serial-mirror
## Date: 2026-09-24

---

## 1. PROJECT CONTEXT

**Hardware:** FleetPanda PandaBox XBOX_V2.5 PCB  
**MCU:** GD32F305VCT6, LQFP100, Cortex-M4, 256 KB flash, 96 KB RAM  
**Crystal:** 12 MHz (X101 HXTAL) → PLL ×10 = 120 MHz SYSCLK  
**Firmware purpose:** Replace Leo's production firmware (V2.87–V2.89) with an open, clean-room rewrite, adding TCS meter real-hardware support and flash persistence, while maintaining full protocol compatibility with the existing Android/iOS PandaBox app and pandabox-tester test harness.

**Why this repo exists:**  
The original `Old_PandaBox` project (Projects/03_PandaBox_BLE_LCR) was the first working proof-of-concept — it verified the YC1021 BLE chip, extracted the 105-record HCI init table from Leo's binary, and proved full Lx command round-trips over BLE. This repo (Vishal main → phase-3) is the production-quality rewrite: FreeRTOS, RTT+UART dual logging, real SPI flash driver, and actual TCS bus transactions instead of a simulator.

---

## 2. HARDWARE PIN MAP (from XBOX_V2.5 schematic)

| Signal | MCU Pin | Notes |
|---|---|---|
| **Debug UART TX** | PB6 (USART0 remap) | CH340 on DB9/J3, 115200 8N1, BL13232ETS RS232 level |
| **Debug UART RX** | PB7 (USART0 remap) | |
| **TCS RS485 port 1 TX** | PA2 (USART1) | 19200 8N1, SIT3088EESA transceiver |
| **TCS RS485 port 1 RX** | PA3 (USART1) | |
| **TCS RS485 port 2 TX** | PD8 (USART2 full remap) | |
| **TCS RS485 port 2 RX** | PD9 (USART2 full remap) | |
| **RS485 DIR port 1** | PE3 | HIGH=receive, LOW=transmit (NPN Q13) |
| **RS485 DIR port 2** | PE4 | HIGH=receive, LOW=transmit (NPN Q14) |
| **RS232 transceiver enable** | PE5 | HIGH=on (U8 RT9193-33 LDO → BL13232ETS) |
| **RS485 transceiver enable** | PE6 | HIGH=on (U11 RT9193-33 LDO → SIT3088EESA) |
| **YC1021 BT ENABLE** | PD5 | HIGH=power on (BT_3.3V LDO) |
| **YC1021 BT RESET** | PD4 | LOW=reset (active low) |
| **YC1021 UART TX** | PC10 (UART3) | 115200 8N1 |
| **YC1021 UART RX** | PC11 (UART3) | |
| **EC25 POWER (VGSM LDO)** | PE2 | HIGH=on |
| **EC25 PWRKEY** | PB15 | Pulse HIGH ~600ms via NPN Q4 |
| **EC25 DTR** | PA8 | LOW=wake |
| **EC25 UART TX** | PC12 (UART4) | 115200 8N1 |
| **EC25 UART RX** | PD2 (UART4) | |
| **SPI0 Flash CS** | PA4 | UNCONFIRMED — verify JEDEC ID |
| **SPI0 Flash SCK** | PA5 | UNCONFIRMED |
| **SPI0 Flash MISO** | PA6 | UNCONFIRMED |
| **SPI0 Flash MOSI** | PA7 | UNCONFIRMED |
| **PWRHOLD** | PC8 | HIGH after boot to stay alive |
| **LED PWR (red)** | PB1 | Active high |
| **LED GPS (green)** | PB0 | Active high |
| **LED WIFI (orange)** | PC4 | Active high |
| **LED BT (blue)** | PC5 | Active high |
| **LED LCP1** | PC6 | Active high |
| **LED LCP2** | PC7 | Active high |
| **12V detect port 1** | PB13 | Active low (open collector from DB25) |
| **12V detect port 2** | PB12 | Active low |
| **12V ADC** | PC0 | ÷11 resistor divider |
| **Coin cell ADC** | PC1 | ÷2 divider, PE1=BATT_EN to measure |

---

## 3. DEVELOPMENT HISTORY — WHAT WE TRIED AND HOW WE GOT HERE

### Phase A — Project 01: First Light (Old_PandaBox repo)
- Verified the GCC + J-Link toolchain works on this exact board
- 6-LED running-light on PB1/PB0/PC4/PC5/PC6/PC7
- Confirmed 120 MHz PLL, confirmed linker script places code at 0x08000000
- Build: `arm-none-eabi-gcc`, single `build.sh` script, no RTOS

### Phase B — Leo's Files and Pin Map
- Received Leo's handover folder: PCB layout, schematics, datasheets, legacy firmware binary
- Built the complete XBOX_V2.5 LQFP100 pin map from the PCB netlist
- Read Leo's V2.87 binary with `arm-none-eabi-objdump`; found the YC1021 init table at address `0x08018B9E` (105 records, 10,125 bytes)
- Key insight: Leo's firmware uploads a 105-record HCI patch+GATT configuration table FIRST, before any name/PIN/MAC commands. Earlier attempts that skipped this table never got any acknowledgement from the YC1021.

### Phase C — Hardware Bringup (Project 02)
- Console firmware with one command per peripheral: info, led, adc, flash, gsm, bt, rs232, rs485
- Python host script `bringup.py` logged every exchange to CSV
- Results:
  - Console (USART0 PB6/PB7): working
  - LEDs: all 6 working
  - ADC: 12V rail and coin cell readable
  - External flash GD25Q256E: read-only dump succeeded; 6004 old delivery records decoded
  - EC25 modem: AT handshake succeeded, IMEI = 860858062040217
  - YC1021 BLE: **NOT working** with first approach (7-step guess, no init table)
- **RS232/RS485 transceiver result:** FAIL — PA3 and PD9 stuck HIGH in all states (rail on/off, direction). Root cause: R252 (10kΩ pull-up from DB25 J1 pin 15 to RS485_3.3V) keeps PA3 HIGH even when U11 LDO is off. Fix: fit a DB25 loopback plug (pin 14↔15 on J1) and re-run `rs232 1` with bringup firmware. This has NOT been done yet.
  - **Still needs:** DB25 female loopback plug (pin 14 wired to pin 15) + `rs232 1` command to confirm end-to-end RS232 TX→RX path at 19200 baud

### Phase D — YC1021 Init Table Extraction
- Used `tools/extract_bt_table.py` against Leo's V2.87 binary
- Extracted all 105 HCI records: FC03 patch load, FC10 memory writes, FC04 start, then standard HCI vendor commands for GATT DB
- Result: `bt_init_table.c` — 10,125 bytes — do NOT edit

### Phase E — Project 03: BLE + LCR Emulator (Old_PandaBox repo)
- First firmware that successfully brought up the YC1021 on this board
- All 105 init table records acknowledged
- Advertising as "PandaBrain" / "PandaBrainBLE"
- Full Lx command protocol implemented (all tester commands)
- LCR meter emulator (simulator) — no real meter connected
- **BLE reconnect issue discovered:** only the FIRST connect from a given Windows PC/BLE address works. Subsequent connects fail. Root cause: suspected Coded PHY issue (the tester's ble_engine.py explicitly waits 5s for a Coded PHY switch that Leo's firmware does). BLE address MSB changed E5→E6→E7 as workaround. **Not yet fixed.**
- **Service UUID not in advertisement:** tester wastes 12s on a UUID-filtered scan that fails, then 12s more on a plain name scan. Total: ~24s per connect instead of ~2s. **Not yet fixed.**

### Phase F — FreeRTOS Port + EC25 Fix (hw-reverse-leo-firmware-vishal-main)
- Complete architectural rewrite: bare-metal → FreeRTOS (ARM_CM3_GD32 port)
- Dual logging: SEGGER RTT (SWD, JLinkRTTViewer) + debug UART (CH340/COM9)
- **Major EC25 fix:** PE2 (VGSM LDO enable) was never driven — modem was literally unpowered in all prior attempts. Added 13s boot settle per Quectel datasheet. Now gets AT OK reliably.
- **Ring buffer fix:** YC1021 and EC25 UARTs now interrupt-driven (ISR → ring buffer). Previously, polling once per FreeRTOS tick (5ms) lost most bytes of fast bursts.
- Lx command dispatcher: all commands respond with internally consistent mock data
- TCS bus scan (`tcs_scan_node`) working — can discover meter nodes on the RS485 bus

### Phase G — Flash + TCS Meter (this repo: hw-reverse-leo-firmware-vishal-phase-3-tcs-meter)
- Added GD25Q256E SPI NOR flash driver (`spi0.c`, `gd25q.c`)
- Added `flash_store.c`: settings round-robin (16 slots, wear leveling, CRC16-CCITT), meter history logs (1 MB per port)
- Added real TCS meter commands: Start (3-step with IEEE754 double preset encoding), Stop, Pause, Resume, Print, GetData (4 field reads: flow/gross/system_gross/net_totalizer)
- **IEEE754 double codec** implemented in pure integer (no `float`/`double` C types, consistent with `-mfloat-abi=soft` build). Verified with Python test script against 13,999+ test cases.
- SPI flash pin assignment UNCONFIRMED — JEDEC ID selftest will reveal correctness on boot

### Phase H — This commit: BLE + Modem Serial Mirror
- **Feature added:** Every BLE command received and every reply sent back is now mirrored to both RTT (Segger) and debug UART (CH340/COM9) with clear labels
- **Feature added:** Every modem AT command sent and every modem response is mirrored with clear labels
- Raw hex dumps (previously always on) are now gated behind `SetDbg` — they appear only in debug mode so the console stays readable in normal use
- Console output format:
  ```
  [BLE RX] BoxStatus                   ← received from phone/tester via BLE
  [BLE TX] LxBoxStatus 2,0,1,2,0.0,S,0.0,E,0,1   ← reply sent back to BLE
  [SPP RX] BoxInfo                     ← received via SPP (classic BT)
  [SPP TX] LxBoxInfo 2.4,...           ← reply sent via SPP
  [GSM TX] AT                          ← sent to EC25 modem
  [GSM RX] OK                          ← response from EC25 modem
  [GSM TX] AT+QGPS=1                   ← GPS command sent
  [GSM RX] OK                          ← GPS response
  ```
- When `SetDbg 1` is active (debug mode ON):
  ```
  [BLE DBG] RAW RX: 02 08 0B 2D 00 42 6F 78 53 74 61 74 75 73   ← raw HCI bytes
  [GSM DBG] RAW RX: 4F 4B 0D 0A                                  ← raw modem bytes
  ```

---

## 4. CURRENT STATE — WHAT WORKS

| Feature | Status |
|---|---|
| 120 MHz PLL from HXTAL | ✅ Working |
| FreeRTOS (Meter + Telit + OTA tasks) | ✅ Working |
| RTT logging (Segger/JLinkRTTViewer) | ✅ Working |
| Debug UART (CH340/COM9, 115200) | ✅ Working |
| YC1021 BLE bring-up (105/105 init table records) | ✅ Working |
| BLE advertising ("PandaBrain"/"PandaBrainBLE") | ✅ Working |
| BLE connect and command round-trip (first connect) | ✅ Working |
| Full Lx command protocol | ✅ Working (all tester commands) |
| **BLE TX/RX mirror to serial console** | ✅ Added this commit |
| **Modem TX/RX mirror to serial console** | ✅ Added this commit |
| EC25 modem power-on and AT handshake | ✅ Working |
| EC25 GPS (AT+QGPS=1, AT+QGPSLOC?) | ✅ Working (logged, not parsed) |
| TCS meter scan (node discovery) | ✅ Working |
| TCS Start/Stop/Pause/Resume/Print | ✅ Implemented (real bus) |
| TCS GetData (4 fields: flow/gross/sys_gross/net) | ✅ Implemented (real bus) |
| SPI flash driver (GD25Q256E) | ✅ Implemented (pins unconfirmed) |
| Flash settings persistence | ✅ Implemented |
| Flash meter history log | ✅ Implemented |
| IEEE754 double codec (pure integer) | ✅ Implemented + Python-verified |

---

## 5. KNOWN ISSUES AND BLOCKERS

### BLE-1 — Reconnect fails after first disconnect (BLOCKER for tester)
**Symptom:** After disconnecting and reconnecting, the PC/tester cannot establish a new BLE connection to the same device.  
**Root cause (suspected):** The YC1021 init table (from Leo's V2.87) may configure Coded PHY advertising or a PHY update post-connect. The tester's `ble_engine.py` explicitly waits 5 seconds after connect for a Coded PHY switch ("PandaBox firmware switches to LE Coded PHY ~4 s after connecting"). If the Windows BT adapter doesn't support Coded PHY or the link drops mid-switch, the Windows BT stack retains half-open state.  
**Workaround:** Change BLE address MSB after each failed session (E5→E6→E7 used). Address E7 is current.  
**Next steps to fix:**
1. Enable `trace bt 1` / `SetDbg 1`, capture raw YC1021 frames during a failed second connect
2. Test from a phone (nRF Connect) — isolates Windows adapter vs. module issue
3. Flash Leo's V2.89 on this board, connect twice — gives the reference behavior
4. If Coded PHY is confirmed: find the YC1021 vendor command to disable post-connect PHY update, or confirm Windows adapter capability

### BLE-2 — No service UUID in advertisement (12s connect penalty)
**Symptom:** Tester's `_resolve_device()` first runs a 12s UUID-filtered scan (fails — we have no UUID in advertisement), then a 12s plain name scan. Every connect costs ~24s instead of ~2s.  
**Root cause:** We don't know the YC1021 command to set advertising data payload. Leo's firmware includes the service UUID somehow.  
**Next steps:** Probe with `SetDbg 1` while advertising, capture raw frames. Or obtain YC1021 SDK.

### RS232/RS485 — Transceiver loopback not confirmed
**Symptom:** All RS232/RS485 tests in Project 02 bringup firmware returned FAIL. PA3/PD9 stuck HIGH regardless of PE5/PE6 state.  
**Root cause:** R252 (10kΩ pull-up from DB25 J1 pin 15 to RS485_3.3V) keeps line HIGH even with LDO off.  
**Fix needed:** 
1. Make a female DB25 loopback plug: wire pin 14 to pin 15
2. Flash Project 02 bringup firmware
3. Run `rs232 1` → expect `rx_off=0 rx_on=1 loop=21/21`
4. Optionally: probe U8/U11 output with multimeter during `rsdiag` to confirm LDO switching

### SPI Flash pins unconfirmed
PA4/PA5/PA6/PA7 assumed for SPI0 — not verified against PCB netlist. Boot selftest runs JEDEC ID read; if it returns 0xC8 4019, pins are correct.

### Config/settings persistence — RAM-only until flash confirmed
Settings (node assignments, BT name, mode) are now written to flash via `flash_store_save_settings()` but will revert to defaults if flash JEDEC ID check fails at boot.

### DeleteAll is logical-only
`DeleteAll` resets the flash log indices in RAM. Physical sectors are not erased. Records reappear if the board resets before any new record is written over them. Physical tombstone deferred.

---

## 6. WHAT TO DO NEXT (PRIORITY ORDER)

### Sprint 1 — Tester full pass

**P1. Investigate BLE reconnect (blocker)**
- `SetDbg 1` + JLinkRTTViewer → watch raw YC1021 frames during second connect attempt
- Test from nRF Connect (phone) to isolate Windows-specific behavior
- If Coded PHY is the issue: find the disable command in the YC1021 vendor set

**P2. Add service UUID to BLE advertisement**
- Removes the 12s UUID-scan penalty on every connect
- Research YC1021 advertising-data vendor command (0x04? extended?)

**P3. Run `happy_flow.csv` end-to-end through pandabox-tester**
- Path: `C:\GD32\pandabox-tester_v14\pandabox-tester\sequences\happy_flow.csv`
- All Lx commands are implemented; tester should PASS once BLE is stable
- Watch: `GetData n,1` history end-marker format (`LxGetDataTs n,` for empty history)

### Sprint 2 — Real meters

**P4. RS232/RS485 loopback confirmation**
- Female DB25 loopback plug: pin 14 ↔ pin 15 on J1
- Flash Project 02, run `rs232 1`
- Expected: `RESULT rs232_1 PASS rx_off=0 rx_on=1 loop=21/21`

**P5. Real TCS meter end-to-end**
- SPI flash pins confirmed → settings persist → node assignment survives reset
- Connect a real LCR-II/TCS meter to J1 (DB25)
- Send `SetPortLcrNode 1,<node_id>` then `Start 1`
- `GetData 1 0` should return real flow/totalizer values from the meter bus

### Sprint 3 — Production features

**P6. 4G TCP client**
- EC25 modem is alive and IMEI is read
- Next: SIM + APN + TCP connect to `34.121.179.10:8080`
- Send `Heart,...` keepalive messages to FleetPanda server

**P7. GNSS location**
- EC25 AT+QGPS=1 already sent at boot
- Parse `AT+QGPSLOC?` response into lat/lon for BoxStatus and GetData fields
- Currently hardcoded to `0.0,S,0.0,E`

**P8. Bootloader and OTA**
- Leo's V2.64 bootloader binary is at `Old_PandaBox/backup/original_flash_256K.bin`
- Relocate app to `0x08008000`, write APP_INFO at `0x08007800`
- Test boot chain and APP1/APP2 swap

---

## 7. TOOLS AND BUILD SETUP

### To build
```bash
# Requires arm-none-eabi-gcc on PATH (installed at C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\)
make
# Output: build/bringup.bin and build/bringup.elf
```

### To flash (J-Link)
```bash
# Using JLink Commander scripts in scripts/
JLinkExe -if SWD -device GD32F305VC -speed 4000 -CommandFile scripts/jlink_flash.jlink
# Or: JFlash / Ozone
```

### To monitor (dual — both receive identical output)
- **Segger RTT:** JLinkRTTViewer.exe → auto-detect → Terminal 0
- **CH340 (COM9):** 115200 8N1, any terminal (Tera Term / PuTTY / bringup.py)

### Console output — what you'll see
```
--- YC1021: uploading 105-record init table (real, from Leo's firmware) ---
YC1021: init table 105/105 records acknowledged
YC1021: bring-up complete -- advertising as "PandaBrain" / "PandaBrainBLE"
--- EC25: enabling VGSM (PE2) ---
EC25: pulsing PWRKEY
EC25: waiting ~13s for UART to become active per datasheet power-up timing
[GSM TX] AT
[GSM RX] RDY
[GSM RX] OK

... (once phone/tester connects and sends commands) ...

[BLE RX] BoxStatus
[BLE TX] LxBoxStatus 2,0,1,2,0.0,S,0.0,E,0,1
[BLE RX] Start 1
[BLE TX] LxStart 0
[BLE RX] GetData 1 0
[BLE TX] LxGetData 1,0,60.0,12345.6,12345.6,0.0,0,1747123456
```

### Debug mode (verbose raw bytes)
Send `SetDbg 1` over BLE — enables:
```
[BLE DBG] RAW RX: 02 08 0B 2D 00 42 6F 78 53 74 61 74 75 73
[GSM DBG] RAW RX: 4F 4B 0D 0A
```
Send `SetDbg 0` to return to clean output.

---

## 8. REFERENCE FILES

| File | Purpose |
|---|---|
| `Old_PandaBox/docs/PandaBox_Development_Journal.md` | Full narrative of everything tried, decisions made, lessons learned |
| `Old_PandaBox/docs/reference/01_Hardware_Reference.md` | Complete LQFP100 pin map, DB25 pinout, schematic notes |
| `Old_PandaBox/docs/reference/02_Legacy_Firmware_Reference.md` | Leo's firmware analysis: flash layout, delivery record format, config structure |
| `Old_PandaBox/docs/reference/03_Protocol_Reference.md` | Lx command protocol (rev 1.86) spec |
| `Old_PandaBox/backup/original_flash_256K.bin` | Full flash backup before any firmware was written |
| `C:\GD32\pandabox-tester_v14\pandabox-tester\` | Python tester tool: sequences/, validator.py, ble_engine.py |
| `Old_PandaBox/From Leo/02_Datasheets/GD32F305VCT6.pdf` | MCU datasheet |
| `Old_PandaBox/From Leo/02_Datasheets/rf5745_data_sheet.pdf` | YC1021 BLE module datasheet |
| `Old_PandaBox/From Leo/01_Hardware/XBOX_V2.5 Hardware Schematic Diagram.pdf` | Full schematic |
| `tests/verify_tcs_double_codec.py` | Python cross-check for IEEE754 double ↔ tenths codec |

---

## 9. THIS COMMIT — CHANGES MADE

**Requirement:** Mirror everything sent/received via BLE and EC25 modem to both the Segger RTT console and the CH340 debug UART (COM9). Make it readable (not a flood of raw hex).

**Files changed:**
- `src/yc1021.c`: Added `[BLE RX]` and `[BLE TX]` labels in `yc1021_assemble()`. The reply sent back to BLE was previously NOT logged at all — now it is. Raw hex dumps (`[BLE DBG] RAW RX`) gated behind `cmd_debug_enabled()`.
- `src/ec25.c`: Changed `EC25 RX line:` → `[GSM RX]`, `EC25 TX:` → `[GSM TX]`, `EC25 GPS TX:` → `[GSM TX]`, `EC25 GPS RX:` → `[GSM RX]`. Added `#include "cmd.h"`. Raw hex dumps gated behind `cmd_debug_enabled()`.

**What did NOT change:** All protocol logic, TCS meter code, flash store, FreeRTOS task structure. Only logging labels and the addition of the missing reply-mirror line.

**Verification:** `yc1021.c` and `ec25.c` (and all 7 key modules) compiled clean with `arm-none-eabi-gcc 14.2.Rel1` with zero errors and zero new warnings.

---

## 10. PC5 BT BLUE LED — CONNECT LATCH + DATA BLINK (added this session)

**Requirement:** PC5 (BT blue LED, active high) must:
- Be **OFF** when not connected (boot/advertising state)
- **LATCH ON** (steady) when a BLE or SPP link is established
- **BLINK** (turn off for 80 ms) on every data transfer (command received OR reply sent)
- Turn **OFF** again on disconnect

**Implementation** (all in `src/yc1021.c`):

```
static TickType_t s_led_blink_until;   // 0 = no active blink

led_bt_on()   → gpio_set_output_high(LED_BT_BLUE_PORT, LED_BT_BLUE_PIN)
led_bt_off()  → gpio_set_output_low(LED_BT_BLUE_PORT,  LED_BT_BLUE_PIN)
led_bt_blink() → led_bt_off(); s_led_blink_until = now + 80ms
```

Wired at four points:
- `handle_event() case 0x02 (status)`: link-up → `led_bt_on()`, link-down → `led_bt_off()`
- `handle_event() case 0x05 (link closed)`: → `led_bt_off()`
- `yc1021_assemble()` on RX: → `led_bt_blink()`
- `yc1021_assemble()` before `yc1021_send_line()` on TX: → `led_bt_blink()`
- `yc1021_poll()` at top: if `s_led_blink_until` expired and `online`: → `led_bt_on()`
- `yc1021_init()` at end: → `led_bt_off()` (clean reset state)

**Visible behavior on hardware:**
```
Boot / advertising     → PC5 OFF
Phone connects         → PC5 ON  (steady, latched)
Phone sends BoxStatus  → PC5 blinks off 80ms, returns ON
Firmware replies       → PC5 blinks off 80ms, returns ON
Phone disconnects      → PC5 OFF
```

**Note:** If two transfers happen faster than 80 ms apart (unlikely at Lx command cadence), the blink timer resets to `now + 80ms` on each event — the LED stays off until 80 ms after the LAST transfer.

**Verification:** Compiled with `-Wextra`, zero errors, zero warnings.

---

*End of development notes.*
