# PandaBox (old board, GD32F305) Development Journal

This document is the full story of the work so far:
- every requirement that was given, in order;
- what was done for each, and which source (document, binary, schematic, tester code) each decision came from;
- what went wrong and how it was fixed;
- where things stand now, and what comes next.

It is written so that anyone joining (Vishal, Neranjen, or a future session) can pick up without the chat history.

| | |
|---|---|
| **Repository** | https://github.com/velmurugan-a11y/Old_PandaBox |
| **Owner / bench** | Velmurugan (velmurugan@fleetpanda.com), board on the bench with a SEGGER J-Link (SWD) and a CH340 USB-serial (COM9) |
| **Collaborators** | `vishalk-sketch` (vishal@fleetpanda.com), `nean-fp` (neranjen@fleetpanda.com), write access |
| **Period covered** | Up to 2026-09-24 |

---

## Contents

1. [Hardware and tools at a glance](#1-hardware-and-tools-at-a-glance)
2. [Timeline: requirements, actions, results](#2-timeline-requirements-actions-results)
3. [Where every decision came from (source map)](#3-where-every-decision-came-from-source-map)
4. [Firmware projects in this repo](#4-firmware-projects-in-this-repo)
5. [Bluetooth (YC1021): how the bring-up was reconstructed](#5-bluetooth-yc1021-how-the-bring-up-was-reconstructed)
6. [PandaBox command protocol as implemented (tester / app side)](#6-pandabox-command-protocol-as-implemented-tester--app-side)
7. [LCR side: LCP protocol and the meter simulator](#7-lcr-side-lcp-protocol-and-the-meter-simulator)
8. [BLE connection investigation (2026-09-24)](#8-ble-connection-investigation-2026-09-24)
9. [Problems hit and how they were fixed](#9-problems-hit-and-how-they-were-fixed)
10. [Board state right now](#10-board-state-right-now)
11. [Rules we are working under](#11-rules-we-are-working-under)
12. [What next: focus list](#12-what-next-focus-list)
13. [Git history and branches](#13-git-history-and-branches)

---

## 1. Hardware and tools at a glance

Full detail is in `docs/PandaBox_Master_Note.md`, `docs/reference/01_Hardware_Reference.md` and `docs/PandaBox_Peripheral_Bringup.md`. The short version:

| Item | Value |
|---|---|
| MCU | GD32F305VCT6, LQFP100, 256 KB flash, 96 KB RAM |
| Clock | 12 MHz HXTAL (not the 8 MHz the GD library assumes) → PLL ÷1 ×10 = **120 MHz**; build flag `-DHXTAL_VALUE=12000000U`, patched `system_gd32f30x.c` |
| Console | USART0 **remapped to PB6/PB7**, CH340 on **COM9**, 115200 8N1 |
| LCR port 1 | USART1 PA2/PA3 (RS232/RS485 transceivers) |
| LCR port 2 | USART2 full remap PD8/PD9 |
| Bluetooth | Yichip **YC1021** on UART3 PC10/PC11, 115200. PD4 = reset (active low), PD5 = enable |
| WiFi | Enable PD6 (not used yet) |
| 4G | Quectel **EC25** (EC25AFAR05A07M4G, IMEI 860858062040217) on UART4 PC12/PD2. **PE2 = modem power**, PB15 = PWRKEY, PA10 = DTR |
| Transceiver power | PE5 = RS232 supply, PE6 = RS485 supply. PE3/PE4 = RS485 direction (high = receive) |
| External flash | GD25Q256E (32 MB) on SPI0 PA4–PA7, JEDEC ID `C8 40 19` |
| LEDs | PB1, PB0, PC4, PC5, PC6, PC7, active high |
| Leo's flash map | Bootloader `0x08000000`, APP_INFO `0x08007800`, APP1 `0x08008000`, APP2 `0x08021000`, config `0x0803F800` |

| Tool | Version | Used for |
|---|---|---|
| Arm GNU Toolchain | 14.2 rel1 | All builds (`build.sh` per project) |
| SEGGER J-Link | installed | Flash + verify (`flash.sh`), live RAM/register reads without halting |
| Keil MDK | 5.43 (Lite) | Installed; not used (32 KB code limit, and our 03 image is 36 KB) |
| Python | 3.14 + pyserial + bleak | Console driver `bringup.py`, BLE smoke test |
| PandaBox tester | `C:\GD32\pandabox-tester_v14\pandabox-tester` | FleetPanda's BLE test app. **Read only: never modified** |

---

## 2. Timeline: requirements, actions, results

Each step gives the requirement as the user gave it (lightly cleaned), then what was done and the result.

### Step 1: create the repository
> "Create one new repo with the name of Old_PandaBox as a main repo and we will develop code on it" / "try push now only gd32f305 related files"

- Created `velmurugan-a11y/Old_PandaBox` with `main` as the main branch.
- First commit `939737b` added only the GD32F30x material: the GD32F30x firmware library and the GD32305R-START demo suites (V3.0.3). Unrelated files on the PC were left out, as asked.

### Step 2: 6-LED sequence test, verified with J-Link
> "Write a 6 LEDs serial toggle code and verify the code working properly by yourself, I have connected J-Link Segger"

- **Source:** the schematic images the user sent (LED nets on PB1/PB0/PC4/PC5/PC6/PC7, active high).
- **Keil vs GCC:** Keil wasn't usable from the command line with its Lite limit, so the user chose **ARM GCC** (asked and answered: "Install ARM GCC (Recommended)").
- **Result:** `Projects/01_LED_Sequence` blinks the LEDs one after another. It was built with GCC and flashed with J-Link. Correct execution was checked through J-Link register and memory reads.

### Step 3: collect Leo's files
> "Get the needful data from `G:\My Drive\FleetPanda\PandaBox V1 & V2 Doc` … paste it in `C:\GD32\Old_PandaBox\From Leo`"

- Leo is the previous firmware developer. His Drive folder was sorted, and the useful files were copied into `From Leo/`:
  - `01_Hardware`: schematic, PCB, BOM;
  - `02_Datasheets`;
  - `03_Protocols_and_Specs`;
  - `04_Reference_Source`: Leo's source (a partial snapshot);
  - `05_Firmware_Binaries`: bootloader V2.64, apps up to X-Box V2.87 / V2.89;
  - `06_LCR_Meter_Manuals`.
- Duplicates and personal files were skipped.

### Step 4: check the tools and write the master note
> "check the relevant software installed … check the entire project flow Leo created, the bin file flow, PCB schematic pin outs … create one complete note of hardware and firmware"

- Installed tools were inventoried (table in §1).
- Every document in `From Leo/` was read, the PCB netlist was exported (`docs/reference/pcb_netlist.txt`), and the MCU pin map was built from the netlist and cross-checked against the schematic.
- Leo's binaries were analysed: vector tables, strings, literal pools, flash map, bootloader and app handover.
- **Output:**
  - `docs/PandaBox_Master_Note.md`: the one-stop note;
  - `docs/reference/01_Hardware_Reference.md`: pin map, connectors, power tree, BOM, open questions;
  - `docs/reference/02_Legacy_Firmware_Reference.md`: Leo's architecture, modules, flash map, binary analysis, gaps;
  - `docs/reference/03_Protocol_Reference.md`: box ↔ app protocol Rev 1.86, FleetPanda command additions, LCR/LCP, TCS 3000.

### Step 5: docs branch and collaborator access
> "create one more branch and push the necessary docs … add vishal@fleetpanda.com, neranjen@fleetpanda.com with full edit access"

- Branch `docs/pandabox-reference`, commit `3fb6304`: reference docs, Leo's handover files and the LED project.
- Access: the user shared a screenshot of the GitHub users (`vishalk-sketch`, `nean-fp`). Both were invited with write access, and the docs were pushed ("push the docs yourself").

### Step 6: peripheral list and handshake plan
> "list out all the peripherals connected with the MCU and pin numbers, protocols … next we need to create a handshake code build for all (Flash IC, BLE, WiFi, module, RS232 transceiver, etc.)"

- **Output:** `docs/PandaBox_Peripheral_Bringup.md`. It covers:
  - every peripheral with its pins, protocol and a safe power-on state;
  - one handshake test per device;
  - a protocol cheat sheet;
  - what the bench needed.
- The user answered the bench questions:
  1. COM9 CH340 is on PB6/PB7 TTL;
  2. toggle PE2 and check the modem in both states;
  3. no LCR meter yet, just get a transceiver acknowledgement;
  4. **"Don't erase the memory; read and document the data"**;
  5. document everything locally and push only when told.

### Step 7: hardware bring-up firmware, session 1
> "push everything into one new branch then let's start"

- **`Projects/02_HW_Bringup`:** a console firmware with one command per peripheral:
  - `info`, `led`, `adc`, `inputs`, `rtc`;
  - `rs232`, `rs485`, `rsdiag`, `rsloop`, `rsack`;
  - `flash id|rd|map`;
  - `gsm test|on|off|pe2`, `at`;
  - `bt …`, `reboot`.
- **`tools/bringup.py`** drives COM9. It logs every exchange to `logs/session_*.log` and `logs/results.csv`.
- **Results:**
  - clock is 120 MHz;
  - console works;
  - SPI flash ID is `C8 40 19`;
  - **PE2 is confirmed as the modem power switch**: with PE2 low there is no modem answer, with PE2 high it answers `RDY`, AT, IMEI and firmware;
  - there is no SIM.
- **External flash was read, never written.**
  - Port 1: 6004 old delivery records (2024-09-27 → 2024-10-04, plus 2025-11-18).
  - Port 2: 4 records.
  - Dumped to `Projects/02_HW_Bringup/logs/extflash_*.bin` and decoded by `tools/decode_flash.py` into `extflash_records.csv`.
- **Pushed:** branch `bringup/hw-handshake`, commits `436d1fb` and `2739730` (the flash dumps needed a force-add because `*.bin` is ignored).

### Step 8: session 2, Leo's V2.89, Bluetooth, transceivers, full self-test
> "check X-Box_V2.89 and Bootload_APP_V2.64 · I'm not getting anything in the nRF app · acknowledge the serial transceivers are working · every peripheral status + handshake · focus on BLE"

- **V2.89:**
  - disassembled;
  - GPIO init confirms the pin map (defaults PE5 = 1, PE6 = 0);
  - its BT start-up sequence was reconstructed (§5).
- **BLE:** the bring-up firmware now uploads the 105-record table (all acknowledged) and runs 7/7 configuration commands (all acknowledged).
- **Transceivers:**
  - RX lines sit at 3.3 V in every PE5/PE6/direction state (`rsdiag`, `rsloop`, `rsack`), and back-powering was ruled out;
  - `rsack` gave a misleading PASS at first; it was fixed to require the line to go low first;
  - **still open:** needs a DB25 pin 14↔15 loopback wire or a multimeter on the transceiver supplies.
- **Output:** `tools/selftest.py` → `logs/status_2026-09-24_0154.md`, plus session 2 in `docs/Bringup_Log.md`.

### Step 9: make the PandaBox answer the tester like an LCR box
> "1. go through the tester code completely, don't change anything in it · 2. go through the LCR Meter documents (RS232, commands) · 3. I can connect the PandaBox BLE with the tester app · 4. whenever I send commands through the tester app, this PandaBox should respond like with the LCR meter; assume port 1 = meter 1, port 2 = meter 2 · 5. go through all the commands of the tester app and the LCR meter datasheet and write the code on the PandaBox"

- **Tester read in full, not modified:**
  - `app.py`, `ble_engine.py`, `runner.py`, `validator.py`, `sequence_parser.py`, `sequences/`, `report_generator.py`, `serial_monitor.py`;
  - its validation rules are listed in §6.
- **LCR material read:**
  - `C:\GD32\LCR Meter` (LCR-II manuals, LCP02 message spec);
  - the LCP chapter of `03_Protocol_Reference.md`.
- **New firmware:** `Projects/03_PandaBox_BLE_LCR`, described in §4, §6 and §7.
- **Result:** boot is clean:
  - LCP self-test ok;
  - both simulated meters answer Product ID over LCP;
  - BT table 105/105, then `02 09 00`;
  - advertising as "PandaBrain" / "PandaBrainBLE".

### Step 10: push, journal, next focus, BLE is slow to connect (this step)
> "1. push the current code · 2. new branch with a note of whatever we tried, the requirements, the actions, where the points came from … the full chat flow · 3. what next · 4. one big long note pushed with the code · 5. It takes more time to connect BLE"
> (the tester showed: `Device 25:11:33:32:09:7B not found in scan`)

- **BLE investigated and partly fixed:** see §8.
- **This journal written; project 03 README written.**
- **Everything committed and pushed** to the new branch `feature/ble-lcr-emulator` (§13).

---

## 3. Where every decision came from (source map)

| Decision / fact | Source |
|---|---|
| Pin map (all peripherals) | PCB netlist export (`docs/reference/pcb_netlist.txt`), schematic `XBOX_V2.5 Hardware Schematic Diagram`, confirmed by V2.89 GPIO init |
| 12 MHz crystal | Schematic (Y1) + BOM; confirmed by UART baud being correct only with `HXTAL_VALUE=12000000` |
| Console on PB6/PB7 | User (CH340 wiring) + USART0 remap in Leo's code |
| PE2 = modem power | Bench test in both states (session 1) |
| Flash map | Leo's bootloader / app binaries (vector tables, APP_INFO parsing), `02_Legacy_Firmware_Reference.md` §3 |
| External flash record format | Leo's source `04_Reference_Source` (history storage) + decoding the dump |
| BT init table (105 records) | X-Box V2.89 binary @ `0x08018B9E` (V2.87 @ `0x08018C94`), extracted by `extract_bt_table.py` |
| BT command sequence and parameters (name, PIN 1234, MAC from UID, visibility 07) | V2.89 command table @ `0x0801B32B`, BT start function @ `0x0800DD34`, strings (`PandaBrain`, `BT_MAC=…`) |
| BLE data frame formats (`02 08 … 2D 00`, `01 09 … 2A 00`) | Leo's source (`app_bt.c`) + Leo's own debug logs in the tester logs folder (`serial_Panda1BLE_20260921_*.log`) |
| GATT UUIDs | Tester `ble_engine.py` (service `49535343-FE7D-…`, notify `…1E4D…`, write `…8841…`) |
| Command names and reply formats | Protocol Rev 1.86 (`03_Protocol_Reference.md` §2), FleetPanda additions (§3), Leo's printf strings in V2.89, Leo's logs (e.g. `LxBoxStatus 0,0,0,0,0.0,S,0.0,E,0,1`) |
| What the tester accepts as PASS | Tester `validator.py` and `runner.py` (rules in §6) |
| LCP framing, CRC, messages, field numbers, command codes, status bits | LCP02 message spec + LCR-II manuals (`C:\GD32\LCR Meter`, `From Leo/06_LCR_Meter_Manuals`), `03_Protocol_Reference.md` §4 |
| Poll fields #2, #4, #17, #18, #100, #101, 1 s poll | Leo's `app_lcr.c` (poll list and period) |
| Port 1 = meter 1 (node 1), port 2 = meter 2 (node 2) | User requirement (step 9) |
| BLE address MSB change (0x25 → 0xE5/E6/E7) | Bench experiment, §8 |

---

## 4. Firmware projects in this repo

| Project | Purpose | Status |
|---|---|---|
| `Projects/01_LED_Sequence` | 6-LED running light; first toolchain + J-Link check | Done |
| `Projects/02_HW_Bringup` | Console firmware with one test per peripheral + Python driver, logs, flash decoder, BT table extractor, self-test | Done (sessions 1 and 2); transceivers open |
| `Projects/03_PandaBox_BLE_LCR` | **Application:** BLE ↔ PandaBox protocol ↔ two LCR meters (simulated, real LCP frames) | Working; reconnect issue (§8) |

**Project 03 structure:**

```
main.c      time base, console, IMEI task, main loop (bt_poll → proto_handle, meter tick 100 ms,
            LCR poll 1 s, LCP LEDs, bt_service, modem_task, proto_tick)
bt.c        YC1021: init table upload with HCI Command Complete checks, Yichip commands with
            acks and retries, event parser, line assembly, chunked BLE notify, re-advertise, self-heal
proto.c     every PandaBox command (see §6)
lcr_host.c  two ports; LCP transactions; 1 s poll; history records (400 per port, RAM only)
lcp.c       LCP build/parse + self-test vectors
meter.c     LCR-II simulator (answers LCP)
```

Main loop timing: nothing blocks for long, except a BLE reply (≤ 200 ms per 125-byte chunk while waiting for the module's "sent" ack) and the rare BT self-heal restart (~1.5 s).

---

## 5. Bluetooth (YC1021): how the bring-up was reconstructed

There is no YC1021 datasheet in Leo's files, so the whole sequence was **read out of X-Box V2.89** and checked on the bench step by step.

1. **Hardware reset:**
   - PD5 (enable) high;
   - PD4 low for 20 ms, then high;
   - wait 100 ms;
   - UART3 at 115200.
2. **Patch and configuration upload:**
   - The table sits at `0x08018B9E` in V2.89. It starts with a 2-byte length, followed by 105 records `[n][01 op_lo op_hi len payload…]`:
     - 34 × `FC03` (patch);
     - 70 × `FC10` (memory writes, including the GATT database);
     - 1 × `FC04` (start).
   - Each record is answered with an HCI Command Complete, `04 0E 04 01 <op> 00`.
   - After `FC04` the module runs the patched firmware, sends `02 09 00` (ready), and is busy for about 2 s (`02 0F 00`), so the next commands need retries.
3. **Yichip commands** `01 <cmd> <len> <data>`, each acknowledged with `02 06 02 <cmd> <status>`:

   | Cmd | Meaning | Value used |
   |---|---|---|
   | `03` | BT (classic) name | `PandaBrain` |
   | `04` | BLE name | name + `BLE` → `PandaBrainBLE` |
   | `0C` | Pairing mode | `00` |
   | `0D` | PIN | `1234` |
   | `00` | BT address | UID word0 + 4 (LE), then `11 25` |
   | `01` | BLE address | UID word0 + 5 (LE), then `11 E7`. Leo used `11 25`, see §8 |
   | `02` | Visibility | `07` (BT discoverable + connectable + BLE advertising) |

4. **Data frames:**
   - **Phone → box (BLE write to handle 0x2D):** `02 08 <len> 2D 00 <data>`. Commands can arrive split across several frames, so the firmware assembles lines on `\n`.
   - **Box → phone (notify handle 0x2A):** `01 09 <n+2> 2A 00 <data>`, at most 125 data bytes per frame. The module confirms each frame with `02 06 02 09 00` before the next one is sent.
   - **SPP (classic):** `01 05 <len> <data>` out, `02 07 <len> <data>` in.
5. **Other events seen:**
   - `02 02 <status>`: module status. It appeared once as `02 02 00`.
   - `02 05 00`: link closed. It appears when the central disconnects, and the module then resumes advertising by itself.

---

## 6. PandaBox command protocol as implemented (tester / app side)

**How the tester talks** (from `ble_engine.py`, `runner.py`, `validator.py`):
- It writes `Cmd a,b,\r\n`, with the trailing comma, to the write characteristic.
- It splits replies on `\r\n`. A reply is complete after 500 ms of silence.
- **Hidden traffic:**
  - `BoxStatus` keepalive every 3 s;
  - `Stop 1` and `Stop 2` before each sequence run;
  - a "drain" `Stop <node>` before every `GetData`.
- **Validation rules:**

  | Command | PASS needs |
  |---|---|
  | BoxStatus | field 0 is 1 or 2, field 2 ≠ 0 |
  | GetData | all fields ≥ 0, meter number matches, timestamp between 2020 and now + 30 days |
  | GetLastMtrCmd | 3rd field is 0 |
  | HisDataTime (empty) | fewer than 3 fields |
  | BoxStorage | remaining > 0 |
  | ACK-type commands | anything except result `1` |
  | Stop | a reply (no reply = FAIL) |

  The PDF report's overall PASS also needs flow > 0 in at least one GetData per meter.
- **Tester quirk:** a reply made only of `LxGetDataTs` lines is filtered out and counted as no reply.

**Implemented replies** (`src/proto.c`). A leading `Lx` in a command is ignored and matching is case-insensitive. Unknown commands get no reply, which is what Leo's firmware does.

| Command | Reply |
|---|---|
| `BoxStatus` | `LxBoxStatus <mode>,0,<node1>,<node2>,0.0,S,0.0,E,0,1` |
| `BoxInfo` | `LxBoxInfo 2.4,250502,3.001,<build YYMMDD>,<IMEI>,LCR` (IMEI read from the EC25 ~27 s after boot) |
| `BoxTime` / `SetBoxTime` | `LxBoxTime <epoch>` / `LxSetBoxTime 0` |
| `BoxReset` | `LxBoxReset 0`, reset after 500 ms |
| `SetMode`, `SetRs485`, `SetHeatTime`, `SetBtPwd`, `SetWifi*`, `SetServer*`, `SetApn` | `Lx<Cmd> 0` |
| `SetApp1/2` | `LxSetApp<n>,1` |
| `RdDiagnostics` | `LxRdDiagnostics n,0,0,0` |
| `SetDbg` | no reply (as Leo) |
| `RdBtName` / `SetBtName` | `LxRdBtName <name>` / `LxSetBtName 0`, then the module is renamed after 300 ms |
| `SetPortLcrNode p,n` / `RdPortLcrNode` | `LxSetPortLcrNode 0` / `LxRdPortLcrNode 1,2` |
| `RdRegister` | `LxRdRegister <online1>,<online2>` |
| `GetLcrNode p,from,` | 28h scan → `LxFindLcrNode p,node,` or `LxFindLcrNode 0` |
| `ModifyLcrNode` | 25h → `LxModifyLcrNode 0/1` |
| `SwitchState p` | `LxSwitchState p,Run` |
| `Start/Resume/Pause/Stop/Print <node>` | 24h command 0/0/1/2/6 → `Lx<Cmd> 0` if the meter answered, else `1` |
| `PresetGross/PresetNet <port>,<gal>` | 21h field #5 (gross) / net → `Lx<Cmd> 0/1` |
| `GetLastMtrCmd` / `GetLastCmd` | `LxGetLastMtrCmd n,<None/Start/Stop/Pause/Print>,0` |
| `GetData n,0` | `LxGetData n,1,<serial>,<ts>,<gross>,<flow>,<#17>,<#18>,<#100>,0.0,0.0,S,0.0,E` (1 decimal, clamped ≥ 0). In mode 1: `LxGetData Mode 1,` |
| `GetData n,1` / `GetDataEcho` | Oldest history record as `LxGetDataTs n,0,…`; an acknowledged record is deleted; end marker `LxGetDataTs n,` |
| `GetDataTs` | All records `LxGetDataTs n,seq,ts,…`, then `LxGetDataTs n,` |
| `HisDataTime n` | `LxHisDataTime n,<first>,<last>,` or `LxHisDataTime n,` when empty |
| `BoxStorage n` | `LxBoxStorage n,<count>,<(400-count)*64>,` |
| `DeleteAll` | `LxDeleteAll 0` |
| `DirectDelivery` | `LxStop 1` |

---

## 7. LCR side: LCP protocol and the meter simulator

**LCP frame:** `7E 7E <to> <from> <status> <len> <data…> <crcLo> <crcHi>`
- **Escaping:** `7E` and `1B` inside the frame are sent as `1B xx`.
- **CRC-16:** poly 0x1021, seed 0x7E7E, shift-in. It covers the inserted escapes but not escapes in front of the CRC bytes.
- **Nodes:** the host (box) is node `0x14`. Meter nodes are 1 and 2.
- **Values:** big-endian, in tenths.
- **Messages used:** `00` (product ID), `20` / `21` (read / write field), `23`, `24` (delivery command: 0 start/resume, 1 pause, 2 end, 6 print), `25` (address), `26`, `27`, `28` (node scan), `40`, `41`.
- **devStatus:** switch bits 0–2, plus the state (`0x00` run, `0x10` stop, `0x20` idle, `0x60` waiting for no flow), plus delCode / delStatus bits.
- **Self-test:** `lcp_selftest()` checks known frames at every boot. Examples: `7E 7E 01 14 02 01 00 C4 EB`, `7E 7E 02 14 01 02 20 02 AB 56`, and one escaped frame ending in `20 1B 1B FA 66`.

**Meter simulator** (`meter.c`, one per port):

| Aspect | Meter 1 | Meter 2 |
|---|---|---|
| Product | "SR200b2.05" | "SR200b2.05" |
| Gross total at start | 12345.6 gal | 49282.7 gal |
| Pump rate | 60.0 gal/min | 45.0 gal/min |

- **Fields:** 2, 3, 4, 5, 6, 17, 18, 22, 23, 25, 27, 37, 38, 39, 44, 45, 92, 100, 101, 102.
- **Flow ramp:** time constant 1.5 s when opening and 0.7 s when closing, with ±2 % jitter.
- **Rules:** the delivery ends when the preset is reached; a no-flow timeout ends it after 180 s; a ticket is never required.
- **24h return codes:** 121 / 120 / 35 / 36 / 37 / 117, as in the LCP02 spec.
- **Pump rate is adjustable** from the console: `flow <port> <tenths>`.

The box polls each port once a second over LCP (fields 2, 4, 17, 18, 100, 101), just like Leo's `app_lcr.c`. It adds a history record whenever a value changes.

**Moving to real meters:** replace `lcr_transport()` with a UART write/read on USART1/USART2 (19200 baud per Leo's code) with the right transceiver direction. Nothing else changes.

---

## 8. BLE connection investigation (2026-09-24)

**Symptom** (the user's tester):

```
Step 2/3 — Scanning to resolve device object…
Device 25:11:33:32:09:7B not found in scan. Ensure it is advertising and within range.
```

It was also generally slow to connect.

**What was found, in order:**

1. **The module stopped advertising after a link attempt.**
   - The console showed `BT event 02 02 00` about 150 s after boot, after which the box was invisible.
   - Fix: `bt_service()` re-applies visibility (`01 02 01 07`) when a `02 02` status says "not linked and not advertising".
2. **Slow first scan in the tester** (`ble_engine.py`, not changed):
   - The tester first runs `find_device_by_address(..., service_uuids=[BOX_SERVICE_UUID])` for 12 s, then a plain `discover()` for 12 s.
   - Our advertisement carries **no service UUID**, so the filtered 12 s scan can never succeed. **Every connect costs ≥ 12 s extra.**
   - Fix (future): add the 128-bit service UUID to the advertising data. This needs the YC1021 command for advertising data, which isn't known yet (not in Leo's command table).
3. **Connect timed out even while advertising.**
   - A PC-side test (`tools/ble_smoke.py`, bleak) found the box in 2–15 s, but the connect timed out after 10 / 30 / 40 s.
   - Checks made:
     - MCU alive (J-Link RAM read of `ms_tick`);
     - UART3 registers healthy (STAT `0xD0`, baud 115200, RX interrupt on);
     - no module event at all during the attempt.
   - After a failed attempt the box is invisible. Windows keeps a half-open link-layer connection, so the module stops advertising.
4. **BLE address class.**
   - Leo's address ends `…11 25`, so the address reads `25:11:…`. Its top bits `00` mark a *non-resolvable private* address.
   - With `E5:11:…` (top bits `11`, *random static*), the **first connect worked**:

     ```
     found 5.1s E5:11:33:32:09:7B
     connected+services after 2.3s
     'BoxStatus'     -> 'LxBoxStatus 2,0,1,2,0.0,S,0.0,E,0,1'
     'BoxInfo'       -> 'LxBoxInfo 2.4,250502,3.001,260924,000000000000000,LCR'
     'RdPortLcrNode' -> 'LxRdPortLcrNode 1,2'
     'Start 1,'      -> 'LxStart 0'
     'GetData 1,0,'  -> 'LxGetData 1,1,16,1790218465,0.1,20.0,12345.7,0.0,12345.6,0.0,0.0,S,0.0,E'
     'Stop 1,'       -> 'LxStop 0'
     re-advertising after disconnect: True (3.6s)
     ```

5. **Only the first connect per address works.** These were tried and ruled out:
   - **A 10 s visibility heartbeat.** It was added, then **removed**: the module gives no event while a central sets up a link, so a command in that window is risky.
   - **The modem power cycle.** The IMEI task used to switch the modem off again; it now leaves it on, as Leo does. No change.
   - **The address.** A fresh address `E6:…` connected first time (2.4 s), and the next connect to `E6` failed.
   - **A Windows bond or device entry.** There isn't one.
6. **Current conclusion:**
   - The **first** connection from this PC to an address works fully.
   - **Reconnecting** from the same Windows PC fails, even after a board reset. So something Windows keeps from the first link doesn't match what the module does on the second.
   - Leo's box ("Panda1BLE", same YC1021 protocol) reconnected fine from this PC on 2026-09-21, so the difference is in our module configuration or address handling. It is not the tester.
7. **Workaround in place:** BLE address MSB `E7`, never used, so the tester's next connect works for one session. The tester must **scan again** because the address changed.
8. **Self-heal:** if a re-advertise command isn't acknowledged twice, the module is probed with `HCI_Reset` (the log shows whether it had rebooted itself) and fully re-initialised.

**Next experiments** (in order):
- (a) Connect twice from a phone (nRF Connect) to see whether this is Windows-only.
- (b) Run Leo's V2.89 on this board and connect twice from the PC. This needs an OK because V2.89 may write the history header in the external flash; the backup exists.
- (c) Capture the module traffic during a failed reconnect with `trace bt 1`.
- (d) Find the YC1021 command for advertising data and put the service UUID in, which removes the 12 s tester delay.

---

## 9. Problems hit and how they were fixed

| Problem | Cause | Fix |
|---|---|---|
| Wrong baud / garbage on the console at first | GD library assumes 8 MHz HXTAL; the board has 12 MHz | `-DHXTAL_VALUE=12000000U` plus the PLL patched to ÷1 ×10 in `system_gd32f30x.c`. The first patch hit the prototype of `system_clock_8m_irc8m`; redone on the function body |
| Prompt `> ` appearing late | newlib buffers stdout | `setvbuf(stdout, NULL, _IONBF, 0)` |
| Build broke after scripted edits (several times, again in this session) | A Python heredoc turned `\n` inside C strings into real newlines; once the old binary got flashed | Edit with the editor tool, or write `chr(92)+'n'`. Always check the build output before trusting a flash |
| `bt raw` cut at 8 bytes | Argument limit | Raised to 40, with ack wait and retries |
| `bt listen` cut short | Script timeout shorter than the listen time | Timeout follows the listen duration |
| Flash dumps missing from git | `*.bin` in `.gitignore` | Force-added in a separate commit |
| First push blocked | Auto-mode safety check | Pushed after the user's explicit "push the docs yourself" |
| `rsack` said PASS when it wasn't | Line already high, so "went high" proved nothing | Now requires seeing the line low first |
| Flash dump was 16 MB | Sparse single file | One file per used range |
| COM9 "Access is denied" | The tester app (`app.py`) keeps COM9 open for its serial monitor | Not killed. The box console is read from the tester's own serial log (`logs/serial_PandaBrainBLE_*.log`, read only), and J-Link is used for everything else |
| Box invisible after a link attempt; tester can't find it | See §8 | Re-advertise on status, random-static address, self-heal; reconnect still open |

---

## 10. Board state right now

- **Internal flash:**
  - `Projects/03_PandaBox_BLE_LCR` at `0x08000000` (43 KB image).
  - This overwrote Leo's bootloader, **APP_INFO (0x08007800) and the start of APP1**. Leo's image is **not bootable** as-is.
  - Everything is restorable from `backup/original_flash_256K.bin` (full 256 KB read before any change).
- **External SPI flash:** never erased or written. The contents are documented in `Projects/02_HW_Bringup/logs/extflash_*`.
- **Modem:** powered after boot (PE2 = 1) and IMEI read. There is no SIM.
- **BT:**
  - "PandaBrain" / "PandaBrainBLE";
  - classic address `25:11:…`;
  - BLE address `E7:11:33:32:09:7B` (not yet connected from the PC);
  - PIN 1234.
- **COM9:** held by the tester app's serial monitor.

---

## 11. Rules we are working under

1. **Never change anything** in `C:\GD32\pandabox-tester_v14\pandabox-tester` or `C:\GD32\LCR Meter`: read only.
2. **Never erase or write the external SPI flash.** Read and document only.
3. **Document every process and all data** in the repo folder (logs, CSVs, dumps, notes).
4. **Commit and push only when asked**, to a new branch.
5. Don't kill the user's processes (e.g. the tester holding COM9); work around them.

---

## 12. What next: focus list

**Now (BLE usable with the tester):**
1. Solve the reconnect issue (§8, experiments a–d). Until then, after flashing, the tester connects once per new address.
2. Put the service UUID into the advertising data to remove the tester's 12 s filtered-scan delay.
3. Run the tester sequences (e.g. `happy_flow`) end to end; check every step against `validator.py`, and that the PDF verdict is PASS (flow > 0 in GetData per meter).
   - Known gap: a GetData history reply made only of `LxGetDataTs` lines is filtered out by the tester (tester bug; tester not to be changed). Decide with the team whether the box should add a summary line.
4. When COM9 is free: use the console `inject` to test every command offline and record the results.

**Next (real hardware instead of simulation):**
5. Transceiver acknowledgement: DB25 pin 14↔15 loopback wire (or a multimeter on the RS232/RS485 supplies), then `rs232 1` / `rsdiag`.
6. Swap `lcr_transport()` for real UART LCP on USART1/USART2 (19200), keeping the simulator as a fallback (`sim` mode), then test with a real LCR-II.
7. Persist history in the external flash. First agree the format (Leo's is documented), and never touch the existing records without an OK.

**Later (Leo feature parity):**
8. 4G: SIM + APN, TCP to the FleetPanda server, `Heart,…` messages, GNSS (`AT+QGPS`).
9. WiFi (PD6 path), OTA update (Leo's bootloader protocol: APP1/APP2, APP_INFO), config page.
10. RTC with a coin cell (CR1220), the 12 V input, ADC scaling.
11. Put a bootloader back (ours or Leo's V2.64) and move the app to `0x08008000` so field updates work.

---

## 13. Git history and branches

| Branch | Content |
|---|---|
| `main` | `939737b` GD32F30x firmware library + GD32305R-START demo suites (V3.0.3) |
| `docs/pandabox-reference` | `3fb6304` reference docs, Leo's handover files, 6-LED project |
| `bringup/hw-handshake` | `436d1fb` bring-up firmware, peripheral plan, session 1 results · `2739730` external flash dumps |
| `feature/ble-lcr-emulator` | This step: session 2 bring-up work (BT table extractor, self-test, logs), `Projects/03_PandaBox_BLE_LCR`, project README, this journal |

Build any project with `cd Projects/<name> && ./build.sh && ./flash.sh`. BLE check: `python Projects/03_PandaBox_BLE_LCR/tools/ble_smoke.py`. Note that this uses up the first connect of the current address (§8).
