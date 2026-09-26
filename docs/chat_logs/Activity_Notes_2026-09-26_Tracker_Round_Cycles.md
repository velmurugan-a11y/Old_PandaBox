# Activity Notes — 2026-09-26: tracker round cycles on the full BLE ↔ RS232 ↔ LCR loop

Previous session: [Activity_Notes_2026-09-25_Project05_BLE_LCR_Loop.md](Activity_Notes_2026-09-25_Project05_BLE_LCR_Loop.md).
Full chat for every session: [PandaBox_Full_Chat_Log.txt](PandaBox_Full_Chat_Log.txt) (kept locally, see the end of this file).

## Bench
| Link | Port |
|---|---|
| LCR simulator (Flask :5000) → USB-RS232 → PandaBox Port 2 (USART2) | COM7 |
| pandabox-tester (Flask :5001) → BLE → PandaBox (YC1021) | Windows BLE |
| PandaBox debug console (CH340, 115200) | COM10 |
| Flash / debug | SEGGER J-Link, SWD |

The board runs `Projects/05_PandaBox_FW_V289` `bench` (0x08000000), rebuilt and reflashed after each change.

## Result
| Round | Tracker checks | Tester happy_flow | What changed before the round |
|---|---|---|---|
| 1 | 127/129 | – | simulator fixed (below), parser gaps filled, IMEI read |
| 2 | 127/129 | – | RS485 direction pins; simulator lands on the preset |
| 3 | 129/129 | – | poll retry + 3-miss offline rule; 14-bit BLE address |
| 4 | 124/129 | 63/63 | simulator: Pause on an idle meter = rc 0 (real meter) |
| 5 | 129/129 | 63/63 | simulator frame-sync fix; V2.89-style command retries |
| final | **137/137** | **63/63** | added PB-106 (meter unplugged mid-delivery), reconnect retries |

Also checked: 40/40 Start/Stop stress cycles, and 294 polls per port with 0 LCP failures (J-Link counters).
Per-case table: [PandaBox_Command_Test_Results_V2901.xlsx](../PandaBox_Command_Test_Results_V2901.xlsx): 83 PASS,
5 DIFFERS, 2 PARTIAL, 39 NOT RUN. The NOT RUN cases are phone-app, LED, printer and physical tests the
bench can't do.

## Why the LCR simulator was "stuck", and what was fixed
The simulator lives in `C:\GD32\LCR Meter\...\lcr-meter-simulator_5`. That folder isn't part of this
repo, so the changes are only described here. `.orig` backups sit next to every edited file.
1. **Gross and flow stayed 0.** `_sync_from_register` read `snapshot()['flow_rate']`, but the key is
   `flow_rate_units_per_min`. The KeyError was swallowed by a bare `except: pass`, so nothing after it
   ever updated. Errors are now logged and exposed as `last_error` in `/api/serial/status`.
2. **Bridge died silently** (`running: false` after a USB glitch). The worker now reopens the port every 2 s.
3. **Both meters shared one state.** Node 1 and node 2 are now independent meters on the same wire,
   routed by LCP address: node 1 = LCR-II register (5000.0 gal, 60 gal/min pump), node 2 = LCR.iQ
   register (49282.7 gal, 45 gal/min, digital valve ramp).
4. **#17 totalizer** didn't carry completed deliveries. It is now base + completed deliveries + the running one.
5. **Pump:** an LCP Start opens the valve at the pump rate if none was set in the web UI. Pause closes it
   (flow 0, volume holds), Resume reopens it.
6. **Preset:** the delivery lands exactly on the preset (two-stage valve), and DS_PRESET_REACHED is set.
7. **Switch position** (devStatus bits 0–2): Run while a delivery is open, Stop when idle.
8. **Real-meter replies:** Pause with no delivery open returns rc 0 / devStatus 0x21, as the real meter
   does in Leo's golden capture (2026-09-21 19:12:19).
9. **Latency and frame loss:** replies now go out a few ms after the request instead of after a fixed
   50 ms read window. A lone trailing `7E` (the start of the next frame) is no longer cleared, which had
   dropped about 1 frame in 50.
10. `python app.py --serial COM7` starts the serial bridge at launch.

## Firmware changes (Project 05)
- **app_ec20.c:** IMEI read from the bare `AT+CGSN` reply. Before, the parser only matched `+CGSN:"…"`,
  so BoxInfo's IMEI field was empty.
- **app_lcr_proto.c:**
  - Input validation, following the tracker: over-long names, bad IP, bad port, empty APN,
    `SetRs485 2`, `SetPortLcrNode 256`, GetLcrNode ranges.
  - New `RdMtrSetting <port>`, read from LCP #102/#25/#37/#27.
  - `RdDiagnostics` reports the real 12 V supply in mV (ADC).
  - Meter commands reply 0 only when the meter accepts (rc 0).
  - `GetLastCmd` returns V2.89's fixed string. Unknown meters give Error / None,1.
  - `FW_MATCH_289 1` switches on V2.89's exact spellings (`LxDiagnostics`, `LxModifytLcrNode`,
    `LxGetLastMtrCmd n,Cmd rc`).
- **app_lcr.c:** `lcr_set_rs485()` switches the transceivers without ever powering both, and drives the
  RS485 direction pins (receive by default, transmit only while sending).
- **lcr_host.c:**
  - Poll fields retry once. A meter goes offline only after 3 failed polls in a row, keeping its last
    values meanwhile.
  - Commands and set-field requests retry up to 3 times on no answer (V2.89 `applcrStopCmdTimeOut`).
  - New `lcr_get_field()`.
- **app_bt.c:** the BLE random-static address now takes 14 bits from the RTC. With 6 bits it repeated
  within a day and hit an address the Windows BLE stack had cached (connect failures).

## Where the V3.01 tracker and V2.89 disagree (firmware follows V2.89)
- **PB-052:** `Pause` with nothing running returns `LxPause 0`. The real meter accepts it; V3.01 blocks it in firmware.
- **PB-072:** `LxBoxTime <unix>` has no `,0,0` GPS suffix (V2.89 golden format).
- **PB-008, PB-011, PB-014:** storage and live-data replies for edge cases (see the workbook).

## Findings worth knowing
- The pandabox-tester runner sends a hidden `Stop <node>` before **every** GetData (`runner.py`
  `drain_buffer`). On V2.89 and a real meter this ends the delivery. That is why its happy flow shows
  flow 0 after the first poll, on the real box too.
- `SetRs485 1` switches PE5/PE6 correctly (GPIOE ODR 0x24 → 0x44), but on this board the RS232
  transceiver keeps passing data with PE5 low, probably back-powered through the adapter.

## Tools added (Project 05 `tools/`)
- `tcmd.py`: send commands through the tester; `--connect` scans and connects.
- `tracker_suite.py`: the tracker cases, with setup/restore and live-delivery checks.
- `run_happy.py`: runs a tester sequence through its own engine.
- `make_results_xlsx.py`: builds the results workbook.

## Not pushed
Nothing is pushed yet. An earlier push that included the full chat log was blocked by a safety check,
because the repo is public. The chat log stays local until the user decides.

---

## Part 2 (2026-09-26, midday): product-wise validation, RCA on Port 1

The user reported: "Port 1 has nothing connected, but the log shows values."

| # | Finding | Root cause | Fix |
|---|---|---|---|
| 1 | Port 1 shown `ON` with values while J1 is empty | A bench shortcut in `app_lcr.c` `lcr_uart()` routed **both** logical ports to USART2 (J2). "p1" was really the simulator's node 1 answering on the J2 wire. | Port 1 = USART1 (J1) and Port 2 = USART2 (J2), same code for both. The console now shows `p1 node1 off \| p2 node2 ON`. |
| 2 | An empty port would block the loop about 1.4 s per second (6 fields × 2 tries × 120 ms) | Retries also applied to ports that were already offline | Offline ports get a single field #2 probe per cycle, as V2.89 does in the golden log. |
| 3 | GetLcrNode scanned even with a live meter (`2,1` instead of `2,2` for the LCR.iQ) | Did not match V2.89 | If the port's meter is online, the box answers its node at once, with no scan (golden 19:04:15). The scan path keeps the trailing comma. |
| 4 | After SetPortLcrNode / ModifyLcrNode, GetData could briefly serve the old meter's values under the new node | The port kept its online flag and values until the next poll | New `lcr_port_set_node()`: drops the old values and re-syncs immediately (Product ID with the sync bit, as V2.89 does). |
| 5 | `SetPortLcrNode 2,2` was accepted | — | Refused, as in Leo's source: the same node on both ports makes node → port lookups ambiguous. |
| 6 | Tester happy flow: `PresetGross 1 0` → `LxPresetGross 1` | My code took the first argument as the **port**. The tester and Leo's source treat it as the **meter node**, like Start/Stop. The two only agree while node == port. | PresetGross and PresetNet now take the node. |

The docs survey (protocol Rev 1.86 docx, Leo's 2023 source, the golden capture) was used for every decision above.

### Results (J1 empty, J2 → simulator)
- `tools/product_suite.py`: **173/173**. Covers box functions; Port 1 empty; and for each product (LCR-II node 1, LCR.iQ node 2, each on J2): identity/settings, GetLcrNode, node change to 9 and back, presets, delivery, preset auto-stop, history, meter unplugged mid-delivery. Also a wrong node, BT rename and BoxReset.
- `tools/tracker_suite.py` (adapted to the real wiring): **129/129**.
- pandabox-tester `happy_flow.csv`, re-pointed to Port 2 (`--replace "SetPortLcrNode 1 2=>SetPortLcrNode 0 1"`, sent as text so the tester's files stay untouched): **59/63**. The 4 flags are the validator's `BOXSTATUS_NO_METER` for port 1, which is correct because J1 is empty.
- PB-095/096 (double meter) now need a second meter on J1: NOT RUN.
