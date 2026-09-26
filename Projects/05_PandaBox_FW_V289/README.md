# Project 05 — PandaBox firmware, V2.89-compatible clean-room build

Our own source for firmware that behaves like Leo's **X-Box V2.89** on the XBOX V2.5 board
(GD32F305VCT6). The full plan and the decisions behind it are in
[docs/PandaBox_V2.89_Rebuild_Plan.md](../../docs/PandaBox_V2.89_Rebuild_Plan.md); the behaviour of
each V2.89 module, read out of the binary, is in [spec/](spec/).

## Layout (mirrors Leo's `src/` tree so the disassembly and his log line numbers line up)

```
config/    version.h, board pin map, gd32f30x_libopt.h
linker/    app.ld.in  -> app1 / app2 / bench
src/hal/   clock (12->120 MHz), gpio, uart (5 ports, IRQ + idle-gap packets), flash, rtc, adc, spi, wdg
src/sys/   DBG (Leo's "L,ms,file,line:" format), TMR software timers, EVT deferred events
src/drv/   led (V2.89 pattern table), gd25q (GD25Q256 4-byte SPI NOR)
src/app/   app_cfg (T_BOX_PARAM), app (3 s init), app_bt (YC1021), app_ec20 (EC25/GNSS/WiFi),
           app_lcr (+ lcp/meter/lcr_host/app_lcr_proto, the LCP engine + App command parser)
src/main/  main super-loop, cli console shell
spec/      V2.89 reverse-engineering notes
re/        objdump + annotate.py workspace over X-Box_V2.89_APP1.bin
```

## Build

```bash
cd Projects/05_PandaBox_FW_V289
./build.sh all        # app1 (0x08008000), app2 (0x08021000), bench (0x08000000)
```

Needs the Arm GNU toolchain (14.2). Output per target in `build/<target>/`.

## Flash (J-Link, SWD)

```bash
./flash.sh bench      # standalone image at 0x08000000 (day-to-day debugging)
./flash.sh app1       # into APP1 slot, booted by Leo's bootloader
./flash.sh restore    # put the original 256 KB flash back
```

## What is verified on the board (2026-09-25)

`bench` image flashed at 0x08000000 and checked over J-Link:

| Check | Result |
|---|---|
| Clock | `SystemCoreClock = 120000000` (12 MHz HXTAL, V2.89 PLL recipe) |
| Super-loop | SysTick tick counter increments |
| LEDs | GPIOC output register animates (boot fast-blink then off) |
| Config | `T_BOX_PARAM` loaded, HeatTime default 1000 ms |
| LCR/LCP | both simulated meters answer LCP Product ID -> `lcr_port[].online = 1` |

### Full loop over BLE (2026-09-26)

PC pandabox-tester (BLE) → PandaBox `bench` → RS232 Port 2 (J2, USART2) → USB-RS232 COM7 → LCR simulator
(LCR-II node 1, LCR.iQ node 2). Port 1 (J1, USART1) has nothing connected and reports offline:

| Check | Result |
|---|---|
| `tools/product_suite.py` vs LCR simulator v6 (real-meter model: rc 38 busy, manual pulser): LCR-II + LCR.iQ, node change, presets (Clear/Multiple), delivery, no-flow timer, ticket gating, history, unplug | **158/158** |
| `tools/tracker_suite.py`: every automatable case of `PandaBox_Command_Test_Tracker.xlsx` | **129/129** |
| pandabox-tester `happy_flow.csv` re-pointed to Port 2 | 55/63: 4 × `BOXSTATUS_NO_METER` (J1 empty) + 4 commands the meter dropped while busy printing the ticket that the tester's hidden `Stop` triggers (real-meter behaviour) |
| Start/Stop stress | 40/40 cycles |
| LCP link health (J-Link `lcr_port[].polls_ok/polls_fail`) | 294 polls per port, 0 failures |
| Delivery math | final #17 = initial #100 + gross #2, exact to 0.1 gal; preset stops on the preset |
| Meter unplugged mid-delivery | offline after 3 missed polls, box keeps answering, recovers with totals intact |
| BoxInfo IMEI | read from the EC25 (`AT+CGSN`) |

Results per tracker case: [docs/PandaBox_Command_Test_Results_V2901.xlsx](../../docs/PandaBox_Command_Test_Results_V2901.xlsx);
raw runs in [test_results/](test_results/). Where the V3.01 tracker and V2.89 disagree, this firmware follows
V2.89 (golden capture) and the workbook marks the case DIFFERS.

Run it again:

```bash
python tools/tcmd.py --connect              # scan + connect the tester (BLE address changes every boot)
python tools/tracker_suite.py --out test_results/run.json
python tools/product_suite.py --out test_results/product.json
python tools/run_happy.py happy_flow.csv --replace "SetPortLcrNode 1 2=>SetPortLcrNode 0 1"
```

RS485 is disabled for now (`LCR_RS485_ENABLE 0`). The box handles the real meter's rc 38 busy windows.

Still open: EC25 on a live SIM (4G/TCP, GNSS fix), OTA (M7–M10), a real LCR meter on the wire, and the
phone-app flows (PB-089…PB-125).

## Matching V2.89 exactly

- `config/version.h`: set `FW_MATCH_289 1` to report Leo's exact `2.891,260827` in `LxBoxInfo`
  (default is `2.901` in the same format, so a field box can be told apart — plan decision D4).
- The YC1021 init table (`app_bt_table.c`) is byte-identical to V2.89 `@0x08018B9C`.
- GPIO init order/levels, LED patterns, LCP poll order and timing, the EC25 AT table, and the
  config layout are copied from the V2.89 binary (see `spec/`).
