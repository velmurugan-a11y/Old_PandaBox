# PandaBox V2.89 Rebuild: Project Structure and Plan

**Goal:** write our own source code for firmware that behaves exactly like Leo's **X-Box V2.89** (`LxBoxInfo 2.4,250502,2.891,260827`) on the same XBOX V2.5 board, so that it can replace V2.89 in the field without the Driver App, the cloud server, Leo's bootloader or the data already stored on the box noticing a difference.

Today we only have V2.89 as binaries (`From Leo/05_Firmware_Binaries/X-Box_V2.89_APP1.bin` / `_APP2.bin`) plus about 30 % of an older source snapshot.

| | |
|---|---|
| Date | 2026-09-25 |
| Status | Plan only: nothing is scaffolded yet (decisions in §9 come first) |
| Inputs reviewed | All 5 repo branches, `docs/`, `From Leo/`, `Projects/01–04`, `C:\GD32\Vishal` (3 branches), `C:\GD32\pandabox-tester_v14` (read only), `C:\GD32\LCR Meter` (read only), `C:\GD32\PandaBox_Command_Test_Tracker.xlsx` |

---

## 1. What "the same as V2.89" means

We are not trying to rebuild the exact same binary; nobody can without Leo's full source and compiler setup. We are building firmware whose **observable behaviour** matches. That behaviour is defined by the contracts below, and every one of them has to hold:

| # | Contract | Must match exactly | Where the truth comes from |
|---|---|---|---|
| C1 | **App protocol** (BLE/SPP, WiFi TCP 5553, 4G TCP) | Every command word, how it is matched (`strstr` order), and every reply string, byte for byte, including Leo's typos (`LxModifytLcrNode`, `LxFindLcrNode`) and trailing commas | V2.89 strings + disassembly (§3.2), Protocol Rev 1.86, Leo's logs |
| C2 | **LCP bus to the meters** | Frames, node 0x14/0x15, toggle byte, poll order #2/#4/#17/#18/#100/#101, 1 s cycle, 120 ms timeout, retries, 50 ms RX gap | Leo's `app_lcr.c` + V2.89 debug trace (`---lcr2 send data-7E 7E 02 14 01 02 20 02 AB 56`) |
| C3 | **Internal flash layout** | Bootloader at `0x08000000`, APP_INFO at `0x08007800` (24 B, flags `ZZZZ`/`bank`/`app1`/`app2`), APP1 at `0x08008000`, APP2 at `0x08021000`, `T_BOX_PARAM` (116 B) at `0x0803F800` | `02_Legacy_Firmware_Reference.md` §2.4, §3 |
| C4 | **External flash records** | GD25Q256: 16 MB per port, header in sector 0 (`T_LCR_DATA_INFO`), 64-byte records, ring buffer, time recovery at boot | `02_Legacy_Firmware_Reference.md` §2.3.6, `decode_flash.py`, 6004 real records on the bench board |
| C5 | **Bluetooth identity** | 105-record YC1021 init table, name `PandaBrain`/`PandaBrainBLE`, PIN 1234, MACs `24:06:…` derived from the UID, GATT handles 0x2A/0x2D, frames `01 05`/`02 07`/`01 09`/`02 08` | `bt_init_table.c` (taken from V2.89 `0x08018B9E`), BT start code at V2.89 `0x0800DD34` |
| C6 | **4G/GPS/WiFi** | The 15-command AT init table (order included), `write(IMEI,…)`, `Heart,…` format and period, `AT+CSQ` every 15 s, RMC GPS, `QDATAFWD` hex WiFi forward | V2.89 table at `0x0801B5D0`, `app_ec20.c` |
| C7 | **Upgrade** | `Update APP<n>,<len>,<sum>` over 4G/BT/WiFi, XMODEM, 32-bit byte-sum check, A/B flag swap, `LxUpdate 0/1`; Leo's bootloader must boot our image | `update.c`, V2.89 update code, bootloader in the dump |
| C8 | **Debug console / CLI** | USART0 PB6/PB7 at 115200, `D,<ms>,<file>,<line>:<text>` log format, CLI `appinfo`, `update`, `reboot`, `reason`, `set debug level`, `debug on/off/time` | V2.89 strings, Leo's serial logs |
| C9 | **LEDs and GPIO** | GPIO init values the same as V2.89 (PE5 = 1 RS232 on, PE6 = 0 …), same LED patterns per state | V2.89 GPIO table `0x08015990` |
| C10 | **Timing** | 3 s app-init delay, 1 s poll, 120 ms field timeout, heartbeat = HeatTime (1000 ms default), 600 ms PWRKEY | Source + disassembly |

**Out of scope for V2.89 parity:** TCS 3000 support (that's Leo's separate firmware line `1.141…,TCS`), the V3.0x EMBTECH firmware (`LxRSSI`, IMEI 3566…), and FleetPanda's requested commands that V2.89 doesn't have (`SetUploadFreq`, `RdEzCmdStatus`, …). These come **after** parity (§8, M12).

---

## 2. Inventory: what we have and what each source gives us

### 2.1 Repository branches (`velmurugan-a11y/Old_PandaBox`)

| Branch | Latest | What it adds | Use for the rebuild |
|---|---|---|---|
| `main` | `939737b` | GD32F30x firmware library + GD32305R-START demos V3.0.3 | Library (CMSIS, headers). We'll use our own register-level HAL or the std-periph library (§9 D2) |
| `docs/pandabox-reference` | `3fb6304` | Reference docs, `From Leo/`, 6-LED project | **Main specification** |
| `bringup/hw-handshake` | `2739730` | `Projects/02_HW_Bringup`, external flash dumps, `decode_flash.py`, `extract_bt_table.py` | Record-format decoder, BT table extractor, per-peripheral tests |
| `feature/ble-lcr-emulator` | `a119942` | `Projects/03_PandaBox_BLE_LCR` (super-loop, BLE + LCP + simulator), journal | **`lcp.c` (CRC/escape + self-test vectors), `bt.c` frame logic, `meter.c` simulator** |
| `feature/phase3-ble-modem-serial-mirror` (checked out; 2 local commits not pushed; `board_config.h`/`tcs.c` modified, uncommitted) | `3ca78dd` (local) | `Projects/04_Phase3_TCS_BLE_Mirror` (Vishal's FreeRTOS code: TCS, SPI flash, LED manager, EC25) | `gd25q.c`/`spi0.c` driver, EC25 power-up timing, UART ring buffers, LED state ideas |

`C:\GD32\Vishal\` holds zipped snapshots of Vishal's `main`, `phase-2-flash-storage` and `phase-3-tcs-meter`. Phase 3 is the same code as `Projects/04`.

### 2.2 Leo's material (`From Leo/`)

| Item | Gives us |
|---|---|
| `05_Firmware_Binaries/X-Box_V2.89_APP1/APP2.bin` (80 076 B, SP `0x20003950`, reset `0x08008165`) | **The reference.** Every string, table, constant and function we need to match |
| `X-Box_V2.87_APP1/2.bin` | Diff baseline. V2.89 differs from V2.87 only in `LxGetDataTs` handling and the port-2 (`lcr2`) receive/update logging (checked by string diff) |
| `Bootlaod_APP_V2.64.bin` | Bootloader + APP_INFO + app V2.62 (factory image) |
| `04_Reference_Source/*.c` (2023) | `main_init.c`, `app_cfg.c`, `app_ec20.c` (part), `app_lcr.c` (2614 lines), `update.c`. About 30 % of the code, and older than V2.89 |
| `03_Protocols_and_Specs/` | Protocol Rev 1.86, "Adding Pandabox Commands", LCP02, TCS3000 RI, tech spec |
| `01_Hardware`, `02_Datasheets` | Schematic (outdated in places), PCB, BOM, GD32F305/GD25Q256/YC1021 datasheets |

### 2.3 Behaviour evidence (golden traces)

| Source | What it proves |
|---|---|
| `pandabox-tester/logs/serial_Panda1BLE_20260921_185457.log` (2.4 MB, 28 202 lines) and `…_192719.log` | **A real V2.89 box (IMEI 860858062040035) with a meter**: Leo's debug output line by line (`D,<ms>,app_lcr.c,0480:---lcr2 send data-…`), LCP frames with real meter replies, GPS RMC, WiFi forward. Line numbers point at Leo's source lines |
| Tester logs `serial_Panda2BLE_*`, `serial_PandaBox_20260925_*` | Other firmware (TCS 1.14, V2.62, test builds): for comparison only |
| `Projects/02_HW_Bringup/logs/extflash_*.bin` | Real records written by Leo's firmware (C4 test data) |
| `backup/original_flash_256K.bin` | Leo's bootloader + V2.77 + real `T_BOX_PARAM` |
| `PandaBox_Command_Test_Tracker.xlsx` | 40 commands with per-command test sheets (Happy/Error cases), plus EMBTECH's 129-case test report (Rev 1.86) |
| `pandabox-tester` (`validator.py`, `sequences/*.csv`) | The FleetPanda acceptance harness: `happy_flow`, `_10min`, `_overnight`, `_36hr`, `_retry` |

### 2.4 Doc errata found during this review (fix before relying on them)

| Where | Says | Correct (source) |
|---|---|---|
| `Projects/04…/DEVELOPMENT_NOTES.md` §2 | EC25 DTR = **PA8** | **PA10** (PA8 = `FLASH2_CS`, not connected). `01_Hardware_Reference.md` line 132, V2.89 GPIO init |
| same | PC8 PWRHOLD "HIGH after boot to stay alive" | PC8 hold path is **DNP**, so it does nothing (`01_Hardware_Reference.md` line 130) |
| same | SPI0 pins PA4–PA7 "UNCONFIRMED" | Confirmed: JEDEC `C8 40 19` read in bring-up session 1 |
| `PandaBox_Master_Note.md` §3.4 | Binary table stops at V2.87 | V2.89 exists (80 076 B, `2.891,260827`) |
| Journal §1/§9 | PLL "÷1 ×10" | Works (120 MHz), but **V2.89 uses** HXTAL /3 ×10 /10 ×30. The clone uses Leo's recipe |
| Journal §10 "Board state" | Project 03 at `0x08000000` | Tester logs from 2026-09-25 show `LxBoxInfo 2.4,240606,2.621,240630` (factory image V2.64) and a `0.1,260101,…,GD32` build, so the board has been reflashed since. **Check with J-Link before any work** |
| V2.89 GPIO init | Drives **PD0** | PD0 = `WDI` to an unfitted external watchdog. Harmless; keep it for parity |

---

## 3. Specification method: how each module gets specified

The binary is the spec. For every function in V2.89 we follow the same loop:

1. **Locate** it in V2.89: ASSERT file paths (`..\src\app\app_lcr.c`), log strings and literal pools point to the functions.
2. **Read** the matching 2023 source if it exists, then **diff the behaviour** against the V2.89 disassembly. V2.89 wins.
3. **Write** the function spec into `spec/<module>.md`: inputs, outputs, reply strings, timing, and the V2.89 addresses.
4. **Implement** it and add a host unit test using the golden bytes (from the binary or Leo's logs).
5. **Verify** on the board by comparing our trace with Leo's trace (M11).

Tools: `arm-none-eabi-objdump` (installed). **Ghidra** would make this much faster (decompiled C, cross-references), but it needs a download (§9 D8). Leo's log line numbers (`app_lcr.c,2290`) map each log call to a source line, which helps line the 2023 source up with V2.89.

### 3.1 Module map: Leo's source tree → status → where the spec comes from

The file tree is taken from the ASSERT paths in the V2.89 binary.

| Leo file | Source we have | Specify from | Size of job |
|---|---|---|---|
| `main/main.c` (`main_init.c`) | ✅ 2023 | Source | S |
| `main/cli.c` | ❌ | V2.89 `0x08014788…0x08014F88` (tokens, `appinfo`, `update`, `factest esp8266`) | S |
| `hal/hal.c, gpio.c, uart.c, flash.c` (+ RTC, ADC, SPI, WDG, timer) | ❌ | V2.89 disassembly + hardware reference; Projects 02/03/04 drivers | M |
| `sys/sys.c` (DBG, TMR, EVT, MQ, reboot reason) | ❌ | V2.89 + the usage in the 2023 source (API names are known) | M |
| `drv/drv.c, led.c, gd25q.c` | ❌ | V2.89 + datasheet; reuse `Projects/04` `gd25q.c` | S–M |
| `app/app.c` (init order, `appInitTimeOut`, `appProcessSysMsg`) | ❌ | V2.89 (`---APP Init Start!---` `0x08009D68`, `…Ok!` `0x0800D8FF`) | S |
| `app/app_cfg.c` | ✅ 2023 | Source + V2.87/2.89 extra fields (BT PWD, HeatTime) | S |
| `app/app_ec20.c` | ◐ (RX parsers missing) | Source + V2.89 `0x08009200–0x0800FC03` | L |
| `app/app_lcr.c` | ◐ (2023, before V2.87/2.89 changes) | Source + V2.89 `0x08009BF0–0x080144F8` | **XL** (the core) |
| `app/app_bt.c` | ❌ | V2.89 `0x08008A9C…0x0800E39C` + Project 03 `bt.c` | L |
| `app/update/update.c` | ◐ | Source + V2.89 `0x0800BFDC…0x080186B8` | M |
| `app/update/xmodem.c` | ❌ | V2.89 `0x0800C898…0x08018B30` + the XMODEM-CRC standard | M |
| Bootloader (`updata/`) | ❌ | Leo's bootloader binary (dump + V2.64) | M, optional (§9 D3) |

---

## 4. Project structure (proposed)

A new, self-contained project that **mirrors Leo's source tree**. That way V2.89 addresses, Leo's ASSERT paths and log file names line up one to one with our files, and his `D,<ms>,app_lcr.c,<line>` traces can be diffed against ours.

```
Projects/05_PandaBox_FW_V289/
├── README.md                    build / flash / test in 10 lines
├── Makefile                     targets: app1, app2, bench, boot, factory, test, clean
├── config/
│   ├── version.h                VER string, build date (see §9 D4)
│   └── board.h                  pin map + GPIO init table (values copied from V2.89 0x08015990)
├── linker/
│   ├── app1.ld                  0x08008000, 98 KB (last page of the slot is never erased)
│   ├── app2.ld                  0x08021000, 98 KB
│   ├── bench.ld                 0x08000000, standalone (J-Link debugging only)
│   └── boot.ld                  0x08000000, 30 KB (only if we rebuild the bootloader)
├── startup/
│   └── startup_gd32f30x_cl.s
├── lib/                         CMSIS + GD32F30x headers (pointer to /GD32F30x_Firmware_Library)
├── src/                         == Leo's tree ==
│   ├── main/   main.c  cli.c
│   ├── hal/    hal.c  gpio.c  uart.c  flash.c  rtc.c  adc.c  spi.c  wdg.c  tick.c  system_gd32f30x.c
│   ├── sys/    sys.c  dbg.c  tmr.c  evt.c  mq.c
│   ├── drv/    drv.c  led.c  gd25q.c
│   ├── app/    app.c  app_cfg.c  app_bt.c  app_bt_table.c  app_ec20.c  app_lcr.c  app_lcr_cmd.c
│   │   └── update/  update.c  xmodem.c
│   └── util/   util.c  (hex, swap, IsValidIp, RemoveStrNewLine, STR2UINT32)
├── boot/                        optional bootloader (same hal/sys, only its own main + updata)
├── spec/                        one .md per module: V2.89 behaviour, addresses, golden bytes
│   ├── cmd_table.md             every App command → exact reply, match order, V2.89 address
│   ├── lcp_frames.md            every LCP frame the box sends/accepts (from Leo's logs)
│   ├── at_sequence.md           EC25 init / runtime / URC handling
│   ├── bt_protocol.md           YC1021 init, commands, frames, events
│   ├── storage.md               T_BOX_PARAM, APP_INFO, external record layout
│   └── timing.md                all timers and delays
├── tests/                       host-side (gcc/python), no board needed
│   ├── test_lcp.c               CRC/escape vectors (from lcp.c self-test + Leo's log frames)
│   ├── test_cmd_replies.py      feeds commands, compares with the golden reply table
│   ├── test_records.py          our writer vs decode_flash.py on the real dump
│   └── golden/                  replies, frames and traces captured from V2.89
└── tools/
    ├── mkappinfo.py             APP_INFO page + 32-bit byte sum for a built image
    ├── mkfactory.py             merge bootloader + APP_INFO + app1 → factory .bin
    ├── pc_update.py             PC side of Leo's UART upgrade (`update app1 <len> <sum>` + XMODEM-CRC)
    ├── trace_diff.py            our console log vs Leo's (ignores ms timestamps, compares frames/replies)
    ├── bin_compare.py           our .bin vs V2.89: string set, tables, vector
    ├── lcr_sim.py               PC-side LCR meter on a USB-RS232 adapter (reuse meter.c logic)
    └── jlink/                   flash_app1 / flash_bench / read_all / restore_backup scripts
```

**Why a new project folder rather than extending 03 or 04:** 03 is a simulator-backed proof of concept and 04 is Vishal's FreeRTOS/TCS design; neither is organised like Leo's code. We lift proven pieces out of them (table below) but keep one clean tree whose only goal is V2.89 parity.

### 4.1 Reuse from existing work

| Take | From | Into | Change needed |
|---|---|---|---|
| `bt_init_table.c` (105 records, 10 125 B) | 03 / 04 | `app/app_bt_table.c` | None; do not edit |
| LCP CRC, escaping, self-test vectors | 03 `lcp.c` | `app/app_lcr.c` helpers + `tests/test_lcp.c` | Rename to Leo's API names |
| YC1021 frame handling, `02 06` ack wait, events | 03 `bt.c` | `app/app_bt.c` | Match V2.89 exactly: address `11 25`, not our `E7` workaround (see §7 R1) |
| GD25Q256 driver (4-byte addressing) | 04 `gd25q.c`, `spi0.c` | `drv/gd25q.c`, `hal/spi.c` | Remove FreeRTOS calls |
| EC25 power-up timing lessons (PE2 first, settle, PWRKEY 600 ms) | 04 `ec25.c` | `app/app_ec20.c` | Follow Leo's sequence |
| Record decoder | 02 `decode_flash.py` | `tests/test_records.py` | None |
| LCR meter simulator | 03 `meter.c` | `tools/lcr_sim.py` (PC) | Port to Python on a real UART, so the box runs real UART code |
| Clock, pin map, safe GPIO state | 02/03 `board.c`, hardware reference | `hal/`, `config/board.h` | Use Leo's PLL recipe and GPIO table |

---

## 5. Architecture (matching V2.89)

```
main():  HAL_Init → SYS_Init → DRV_Init → APP_Init (3 s timer → module inits) → CLI_Init
while(1) { HAL_FeedWatchDog(); HAL_DoEvent(); EVT_DoEvent(); MQ_ProcessMsg(); TMR_ProcessTimeout(); }
```

- **Super-loop, no RTOS**, the same as Leo. UART RX is interrupt-driven into buffers; a packet is delivered to its callback after an idle gap (EC25 20 ms, LCR 50 ms, BT per V2.89). This matters for parity: replies, retries and poll timing come from these timers, and matching them is simpler without a scheduler in between (§9 D1).
- **Clock:** HXTAL 12 MHz /3 ×10 /10 ×30 = 120 MHz; AHB 120, APB2 120, APB1 60. The bootloader runs at 12 MHz and the app reconfigures.
- **App at `0x08008000`/`0x08021000`**, with VTOR set by the app's SystemInit, booted by Leo's bootloader.
- **Memory budget:** V2.89 is 80 KB of code; the slot holds 98 KB. We must stay under 98 KB with `-Os` (§7 R3).

---

## 6. Build, flash and debug

| Item | Choice |
|---|---|
| Compiler | Arm GNU Toolchain 14.2 rel1 (installed). Keil Lite is ruled out (32 KB limit) |
| Build | `make app1 app2 bench factory` → `.elf/.bin/.hex/.map` per target |
| Flash | J-Link (`GD32F305VC`, SWD 4 MHz). `bench` at `0x08000000` for day-to-day debug; `app1` behind Leo's bootloader for integration |
| Console | USART0 115200. COM9 is often held by the tester; then read the tester's `logs/serial_*.log` and use J-Link |
| Restore | `backup/original_flash_256K.bin` → `0x08000000` restores Leo's bootloader + V2.77 + config |

---

## 7. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | **BLE reconnect bug** (only the first connect per address works) is still open for our BT code. Leo's box reconnects fine | Run V2.89 on the bench (M0) and capture its YC1021 traffic, then copy it exactly. This is the strongest reason for M0 |
| R2 | Undocumented behaviour hidden in the binary (edge cases, error replies) | Spec every command from disassembly (§3), not only from the protocol doc; golden tests |
| R3 | Code size > 98 KB slot | `-Os`, no `printf` float, strip debug strings in release; watch the `.map` from M1 |
| R4 | Writing the bench board's external flash would destroy 6004 real records | Standing rule: read only. Records module is tested against a RAM/host model and a copy of the dump; the first write test needs an explicit OK (or a spare board / blank region) |
| R5 | No real LCR meter on the bench | `tools/lcr_sim.py` on a USB-RS232 adapter + the vendor LCR simulator in `C:\GD32\LCR Meter` (run, never modified); a real meter for final acceptance |
| R6 | Modem VGSM voltage still marked UNVERIFIED (EC25 max 4.3 V) | Leo's firmware has powered it for years and bring-up confirmed it answers; still measure once before 4G work |
| R7 | No SIM on the bench | Need a SIM + APN for M8 |
| R8 | V2.89 bugs that the App depends on | Keep the observable behaviour (§9 D5) |

---

## 8. Milestones

Each milestone ends with a check on the board. Rough sizes are for one developer.

| M | Deliverable | Done when | Size |
|---|---|---|---|
| **M0** | **Golden reference**: Ghidra/objdump project of V2.89; `spec/cmd_table.md` with every command and reply; run V2.89 on the bench (needs OK, §9 D6) and record console + BLE for every command in the tracker | A golden reply for each of the 40 tracker commands, plus a V2.89 boot/poll/BT trace | 3–4 d |
| M1 | Skeleton: Makefile, linkers (app1/app2/bench), startup, Leo's clock recipe, GPIO init table, watchdog | Leo's bootloader prints `Jump to APP` and boots our app1; clock registers match V2.89's | 1–2 d |
| M2 | SYS + CLI: DBG (`D,<ms>,file,line:` format), TMR, EVT, MQ, reboot reason, `appinfo`/`reboot`/`reason`/`debug` | Console output has the same shape as Leo's logs | 2 d |
| M3 | Storage: `T_BOX_PARAM` (116 B) + APP_INFO read/write, defaults as V2.89 (`118.89.111.211:80`, `TBOX_APP`, `123456789`, HeatTime 1000) | Reads the real config from the backup image correctly; survives reboot | 1 d |
| M4 | GD25Q256 + record store (`T_LCR_DATA_INFO`, 64 B records, ring, boot rescan, RTC catch-up) | Parses the bench board's 6004 + 4 records identically to `decode_flash.py` (read only) | 2–3 d |
| M5 | LCR engine: RS232/RS485 switching, LCP frames, BRIDGE/CMD/IDLE modes, 1 s poll, 120 ms timeout, record trigger | Our `lcr send/rev` trace = Leo's trace for the same meter replies | 4–5 d |
| M6 | App command layer: all ~45 commands, same match order and replies, streaming history (`GetData`, `GetDataTs`, `GetDataEcho`, "send done" pacing) | `test_cmd_replies.py` 100 % against the golden table | 4–5 d |
| M7 | Bluetooth (`app_bt.c`): init table, name/PIN/MAC, SPP + BLE framing, online/offline, RSSI, BT upgrade path | Tester connects, **reconnects repeatedly**, runs `happy_flow` PASS | 4–5 d |
| M8 | EC25: power-up, 15-command init, IMEI/IMSI/CSQ, TCP + `write(IMEI,…)`, heartbeat, reconnect, GPS RMC → BoxStatus/records, WiFi AP + `QDATAFWD` | Server receives heartbeats; phone on `TBOX_APP` port 5553 runs commands; GPS fix in `LxBoxStatus` | 5–6 d |
| M9 | Upgrade: `Update APP<n>,…` over 4G/BT/WiFi, XMODEM, byte-sum check, flag swap, `SetApp1/2`, `LxUpdate` | Upgrade V2.89 → ours → V2.89 over the air, both directions | 3–4 d |
| M10 | Bootloader (optional, §9 D3): same flash map, `update` UART protocol, 200 ms window | `pc_update.py` flashes app1/app2 through our bootloader | 3 d |
| **M11** | **Acceptance**: tracker (40 commands, Happy + Error), tester `happy_flow` → `_36hr` soak, `trace_diff.py` against V2.89, real LCR meter, field upgrade | Every contract C1–C10 signed off | 3–5 d |
| M12 | After parity: FleetPanda's new commands, TCS, BLE UUID in advertising | Separate branch | — |

The critical path is M0 → M5 → M6 → M7. M8/M9 can run in parallel once M2–M3 are in.

---

## 9. Decisions needed before scaffolding

| # | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Runtime | (a) super-loop like Leo; (b) FreeRTOS like `Projects/04` | **(a)**: parity in timing and ordering is the goal, and it's what V2.89 does |
| D2 | Peripheral library | (a) GD32 std-periph library V3.0.3; (b) register-level like `Projects/04` | **(a)**: Leo used it (same function names in the binary), so it is easier to match disassembly |
| D3 | Bootloader | (a) keep Leo's bootloader binary; (b) rebuild it too | **(a) first**, then (b) as M10 if we need our own factory image |
| D4 | Version reported in `LxBoxInfo` | (a) identical `2.4,250502,2.891,260827`; (b) same format, our own number (e.g. `2.901,<date>`) | **(b)**, so field boxes can be told apart; confirm that the App/cloud don't check the value |
| D5 | Leo's bugs (port-2 save, inverted Start/Stop address check, APN after QIACT, SSID comma) | (a) copy them; (b) fix them | Fix those invisible to the App/server; copy anything the App might depend on. Check each against V2.89 first (some are already fixed there) |
| D6 | Run V2.89 on the bench board for M0 | It may rewrite the external-flash header (the backup exists) | Needs your OK. Alternatives: a spare board, or only Leo's existing logs (weaker, and doesn't solve R1) |
| D7 | Git | New branch `feature/v289-rebuild` from `docs/pandabox-reference` (or from the current branch after its local changes are committed) | Your call; nothing is pushed until you say so |
| D8 | Ghidra (free, NSA, ~500 MB download) | Install / don't | Install; it saves days on M0–M9 |

---

## 10. Working rules (unchanged)

- `C:\GD32\pandabox-tester_v14\pandabox-tester` and `C:\GD32\LCR Meter`: **read only**.
- External SPI flash: **never erase or write**; read and document only.
- Document everything in the repo; **commit/push only when asked**, to a new branch.
- COM9 is often held by the tester app: don't kill it; use its logs and J-Link.
