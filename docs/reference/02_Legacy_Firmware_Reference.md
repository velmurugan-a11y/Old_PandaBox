# PandaBox (X-Box) legacy firmware: reverse-documentation findings

Target: GigaDevice GD32F305VCT6 (Cortex-M4F, 256 KB flash at 0x08000000, 96 KB SRAM at 0x20000000).
Original author: 徐文杰 (Xu Wenjie, "Leo"). Source dates: app_ec20.c 2023-09-01, app_lcr.c 2023-09-03.

Inputs analysed:
- Source: `From Leo/04_Reference_Source/` main_init.c (184 lines), app_cfg.c (186), app_ec20.c (812), app_lcr.c (2614), update.c (1095). `original_GBK/` is empty.
- Binaries: `Bootlaod_APP_V2.64.bin` (108 940 B), `X-Box_V2.87_APP1.bin` / `_APP2.bin` (80 324 B each).
- Board dump: `backup/original_flash_256K.bin` (262 144 B).

Working files from the analysis (not included in the repo): `strings_{bl,a1,a2,dump}.txt` (all printable strings, min length 4, with absolute addresses), `bl.dis` (bootloader disassembly), `a1.dis` (V2.87 APP1 disassembly), `d2.dis` (dump APP2 slot), and the scripts `regions.py`, `strs.py`, `lits.py`.

Confidence tags: **[SRC]** = read directly from the source files. **[BIN]** = confirmed in binary or disassembly (addresses cited). **UNVERIFIED** = inference only.

---

## 0. Executive summary

- The build has two separate programs. The **bootloader** sits at 0x08000000–0x08007FFF (about 26 KB used). The **application** runs A/B from two 100 KB slots: APP1 at 0x08008000 and APP2 at 0x08021000. APP_INFO (the boot and upgrade record) is the 2 KB page at 0x08007800. User config (T_BOX_PARAM) is the last 2 KB page at 0x0803F800. **[BIN]**
- `Bootlaod_APP_V2.64.bin` is a **merged factory image**. It holds the bootloader, an APP_INFO page pre-set to "APP1 OK" (0x5A5A5A5A), and an **application V2.62** (LxBoxInfo `2.621,240630`) at 0x08008000. That app is not the same as V2.87. **[BIN]**
- The board dump contains the bootloader (a slightly different build from the factory image: 1 742 bytes differ, same strings) and **application V2.77** (LxBoxInfo `2.4,250502,2.771,251031`) in APP1. The APP2 slot does not hold a Leo application. It holds a 2.4 KB **clock-diagnostic program** that prints RCU_CTL/CFG0/CFG1, SystemCoreClock, flash size and PLL/PREDV values. APP_INFO in the dump reads: APP1 = OK, length 0x13280. **[BIN]**
- Clocks: HXTAL_VALUE is compiled as **12 MHz**. The bootloader runs directly from HXTAL (12 MHz, no PLL). The app runs **HXTAL 12 MHz /3 (PREDV1) x10 (PLL1) /10 (PREDV0) x30 (PLL) = 120 MHz**, with AHB/1, APB2/1 and APB1/2. **[BIN]**
- UART map (HAL enum index = physical peripheral): 0 = USART0 debug/CLI 115200; 1 = USART1 LCR port 1 19200; 2 = USART2 LCR port 2 19200; 3 = UART3 Bluetooth (Yichip SPP/BLE) 115200; 4 = UART4 Quectel EC20/EC25 4G+GPS+WiFi 115200. **[BIN]**
- There is no CAN, I2C, ENET, USB or DMA usage. SPI0 drives an external GD25Q256 (32 MB) NOR flash that stores LCR delivery records. Other peripherals used: RTC, BKP, ADC0, FWDGT, TIMER2, EXTI. **[BIN]**

---

## 1. Software architecture (from source)

### 1.1 Layers
| Layer | Init function | Contents (main_init.c) | Source provided? |
|---|---|---|---|
| HAL (MCU hardware) | `HAL_Init()` L31 | `MCU_Init`, `HWEVT_Init` (hardware event), `GPIO_Initialize`, `UART_Init`, `RTC_Init`, `IT_Init` (interrupts), `SPI_Initialize`, `ADC_Initialize`, `FLASH_Init`, `WDG_Init`, `ASSERT_Init(halGetUartID(E_HAL_UART_PRINT))`. `IIC_Init` is commented out. | No |
| SYS (common services) | `SYS_Init()` L60 | Clears the reboot-callback table `g_pfnRebootCB[SYS_REBOOT_CB_NUM]`, then runs `DBG_Init`, `TMR_Init`, `EVT_Init`, `MQ_Init`, then `MQ_Creat(sysMqCallback, &g_stMqMgr.mq)`, which creates the system message queue. `VER_Init`, `ISR_Init` and the reboot-reason read are commented out. | No |
| DRV (off-chip devices) | `DRV_Init()` L104 | `LED_Init`, `gd25q256df_init()`, then `gd25q256df_read_id()` (printed as "u32GdFlashId = %X"). `PDDETECT_Init` (power detect) is commented out. | No |
| APP | `APP_Init()` L132 (duplicated verbatim at L165, so the file cannot compile as-is) | `SYS_SetSysMsgCallback(appProcessSysMsg)`. Sets the 4G, BT, WiFi and GPS LEDs to fast blink, then starts one-shot timer `g_tmrInitID` (3000 ms) → `appInitTimeOut`. That callback presumably calls APPCFG_Init, APPEC20_Init, APPLCR_Init, APPBT_Init and the rest (UNVERIFIED: `app.c` is missing; the binary has "---APP Init Start!---" and "---APP Init Ok!---" in `..\src\app\app.c`). | Partial |
| CLI | `CLI_Init()` | Serial command shell on the debug UART (`..\src\main\cli.c`). | No |

Paths embedded in the V2.87 binary (ASSERT file names) give the real source tree:
`src/app/{app.c, app_bt.c, app_cfg.c, app_ec20.c, app_lcr.c, update/update.c, update/xmodem.c}`, `src/drv/{drv.c, led.c, gd25q.c}`, `src/hal/{hal.c, gpio.c, uart.c, flash.c}`, `src/sys/sys.c`, `src/main/cli.c`. In the bootloader the update folder is spelled `src/app/updata/updata.c` and `updata/xmodem.c` (update.c includes `"updata.h"`).

### 1.2 Main loop (super-loop, no RTOS) [SRC main_init.c L9-16]
```
while(1){ HAL_FeedWatchDog(); HAL_DoEvent(); EVT_DoEvent(); MQ_ProcessMsg(); TMR_ProcessTimeout(); }
```
- **Watchdog**: fed on every pass. The binary uses FWDGT (0x40003000): literals at 0x08015684/94/A4 in V2.87 and 0x0800416C in the bootloader.
- **HAL_DoEvent**: processes hardware events posted from ISRs (HWEVT). UART RX packets are delivered through per-UART callbacks (`HAL_UartSetCallback`) once a gap of "pack interval" ms is seen (`HAL_UartSetPackInterval`). The EC20 uses 20 ms, LCR ports 50 ms, and the updater `UPDATE_UART_INTERVAL_MS`.
- **EVT**: software events. `EVT_Creat(cb,&id)` registers one; `EVT_PostEvent(id,arg)` makes the callback run on the next loop pass. This is used as a deferred call ("run in main context").
- **MQ**: message queues. `MQ_Creat(cb,&mq)` registers one. The system queue delivers to `sysMqCallback`, which dispatches to the APP callback set by `SYS_SetSysMsgCallback`.
- **TMR**: software timers. `TMR_Creat(ms,cb,arg,&id)` creates a one-shot and `TMR_CreatRepeatTimer` a periodic timer. Control calls are `TMR_Start/Stop/Restart/SetPeriod/Kill/IsStarted`. The tick source is probably TIMER2 (literals 0x080111DC.. in the bootloader, 0x0800ABD4 in the app; UNVERIFIED).
- Debug output: `DBG(level, fmt,...)`, `DBG_PrintLine`, `DBG_SetPrintLevel(DBG_D/DBG_I/...)` and `DBG_RecoverRcvData()`. The last one gives the debug UART's RX back to the CLI. Levels in the binary are DEBUG, INFO, WARNING, ERROR and ASSERT.
- Reboot reasons in the binary: "Reset PIN", "Power on", "Software reset", "Watchdog reset", "Low power reset", "Unknown".

### 1.3 Key data structures

**E_SEND_MODE** [SRC app_cfg.c L17, app_lcr.c L106]: `E_SEND_BT` (default), `E_SEND_WIFI`, `E_SEND_4G`. It is a global in RAM and is not persisted. `applcrSendDataToAPP()` routes every "Lx..." reply by this mode: BT goes to `APPBT_SendDataToBt`, WiFi to `APPEC20_SendDataToWifi`, and 4G to `APPEC20_SendDataToServer`.

**T_BOX_PARAM** (persisted config). Field names come from the source. Offsets are recovered from V2.87 (`APPCFG_Init` at 0x08008E94; the AT sprintf code at 0x0800F8C8-0x0800F958 uses a stack copy at sp+0x78). **sizeof = 0x74 (116 B)**: memset/read/write all use a length of 116.

| Off | Size | Field | Evidence |
|---|---|---|---|
| 0x00 | 1 | `u8WifiSsidFlag` (0x5A = user SSID valid) | ldrb [r0,#0] cmp #0x5A → print SSID at +4 |
| 0x01 | 1 | `u8WifiPwdFlag` | [1] → PWD at +0x14 |
| 0x02 | 1 | `u8BtNameFlag` | [2] → BT NAME at +0x24 |
| 0x03 | 1 | BT-password flag (V2.87 only, "BT PWD: %s") | [3] → +0x34 |
| 0x04 | 16 | `au8WifiSsid[16]` | AT+QWSSID uses sp+0x7C |
| 0x14 | 16 | `au8WifiPwd[16]` | AT+QWAUTH uses sp+0x8C |
| 0x24 | 16 | `au8BtName[16]` | |
| 0x34 | 16(+3?) | `au8BtPwd` (V2.87) | 0x44-0x46 are unaccounted for (UNVERIFIED) |
| 0x47 | 15 | `au8ServerIp[15]` (ASCII dotted, NUL-terminated) | AT+QIOPEN uses sp+0xBF. The default is written at +0x47 (0x08008FAC) |
| 0x56 | 8 | `au8ServerPort[8]` (ASCII) | sp+0xCE; default "80" written at +0x56 |
| 0x5E | 16 | `au8Apn[16]` | AT+QICSGP uses sp+0xD6 |
| 0x6E | 1 | `eLcrWorkMode` (UNVERIFIED; the dump value is 0x02, taken to be CMD mode) | |
| 0x6F | 1 | HeatTime flag (0x5A) (V2.87) | ldrb [#0x6F] |
| 0x70 | 4 | `u32HeatTime` in ms. It is the heartbeat period; it defaults to 1000 when the flag is not set. | ldr [#0x70], default 1000 |

Notes: string setters strip CR/LF (`APPCFG_Set/GetBoxParam` replace "\r\n" with NUL in the first 16 bytes). Length limits: SSID ≤ 15, WiFi password 8–15, IP 10–20 characters, port 2–6, APN ≤ 15. The source default (`APPCFG_ReDefaultBoxParam`) is server "139.9.203.9", port "80". **The V2.87 binary instead defaults to "118.89.111.211", port "80"** (0x08008FD8, 0x08008FE8). V2.62 and V2.77 use the same 118.89.111.211.

**T_UPDATE_APP_INFO** (24 B) is described in §2.4.

**LCR structures** [SRC app_lcr.c]: two ports, each with `T_LCR_REV_DATA g_stLcrData[2]`, `T_LCR_SAVE_DATA g_stLcrSave[2]` and `T_LCR_DATA_INFO g_stLcrInfo[2]`.
- `T_LCR_REV_DATA`: `u8DevNum`, `u8PackNum`, `u8HandCnt` (link heartbeat, 10 = alive), `bIsRpt`, `au32Data[6]`, `au32DataOld[6]`.
- `T_LCR_SAVE_DATA`: `au8WriteBuf[256]` (one flash page), `u8WritePos`, `u32WrAddr`.
- `T_LCR_DATA_INFO` is the header stored in sector 0 of each port area. Fields: `u32Flag` (0x5A5A5A5A = valid), `u8DevNum` (LCR node address bound to the port), `u32DataCnt`, `u32StartAddr`, `u32EndAddr`, `u32RdAddr`.

### 1.4 Config / data storage
- Internal flash: APP_INFO page 0x08007800 (written by bootloader and app). The config page is `HAL_FlashUserDataWrite/Read(offset 0, ...)`. Its address is computed at run time from the FLASH_SIZE register (0x1FFFF7E0) as 0x08000000 + size·1 KB − 2 KB, which gives **0x0803F800**. **[BIN: FLASH_SIZE literal used 6x in flash.c; data found in the dump]**
- External SPI NOR GD25Q256 (32 MB) holds LCR history (§2.3.6).

---

## 2. Per-module behaviour

### 2.1 app_cfg.c
- `APPCFG_Init()` reads T_BOX_PARAM from the user flash page and prints SSID, PWD, BT name, IP and port. In V2.87 it also prints BT PWD and HeatTime, and sets HeatTime to 1000 if flag 0x6F ≠ 0x5A.
- `APPCFG_SetBoxParam()` copies the struct, cleans CR/LF, and writes the whole struct to flash. The erase is inside HAL and is not visible.
- `APPCFG_GetBoxParam()` returns a copy by value.
- `APPCFG_ReDefaultBoxParam()` zeroes the struct, writes the default IP and port, and saves. It is triggered by the "ReBoxParam" command.
- `APPCFG_Set/GetSendMode()` sets or gets the RAM-only E_SEND_MODE.

### 2.2 app_ec20.c (Quectel EC20/EC25 4G + GNSS + WiFi AP)
UART4 at 115200 8N1 with a 20 ms pack interval [BIN 0x08009130-0x0800913E].

**Power-up** (`APPEC20_Init` L771): WIFI_EN = 1, GPS_PWR = 1, wait 100 ms, then PWRKEY = 1 for 600 ms, then PWRKEY = 0. After that the init RX callback is `appec20RcvInitDataProcess` (missing). The module creates:
- event `g_evtEc20CmdID` → `appec20SendCmdEvt`
- one-shot `g_tmrEc20CmdID` (CMD_TIMEOUT, **10 000 ms** in V2.87) → resend
- repeating heartbeat `g_tmrHeartID` (HEART_TIME; V2.87 takes it from config HeatTime)
- repeating `g_tmrCsqID` (CSQ_TIME, **15 000 ms**) → `AT+CSQ`

LEDs: 4G fast blink, GPS on.

**Init AT sequence**: `g_apszAtCmd[15]`, sent one at a time with each expecting "OK". The V2.87, V2.77 and V2.62 binaries hold the identical table (V2.87 at 0x0801B42C).

| # | Command sent | Purpose |
|---|---|---|
| 0 | `ATE0` | Echo off |
| 1 | `AT+QGMR` | Modem firmware revision |
| 2 | `AT+CGSN` | Read IMEI (→ `g_au8ImeiCode[15]`, cmd state E_EC20_CGSN) |
| 3 | `AT+CIMI` | Read IMSI (→ `g_au8CimiCode`, also used to detect SIM presence `g_bIsHaveSim`) |
| 4 | `AT+QWSSID=TBOX_APP`, or `AT+QWSSID=<cfg ssid>` if flag 0x5A | Set the WiFi hotspot SSID (default **TBOX_APP**) |
| 5 | `AT+QWAUTH=5,4,"123456789"`, or the cfg password | WiFi AP authentication: mode 5, encryption 4 (UNVERIFIED: WPA2-PSK/AES), default password **123456789** |
| 6 | `AT+QWIFI=1` | Turn on the WiFi AP |
| 7 | `AT+QWTOCLIEN=1,5553` | Open a WiFi-side TCP server/port 5553 so the phone app can connect (UNVERIFIED semantics; Quectel-specific) |
| 8 | `AT+CSQ` | Signal quality (→ `g_u8Csq`) |
| 9 | `AT+QIACT=1` | Activate PDP context 1 |
| 10 | `AT+QICSGP=1,1,"<APN>","","",1` (table default "MOBILE") | Configure PDP context 1: IPv4, APN, no user/password, auth=PAP. The code always uses the cfg APN. Note that it runs **after** QIACT, which is an ordering bug. |
| 11 | `AT+QIOPEN=1,0,"TCP","<ip>",<port>,0,0` | Open TCP socket 0 to the server (cfg IP if `UTIL_IsValidIp`, else SERVER_IP/SERVER_PORT). **Skipped if there is no SIM.** Buffer access mode. |
| 12 | `AT+QGPS=1` | Start GNSS |
| 13 | `AT+QGPSCFG="nmeasrc",1` | Enable NMEA sentence output through AT |
| 14 | `AT+QDATAFWDHEX=1` | Forwarded WiFi data in hex format |

After the last command, the RX callback switches to `appec20RcvDataEvtProcess` (missing). If TCP is connected, the module sends `write(IMEI,<15-digit IMEI>)` to the server and sets the 4G LED on. Otherwise it logs "---EC25 Init End !---".

**Runtime AT commands**
- `AT+QISEND=0,<len>\r\n` followed by the payload is the TCP send (`APPEC20_SendDataToServer`). ",<IMEI>" is appended to every payload unless it starts with "wr". The source uses 20 ms delays between the two writes.
- `AT+QIRD=0,512` / `AT+QIRD=0,256` read TCP data after the URC `+QIURC: "recv",0`. The payload arrives as `+QIRD: <n>\r\n<data>`.
- `AT+QISTATE?` checks socket state. The binary parses `+QISTATE` ("socket_state = %c"). `AT+QICLOSE=0` closes the socket before a reconnect.
- **Heartbeat / reconnect state machine** (`appec20HeartTimeOut`): when connected, ticks alternate between `AT+QISTATE?` and sending `Heart`. In V2.87 the heartbeat is `Heart,%d,%d.%01d,...`, i.e. it carries LCR values. When the state is E_TCP_CLOSING and the heartbeat count reaches ≥3, it sends `AT+QICLOSE=0`, sets the period to HEART_TIME/5, then re-sends `AT+QIOPEN...` (E_TCP_OPENING).
- `AT+CSQ` every 15 s.
- GPS: `AT+QGPSGNMEA="RMC"` (`APPEC20_SendGpsCmd`, only once the init index is >8). The RMC parser is in the missing RX handler. Results go to `T_GPS_DATA g_stGpsData {u32Time, u32Lati, u32Latimm, u32Longi, u32Longimm}`.
- GPS encoding (from the consumers in app_lcr): `u32Longi/2` is integer degrees+minutes (UNVERIFIED exact ddmm), and `u32Longi%2` selects the hemisphere: an index into "EWSN" (longitude uses [2+x], latitude uses [x]).
- WiFi TX to the phone: `AT+QDATAFWD=0,3,<2*len>,"<HEX>",1`. The payload is hex-encoded (`UTIL_DataToHexSendString`), and CR/LF is appended unless the payload ends with ';'. Sending is one-shot: `g_bWifiFlag` is cleared after each send. WiFi RX URCs are `+QWIFIND` and `+QDATAFWD` [BIN strings 0x0800EC10/18].
- Setters: `APPEC20_SetWifiSsid/SetWifiPassword/SetServerIp/SetServerPort/Set4GApn` update T_BOX_PARAM. Changes take effect after reboot (the init table reads the cfg).

**Server / network constants found**
- Source default 139.9.203.9:80 (Huawei Cloud CN; not used by the binaries).
- Binaries (V2.62/V2.77/V2.87) use a default SERVER_IP of **118.89.111.211** and port **80**.
- The board's config page holds **34.121.179.10:8080** (a Google Cloud IP, presumably the FleetPanda server).
- The table default APN is `MOBILE`.

### 2.3 app_lcr.c (Liquid Controls LCR meter interface)

#### 2.3.1 Ports and modes
- LCR1 = USART1 and LCR2 = USART2, both 19200 8N1 with a 50 ms pack interval [BIN 0x08009882/0x080098A2]. Both RX callbacks go to `applcrDataEvtProcess`. They are RS-485 in V2.87 (strings "TEST1_RS485", "TEST2_RS485", "SetRs485").
- On boot the app sends "TEST LCR1!" and "TEST LCR2!" on each port (V2.77 and the source).
- Link LEDs: repeating 1000 ms timer `applcrLinkCheckTimeOut`. GPIO input PORT1/PORT2 low turns LCRx_LED on (cable-detect inputs).
- Work modes (`E_LCR_WORK_MODE`), set by `SetMode <n>,` and persisted into T_BOX_PARAM.eLcrWorkMode:
  - **E_BRIDGE_MODE** (default at boot) is a transparent bridge. Any meter frame (first byte 0x7E) is forwarded to BT wrapped as `01 05 <len> <frame>`. App-side raw bytes, where byte[2] = node addr (with CR/LF stripped), are sent to the matching LCR port. The debug UART RX is also routed to the command parser, and debug level is set to INFO.
  - **E_CMD_MODE**: the box itself polls both meters. It sends the init/sync frame to both ports, clears the old data and starts the 1 s poll timer. The debug level is set to DEBUG.
  - **E_IDLE_MODE**: stops polling.
  - Numeric values are UNVERIFIED (likely IDLE=0, BRIDGE=1, CMD=2).

#### 2.3.2 Meter frame format (as used by the code; LCP-style)
`7E 7E | to(node) | from | status/toggle | len | cmd | data... | CRC_lo CRC_hi`
- CRC: CRC-CCITT polynomial 0x1021, **init 0x7E7E**, bit-by-bit MSB-first "augment" style (`applcrGetCrc` L69). It covers bytes [2 .. n-3] and is stored little-endian at [n-2],[n-1] (`applcrFillCrc` L89).
- The `from` byte is 0x14 (20) in most frames and 0x15 in SwitchState/PresetGross frames. The box acts as node 20/21 (UNVERIFIED meaning).
- Byte [4] alternates 0/1 as a sequence toggle on find/preset.

| Purpose | Frame (template) | cmd |
|---|---|---|
| Init/sync (sent after every command reply and on mode change) | `7E 7E dd 14 02 01 00 C4 EB` | 0x00 |
| Read field (poll) | `7E 7E dd 14 t 02 20 ff crc crc` | 0x20 (get field `ff`) |
| Poll fields, in order: au32Data[0..5] | ff = 0x02 GrossQty, 0x04 FlowRate, 0x11 (17), 0x12 (18), 0x64 (100), 0x65 (101) | |
| Set field (PresetGross) | `7E 7E dd 15 t 06 21 05 v3 v2 v1 v0 crc crc` (value big-endian, in tenths: "123456.7" → 1234567) | 0x21 |
| Start/Pause/Stop/Print (issue command) | `7E 7E dd 14 01 02 24 k 00 00`, k = eCmdID − E_START_ID | 0x24 |
| Modify node address | `7E 7E old 14 01 02 25 new crc crc` | 0x25 |
| Find node / status (SwitchState) | `7E 7E dd 14|15 t 01 28 crc crc` | 0x28 |

A poll reply is recognised when `(len == 14 || rsp[5] == 6) && rsp[2] == 0x14`. The value is the big-endian u32 at rsp[8..11], swapped with `UTIL_SwapU32Data`. The meter node is rsp[3].

#### 2.3.3 Timers and events (APPLCR_Init L2463)
| Handle | Period | Callback | Role |
|---|---|---|---|
| g_tmrLcrCmdID | 1000 ms repeat | applcrTimeOut → post g_evtLcrCmdID | Poll cycle start (CMD mode) |
| g_tmrLcrGetDataID | 120 ms one-shot | applcrGetDataTimeOut | Per-field response timeout. Decrements u8HandCnt. After 2 misses it switches port and re-posts. |
| g_tmrStopCmdID | 120 ms | applcrStopCmdTimeOut | Retries Start/Stop/Pause/Print up to 3x |
| g_tmrLinkCheckID | 1000 ms repeat | applcrLinkCheckTimeOut | Port-connected LEDs |
| g_tmrFindAddrID | 5 ms (created on demand) | applcrGetLcrNodeTimeOut | Node scan: one address per 5 ms tick, sending cmd 0x28 over start..end |
| g_tmrRebootCmdID | 1000 ms | applcrRebootTimeOut (missing) | BoxReset |
| g_evtLcrCmdID | event | applcrSendCmdEvt | Sends the next field read (g_u8LcrCmdInx 0..5) for g_u8Port |
| g_evtLcrSaveID | event | applcrSaveDataEvt | Writes a 64 B record to the external flash |
| g_evtSendHisAppID | event | applcrSendHisDataToAppEvt | Streams one history record; the next record is sent on the BT/WiFi "send done" callback |

#### 2.3.4 Poll / record state machine (CMD mode)
1. The 1 s tick posts an event, which sends field[g_u8LcrCmdInx] to meter `g_stLcrInfo[g_u8Port].u8DevNum` and starts the 120 ms timeout.
2. When the reply arrives, the value is stored in au32Data[idx] and idx++. At idx == 3 the module also requests GPS RMC.
3. At idx == 6 (all fields read): if the data changed and GrossQty did not decrease (or it is 0, meaning a Start reset), a consistency fix-up runs between data[0], [2] and [4] (Gross + field100 vs field17). Then PackNum++, bIsRpt = TRUE, and **g_evtLcrSaveID** is posted. u8HandCnt is set to 10 and the port toggles 0 → 1.
4. Bug to note: `applcrSaveDataEvt` forces u8PortInx = 0 when the device number matches port 0, and returns otherwise ("DevNum is change"). As written, port 2 data is effectively never saved (UNVERIFIED whether this was fixed in V2.87; the binary strings changed).

#### 2.3.5 Delivery-control flow (phone app → box → meter)
- "Start n," / "Pause n," / "Stop n," run `applcrStartOrStopCmd(addr, E_*_ID)`, which sends cmd 0x24 and stops the poll timer. The meter's reply (a frame that is not a poll reply, while g_eCmdID ≠ MAX) makes the box re-send init/sync, record `g_au8LastMtrCmd[port]` (0 = Start, 1 = Pause, 2 = Stop, 3 = None), and reply to the app with `LxStart 0`, `LxPause 0`, `LxStop 0`, `LxPrint 0`, `LxModifytLcrNode 0`, `LxPresetGross 0` (or `LxPresetNet 0` in V2.87). Polling then restarts.
- Source bug: `applcrStartOrStopCmd` only sends when `g_stLcrInfo[g_u8Port].u8DevNum != addr`, which looks inverted. PresetGross has the same pattern ("== → break").
- "Print n," goes to `applcrPrintCmd`, which is **missing from the source**.
- "SwitchState n," builds a 0x28 frame but never sends it (the source is incomplete). The reply is formatted as `LxSwitchState <Run|Stop|Print|Shift Print>`.
- Node discovery: "GetLcrNode port,start,end," scans addresses and replies `LxFindLcrNode port,addr,` or `LxFindLcrNode 0`. "RdRegister" scans port 3 then 4 over 1..255 and replies `LxRdRegister %d,0/1,`. "ModifyLcrNode,port,old,new," changes a node address.
- The full phone/app command set (string matching via `strstr`, so **order matters**; e.g. "GetDataTs" and "GetDataEcho" are tested before "GetData", and "SetBoxTime" before "BoxTime"):
  - Config: `SetMode`, `SetBtName`, `SetBtPwd`, `SetWifiName`, `SetWifiPwd`, `SetServerIp`, `SetServerPort`, `RdBtName`, `SetPortLcrNode a,b,`, `RdPortLcrNode`, `ReBoxParam`, `SetDbg n`, `BoxReset`.
  - Data: `DeleteAll addr,`, `GetDataTs addr,t0,t1,`, `GetDataEcho` (missing), `GetData addr,mode,`, `BoxStorage port`, `BoxStatus`, `BoxInfo`, `SetBoxTime <epoch>`, `BoxTime`, `HisDataTime addr,`, `GetLastMtrCmd port`, `GetLastCmd port`.
  - CMD mode only: `Stop`, `Start`, `Pause`, `Print`, `SwitchState`, `GetLcrNode`, `RdRegister`, `ModifyLcrNode`, `PresetGross`.
  - **V2.87 additions (binary only)**: `RdCalib`/`SetCalib` (`LxRdCalib`, `LxSetCalib`), `SetApp` (`LxSetApp1,1`), `SetRs485`, `SetHeatTime`, `SetApn`, `RdDiagnostics` (`LxDiagnostics %d,%d,%d,%d`), `PresetNet`, 4G `Update Imei` / `Update App` / `Update APP%d,%d,%d`. Fluid names appear as strings: Gasoline, Distillate, Lube Oil, Methanol, Aviation, Ammonia.
  - Every reply has the prefix "Lx" and ends with " 0" (OK) or " 1" (error).
- `BoxStatus` reply: `LxBoxStatus <mode>,0,<node1>,<node2>,<lon>.<mm>,<E/W>,<lat>.<mm>,<N/S>,<4Gflag>,1`.
- `BoxInfo` reply: the `VER` string followed by the IMEI. In V2.87 this is `LxBoxInfo 2.4,250502,2.871,260628,<IMEI>`. UNVERIFIED reading: HW 2.4, HW/BT date 2025-05-02, FW 2.87 build 1, FW date 2026-06-28.
- `GetData` reply (live): `LxGetData <dev>,1,<pack>,<time>,<v1/10>.<d>,...(6 values),<lon>,<E/W>,<lat>,<N/S>`. Value 1 may be negative (two's complement).

#### 2.3.6 External flash history (GD25Q256 on SPI0)
- The layout constants are inferred from code arithmetic and the comment "0x1000000":
  - `LCR_FLASH_TOTAL` = 32 MB.
  - Port area = `LCR_FLASH_TOTAL/2` = 16 MB, starting at `LCR_START_ADDR2 * port` (0x0000000 or 0x1000000).
  - `SECTOR_BYTE` = 4096; sector 0 of each area holds the `T_LCR_DATA_INFO` header (flag 0x5A5A5A5A).
  - `PAGE_SIZE` = 256.
- Record = 64 B: `u32 time(RTC epoch) | 6×u32 LCR fields | u32 Longi | u32 Longimm | u32 Lati | u32 Latimm | 20 B reserved`. Records are buffered 4 at a time in au8WriteBuf and written one page at a time. When a new sector starts, the header is rewritten and the data sector erased. It is a ring buffer: when full, the oldest sector (64 records) is dropped. Capacity per port is (16 MB − 4 KB)/64 = 262 080 records.
- `GetDataTs addr,t0,t1,` binary-searches the start time, then streams records as `LxGetDataTs dev,idx,...;`. The next record is sent when the BT/WiFi "send done" callback fires, until the record time exceeds t1.
- On boot (`APPLCR_Init`): the header is read, then the last sector is scanned to rebuild EndAddr/DataCnt. **The RTC is advanced to the newest record timestamp if the RTC is behind**, which keeps time monotonic across RTC loss.

### 2.4 update.c: bootloader and in-app upgrade

#### 2.4.1 APP_INFO record: page **0x08007800** (2 KB, FLASH_APP_INFO_ADDR) [BIN: literal 0x08007800 at 0x080020A8/0x080020D0/0x08002174 in the bootloader and 0x0800BF94/.. in the app]
```
typedef struct {            // 24 bytes, little-endian u32
  u32 u32AppFlag;    // +0x00  APP1 slot state
  u32 u32AppLen;     // +0x04
  u32 u32AppChkSum;  // +0x08  32-bit sum of all bytes
  u32 u32AppFlag2;   // +0x0C  APP2 slot state
  u32 u32AppLen2;    // +0x10
  u32 u32AppChkSum2; // +0x14
} T_UPDATE_APP_INFO;
```
Field order is confirmed by the bootloader's print sequence ("u32AppFlag:%x u32AppLen:%d u32AppChkSum:%d u32AppFlag2:%x ...") and by stack offsets (Flag at +0, Flag2 at +12) in `updateGetAppStartAddr` at 0x080059A4.

Flag values [BIN]:
| Constant | Value | ASCII in flash | Meaning |
|---|---|---|---|
| UPDATE_APP_OK_FLAG | 0x5A5A5A5A | "ZZZZ" | Slot valid, boot this one (`cmp.w r0,#0x5A5A5A5A` at 0x080059C6) |
| UPDATE_BANK_FLAG | 0x62616E6B | "knab" | Valid but backup (the previous image) |
| UPDATE_APP1_FLAG | 0x61707031 | "1ppa" | APP1 upgrade pending / incomplete |
| UPDATE_APP2_FLAG | 0x61707032 | "2ppa" | APP2 upgrade pending / incomplete |
| 0 / 0xFFFFFFFF | | | Empty |

#### 2.4.2 Slot constants [BIN]
- FLASH_APP_ADDRESS = 0x08008000 and FLASH_APP_ADDRESS_NEW = 0x08021000 (literal pool 0x08005A40/44).
- FLASH_APP_SIZE = 0x19000 (100 KB). `updateEraseAppData` erases **49** pages of 2 KB (`movs r5,#49` at 0x080058E2), i.e. (100K − 2K)/2K. **The last 2 KB page of each slot is never erased**, which caps usable images at 98 KB (0x18800). V2.87 is 80 324 B, so it fits.
- In the binary, `u32AppFlashLenMax` is computed as flash-size-based (FLASH_SIZE·1024 − (slot − 0x08000000) − 2 KB, function 0x08005978) rather than FLASH_APP_SIZE. That value is only used for a warning.
- FLASH_PAGE_SIZE_2048 = 2 KB, matching the GD32F305 page size. Programming is by halfword (`ProgramDataToFlash(addr,(u16*)...)`).

#### 2.4.3 Slot selection: `updateGetAppStartAddr(mode)` [SRC L49, BIN 0x080059A4]
- mode 0 (boot): Flag1 == OK → APP1; else Flag2 == OK → APP2; else Flag1 == BANK → APP1; else Flag2 == BANK → APP2; else 0.
- mode 1 (write target): Flag2 == 'app2' → APP2, else APP1.

#### 2.4.4 Boot flow
1. Reset → bootloader vector (SP 0x20002F48, Reset 0x08000165). `SystemInit` (0x080019A8) enables the FPU (CPACR |= 0xF00000), resets RCU and calls `system_clock_hxtal` (0x08004E80): **HXTAL on, SYSCLK = HXTAL (no PLL)**, AHB/1, APB2/1, APB1/2.
2. The bootloader main (0x08000E40…) runs HAL/SYS init and then `UPDATE_ReceiverInit` (0x080021C8). That function prints `update` on the debug UART (USART0, 115200) as a handshake for the PC tool, starts `tmrWaitUpdate` = **UPDATE_REBOOT_WAIT_TIME_MS = 200 ms** (`movs r0,#200` at 0x080021E8/0x0800221C), and a 200 ms WiFi-LED blink timer.
3. Inside the 200 ms window the PC tool may enter a CLI command (see 2.4.6). Otherwise the timer fires `updateRcvWaitUpdateTmrCallback` (0x08005E54):
   - If Flag1 == 'app1': "APP1 NOT OK, wait update" → `UPDATE_ReceiverStartUartUpdate(AppLen, AppChkSum)`, which resumes the upgrade over UART.
   - Else if Flag2 == 'app2': the same for APP2.
   - Else: "Jump to APP: %X" → `HAL_JumpToApp(updateGetAppStartAddr(0))`.
   - If the result is 0, the binary differs from the source. It prints "APP NOT OK, wait update", **tries `HAL_JumpToApp(0x08008000)` anyway** (the jump itself checks SP), and only then starts a UART update. The source instead used hard-coded len 0xC800 / sum 0x0095B68C.
4. `HAL_JumpToApp` (0x08000E84):
   - Checks `(*(u32*)app & 0x2FFE0000) == 0x20000000`, i.e. a valid initial SP in SRAM.
   - Deinits peripherals and the 5 UARTs (USART0/1/2, UART3/4 via `usart_deinit`), then calls 0x080049A8 (UNVERIFIED: RCU deinit / IRQ off).
   - Sets `MSP = app[0]`, writes **SCB->VTOR (0xE000ED08) = app base**, and calls `app[1]` (the reset handler).
   - The app's own SystemInit then reconfigures clocks to 120 MHz PLL.

#### 2.4.5 Upgrade (OTA) flows
- **In-app OTA over 4G** ("OMS", V2.87 at 0x0800EE4A):
  1. The server sends `Update APP<n>,<len>,<chksum>`. The IMEI is required first ("please send imei!").
  2. The app reads APP_INFO. If target slot n already has the OK flag, it refuses with "APPn is running, Can not update!" (this prevents overwriting the running image).
  3. Otherwise it sets Flag[n] = 'app1' or 'app2' plus len/chksum, erases the APP_INFO page, waits 100 ms, writes APP_INFO, and calls `UPDATE_ReceiverInit` (type E_UPDATE_TYPE_BY_OMS_RCV). UART4 is taken over and XMODEM frames arrive through `+QIURC`/`AT+QIRD=0,512`. ACK/NAK go back through `AT+QISEND=0,<n>`.
  4. Each data packet is cached in RAM (`UPDATE_APP_DATA_CHCHED_LEN_MAX`) and flushed to the target slot.
  5. On XMODEM end: 32-bit byte-sum check over len (`updateCheckApp`). If it passes, the new slot gets flag OK and the other slot's OK becomes BANK. APP_INFO is erased and rewritten, "Update succeed" / "LxUpdate 0" is sent, and the board reboots after UPDATE_DELAY_TO_REBOO_TIME_MS. On failure it replies "Update fail: check error" / "LxUpdate 1".
  6. On XMODEM error end, the pending flag is cleared, the other slot is restored to OK, and the board reboots.
- **Over BT** (E_UPDATE_TYPE_BY_BT_RCV): UART3 frames `02 07 <len> <data>` carry the payload and `02 06` means "send ok". Only a reassembled length of 1 (control byte) or **389** is accepted. That fits 3 + 384 + 2 (UNVERIFIED). TX uses `01 05 <len> <data>`. The BT module "Update" strings are in app_bt.c.
- **Over PC UART** (bootloader, E_UPDATE_TYPE_BY_PC_UART_RCV): the debug UART is taken over. The bootloader clamps packets >150 B to **133 B**, i.e. standard XMODEM-CRC with 128-byte blocks (0x08002294). CRC check type is `E_XMODEM_CHECK_TYPE_CRC`. "Update by UART start" is printed. For debugging, received packet byte[1] (the sequence number) mod 8 drives the 4 LEDs.
- The PC-side tool and xmodem.c are **not provided**.

#### 2.4.6 CLI commands (bootloader and app)
Bootloader cli.c [BIN 0x0800391C; tokens are 20 B each]:
- `update app1|app2 <len> <chksum>`: sets 'app1'/'app2' + len + sum, erases and writes APP_INFO, then `UPDATE_ReceiverInit`. It refuses if that slot is OK ("APP1 is running..." / "APP2 is running...").
- `set debug level <n>`, `debug on|off|time`, `reboot`, `reason`, `factest esp8266` / `FACTEST ESP8266` (a factory test; an ESP8266 was apparently used on some variant, UNVERIFIED).

The app cli.c (V2.87) also has `appinfo` (prints APP_INFO) and `update`.

---

## 3. Flash memory map

### 3.1 From source and binary constants
| Range | Size | Content |
|---|---|---|
| 0x08000000 – 0x080077FF | 30 KB | Bootloader code (used about 26 KB) |
| 0x08007800 – 0x08007FFF | 2 KB | APP_INFO page |
| 0x08008000 – 0x08020FFF | 100 KB (0x19000) | APP1 slot (erasable 98 KB) |
| 0x08021000 – 0x08039FFF | 100 KB | APP2 slot |
| 0x0803A000 – 0x0803F7FF | 22 KB | Unused (all 0xFF in the dump) |
| 0x0803F800 – 0x0803FFFF | 2 KB | User config T_BOX_PARAM (last page) |
| SRAM 0x20000000 | 96 KB | Bootloader initial SP 0x20002F48 (about 12 KB used). App SP 0x200038D0 (V2.87) / 0x20003950 (V2.77) / 0x20003920 (V2.62). About 14 KB used, so plenty of headroom. |

### 3.2 Non-0xFF regions actually found (256-byte granularity, trailing 0xFF trimmed)
| Image | Region | Bytes | Interpretation |
|---|---|---|---|
| Dump | 0x08000000 – 0x0800662F | 26 160 | Bootloader (the dump build; differs from the factory image at 0x6F8–0x662F, 1 742 bytes, identical string set) |
| Dump | 0x08007800 – 0x08007807 | 8 | APP_INFO |
| Dump | 0x08008000 – 0x0801B27F | 78 464 (0x13280) | APP1 = application **V2.77** (vector SP 0x20003950, Reset 0x08008165) |
| Dump | 0x08021000 – 0x0802197B | 2 428 | APP2 slot = **clock-diagnostic test program** (SP 0x200038C0, Reset 0x08021151, strings "RCU_CTL", "RCU_CFG0", "RCU_CFG1", "SystemCoreClock = ", "F-SIZE = ", "PLL x", "PREDV1 /", "PREDV0SRC=", "=== end ==="). It uses USART0. Not Leo's code; most likely flashed during earlier bring-up (UNVERIFIED). |
| Dump | 0x0803F800 – 0x0803F86E | 111 | Config page |
| Bootlaod_APP_V2.64.bin | 0x08000000 – 0x08006623 | 26 148 | Bootloader |
| Bootlaod_APP_V2.64.bin | 0x08007800 – 0x08007803 | 4 | APP_INFO = 5A5A5A5A (APP1 OK, len/sum left 0xFF) |
| Bootlaod_APP_V2.64.bin | 0x08008000 – 0x0801A98B | 76 172 | **Application V2.62** (`LxBoxInfo 2.4,240606,2.621,240630,`; SP 0x20003920, Reset 0x08008165) |
| X-Box_V2.87_APP1.bin | 0x08008000 – 0x0801B9C3 | 80 324 | App V2.87 linked at 0x08008000 (SP 0x200038D0) |
| X-Box_V2.87_APP2.bin | (0x08021000 – 0x0803A9C3) | 80 324 | The same app relinked at 0x08021000. It differs from APP1 in 469 bytes (absolute addresses only). |

So the "Bootloader" bin is bootloader + APP_INFO + app V2.62 merged into one image (0x0801A98C − 0x08000000 = 108 940 B). Its embedded app differs from V2.87 in about 96 % of byte positions (73 413 of 76 172 bytes); it is a different app version. The file name says "V2.64", but no bootloader version string exists in the image. The "V2.64" label probably refers to the package (UNVERIFIED).

### 3.3 Page dumps and interpretation
**APP_INFO (dump) @0x08007800**
```
5A 5A 5A 5A  80 32 01 00  FF FF FF FF  FF FF FF FF  FF FF FF FF  FF FF FF FF
```
This decodes as u32AppFlag = 0x5A5A5A5A (APP1 OK), u32AppLen = 0x00013280 (78 464, exactly the APP1 image size), u32AppChkSum = 0xFFFFFFFF (not written), and APP2 fields empty. The bootloader will therefore boot APP1 and never the APP2-slot test program. The checksum is not checked at boot, only after an upgrade. The actual byte sum of the dump's APP1 image is 0x007300C6. Byte sums for reference: V2.87 APP1 = 0x0075CEFB, V2.87 APP2 = 0x00757E5C, V2.62 = 0x006FA8A0. Pass these as `<chksum>` in decimal to `update appN <len> <sum>`.

**Config page (dump) @0x0803F800**
```
0000: 5A FF FF FF 70 61 6E 64 61 30 30 37 2C 00 FF FF  Z...panda007,...
0010-0046: FF ...
0040: .. .. .. .. .. .. .. 33 34 2E 31 32 31 2E 31 37  .......34.121.17
0050: 39 2E 31 30 00 00 38 30 38 30 00 00 FF FF FF FF  9.10..8080......
0060: FF FF FF FF FF FF FF FF FF FF FF FF FF FF 02 FF
```
| Field | Value |
|---|---|
| WiFi SSID flag [0] | 0x5A |
| WiFi SSID | **"panda007,"**. The trailing comma is stored verbatim because SetWifiName does not strip ','. The AP SSID really includes the comma unless it is stripped elsewhere (UNVERIFIED). |
| WiFi password, BT name, BT pwd | Flags 0xFF, so the defaults apply: password 123456789. The BT name is probably "PandaBrain" (the string appears in app_bt and app_lcr). |
| Server IP @0x47 | **34.121.179.10** |
| Port @0x56 | **8080** |
| APN @0x5E | unset (0xFF), so the AT command would carry garbage/empty. UNVERIFIED how the 0xFF APN behaves. |
| [0x6E] | 0x02, taken to be eLcrWorkMode = CMD mode (UNVERIFIED) |
| HeatTime flag [0x6F] | 0xFF, so 1000 ms is used |

Bytes 0x70–0x73 are 0xFF. The dumped region ends at 0x6E because the rest of the 116-byte struct is 0xFF.

---

## 4. Binary analysis details

### 4.1 Interesting strings (full lists in `strings_*.txt`)
- **Version**
  - V2.87: `LxBoxInfo 2.4,250502,2.871,260628,` (0x08010FD4)
  - Dump V2.77: `LxBoxInfo 2.4,250502,2.771,251031,` (0x08010BF0)
  - Factory image app V2.62: `LxBoxInfo 2.4,240606,2.621,240630,` (0x080106DC)
  - BT module firmware: `HV1.001`
  - The bootloader has no version string.
- **AT / 4G**: the whole init table (0x0801B6C8…), `AT+QISEND=0,%d`, `AT+QIOPEN=1,0,"TCP","%s",%s,0,0`, `AT+QICLOSE=0`, `AT+QISTATE?`, `AT+CSQ`, `AT+QGPSGNMEA="RMC"`, `AT+QDATAFWD=0,3,%d,`, `AT+QIRD=0,256`/`512`, `AT+QWSSID=%s`, `AT+QWAUTH=5,4,"%s"`, `AT+QICSGP=1,1,"%s","","",1`, `+QIURC`, `+QIOPEN`, `+QISTATE`, `QWIFIND`, `QDATAFWD`, `SEND OK`, `write(IMEI,`, `IMEI:%s`, `CIMI:%s`, `---EC25 Init End !---` (so the module is actually an EC25).
- **Hosts/IPs**: `118.89.111.211` (binary default server), `34.121.179.10` (board config), `139.9.203.9` (source only).
- **Bluetooth**: `PandaBrain` (default BT name), `YichipSmartSPP`, `YichipSmartLE`, `Flagtrip`, `yichip`, `SPP slave`, `BT_MAC=24:06:%.2X:%.2X:%.2X:%.2X`, `BLE_MAC=24:06:...` (MAC derived from the MCU UID; the UID literal 0x1FFFF7E8 is at 0x0800DFF8), `---ble is online---`, `appBtReSendCmdTimeOut %d`.
- **Protocol keywords**: all `Lx*` replies and command words listed in §2.3.5. Also `Heart,%d,%d.%01d,...` and `CRC is error!%x%x` (V2.87 checks the LCR CRC).
- **Upgrade**: `update`, `Update by UART start` (BL), `Update by oms start` (app), `Update OK`, `Update succeed`, `Update fail: check error`, `Update error end`, `Update timeout, reboot`, `APP1 NOT OK, wait update`, `APP2 NOT OK, wait update`, `APP NOT OK, wait update`, `Jump to APP: %X`, `app len overflow`, `Erase APP info Flash start/end`, `Erase APP data Flash start/end`, `u32AppChkSum = %d, u32AppLen = %d, u32CheckSum = %d`, `stRcvCmd.u8SN = %d---%d`, `LxUpdate 0/1`, `Update Imei`, `please send imei!`, `Update APP%d,%d,%d`.
- **Debug**: `System reboot, reason: %s`, `Reboot reason: %d, %s`, `Time: 20%02d-%02d-%02d %02d:%02d:%02d`, `DBG level: %s`, `ASSERT: %s,%04d`, `Command is too long`, `Invalid command`, `factest esp8266`, `-------Read Flash over!----------`, `sector_erase, ADDR: %x, u32DataCnt = %d`.

### 4.2 Peripheral usage (literal-pool scan, `lits.py`)
| Peripheral | BL | V2.87 | Notes |
|---|---|---|---|
| USART0 0x40013800 | yes | yes | Debug/CLI, 115200 (default pins PA9/PA10, UNVERIFIED no remap) |
| USART1 0x40004400 | yes | yes | LCR1 (RS-485), 19200 |
| USART2 0x40004800 | yes | yes | LCR2 (RS-485), 19200 |
| UART3 0x40004C00 | yes | yes | BT module, 115200 (PC10/PC11) |
| UART4 0x40005000 | yes | yes | EC25, 115200 (PC12/PD2) |
| SPI0 0x40013000 | 1 | 3 | GD25Q256 external flash (PA5/6/7, CS UNVERIFIED) |
| SPI1/SPI2, I2C0/1, CAN0/1, ENET, USBFS, DAC | – | – | **Not used** |
| DMA0/DMA1 | – | – | Not used; UART is interrupt-driven |
| FMC 0x40022000 | yes | yes | Internal flash erase/program |
| FWDGT 0x40003000 | yes | yes | Watchdog |
| RTC 0x40002800 / BKP | yes / – | yes / BKP+0x2C,+0x30 | RTC epoch time. BKP data registers are probably used for reboot reason or a flag (UNVERIFIED). |
| ADC0 0x40012400 | – | yes | Likely VCC12 supply monitor (the source mentions `HAL_AdcGetValue(E_HAL_ADC_VCC12)`) |
| TIMER2 | yes | yes | Probably the software-timer tick. TIMER0–7 appear together in one table (timer deinit list) |
| EXTI, AFIO, PMU, GPIOA-E | yes | yes | PMU LDO high-drive for 120 MHz. AFIO use suggests some remap or EXTI (UNVERIFIED) |
| SCB->VTOR | 0x08000EEC | 0x0800A8E4 | Bootloader jump. The app also has one (the app can jump too, UNVERIFIED) |

### 4.3 Clock configuration [BIN]
- The `rcu_clock_freq_get` constants (BL 0x08004998–A4, app 0x080166E8–F4) are IRC8M = 8 000 000, **HXTAL_VALUE = 12 000 000**, IRC48M = 48 000 000, and IRC8M/2 = 4 000 000. The GD default for CL parts would be 25 MHz, so this was customised. There are no 25 MHz or 8 MHz HXTAL literals.
- **Bootloader** (0x08004E80): `RCU_CTL |= HXTALEN`, wait for HXTALSTB (hang on timeout), CFG0: AHB/1, APB2/1, APB1/2, then SCS = 01 (HXTAL). **SYSCLK = 12 MHz.**
- **App** (0x08016BE4):
  - HXTAL on, `RCU_APB1EN |= PMUEN`, `PMU_CTL |= 0xC000` (LDOVS high).
  - AHB/1, APB2/1, APB1/2.
  - **RCU_CFG0**: `&= 0x9FC3FFFF; |= 0x20350000`. This gives PLLSEL = HXTAL/PREDV0 and PLLMF = 0b1_1101 + bit29, i.e. PLLMF = 29, meaning **×30**. USBFSPSC = 0.
  - **RCU_CFG1**: `&= 0xBFFEF000; |= 0x00010829`. This gives PREDV0 = /10, PREDV1 = /3, PLL1MF = ×10, PREDV0SEL = PLL1, PLLPRESEL = HXTAL.
  - PLL1 is enabled (wait PLL1STB) and PLL is enabled (wait); then SCS = PLL.
  - **SYSCLK = 12 MHz /3 ×10 /10 ×30 = 120 MHz**, which matches `SystemCoreClock` = 120 000 000 at 0x0801B8F8. AHB = 120 MHz, APB2 = 120 MHz, APB1 = 60 MHz.
  - If the crystal were really 25 MHz this recipe would produce 250 MHz (out of spec), and 8 MHz would give 80 MHz. **The crystal should be 12 MHz.** Confirm on the schematic/board (UNVERIFIED hardware); the dump's clock-diagnostic program was presumably written to answer exactly this.

### 4.4 Version summary
| Image | Role | Version evidence |
|---|---|---|
| Bootlaod_APP_V2.64.bin [0x0000-0x7FFF] | Bootloader | No version string. The file name says V2.64. |
| Bootlaod_APP_V2.64.bin [0x8000-] | App | V2.62 build 1, dated 240630 |
| Dump [0x0000-0x7FFF] | Bootloader | A different build from the factory image (same strings) |
| Dump APP1 | App | **V2.77 build 1, dated 251031**, which is on the real board |
| X-Box_V2.87_APP1/2 | App | V2.87 build 1, dated 260628 |

---

## 5. What is missing from the source (needs rewriting)

- **All of HAL**: `MCU_Init`, `HWEVT_Init`/`HAL_DoEvent`, GPIO (the `E_HAL_GPIO_*` pin map: the 4G/BT/WiFi/GPS/LCR1/LCR2 LEDs, WIFI_EN, GPS_PWR, GPS_PWRKEY, PORT1/PORT2 detect inputs, RS-485 DE), UART driver (RX packetising by inter-byte gap, `HAL_UartSetCallback`, `SetPackInterval`, `SetBaudRate`, TX; buffers are 389 B in the binary), RTC (`RTC_GetDateTimeToSec`, `RTC_SetDateTime`), IT, SPI, ADC, internal FLASH (`FlashRead`, `ProgramDataToFlash`, `EraseMultiPages`, `FLASH_WritePermissionEnable`, `HAL_FlashUserDataRead/Write`), WDG (`HAL_FeedWatchDog`), `HAL_JumpToApp`, `HAL_DelayMs`, `ASSERT`.
- **SYS**: `DBG_*`, `TMR_*`, `EVT_*`, `MQ_*`, `SYS_Reboot`, `SYS_DelayMs`, `SYS_SetSysMsgCallback`, `sysMqCallback`, reboot-reason handling.
- **DRV**: `LED_Init`/`LED_Play` (modes QUICK/ON, ...) and the GD25Q256 driver (`gd25q256df_init`, `read_id`, `read_data`, `write_sector`, `sector_erase`, `chip_erase`).
- **APP**: `app.c` (`appInitTimeOut`, `appProcessSysMsg`, the actual module init order), **app_bt.c** (the whole Yichip BT module driver and its AT/config protocol, `APPBT_SendDataToBt`, `APPBT_SetBtName`, `APPBT_SetBtPwd`, `01 05`/`02 07`/`02 06` framing, MAC derivation).
- **app_ec20.c pieces**: `appec20RcvInitDataProcess`, `appec20RcvDataEvtProcess` (all response/URC parsing: IMEI/CIMI/CSQ/QIOPEN/QISTATE/QIRD/WiFi forward/NMEA RMC parser).
- **app_lcr.c pieces**: `applcrPrintCmd`, `applcrGetDataEcho`, `applcrRebootTimeOut`, and the V2.87 features (calibration, RS-485 set, diagnostics, PresetNet, HeatTime, SetApp, fluid table, CRC check of meter responses).
- **Update**: `xmodem.c` (`XMODEM_RcvStart/RcvEnd/RcvMsgProcess`, frame and CRC, 128-byte versus 384-byte blocks), `updata.h`/`update.h` constants, **the bootloader's own main.c**, `UPDATE_ReceiverInit` caller logic, the app-side "Update APPn" handler, and the PC-side upgrade tool.
- **CLI** (`cli.c`): tokenizer, commands `update`, `appinfo`, `reboot`, `reason`, `set debug level`, `debug on/off/time`, `factest esp8266`.
- **Utilities**: `UTIL_DataToHexString`, `UTIL_DataToHexSendString`, `UTIL_SwapU32Data`, `UTIL_IsValidIp`, `UTIL_RemoveStrNewLine`, `STR2UINT32`, `MEMCPY`/`STRLEN` macros.
- **All headers**: types `MUINT8`/`MUINT32`/`MBOOL`, enums (E_HAL_UART, E_CMD_ID with E_START_ID/E_PAUSE_ID/E_STOP_ID/E_PRINT_ID/E_MDF_LCR_ND_ID/E_RE_GR_ID/E_STATE_ID/E_FIND_ADDER, E_LCR_WORK_MODE, E_TCP_STATUS, E_EC20_CUR_CMD), constants (HEART_TIME, CSQ_TIME, CMD_TIMEOUT, SERVER_IP/PORT, VER, LCR_FLASH_TOTAL, LCR_START_ADDR2, SECTOR_BYTE, PAGE_SIZE, FLASH_* and UPDATE_* timing, XMODE_DATA_SIZE_*).
- **Startup, linker scripts, GD32 firmware library and project files** (Keil, judging by the `..\src\` paths).

---

## 6. Open questions (UNVERIFIED)

1. The HXTAL crystal is 12 MHz according to firmware constants and the PLL recipe. It needs confirmation on the XBOX V2.5 schematic or by measurement. What did the clock-diagnostic program at 0x08021000 report?
2. Who flashed the clock-diagnostic program into the APP2 slot of the "original" dump? If our team did, the original APP2 content (probably an older app or empty) is lost.
3. Why is APP_INFO.u32AppChkSum 0xFFFFFFFF in the dump when AppLen was written? Possibly a factory flash tool wrote only flag+len, or the V2.77 flow differs.
4. Enum values: E_LCR_WORK_MODE (is 0x6E = 2 really CMD mode?), E_SEND_MODE, E_CMD_ID base, E_UPDATE_TYPE values.
5. T_BOX_PARAM bytes 0x44–0x46 and the exact size of the BT password field.
6. XMODEM block size: the bootloader UART clamps to 133 B (128-byte blocks), while the app BT path expects 389 B (3+384+2?). Is there a custom 384-byte XMODEM variant for BT/4G?
7. Exact semantics of the Quectel WiFi commands (`QWTOCLIEN=1,5553`, `QWAUTH=5,4`, `QDATAFWD=0,3,...`) require the EC25 + FC20 WiFi AT manual.
8. GPS lat/long encoding (`u32Longi/2` with hemisphere in the LSB, `u32Longimm` as fractional minutes?) depends on the missing RMC parser.
9. LCR protocol: meaning of cmd 0x00 (init/sync frame `02 01 00`), `from` = 0x14 versus 0x15, and field numbers 17/18/100/101. Cross-check against the Liquid Controls LCP documentation in `06_LCR_Meter_Manuals`.
10. Port-2 save bug in `applcrSaveDataEvt` and the inverted `!=` in `applcrStartOrStopCmd`: are they fixed in V2.87? This needs disassembly of those functions.
11. The GPIO pin map (LEDs, power keys, RS-485 direction, port detect) is not recovered. The table at app 0x08015A30–0x08015BAC (GPIOA-E pointers) is a starting point, but the schematic is faster.
12. Is "V2.64" the bootloader version or the package version? No version string exists in the bootloader.
13. Does the app also contain a bootloader-style jump (a VTOR literal is present at 0x0800A8E4), e.g. for `SetApp` switching between APP1 and APP2 without a reboot?
