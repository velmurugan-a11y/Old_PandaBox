# Activity Notes — 2026-09-25/26: Project 05 (V2.89 rebuild) full LCR ↔ BLE loop

Full chat for every session: [PandaBox_Full_Chat_Log.txt](PandaBox_Full_Chat_Log.txt)

## Setup
PC LCR simulator (Flask :5000, serial bridge on COM7) → USB-RS232 → PandaBox Port 2 (USART2) → YC1021 BLE → tester / bleak on the PC.
The board runs `Projects/05_PandaBox_FW_V289` `bench` build, flashed at 0x08000000.

## Done and verified
1. **BLE not connecting: fixed.** Root cause was the documented reconnect bug. The old static address (MSB 0x25) got cached half-open by the Windows stack. The BLE address is now random-static, and its low bits come from the RTC, so every boot gets a fresh address (it connected at E8/EC/C3:11:… on successive resets). BT classic stays 0x25. The box now marks the link online when BLE/SPP data arrives, so replies are no longer dropped (`app_bt.c`).
2. **Real LCP on RS232 Port 2.** Firmware talks real LCP over USART2 instead of the internal simulator (`lcr_host.c` lcr_transport() → HAL raw UART, `app_lcr.c` bridge). The sim logged rx=2980+ bytes. CRC is byte-identical to Leo's real meter (54 74, AB 56).
3. **Reply format matches the factory log / V2.87:** `LxBoxStatus 2,0,1,2,0.0,S,0.0,E,0,1`, `LxRdRegister 1,1`, `LxSetPortLcrNode 0`, `LxStart 0`, `LxGetData 1,1,…`, `LxBoxStorage …`.
4. **LCR meter protocol** decoded from Leo's golden capture.
5. **Full circle over BLE in one connection (all 8 commands OK):** RdRegister → 1,1 (both meters online). Start 1 → LxStart 0. GetData 1 → meter 1, real totalizer 5000.0 from the sim, valid timestamp. GetData 2 works. Stop and BoxStorage OK.
6. **Simulator fixes** (a copy is in `Projects/05_PandaBox_FW_V289/tools/lcr_sim_patch/lcp_endpoint.py`): it answers both node 1 and node 2 on the single Port-2 wire, and it ticks the register on the serial path. Before, `tick()` only ran from the web `/state` route.

## Still open
- GetData gross (#2) and flow (#4) are 0 after Start. The sim register does accrue offline (units went 1.19 → 4.97 in 4 s), so the delivery is not advancing in the live serial path. Likely cause: the endpoint's `_push_to_register` only calls `start_delivery()` when its state is END, or the commanded flow isn't applied on the live singleton. The tester's happy_flow needs flow > 0. This is on the simulator side only.

## How to resume
- Restart the sim after code edits (the reloader is off). Re-arm it with `POST /api/serial/config {port: COM7}`, then `/api/serial/start`, then `/api/lcr2/pulser {mode: flowing, flow_rate: 75}`.
- Board oracle over J-Link: lcr_port[0] @ 0x20000C24, port stride 0x324C, online @ +1, v[] @ +8.

## Files changed this session
`lcr_host.c`, `app_lcr.c`, `app_bt.c`, `tools/lcr_sim.py` (Project 05), and the simulator's `meter_core/lcp_endpoint.py`.

## Working rules (unchanged)
Tester/LCR folders are read-only. No external-flash writes. Push only when told, and to a new branch. COM9 is held by the tester.
