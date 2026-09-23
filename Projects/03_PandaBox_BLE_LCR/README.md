# 03_PandaBox_BLE_LCR: PandaBox over BLE with two simulated LCR meters

This is the first application firmware for the old PandaBox board (GD32F305VCT6).

The phone or tester connects over BLE (Yichip YC1021, name **PandaBrainBLE**) and sends the PandaBox text commands (`BoxStatus`, `Start 1,`, `GetData 1,0,` and so on). The box answers exactly the way Leo's firmware answers when real LCR meters are attached:

- **Port 1** holds **meter 1** (LCP node 1).
- **Port 2** holds **meter 2** (LCP node 2).

The meters are simulated on the MCU, but the box talks to them through **real LCP frames**:

- `7E 7E` framing, `1B` escaping, CRC-16 (poly 0x1021, seed 0x7E7E);
- field reads/writes 20h/21h;
- delivery commands 24h;
- address change 25h;
- node scan 28h.

Replacing the simulator with a UART exchange (USART1 PA2/PA3, USART2 PD8/PD9) connects real meters. That is one function, `lcr_transport()` in `src/lcr_host.c`.

Build: `./build.sh` (GCC 14.2, `-DHXTAL_VALUE=12000000U`, 120 MHz). Flash: `./flash.sh` (J-Link, SWD).
Console: USART0 PB6/PB7 (CH340, COM9), 115200 8N1. Type `help`.

## Files

| File | What it does |
|---|---|
| `src/main.c` | SysTick time base (starts at the build time, since the board has no RTC coin cell), console, main loop, IMEI read from the EC25 |
| `src/bt.c/.h` | YC1021 driver: 105-record HCI init table, name/PIN/MAC/visibility, BLE/SPP data frames, line assembly, chunked replies, re-advertise and self-heal |
| `src/bt_init_table.c` | Init table extracted from Leo's X-Box V2.89 (`0x08018B9E`), by `02_HW_Bringup/tools/extract_bt_table.py` |
| `src/proto.c` | PandaBox command protocol: every command the tester and phone app use, with reply formats taken from Leo's firmware and the tester validator |
| `src/lcr_host.c/.h` | The box side of the LCR link: two ports, 1 s LCP polling of fields 2/4/17/18/100/101, commands, 400-record RAM history per port |
| `src/lcp.c/.h` | LCP frame builder and parser, plus a self-test against known frames |
| `src/meter.c/.h` | LCR-II meter simulator (fields, 24h command set, flow ramp, preset, ticket, no-flow timer) |
| `tools/ble_smoke.py` | Scan, connect, send commands, check replies, then check the box advertises again |
| `tools/bringup.py` | Serial console driver (from project 02) |

## Console commands

| Command | Effect |
|---|---|
| `status` | Time, BT state/status/restarts, each port's values and meter state |
| `inject <cmd>` | Run a protocol command as if it came over BLE; prints `REPLY …` and `INJECT_END` |
| `flow <port> <tenths>` | Set the simulated pump rate (tenths of gal/min) |
| `trace bt 0/1`, `trace lcp 0/1` | Print raw YC1021 frames or LCP frames |
| `lcptest` | LCP self-test |
| `btstart` | Re-run the BT bring-up |
| `reboot` | Reset the MCU |

## Verified on the bench (2026-09-24, over BLE from the PC with bleak)

```
found 5.1s E5:11:33:32:09:7B
connected+services after 2.3s
'BoxStatus'     -> 'LxBoxStatus 2,0,1,2,0.0,S,0.0,E,0,1'
'BoxInfo'       -> 'LxBoxInfo 2.4,250502,3.001,260924,000000000000000,LCR'   (IMEI is filled in ~27 s after boot)
'RdPortLcrNode' -> 'LxRdPortLcrNode 1,2'
'Start 1,'      -> 'LxStart 0'
'GetData 1,0,'  -> 'LxGetData 1,1,16,1790218465,0.1,20.0,12345.7,0.0,12345.6,0.0,0.0,S,0.0,E'
'Stop 1,'       -> 'LxStop 0'
re-advertising after disconnect: True (3.6s)
```

## Known issue: reconnecting from the same Windows PC

- **Works:** the first connection from the bench PC to a given BLE address, fully (connect about 2.4 s, all commands).
- **Fails:** every later connection from that PC to the same address, even after a board reset. WinRT times out, and the box then stops advertising because Windows keeps a half-open link-layer connection.
- **Ruled out:** Windows has no bond or device entry for it.
- **Changing the address gives one more working first connection.** That is why the BLE address MSB is `E7` (E5 and E6 were used up in the tests).

See `docs/PandaBox_Development_Journal.md` §8 for the full investigation and next steps: phone test with nRF Connect, Leo's V2.89 on this board for comparison, and adding the service UUID to the advertising data.
