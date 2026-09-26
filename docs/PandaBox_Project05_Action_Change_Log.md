# PandaBox Project 05: complete action and change log

This log covers the V2.89-compatible firmware rebuild (`Projects/05_PandaBox_FW_V289`) and the bench validation work, in order. Each entry lists what was done, what changed, and the result.
- Detailed notes: [Activity_Notes_2026-09-25](chat_logs/Activity_Notes_2026-09-25_Project05_BLE_LCR_Loop.md) and [Activity_Notes_2026-09-26](chat_logs/Activity_Notes_2026-09-26_Tracker_Round_Cycles.md).
- Per-case test results: [PandaBox_Command_Test_Results_V2901.xlsx](PandaBox_Command_Test_Results_V2901.xlsx).
- Raw test runs: `Projects/05_PandaBox_FW_V289/test_results/`.

**Bench**

| Link | Port / tool |
|---|---|
| pandabox-tester (BLE commands) | Flask :5001; code never modified |
| PandaBox Port 2 (J2, USART2) → USB-RS232 → LCR simulator | COM7; simulator on Flask :5000 |
| PandaBox Port 1 (J1, USART1) | nothing connected |
| Box debug console | CH340, COM10 |
| Flash and debug | SEGGER J-Link |

---

## 2026-09-25: Project 05 bring-up and first BLE ↔ LCR loop
| # | Action | Change | Result |
|---|---|---|---|
| 1 | Scaffolded Project 05 from the V2.89 disassembly | `src/` HAL/SYS/DRV/APP tree, `build.sh`, `flash.sh`, `re/`, `spec/` | builds app1, app2, bench |
| 2 | Flashed `bench` at 0x08000000 | — | 120 MHz, super-loop, LEDs, config verified over J-Link |
| 3 | Real LCP over USART2 instead of the internal meter simulator | `lcr_host.c` lcr_transport(), `app_lcr.c` UART bridge | LCP CRC matches Leo's meter (54 74 / AB 56) |
| 4 | Fixed the BLE reconnect problem | `app_bt.c`: random-static BLE address from the RTC; mark the link online on BLE/SPP RX | tester connects after each reset |
| 5 | Ran the first full loop over BLE | — | RdRegister 1,1; Start → LxStart 0; GetData with real totalizer. Open issue: gross/flow read 0 |

## 2026-09-26 (morning): simulator fixes and tracker round cycles
| # | Action | Change | Result |
|---|---|---|---|
| 6 | RCA: simulator gross/flow always 0 | Wrong snapshot key, hidden by a bare `except` | Fixed (simulator folder, `.orig` backups) |
| 7 | RCA: simulator "stuck" | Serial worker died on a USB glitch | Port now reopens every 2 s |
| 8 | Simulator realism | Independent meters (node 1 LCR-II, node 2 LCR.iQ); totalizer carries over; pump on Start; Pause holds volume; lands exactly on the preset; switch position; `--serial COM7` autostart | Deliveries match real meter behaviour |
| 9 | Box IMEI was empty | `app_ec20.c`: parse the bare `AT+CGSN` reply | BoxInfo now includes the IMEI |
| 10 | Gaps against the tracker | `app_lcr_proto.c`: validation replies; RdMtrSetting; RdDiagnostics with supply mV; strict ACKs; GetLastCmd; `FW_MATCH_289` for V2.89 spellings | round 1: 127/129 |
| 11 | SetRs485 switching | `app_lcr.c` `lcr_set_rs485()` with RS485 direction pins | pins verified (GPIOE 0x24 ↔ 0x44) |
| 12 | Single-poll glitch dropped a meter | `lcr_host.c`: one poll retry; offline only after 3 misses | round 3: 129/129 |
| 13 | BLE address repeats hit the Windows cache | `app_bt.c`: 14 bits from the RTC | clean reconnects |
| 14 | Tester happy flow: Pause failed | RCA: the tester sends a hidden `Stop` before every GetData. Simulator changed so Pause on an idle meter returns rc 0 (real meter, golden capture) | happy flow 63/63 |
| 15 | About 1 frame in 50 lost | Simulator cleared a trailing `7E`; firmware now retries commands 3× (V2.89) | 40/40 Start/Stop, 0 poll failures |
| 16 | Meter unplugged mid-delivery (PB-106) | Added to the suite | pass |
| 17 | Committed and pushed | branch `feature/v289-tracker-round-cycles`, commit 7535d0b | 137/137, happy flow 63/63 |

## 2026-09-26 (midday): Port 1 RCA and product-wise validation
| # | Action | Change | Result |
|---|---|---|---|
| 18 | User report: Port 1 shows values with nothing connected | RCA: my bench shortcut routed both ports to USART2. `app_lcr.c` `lcr_uart()` now uses Port 1 = USART1, Port 2 = USART2 | log shows `p1 node1 off \| p2 node2 ON` |
| 19 | Empty port would block the loop | `lcr_host.c`: an offline port gets one probe per cycle (V2.89) | poll stays at 1 s |
| 20 | Docs survey (protocol Rev 1.86, Leo's source, golden capture) | — | used for items 21–24 |
| 21 | GetLcrNode | Answers the live node without scanning (golden 19:04:15) | pass |
| 22 | Node change left stale values | `lcr_port_set_node()`: drop old values, immediate re-sync | pass |
| 23 | SetPortLcrNode with the same node on both ports | Refused (Leo's source) | pass |
| 24 | Tester happy flow: `PresetGross 1 0` failed | RCA: first argument is the meter node, not the port. `app_lcr_proto.c` changed to node | pass |
| 25 | Product-wise suite | `tools/product_suite.py` | **173/173** |
| 26 | Tracker suite adapted to the real wiring | `tools/tracker_suite.py` | **129/129** |
| 27 | Tester happy flow re-pointed to Port 2 (sequence sent as text; tester files untouched) | `tools/run_happy.py --replace` | 59/63; the 4 flags are `BOXSTATUS_NO_METER` for the empty J1, which is correct |
| 28 | Committed | 6b3aad7 | — |

## Firmware behaviour that deliberately follows V2.89 over the V3.01 tracker
- **PB-052:** `LxPause 0` with no delivery open.
- **PB-072:** `LxBoxTime <unix>`, with no GPS suffix.
- **PB-008 / 011 / 014:** edge-case replies for storage and live data.

## Not run on this bench
- Phone-app flows.
- LED, printer and power-cut checks.
- Double meter (needs a second meter on J1).
- Negative pulses.
- Live SIM, 4G, GNSS.
- OTA.

## Not in the repo
- **LCR simulator source:** it lives in `C:\GD32\LCR Meter\...\lcr-meter-simulator_5`, edited in place at the user's request. Copying it into this public repo was blocked.
- **Full raw chat transcript:** generating it from the session files for the public repo was blocked. The activity notes and this log cover the actions and changes.

## 2026-09-26 / 27 (night): real-meter LCR simulator v6, RS485 disabled, busy-aware firmware
| # | Action | Change | Result |
|---|---|---|---|
| 29 | User: the simulator doesn't behave like a real meter, gets stuck, and live data doesn't update | Two research passes: the LCP spec (Rev L, 62 pp., checked against all 19,303 frames of Leo's real SR260 capture) and the LCR-II / 600 / iQ manuals | reference docs for the rebuild |
| 30 | Built **LCR simulator v6** (`C:\GD32\LCR Meter\...\lcr-meter-simulator_6`; not in this repo, and `_5` left untouched) | One meter per COM port (model + node from the page); full field table and access levels; rc 38 busy windows; duplicate message-IDs cached; valve + manual RUN PULSER; presets, no-flow timer, tickets, shift, LCR-II switch, iQ soft keys and settings; persistence; new web UI; `test_meter.py` | 68/68 offline checks |
| 31 | Firmware: RS485 disabled (user) | `LCR_RS485_ENABLE 0`: ports stay RS232; `SetRs485 1` → `LxSetRs485 1` | — |
| 32 | RCA: a real meter answers rc 38 for ~7 s after Start and ~5 s after End; the box marked it offline | `lcr_host.c`: an rc 38 poll means busy → meter stays online and keeps its last values | GetData stays valid during the counter test |
| 33 | RCA: a queued Start (rc 38) was reported `LxStart 1` | rc 38 = success only for a Start to a meter that wasn't busy. Commands sent while the meter is busy are dropped by the meter, so the box reports `1` (V2.89 wrongly said 0) | — |
| 34 | RCA: after a sync, the first request reused the sync's message-ID; a frame to another node (scan, wrong ModifyLcrNode) broke the ID sequence, so the meter answered a later PresetGross from its cache without applying it | `lcr_xfer_ex()`: the first frame to a new node carries Sync; retries keep the same ID (no double execution); a fresh ID after each sync | preset lands on the meter |
| 35 | Validation (J1 empty, J2 → simulator v6) | `product_suite.py` rewritten for the real-meter model: drives the pulser, waits out busy windows; adds Multiple preset, no-flow timer, ticket gating | **158/158** (LCR-II + LCR.iQ) |
| 36 | Tracker suite adapted (pulser, busy, RS485 disabled) | `tracker_suite.py` | 129/129 (PB-060 expectation corrected) |
| 37 | Tester happy flow (Port 2 variant) | — | 55/63: 4 × "no meter on port 1" (J1 empty, correct) + 4 commands the meter dropped. The tester's hidden `Stop` before every GetData ends the delivery and the meter is busy printing the ticket, so the next Pause/Start/Preset gets rc 38 — same as a real SR260 |
