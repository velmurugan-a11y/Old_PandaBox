# PandaBox (X-Box / "LCRBOX") — Master Protocol Reference

Compiled from the documents in `C:\GD32\Old_PandaBox\From Leo\03_Protocols_and_Specs\` and `06_LCR_Meter_Manuals\`.
Nothing in the source folders was modified. Items marked **UNVERIFIED** or **CONFLICT** need to be confirmed against the legacy firmware or hardware before re-implementation.

| Source | Identity |
|---|---|
| `Panada-Box Communication Protocol (Ver1.86).docx` | "Panada-Box Communication Protocol Rev 1.86", PCL TECHNOLOGY CO., LIMITED, December 2025 (docx core: created 2023-04-29, author "wjh", last modified 2025-11-12). Header text: "Panada-Box Communication Protocol" (one header part reads "Firmware Release Notes", which is a leftover template). |
| `Adding Pandabox Commands.docx` | FleetPanda-side request/spec for new commands (undated, no author, no tables). |
| `Panda BOX Technical Specification_v1.2.pdf` | 4 pages. The filename says v1.2 but every page footer says "Rev 1.0". |
| `LCR API Internal Messages for LCP.pdf` | Liquid Controls, "LCR LCP API Internal Messages", Revision L, © 1998-2018. Applies to SR200…SR269, SR600/601 (LCR 600), SR1000 (LCR.iQ). 62 pages. |
| `TCS3000_REMOTE_INTERFACE_DESCRIPTION_V1_4_4_1002.pdf` | TCS 3000 Remote Interface Protocol V1.4.4.1002, dated 10.02.2019, CONFIDENTIAL. The footers say "of 94", but the PDF has only **90 pages**. **Footer pages 8–11 are missing** from the file. |
| LCR manuals | LCR600_manual, LCR_II_installation_manual, LCR_II_setup_manual, LCRiQ/MASTERLOAD.iQ Product Manual (Rev 06AUG2019). I only skimmed these for communication settings. |

---

## 1. Technical specification summary (Panda BOX Tech Spec)

**Description.** A 4G/WiFi/Bluetooth industrial box (ODM/OEM) for tank trucks with LCR/TCS meters. It monitors vehicle location and fuel data from LCR/TCS meters and supports printing. It can act as an in-vehicle WiFi hotspot for up to 8 WiFi devices sharing the 4G LTE link.

**Features.** Industrial-class components; compact size; 4G LTE Cat.4; Bluetooth 4.2 BR/EDR and BLE 5.0; 2.4 GHz WiFi with hotspot; wide operating voltage; 32 MB flash that stores 262,144 data packages (that works out to 32 MiB / 262,144 = **128 bytes per stored record**, which is derived, not stated); multiple interfaces for meter monitoring and print control; OTA firmware upgrade.

**Functions listed.** Real-time tracking; read history record time; read storage status; read stored data by timestamp; delete stored data; BT/WiFi/TCP-server parameter configuration; read BOX status; reset/start BOX; set/read BOX time; Start/Pause/Stop meter; print control; set/read/modify meter address; pre-set gallon value; read meter connection status; read meter working status; report in real time the "data flow (gallon value), totalizer values (gallon value), GrossTotal_WM, NetTotal_WM, PreviousGross, PreviousNet". Those last four names are the LCR LCP fields #17, #18, #100 and #101 (see §4).

| Group | Item | Value |
|---|---|---|
| Mechanical | Size | 158 (L) × 99 (W) × 39 (H) mm |
| | Weight | 350 g |
| | Communication ports | 3× DB25, 1× DB9, 1× Micro USB |
| | Configuration port | Micro-USB |
| | SIM socket | Micro SIM, push-push |
| Data transmission | | LTE/WCDMA, Bluetooth, WiFi, Serial |
| Positioning | | GPS |
| Storage | | 32 MB flash, 262,144 data packages |
| Power | Working voltage | 9–36 VDC |
| | Working current | Max < 200 mA @ 13.8/27.6 VDC; Average < 150 mA @ 13.8/27.6 VDC; Sleep < 5 mA @ 12/24 VDC |
| | Button cell | 3.7 V / 40 mAh |
| GPS | | 66 channels; −165 dBm sensitivity; 5 m CEP; TTFF cold < 30 s typ., hot < 1 s typ. |
| WiFi | | 2.4–2.4835 GHz; 802.11b/g/n; 65 Mbps (n), 54 Mbps (g), 11 Mbps (b); range 120 m |
| Bluetooth | | "Bluetooth low energy"; 2.4–2.48 GHz; GFSK (BT 4.2 BR/EDR + 5.0 BLE); range 10 m (max, no shield); −93 dBm @ BLE; "transmission distance 100M" |
| Cellular | X-BOXLE (Nepal) | FDD LTE B1/B3/B5/B7/B8/B20; TDD LTE B38/B40/B41; WCDMA/HSPA+ B1/B5/B8; GSM/GPRS 900/1800 |
| | XBOXLA (US) | FDD LTE B2/B4/B12; WCDMA/HSPA+ B2/B4/B5 |
| | Data | LTE-FDD 150/50 Mbps; LTE-TDD 130/35 Mbps; HSPA+ 42/5.76 Mbps (DL/UL) |
| | Network protocol | Embedded TCP/IP stack |
| LED indication | | Cellular / WiFi / Bluetooth / GPS / Power / LCR Meter |
| Antennas | | Cellular external; WiFi internal; Bluetooth internal; GPS external |
| Environment | | Operating −30 … +80 °C; storage −40 … +85 °C; humidity 5–95 % (non-condensing) |

**Operating modes.** The spec does not describe any "continuous / non-continuous delivery" mode. None of the documents in this folder use those words. The mode-related information that does exist:
- Protocol §4.1/§4.2 BOX modes: `1` = bridge connecting (real-time transparent transmit), `2` = "LCR analyze storage" (per BoxStatus) or "cmd" (per SetMode v1.86), `3` = print (only accepts print data from the App). See CONFLICT C6.
- Meter-side behaviour that is close to continuous vs. single deliveries:
  - LCR Field #25 NoFlowTimer. `0` disables auto-end, which gives "multiple deliveries at one site" on the ticket.
  - LCR Field #27 PresetType = Multiple.
  - LCR.iQ Field #261 MultipleSiteDelivery.
  - TCS delivery status 8 "MULTIPLE PENDING".
- **UNVERIFIED:** the FleetPanda meaning of "continuous / non-continuous delivery" has to come from the firmware, the App, or Notion/Slack. It is not in these documents.

**CONFLICT C1 (hardware I/O).** The spec table lists "3× DB25, 1× DB9, 1× Micro USB" and a Micro-USB config port. The product photo on page 1 shows one DB25 labelled PRINTER, a **TYPE-C** port (silk "TPYE-C"), SIM, and DB9, with LEDs PWR/WIFI/BT/GPS/4G and SMA connectors for WiFi, BT, GPS and 4G. The photo shows WiFi and BT on external SMA connectors, but the table says those antennas are internal.

---

## 2. PandaBox ↔ App / Cloud protocol (Rev 1.86)

### 2.1 Transport
- The document title covers "Bluetooth/WIFI/TCP Server". The same ASCII command set is used over whichever link carries it.
  - §1.5 notes that "Bluetooth transmission data is too long, it will be subcontracted", meaning responses are split into multiple packets.
  - §3.6/§3.7 configure a **TCP/IP server IP and port**, which is the cloud link over 4G. §3.8 sets the APN.
  - BoxStatus reports 4G status and whether the App is connected over Bluetooth or WiFi.
- **UNVERIFIED:** the BT profile (Classic SPP vs. BLE GATT, UUIDs, MTU) is not specified. The spec only says the module supports BT 4.2 BR/EDR + BLE 5.0.
- **UNVERIFIED:** MQTT is not mentioned anywhere. The cloud link is described only as a TCP/IP server (raw TCP socket assumed). The payload format for cloud uploads (other than the "spontaneous upload" line in §3) is not defined.
- The stored-data model is two flash zones, one per meter.
  - When the App is not connected, the BOX stores records in a circular buffer and deletes the oldest when full.
  - If the BOX later talks successfully to a meter with a *different meter number*, it **deletes that zone's stored data**. Import data to the App before swapping meters.

### 2.2 Frame format (App ↔ BOX)
- **Plain ASCII text lines.** There is no binary header, no length field, no checksum/CRC, no escaping, and endianness does not apply. Numbers are ASCII decimal.
- A frame is: `CommandWord` + (optional) space + comma-separated parameters, **each parameter followed by a comma** (the last one too, since v1.82) + `<CR><LF>` (0x0D 0x0A).
- The command word starts with an uppercase ASCII letter (per the doc).
- **Downlink** (App→BOX): `Xxxxx p0,p1,...,\r\n`
- **Uplink** (BOX→App): the response word is the request word with **`Lx` prefixed**, e.g. `DataTime` → `LxDataTime`. Status results are `0` = success, `1` = fail unless stated otherwise.
- Example from the doc:
  ```
  APP->BOX  HisDataTime 1,<CR><LF>
  BOX->APP  LxHisDataTime 1,1695875000，1695875398，<CR><LF>
  ```
  **CONFLICT C2:** this example uses full-width Chinese commas `，` (U+FF0C). A parser should accept only ASCII `,` but must be robust to these. Several syntax lines in the doc also omit the space (`SetBtPwd<data0>`, `LxSetServerIp<data0>`) or the trailing comma (`BoxStorage <data0>`, `DeleteAll <data0>`). **UNVERIFIED:** exact whitespace and comma handling in the legacy parser.
- Times are **UNIX epoch seconds** (since v1.7). GPS appears as longitude and latitude fields. The numeric format is not specified (**UNVERIFIED**); `BoxTime` returns GPS as a "string".

### 2.3 Complete command table

"meter no." = the LCP node address of the meter (1–255). "port" = physical meter port 1 or 2. Per §6.1, **port 1 is the port "straight to the LED"** and port 2 is the other.

#### 1. Storage / history
| # | Request (App→BOX) | Params | Response (BOX→App) | Response fields / notes |
|---|---|---|---|---|
| 1.1 | `HisDataTime <m>,` | m = meter no. | `LxHisDataTime <m>,<t_first>,<t_last>,` | Earliest and latest stored record time (UNIX). The header lists `<data0>,<data1>` but three fields are defined (CONFLICT C3). |
| 1.2 | `BoxStorage <m>` | meter no. | `LxBoxStorage <m>,<count>,<free>` | Stored record count and remaining storage space (units unspecified: **UNVERIFIED** records vs. bytes). |
| 1.3 | `GetData <m>,<mode>,` | m = meter no. (BOX finds the port from the meter address); mode `0` = real-time report, `1` = history report (starting from the oldest stored record) | Real-time: `LxGetData ...`; history: **`LxGetDataTs ...`** | Record packet, see 2.4. The doc labels both params `<data0>` (CONFLICT C4); the order (meter, mode) is inferred. |
| 1.4 | `GetDataEcho <seq>,<result>,<nreq>,` | seq = packet serial just received; result `0` = OK, `1` = fail; nreq = number of records requested next | `LxGetDataTs ...` (next packet) | Flow control for history upload. The BOX sends the next packet **only after an OK echo** and **deletes the acknowledged packet from flash**. A wrong seq counts as failure, and the App must re-issue `GetData`. With no echo, the upload stops. |
| 1.5 | `GetDataTs <m>,<t_start>,<t_end>,` | meter address, UNIX start, UNIX end | Multiple `LxGetDataTs <m>,<seq>,<time>,<8 values>` lines, terminated by `LxGetDataTs <datan>,` | Each line here carries one record (no count field). Seq runs **1..100** here, versus 0..255 in 1.3 (CONFLICT C5). The terminator format is **UNVERIFIED**. |

#### 2. Delete
| 2.1 | `DeleteAll <m>` | meter no.; nothing happens if it does not match the stored meter | `LxDeleteAll <r>` | 0 OK / 1 fail |
|---|---|---|---|---|

#### 3. Bluetooth / WiFi / TCP server / APN
| # | Request | Params | Response | Notes |
|---|---|---|---|---|
| 3.1 | `RdBtName` | – | `LxRdBtName <name>` | "max 8 bytes" (CONFLICT C7 with 16 in 3.2) |
| 3.2 | `SetBtName <name>` | string ≤ 16 bytes | `LxSetBtName <r>` | The BOX restarts the BT module. The App must wait, rescan and reconnect. |
| 3.3 | `SetBtPwd <pwd>` | string ≤ 4 bytes | `LxSetBtPwd <r>` | Added in v1.83 |
| 3.4 | `SetWifiName <ssid>` | ≤ 16 bytes | `LxSetWifiName <r>` | |
| 3.5 | `SetWifiPwd <pwd>` | ≤ 16 bytes | `LxSetWifiPwd <r>` | Takes effect after a BOX restart. The App must rescan and reconnect. |
| 3.6 | `SetServerIp <ip>` | server IP (format **UNVERIFIED**, dotted string assumed) | `LxSetServerIp <r>` | v1.84 |
| 3.7 | `SetServerPort <port>` | port | `LxSetServerPort <r>` | v1.84 |
| 3.8 | `SetApn <apn>` | APN string, e.g. `SetApn cmnet` (the doc typos it as "cment") | `LxSetApn <r>` | v1.85 |

#### 4. BOX configuration / status
| # | Request | Params | Response | Response fields |
|---|---|---|---|---|
| 4.1 | `BoxStatus` | – | `LxBoxStatus d0,...,d8` | d0: `1` bridge connecting (real-time transmit), `2` LCR analyze storage<br>d1: working status (reserved)<br>d2: meter no. 1<br>d3: meter no. 2<br>d4: current UNIX time<br>d5: GPS longitude<br>d6: GPS latitude<br>d7: 4G status (`1` normal, `2` connection failure)<br>d8: App link (`0` none, `1` BT OK, `2` WiFi OK)<br>The doc omits the comma between d4 and d5. |
| 4.2 | `SetMode <m>` | `1` bridge (real-time transmit), `2` cmd, `3` print ("print mode only accepts information for printing by the app") | `LxSetMode <r>` | Modified in v1.86 (blue text). CONFLICT C6. |
| 4.3 | `BoxInfo` | – | `LxBoxInfo <hw>,<hwdate>,<sw>,<swdate>` | hw/sw version: 5 ASCII chars `xx.xx`. Dates are shown as `ddhhmm` (**UNVERIFIED**: likely a typo for yymmdd). |
| 4.4 | `BoxReset` | – | `LxBoxReset <r>` | The BOX replies first, then soft-resets. Some modules start late and ignore commands for a few seconds. BT must reconnect. |
| 4.5 | `SetBoxTime <t>` | UNIX time | `LxSetBoxTime <r>` | |
| 4.6 | `SetRs485 <0/1>` | `1` = RS-485, `0` = RS-232 (meter serial physical layer) | *not defined* | v1.86 blue text. **UNVERIFIED:** response (probably `LxSetRs485 <r>`), whether it is per-port or global, and persistence. |
| 4.7 | `SetApp1` / `SetApp2` | – | *not defined* | "set start from APP1 / APP2" (v1.86). **UNVERIFIED:** meaning (boot/firmware image slot select for OTA? or which App instance may start deliveries?). No response defined. |

#### 5. Meter communication control
| # | Request | Param | Response | Notes |
|---|---|---|---|---|
| 5.1 | `Start <m>` | meter address | `LxStart <r>` | Start communicate / start delivery |
| 5.2 | `Pause <m>` | meter address | `LxPause <r>` | |
| 5.3 | `Stop <m>` | meter address | `LxStop <r>` | |
| 5.4 | `Print <m>` | meter address | `LxPrint <r>` | v1.84 |

**UNVERIFIED mapping to LCP** (inferred from LCP semantics, not stated in the PandaBox doc): Start → Issue Command (24h) cmd 0 (start/resume); Pause → cmd 1; Stop → cmd 2 (end delivery + print ticket if settings allow); Print → cmd 6 (print ticket for current state). For TCS, the mapping is given in §2.5 (0x3C/0x39/0x3D/0x3E).

#### 6. "New commands"
| # | Request | Params | Response | Notes |
|---|---|---|---|---|
| 6.1 | `SetPortLcrNode <n1>,<n2>,` | meter no. on port 1 and port 2 (1–255, `0` = no meter) | `LxSetPortLcrNode <r>` | The doc writes "<data0> Execution succeeded 1: fail" (the "0:" is missing) |
| 6.2 | `RdPortLcrNode` | – | `LxRdPortLcrNode <n1>,<n2>` | 0 = none |
| 6.3 | `ModifyLcrNode <port>,<old>,<new>,` | port 1–2, original meter no. (error if it does not match the actual meter), new 1–255 | **`LxModifytLcrNode <r>`** (sic, extra "t") | CONFLICT C8. Likely LCP Set Device Address 25h (**UNVERIFIED**). |
| 6.4 | `PresetGross <port>,<gal>,` | port 1–2, preset gallons (float, 1 decimal). Example `PresetGross 1,1234.5,` | `LxPresetGross <r>` | Likely LCP Set Field #5 GrossPreset_PL as a VOLUME long scaled by Field #39 decimals (**UNVERIFIED**) |
| 6.5 | `BoxTime` | – | `LxBoxTime <t>,<gps>,` | UNIX time + GPS as string |
| 6.6 | `RdRegister` | – | `LxRdRegister <p1>,<p2>,` | `'0'` = no meter registered, `'1'` = port registered |
| 6.7 | `SwitchState <port>,` | 1 or 2 | `LxSwitchState <m>,<state>,` | state = "Run / Stop / Print / Shift Print" (the LCR switch position from the device-status byte). **UNVERIFIED:** text vs. numeric encoding |
| 6.8 | `GetLcrNode <port>,<from>,<to>` | port 1/2, scan range 1–250 | **`LxFindLcrNode <port>,<node>,`** | Name breaks the `Lx`+request rule (CONFLICT C8). Presumably scans LCP nodes with Get Product ID (00h). |

#### 7. TCS commands (v1.86, blue text, sketchy)
- `DirectDelivery 2,5`: "configure the address to meter No. 2 directly … meter product ID 1009+5 for meter No.2". This implies TCS **0x37 Configure Direct Delivery** with ProductID U16 = 1009 + 5 = **1014**.
- `PresetGross 2,5,125.3`: preset with product ID 1009+5, amount 125.3. This implies TCS **0x38 Configure Preset Delivery** (DlvType presumably 3 = PRESET_GRS_VOLUME).
  - **CONFLICT C9:** it has 3 parameters here versus 2 for the LCR variant in §6.4.
  - **CONFLICT C10:** "meter No. 2" is ambiguous (PandaBox port 2 vs. TCS slave address 2).
  - **UNVERIFIED:** why the base is 1009. TCS product IDs range 1001–9999.
- Firmware lookup table (TCS command code → App response word), copied verbatim:

| TCS CMD | Response word | TCS meaning (from TCS doc) |
|---|---|---|
| 0x1C | `LxGetSysTotalNet` | Get SysNet Totalizer (Dbl) |
| 0x1E | `LxGetSysTotalGross` | Get SysGross Totalizer (Dbl) |
| 0x2B | `LxGetDisplayGross` | Get Gross Display (Dbl) |
| 0x10 | `LxGetProdTotalGross` | Get Product GROSS Totalizer (Dbl) |
| 0x11 | `LxGetProdTotalNet` | Get Product NET Totalizer (Dbl) |
| 0x42 | `LxGetFlowrate` | Get Flowrate (Dbl) |
| 0x2C | `LxGetDisplayNet` | Get Net Display (Dbl) |
| 0x2D | `LxGetDisplayVol` | Get Volume Display (auto net/gross, Dbl) |
| 0x1F | `LxGetDeliveryState` | Get Delivery State (ENUM) |
| 0x35 | `LxGetStatusShift` | Get Shift Status (ENUM) |
| 0x39 | `LxPause` | Pause Delivery |
| 0x3A | `LxResume` | Resume Delivery |
| 0x3B | `LxAbort` | Abort Delivery |
| 0x3D | `LxStop` | End Delivery |
| 0x3E | `LxPrint` | Print Internal Ticket |
| 0x1A | `LxGetNoFlow` | Get NoFlow Timer (U16 s) |
| 0x3C | `LxStart` (comment "//Start 2") | Begin Delivery (1-byte DlvType) |

**UNVERIFIED:** the App request words (presumably the same without `Lx`, e.g. `GetSysTotalNet <port>,`), their parameters, and how the Dbl/ENUM results are formatted in ASCII. `Resume`/`Abort` exist only for TCS (LCR resume = Start / cmd 0).

### 2.4 Record packet format (`LxGetData` / `LxGetDataTs`)
```
LxGetData   <meter>,<n>,<seq>, {<time>,<a>,<b>,<c>,<d>,<e>,<f>,<lon>,<lat>,} × n  <CR><LF>
LxGetDataTs <meter>,<n>,<seq>, {...same 9-field record...} × n                 (history, §1.3/1.4)
LxGetDataTs <meter>,<seq>,<time>,<a>,<b>,<c>,<d>,<e>,<f>,<lon>,<lat>,          (§1.5 by-timestamp, one record/line)
```

| Field | Doc name | Probable LCR source (see §4) |
|---|---|---|
| n | "data length": number of records in this packet (`0` = none, and no records follow) | – |
| seq | packet serial 0..255 wrapping (§1.3/1.4). 1..100 in §1.5 | – |
| time | UNIX time | BOX RTC |
| a | "data content 1a (data flow)" | **UNVERIFIED:** current delivery quantity, likely Field #2 GrossQty_NE or #44 GrossCount_NE |
| b | "1b (totalizator/totalizer values)" | **UNVERIFIED:** which totalizer (possibly the same as #17) |
| c | "data flow 17" | Field #17 GrossTotal_WM |
| d | "data flow 18" | Field #18 NetTotal_WM |
| e | "data flow 100" | Field #100 PreviousGross (gross totalizer at start of last delivery) |
| f | "data flow 101" | Field #101 PreviousNet |
| lon, lat | GPS longitude, latitude | GPS |

The v1.5 history entry mentions adding "odometer of the meter", but no odometer field appears in the record (LCR odometer fields #10/#12 are "not in use"; **UNVERIFIED**). Number formatting (decimals) is **UNVERIFIED**.

### 2.5 Version history (verbatim content)
| Ver | Date | Change |
|---|---|---|
| 1.3 | 2023/6/2 | Add the totalizator values |
| 1.4 | 2023/7/2 | Add new commands (set/read the meter address of the corresponding port) |
| 1.5 | 2023/7/23 | Add meter pre-set gallon value; add odometer of the meter |
| 1.6 | 2023/8/12 | "Add the blue content" |
| 1.7 | 2023/8/26 | Time changed to UNIX time; identify real-time vs. history meter data reporting |
| 1.8 | 2023/10/24 | Modify history record time query; add GPS to real-time and history data; history query method; query meter working status, connection status, meter address number, etc. |
| 1.82 | (no date) | History data report starts with `LxGetDataTs`; **all commands end with ","** |
| 1.83 | 2023/11/11 | Bluetooth password set/modify; WiFi user/password set/modify |
| 1.84 | "20223/12/4" (typo, 2023/12/4) | Print command; set server IP/port |
| 1.85 | 2024/1/27 | APN configuration command |
| 1.86 | 2025/11/04 | Modify 4.2 set box connection mode, 4.6 RS485/RS232 switch, 4.7 set start, 7. TCS commands (these are the only blue-text sections in the current file) |

---

## 3. "Adding Pandabox Commands" (FleetPanda requirements)

This document does **not** describe a firmware procedure for adding commands. It is a wish-list of new ASCII commands, following the same `Cmd p0,p1,` / `LxCmd r,` convention, meant to replace the Liquid Controls **EZ-Command** PC tool.

1. **Periodic upload to server**
   - `SetUploadFreq <meter 1|2>,<1..3600>` → `LxSetUploadFreq <0 ok|1 fail>,`. Units are presumably seconds (**UNVERIFIED**). Note that "meter number (1 or 2)" here is really a port index.
   - Spontaneous upload line: `<timestamp>,<meter 1|2>,<totalizer>,<IMEI>`. It has no command word and no `Lx` prefix. Destination is presumably the TCP server (**UNVERIFIED**).
2. **Query EZ-Command settings:** `RdEzCmdStatus <port>,` → `LxRdEzCmdStatus <port>,<lcp_node 1..250>,<noflow 0..3600>,<ticket 0 yes|1 no|2 skip>,<preset clear|multiple|retain>`. The header lists data0..data3 but five fields are defined (CONFLICT C11).
3. **Set EZ-Command settings**
   - `SetNoFlow <port>,<0..3600>` → `LxSetNoFlow <0|1>`
   - `SetTicketReqd <port>,<1>` → `LxSetTicketReqd <0|1>,`. Only value 1 = "No" is requested; values 0/2 exist.
   - `SetPresetType <port>,<clear|multiple|retain>` → `LxSetPresetType <0|1>,`

Raw LCP frames quoted in the doc (the PandaBox host node is **0x14 = 20**). I verified every CRC below with the LCP algorithm in §4.3.

| Purpose | Frame | Decode |
|---|---|---|
| Get NoFlow timer, node 1 | `7E 7E 01 14 01 02 20 19 6C CD` | to=01 from=14 st=01 len=2, Get Field (20h) #25 (0x19) |
| Get NoFlow timer, node 2 | `7E 7E 02 14 01 02 20 19 B0 56` | |
| Response node 1 | `7E 7E 14 01 81 04 00 31 00 64 DA 7F 0D 0A` | st=81 (response, msgID 1); rc=00; devStatus=31h (state 30h "auxiliary", switch 01 "run"); value 0x0064 = 100 s (big-endian INTEGER) |
| Get Ticket Required, node 1/2 | `7E 7E 01 14 01 02 20 25 50 CD` / `7E 7E 02 14 01 02 20 25 8C 56` | Get Field #37 (0x25) |
| Response YES/NO/SKIP | `7E 7E 14 01 81 03 00 31 00 07 E0 0D 0A` (00=YES), `…31 01 06 E0 0D 0A` (01=NO), `…31 02 05 E0 0D 0A` (02=SKIP) | 1-byte list value |

- The trailing `0D 0A` after the CRC is **not part of LCP**. It is presumably added by the PandaBox when relaying or logging raw frames to the App (**UNVERIFIED**).
- The document says the author "could not generate the command to query the Preset Type". The query is Get Field #27 = **0x1B, which is the LCP `<esc>` character and must be escaped**. That is almost certainly why it failed. Correct frames, computed here:
  - Node 1: `7E 7E 01 14 01 02 20 1B 1B FA 66`
  - Node 2: `7E 7E 02 14 01 02 20 1B 1B 28 88`
  - Here the `1B 1B` is the escaped field number. Per spec, the CRC includes the escape byte and len stays 2.

Suggested LCP implementation of the new commands (inferred, **UNVERIFIED**):

| Command | LCP field | Encoding |
|---|---|---|
| SetNoFlow | Set Field (21h) #25 NoFlowTimer_DL | INTEGER, 2 bytes big-endian |
| SetTicketReqd | #37 TicketRequired_WM | 1 byte: 0 yes / 1 no / 2 skip |
| SetPresetType | #27 PresetType_DL | List 7: 0 Clear, 1 Multiple, 2 Retain, 3 Inventory, 4 Required |
| RdEzCmdStatus | Get Field #25, #37, #27, plus #102 LCRNode | |

Setting these fields may require the right security level or switch position (rc 35 "not set due to mode", rc 119/120).

---

## 4. LCR / LCP protocol (Liquid Controls Protocol, LCP02 message set)

### 4.1 Physical layer and addressing
- **LCP physical layer:** RS-232, RS-422, RS-485 or TCP/IP.
- **Baud rates:** 2400/4800/9600/19200/57600/115200. **LCR-II does not support 115200.**
- **Framing:** 8N1 (1 start bit, 8 data bits, no parity, 1 stop bit).
- **Masters:** any device can be master or slave. In practice there is one master; collision control is "via timing" and not implemented.
- **Node addresses:** `<to>` 0–255, where **0 = broadcast (no response)**. `<from>` 1–255. The LC reference host uses **255**; the PandaBox uses **0x14 (20)**, per the "Adding" doc frames. **Factory default LCR node = 250.** LCR-II setup "LCR #": 1–250, default 250. LCR.iQ "LCP Node Address": 1–250. LCR 600 "LCP Node Address" is set in System Setup 1. Field #102 LCRNode (INTEGER) holds the node.
- **Set Baud (7Ch)** index: 0 = 57600, 1 = 19200, 2 = 9600, 3 = 4800, 4 = 2400. **UNVERIFIED:** the baud the PandaBox uses and the LCR default RS-485 baud are **not stated** in these documents. Get them from the firmware or meter configs. LCR 600 List 51 (FSBaud) uses a different index order: 0 = 115200, 1 = 57600, 2 = 19200, 3 = 9600, 4 = 4800, 5 = 2400.

**Wiring (from the manuals):**

| Meter | Connector | Pins |
|---|---|---|
| LCR-II CPU 840405 | J2 "SERIAL 485" | **24 = 485+A, 25 = 485-B** |
| | J3 "TERMINAL" (RS-232) | 46 = +VP, 47 = RTS, 48 = TXD, 49 = RXD, 50 = CTS, 51 = GND |
| | J1 PRINTER (RS-232) | 26 = RTS, 27 = TXD, 28 = RXD, 29 = CTS, 30 = GND |

- LCR-II notes:
  - Jumper J10 must be in position B for external computing devices (A = lap pad / EZCommand / flashing).
  - The manual's wiring table lists: external device RS-232 red wire → J3-47; RS-485 red → J2-24, violet → J2-25; EZCommand RS-232 red → J3-46.
  - Spec sheet: RS-232 = EIA-232E, RS-485 = **SAE J1708** standard.
  - Lap-pad VT100 terminal settings: 9600 baud, 8 data bits, no parity.
- LCR.iQ register board: five serial ports COM0–COM4 (COM4 is reserved for I/O boards at 115200). Each port has Service, Type (RS232/RS485), Baud (2400…115200), Timeout (ms) and Retries. LCP can also run over **BT0 (Bluetooth)** or **WF0/WF1 (Wi-Fi)**.
  - J13 "RS232/RS485 PORT 0 (Printer)": 79 EARTH, 80 GND, 81 CTS, 82 RXD, 83 TXD, 84 RTS, 85 485-A-0, 86 485-B-0.
  - J14 "RS-232/RS-485 PORT 1": 87 EARTH, 88 GND, 89 CTS-1, 90 RXD-1, 91 TXD-1, 92 RTS-1, 93 485-A-1, 94 485-B-1.
  - I derived these pin mappings from the wiring diagram's text layer and the printer drawing. Verify against the drawing (**UNVERIFIED**).
  - **"Allow Pump & Print with LCP Host" = No** disables the register's Start key for 60 s after any LCP message, so deliveries must then be started by the host. Set it to Yes to keep local start.
- The LCR 600 manual contains no serial/baud/pinout text (only the LCP Node Address setting). **UNVERIFIED** for LCR 600 wiring.

### 4.2 LCP frame format
```
~~ <to> <from> <status> <len> <data0> ... <data[len-1]> <crc0 (LSB)> <crc1 (MSB)>
```
- `~~` = 0x7E 0x7E sync. It only ever appears at the start of a frame.
- **Escaping:** any 0x7E or 0x1B (`<esc>`) byte after the two leading `~` is sent as `1B 7E` or `1B 1B`. This applies to to/from/status/len/data and **also to the CRC bytes**.
  - The escape bytes are **not** counted in `<len>`.
  - The CRC **includes** escape bytes inserted in header/data, but **not** escapes inserted in the CRC itself.
  - The receiver de-escapes before processing.
- Max raw frame is 523 bytes, which is 263 bytes de-escaped, leaving 255 data bytes max.
- **Status byte:**

| Bit | Command (bit 7 = 0) | Response (bit 7 = 1) |
|---|---|---|
| 0 | Message ID: toggles 0/1 per new message so the slave can discard repeats | same (echo) |
| 1 | **Synchronization:** process as new regardless of bit 0. Set once per session; the next message clears it and toggles bit 0. | – |
| 2 | Check Request (poll a delayed request) | **Busy** (will answer later) |
| 3 | Abort Request | Request Aborted |
| 4 | reserved 0 | No Request Active |
| 5 | reserved 0 | Buffer Overrun |
| 6 | reserved 0 | Not Supported |
| 7 | 0 = command | 1 = response |

- **Data endianness:** all integers, longs and floats are **big-endian** (MSB first). Example: no-flow 180 is sent as `00 B4`.
  - **VOLUME** = signed 32-bit with implied decimals per Field #39 (List 14: 0 = hundredths, 1 = tenths, 2 = whole).
  - Floats are IEEE-754.

### 4.3 CRC-16 (LCP)
Algorithm: CRC = 0 initially. Feed every frame byte **including the two 0x7E** (equivalently, seed 0x7E7E and start at `<to>`). For each bit, MSB first:
1. `carry = crc & 0x8000`.
2. `crc = (crc << 1) | bit`.
3. If carry, `crc ^= 0x1021`.

There is no augmentation, no final XOR, and data bits are shifted into the LSB. This is **not** standard CRC-CCITT. Transmit order is the low byte, then the high byte.
```c
static void lcp_crc(uint16_t *crc, uint8_t b){ for(int i=7;i>=0;--i){ int x=(*crc&0x8000)!=0; *crc=(uint16_t)((*crc<<1)|((b>>i)&1)); if(x) *crc^=0x1021; } }
/* crc=0x7E7E; for to,from,status,len,data(+inserted ESCs): lcp_crc(&crc,b); send crc&0xFF then crc>>8 (escaped) */
```

**Test vectors (all verified).**

From the LCP doc (host 0xFF):
- Product ID, node 250, sync: `7E 7E FA FF 02 01 00 2F 34` → response `7E 7E FF FA 80 0D 00 02 "SR200b2.05" 00 1F 77`
- Node 1: `7E 7E 01 FF 02 01 00 8C 27` → response `…2E 36`
- Get Version: `7E 7E 01 FF 01 01 26 C9 17` → response `7E 7E FF 01 81 04 00 21 01 00 15 24` (devStatus 21h, version 1.00)

Extra frames I computed for PandaBox host 0x14 (the status toggle bit is illustrative):

| Purpose | Frame |
|---|---|
| Get Product ID node 1, sync | `7E 7E 01 14 02 01 00 C4 EB` |
| Issue Cmd 0 (start/resume) | `7E 7E 01 14 01 02 24 00 75 C9` |
| Issue Cmd 1 (pause) | `7E 7E 01 14 00 02 24 01 45 FA` |
| Issue Cmd 2 (end + ticket) | `7E 7E 01 14 01 02 24 02 77 C9` |
| Get Delivery Status | `7E 7E 01 14 00 01 28 AE CB` |
| Get Field #17 GrossTotal | `7E 7E 01 14 01 02 20 11 64 CD` |
| Set #25 NoFlow = 100 | `7E 7E 01 14 00 04 21 19 00 64 63 3A` |
| Set #37 = 1 | `7E 7E 01 14 00 03 21 25 01 C0 4C` |
| Set #27 = Clear | `7E 7E 01 14 00 03 21 1B 1B 00 D5 84` (escaped) |
| Set #5 GrossPreset = 12345 raw (1234.5 at tenths) | `7E 7E 01 14 00 06 21 05 00 00 30 39 01 D8` |

### 4.4 LCP message list (data portion; `msgID` is data[0])
| ID | Name | Request data | Response data |
|---|---|---|---|
| 00h | Get Product ID (generic) | – | rc, productID (LCR = 02h), ASCIIZ name/rev (≤ 16 incl. NUL) |
| 20h | Get Field Data | UB fieldNum | rc, devStatus, fieldData |
| 21h | Set Field Data | UB fieldNum, data | rc, devStatus |
| 22h | Print Text on LCR Printer | text | rc, devStatus |
| 23h | Get Machine Status | – | rc, devStatus, **prnStatus**, US delStatus, US delCode (7 bytes). **2 s delay if the printer is offline.** |
| 24h | **Issue Command** | UB command | rc, devStatus |
| 25h | Set Device Address | UB newAddr | rc, devStatus |
| 26h | Get Version Number | – | rc, devStatus, 2 B version (major, minor) |
| 27h | Get Security Level | – | rc, devStatus, SB security |
| 28h | **Get Delivery Status** | – | rc, devStatus, US delStatus, US delCode (6 bytes). No printer delay, so **prefer this for polling.** |
| 29h | Activate Pump & Print | – | rc |
| 2Ah | Get Transaction Record (SR266) | – | rc, F temp, UL customerID, SL saleNumber, V gross, V net, US status, UB compType, dateFormat, decimals, product, qtyUnits, tempScale, AZ[18] dateTime (total 0x2F) |
| 2Bh | Delete Transaction Record (SR266) | – | rc |
| 2Ch / 2Dh | Get First / Next Ticket Line (last delivery ticket) | – | rc, ASCIIZ line |
| 40h / 41h | Get / Set **Extended** Field Data | US fieldNum (+ data) | rc, devStatus (+ data). **Required for LCR.iQ fields ≥ 256**; also works for the classic fields. |
| 7Ah / 7Bh / 7Fh | Get LCR.iQ / LCR 600 / LCR-II Field Parameters | UB param (0 type, 1 width, 2 edit security), UB block (0,1,…) | rc, devStatus, n parameter bytes |
| 7Ch | Set Baud | UB baudIX | rc, devStatus |
| 7Dh / 7Eh | Check Request / Abort Request (legacy queued-request handling) | – | rc (+ queued response) |

- Messages that return devStatus: 20h–28h, 40h, 41h, 7Ah, 7Bh, 7Ch, 7Fh.
- **Issue Command codes:**

| Code | Action |
|---|---|
| 0 | Start/Resume delivery |
| 1 | Pause |
| 2 | End delivery and print ticket (if settings allow) |
| 3 | Auxiliary state |
| 4 | Shift state and print shift ticket |
| 5 | Calibration state (only after factory mode) |
| 6 | Print ticket for the current state (never ends a delivery, never prints during an active delivery) |

### 4.5 Status words

**Device status byte (devStatus):**
- Switch position = bits 0–2 (mask 0x07): 00 between, 01 Run, 02 Stop, 03 Print, 04 Shift-Print, 05 Calibrate.
  - "0x07" is also listed as "actual status not available (broadcast)" (CONFLICT C12, as extracted).
- 0x08 = RS-232 printer printing.
- Machine state = bits 4–6 (mask 0x70): 00 Run (delivery started, flow active), 10 Stop (started, no flow), 20 End-delivery (idle), 30 Auxiliary, 40 Shift, 50 Calibrate, 60 Wait-for-no-flow.
- 0x80 = error flag. Check the delivery, printer and hardware status for details.
- LCR.iQ has no red switch. It **emulates** the switch position: Run by default; Stop when paused; Print or Shift-Print while those tickets print from the soft keys; Calibrate when the W&M bolt is removed.

**Delivery Code word (delCode), the primary delivery-state source:**

| Bit | Meaning |
|---|---|
| 0x0001 | DEL_TICKET_PENDING (no new delivery until the ticket prints) |
| 0x0002 | shift ticket pending |
| 0x0004 | FLOW_ACTIVE |
| 0x0008 | DELIVERY_ACTIVE |
| 0x0010 | gross preset delivery |
| 0x0020 | net preset delivery |
| 0x0040 | stopped: gross preset reached |
| 0x0080 | stopped: net preset reached |
| 0x0100 | temperature compensated |
| 0x0200 | S1 closed (dwell) |
| 0x0400 | BEGIN_DELIVERY (starting) |
| 0x0800 | NEW_DELIVERY_QUEUED (Cmd 0 while switch not in Run) |
| 0x1000 | non-critical data access error |
| 0x2000 | configuration event |
| 0x4000 | calibration event |
| 0x8000 | transaction record saved to NV |

**Delivery Status word (delStatus):**

| Bit | Meaning |
|---|---|
| 0x0001 | code checksum fail |
| 0x0002 | temperature hardware error |
| 0x0004 | watchdog reset |
| 0x0008 | comp. factor error |
| 0x0010 | temperature out of comp. range |
| 0x0020 | meter calibration error |
| 0x0040 | too many pulser reversals |
| 0x0080 | preset reached |
| 0x0100 | **no-flow timeout ended delivery** |
| 0x0200 | STOP_REQUEST (Cmd 1 while active) |
| 0x0400 | DELIVERY_END_REQUEST (Cmd 2 or 6) |
| 0x0800 | power fail > 15 s |
| 0x1000 | preset field access error |
| 0x2000 | RS-232 lap pad disconnected |
| 0x4000 | ticket required but printer offline/busy (blocks start) |
| 0x8000 | critical data access error |

**Printer status byte (23h only):**

| Bit | Meaning |
|---|---|
| 0x01 | delivery ticket requested |
| 0x02 | shift ticket requested |
| 0x04 | diagnostic ticket requested |
| 0x08 | user print requested |
| 0x10 | out of paper |
| 0x20 | no print processor online |
| 0x40 | print processor/data error |
| 0x80 | printing in progress |

**Security levels:** 00 paused delivery; 01 out of cal, locked; 02 out of cal, unlocked; 03 in cal, no factory key; 04 in cal, factory key; 08 always editable; 09 never editable; 0x80 bit = delivery active.

**Return codes (rc), main ones:**

| rc | Meaning |
|---|---|
| 0 | OK |
| 32 | invalid parameter ID |
| 33 | invalid field |
| 34 | data incompatible with type |
| 35 | not set due to mode |
| 36 | invalid command |
| 37 | invalid device address |
| 38 | queued |
| 39 | no queued request |
| 40 | aborted |
| 41 | aborted but still processing |
| 42 | can no longer abort |
| 43 | invalid block |
| 44 | invalid baud |
| 48–53 | flash errors |
| 64/65 | ADC over/under |
| 67–69 | compensation errors |
| 96 | printer busy |
| 97 | print buffer overflow |
| 98 | printer status timeout |
| 112–127 | field/flash/preset/switch/state errors; notably **119** switch position disallows, **120** LCR state disallows, **121** ignored due to pending delivery ticket, **122** flash write-protected by switch |
| 200–223 | host-side driver errors |

### 4.6 Key LCR fields for PandaBox
The type IDs used in field-parameter queries are: TEXT = 0, INTEGER = 1 (s16), DATE = 2, TIME = 3, LONG = 4 (s32), VOLUME = 5, FFLOAT = 6, BYTE = 7, LIST600+n = 10, LIST+n = 50, UFLOAT+n = 80, SFLOAT+n = 90, LIST1000+n = 100.

| # (hex) | Name | Type | Use |
|---|---|---|---|
| 0 | ProductNumber_DL | LIST+0 | active product |
| 2 (02) | GrossQty_NE | VOLUME | current delivery gross (can go negative) |
| 3 | NetQty_NE | VOLUME | current delivery net |
| 4 | FlowRate_NE | VOLUME | flow rate |
| 5 | GrossPreset_PL | VOLUME | gross preset (PresetGross) |
| 6 | NetPreset_PL | VOLUME | net preset |
| 17 (11) | **GrossTotal_WM** | VOLUME | cumulative gross totalizer |
| 18 (12) | **NetTotal_WM** | VOLUME | cumulative net totalizer |
| 20/21 | Date_UL / Time_UL | DATE/TIME | meter clock |
| 22 | SaleNumber_WM | LONG | increments at delivery start |
| 23 | TicketNumber_WM | LONG | next ticket number |
| 25 (19) | **NoFlowTimer_DL** | INTEGER | seconds; 0 = disabled (multiple deliveries at one site) |
| 27 (1B!) | **PresetType_DL** | LIST+7 | 0 Clear, 1 Multiple, 2 Retain, 3 Inventory, 4 Required |
| 37 (25) | **TicketRequired_WM** | LIST+5 (values per List 49) | 0 required, 1 print if printer present, 2 never (CONFLICT C13) |
| 38 | QtyUnits_WM | LIST+4 | 0 Gal, 1 L, 2 m³, 3 lb, 4 kg, 5 bbl, 6 other |
| 39 | Decimals_WM | LIST+14 | 0 hundredths, 1 tenths, 2 whole: VOLUME scaling |
| 44/45 | GrossCount_NE / NetCount_NE | VOLUME | current delivery, never negative (matches display) |
| 64 | DiagnosticMessages_AE | LIST+16 | diagnostic index iterator |
| 85 | PresetsAllowed_DL | LIST+1 | none/gross/net/both |
| 89/90 | DeliveryStart_NE / DeliveryFinish_NE | TEXT | last delivery start/finish datetime |
| 92/93 | Gross/NetRemaining_NE | VOLUME | preset remaining |
| 100 (64) | **PreviousGross** | VOLUME | gross totalizer at start of last delivery |
| 101 (65) | **PreviousNet** | VOLUME | net totalizer at start of last delivery |
| 102 (66) | LCRNode | INTEGER | node address |
| 262–264 | LastGross/LastNet/LastDeliveryQty | VOLUME | LCR.iQ only (use 40h) |
| 261 | MultipleSiteDelivery | LIST+5 | LCR.iQ: next delivery with no-flow timer disabled |

- Useful derivation: last delivered gross = #17 − #100 (after delivery end).
- Fields 9, 10, 12, 112, 113, 122–125, 128, 130, 131 and the LCR 600 POS fields (192–240) are **unsupported on LCR.iQ**.
- Full field list 0–286 and list tables 0–107 are in the LCP PDF, pages 26–56.

### 4.7 Delivery state machine (LCR, as the PandaBox should drive and observe it)
Derived from the LCP doc. The PandaBox doc itself does not define a state machine (**UNVERIFIED** against firmware).
```
          (session start: Get Product ID 00h with Sync bit; confirm productID=02h)
IDLE  [state 0x20 End-delivery; delCode DELIVERY_ACTIVE=0]
  | App "Start m" -> Issue Command 0
  |   switch not in RUN (LCR/LCR-II/600) -> delCode NEW_DELIVERY_QUEUED 0x0800 (tell driver: move switch to RUN)
  |   rc 121 / delCode DEL_TICKET_PENDING 0x0001 -> must print last ticket first (Cmd 6 / Print)
  v
STARTING [delCode BEGIN_DELIVERY 0x0400]
  v
ACTIVE [delCode DELIVERY_ACTIVE 0x0008; FLOW_ACTIVE 0x0004 while flowing; state 0x00 Run / 0x10 Stop(no flow)]
  | App "Pause m" -> Cmd 1 -> delStatus STOP_REQUEST 0x0200, flow bit off  => PAUSED (security 0x00, LCR.iQ switch "Stop")
  |      PAUSED --App "Start m" (Cmd 0)--> ACTIVE
  | App "Stop m" -> Cmd 2 -> delStatus DELIVERY_END_REQUEST 0x0400 -> state 0x60 wait-for-no-flow
  | auto end: no-flow timer (#25) expires -> delStatus 0x0100 ; preset reached -> delStatus 0x0080 /
  |           delCode 0x0040/0x0080 (Clear/Retain end, Multiple pauses) ; error bits (delStatus) terminate
  v
ENDED [state 0x20; DELIVERY_ACTIVE=0; DEL_TICKET_PENDING=1 if ticket required & not yet printed]
  | ticket prints automatically if #37 allows, else App "Print m" -> Cmd 6
  | read #17/#18 (totals), #100/#101 (totals at start), #2/#3 or #44/#45, #22 sale no., #23 ticket no.; store record w/ time+GPS
  v
IDLE
```
Polling guidance (LCP doc): use **28h Get Delivery Status**, not 23h (which has a 2 s printer delay), and base decisions on the **delCode** bits, not the switch position.

---

## 5. TCS 3000 Remote Interface (V1.4.4.1002)

### 5.1 Physical layer
- The remote interface is **RS-232** on the TCS3000 "COMP." port. In multi-device chains, the devices share an RS-485 "daisy chain" with a TCS3000 Print Host, and the RI host connects via RS-232 to device ADDR 01. Byte order is big-endian ("transmitted … through RS232 interface").
- **UNVERIFIED:** baud rate, data bits and parity are **not stated** in the available pages. Footer pages 8–11 are missing from the PDF (probably connection or timing details). Obtain the full document or check the legacy firmware.
- **Timing:**
  - Use a response timeout of **≥ 1500 ms**. The print host and bridge can add up to 500 ms of delay.
  - When polling an idle chain, leave 1–1.5 s gaps (print-host time slots) between devices or cycles.
  - Perform data transfers back-to-back, then leave a gap.
  - Retry on error **0xFC "INTERFACE/BUS BUSY"**.
  - These restrictions apply only when a Print Host and multiple devices are used. A direct RS-232 link to a single TCS has no special restrictions.

### 5.2 Frame format
```
Downlink (HOST->TCS):  7E  DEST  SRC  FLAG  CMD  BTCNT  <DATA: N bytes>             CRC8     (N = 0..249)
Uplink   (TCS->HOST):  7E  DEST  SRC  RFLAG CMD  BTCNT  ERR  STAT  <DATA: N bytes>  CRC8     (BTCNT = N+2 = 0..249, counts ERR+STAT)
```
- Header is always 0x7E. **No byte-stuffing or escaping is documented** (**UNVERIFIED**: framing must rely on BTCNT).
- **Addresses:** HOST = 00; default TCS = 01; valid slaves 01…127; 250…255 reserved. The page-3 diagram also shows the annotation "01…254" (CONFLICT C14).
- **FLAG (downlink):** 0x20 = SET, 0x40 = GET. Other bits are don't-care. The 16-bit identifier = CMD<<8 | FLAG (e.g. 0x1F40).
- **RFLAG (uplink):** 0x01 CREC (command recognized), 0x02 OPOK (operation OK), 0x04 OK (all OK), 0x20/0x40 echo SET/GET, **0x80 ACK** (transfer acknowledge; 0 on CRC error or bad package).
- **ERR:** 0 = no error (table in 5.4).
- **STAT:** high nibble = delivery status (0 ERROR, 1 IDLE, 2 ACTIVE, 3 AIR DETECTED, 4 PAUSED, 5 STOPPED, 6 TICKET PENDING, 7 PRINTING, 8 MULTIPLE PENDING); low nibble = system status (0 ERROR, 1 IDLE, 2 W&M, 3 DELIVERY ACTIVE, 4 BUSY).
  - **CONFLICT C15:** the page-2 legend says STAT is "currently unused (always 0)", while page 4 defines it as above.
- **CRC:** **Dallas/Maxim DOW CRC-8** (1-Wire; poly x⁸+x⁵+x⁴+1, reflected 0x8C, init 0) over the whole frame from 0x7E through the last data byte.
  - Running the CRC over a received frame including its CRC byte yields 0.
  - The lookup table in the doc starts `0, 94, 188, 226, 97, 63, 221, 131…` and ends with 53. I verified it matches the reflected-0x8C computation.
  - `crc = table[crc ^ byte]`
- Multi-byte values are **big-endian**. Dbl = IEEE-754 64-bit. ENUM = U8 (0 = ERROR). Strings are ASCII.

Example frames (computed):

| Purpose | Frame |
|---|---|
| Get Delivery State | `7E 01 00 40 1F 00 83` |
| Get Gross Display | `7E 01 00 40 2B 00 95` |
| Configure Direct, product 1014 | `7E 01 00 20 37 02 03 F6 B4` |
| Configure Preset gross, 1014, 125.3 | `7E 01 00 20 38 0B 03 03 F6 40 5F 53 33 33 33 33 33 B5` |
| Begin Delivery (direct) | `7E 01 00 20 3C 01 01 14` |

### 5.3 Command list
Tx = host data bytes, Rx = returned data bytes after ERR/STAT. Sizes come from the text extraction; the byte-count ordering on a few pages is uncertain.

| CMD | Flag | Name | Tx | Rx / data |
|---|---|---|---|---|
| 0x02 | 20 | Save System State | 0 | ack |
| 0x03 | 40 | Get System Mode | 0 | ENUM SYSSTATE: 1 IDLE, 2 WM, 3 DLV_ACTIVE, 4 BUSY |
| 0x04 | 20/40 | Set/Get Time | 6 | "HHMMSS" |
| 0x05 | 20/40 | Set/Get Date | 8 | "YYYYMMDD" |
| 0x06 | 40 | Get System Metrics | 0 | ENUM RI_VOLMETRICS (1 GAL, 2 L, 3 UKG, 4 daL, 5 dL, 6 cL, 7 mL, 8 m3, 9 cm3, 10 bbl, 11 floz, 12 ft3, 13 in3, 14 NOUNIT) |
| 0x07 | 20/40 | Set/Get System Precision | 1 | ENUM RI_SYSDEC 1 = "1" … 4 = "1.111" (Get Rx listed as 6 bytes: **UNVERIFIED**) |
| 0x08 | 20/40 | Set/Get TruckID | ≤ 20 | string |
| 0x0C | 20/40 | Set/Get Current Product ID | 2 | U16 1001–9999 (Set also loads product data) |
| 0x0D | 20/40 | Set/Get Product Name | ≤ 20 | string |
| 0x0E | 40 | Get Product Parameter | 1 ENUM RI_PPAR_G | string ≤ 120 |
| 0x0F | 20 | Set Product Parameter | ENUM + string (≤ 120) | ack |
| 0x10 / 0x11 | 40 | Get Product GROSS / NET Totalizer | U16 ProdID (0 or none = current) | Dbl |
| 0x15/16/17 | 20/40 | Meter Make / Model / Serial | ≤ 20 | string |
| 0x19 | 40 | Get CalBolt Status | 0 | 1 REMOVED, 2 IN PLACE |
| 0x1A | 20/40 | Set/Get NoFlow Timer | 2 | U16 seconds |
| 0x1C | 40 | Get SysNet Totalizer | 0 | Dbl |
| 0x1D | 20/40 | Set/Get Next Ticket Nr | 8 | U64 |
| 0x1E | 40 | Get SysGross Totalizer | 0 | Dbl |
| 0x1F | 40 | **Get Delivery State** | 0 | ENUM DLVSTATE: 1 IDLE, 2 ACTIVE, 3 AIR, 4 PAUSED, 5 STOPPED, 6 TCKT_PENDING, 7 PRINTING, 8 MULTIPLE DELIVERY PENDING |
| 0x20 | 20/40 | Date Format | 1 | 1 MM/dd/yyyy, 2 yy/MM/dd, 3 MMMM dd, yyyy, 4 dd/mm/yyyy, 5 dd/mm/yy |
| 0x21 | 20/40 | Time Format | 1 | 1 AM/PM, 2 24 h |
| 0x22 | 40 | Get Temperature Metrics | 0 | "not functional" |
| 0x23 | 20/40 | Decimal Point | 1 | 1 ".", 2 "," |
| 0x27/0x28 | 20/40 | Set/Get CustID Value | ENUM(1–4) + string / ENUM | string ≤ 120 |
| 0x29/0x2A | 20/40 | Set/Get CustID Label | same | same |
| 0x2B | 40 | Get Gross Display | 0 | Dbl |
| 0x2C | 40 | Get Net Display | 0 | Dbl |
| 0x2D | 40 | Get Volume Display (auto net/gross) | 0 | Dbl |
| 0x30 | 20/40 | Set (calibrate) / Get Current Temp | ENUM unit + Dbl (9) | Dbl |
| 0x35 | 40 | Get Shift Status | 0 | 1 NOT_ACTIVE, 2 ACTIVE |
| 0x36 | 20 | Cancel Configured Delivery | 0 | ack |
| 0x37 | 20 | **Configure Direct Delivery** | U16 ProdID | ack |
| 0x38 | 20 | **Configure Preset Delivery** | 11: ENUM RI_DLVTYPE, U16 ProdID, Dbl amount | ack |
| 0x39 | 20 | Pause Delivery | 0 | ack |
| 0x3A | 20 | Resume Delivery | 0 | ack |
| 0x3B | 20 | Abort Delivery (immediate) | 0 | ack |
| 0x3C | 20 | **Begin Delivery** | 1: ENUM RI_DLVTYPE, **must match the configured type** | ack |
| 0x3D | 20 | End Delivery (mainly for direct; can interrupt a preset) | 0 | ack |
| 0x3E | 20 | Print Internal Ticket (also generates G-Ticket) | 0 | ack |
| 0x3F | 20 | Generate Internal Ticket (no print) | 0 | ack |
| 0x40 | 40 | Get Average Temperature | 0 | Dbl |
| 0x42 | 40 | Get Flowrate | 0 | Dbl |
| 0x43 | 20/40 | Reset No-Flow Timer / Get remaining | 0 | U32 ms |
| 0x4E | 40 | Get Product Status | U16 ProdID | 0 err/not cal, 1 calibrated, 2 error, 3 active, 4 active and selected |
| 0x4F | 40 | Get Printer State | 0 | 0 N/A, 1 OK, 2 PAPER, 3 PRINTER ERROR, 4 BUSY |
| 0x54 | 40 | Get Number of E-Tickets | ENUM RI_ETKT_TYPE (1 NONPRINTED, 2 PRINTED-not-transferred, 3 TRANSFERRED, 4 ALL_DLV, 5 SHIFT, 6 PROVER) | U32 (builds internal table) |
| 0x55/0x56/0x57 | 40 | First / Next / By-index E-Ticket Number | – / – / U32 | U64 |
| 0x59 | 20 | Clear Print Status of E-Ticket | U64 | ack |
| 0x5A | 40 | ROPEN E-Ticket | ENUM + U64 (9) | U32 number of blocks |
| 0x5B/0x5C/0x5D | 40 | Get E-Ticket First / Next / NR block | – / – / U32 | string ≤ 120 |
| 0x5E | 40 | RCLOSE E-Ticket (marks Printed and Transferred) | U8 DOW-CRC8 of all ticket data | ack |
| 0x6B | 40 | ROPEN LAST E-Ticket | 0 | U32 blocks |
| 0x6C–0x6F | 40 | FW Build / FW Version / SW Build / SW Version | 0 | string ≤ 20 |
| 0x81–0x87 | 20/40 | Product Unit Price / Tax1 / Tax2 value (Dbl), Tax text (≤ 20), Tax type (ENUM). Set only after delivery init. | | |
| 0x90 | 20 | Load Products Table | 0 | |
| 0x91 | 40 | Get Number of Products | 0 | U16 |
| 0x92/93/94 | 40 | First / Next / By-index Product ID | – / – / U16 | U16 ProdID + U8 status |
| 0x95/96/97/98 | 40 | Product Name First / Next / By-index / By-ID | | string ≤ 20 |
| 0x9E/9F/A0/A1/A2 | 20 | G-Ticket write (first line, next line, WOPEN (4 B), WRITE (≤ 120), WCLOSE (1 B checksum)) | | |
| 0xA4–0xA7 | 40 | G-Ticket read (ROPEN, next line, line NR (U16), first line) | | |
| 0xA8 | 20 | Print G-Ticket (acks immediately) | 0 | |
| 0xA9 | 20 | Clear Last Ticket Status | 0 | |
| 0xF0 | 20 | Force IDLE Mode (1 IDLE, 2 WMCONFIG). Not during delivery. | 1 | |
| 0xF6 / 0xF7 | 20 | Shutdown / Reboot. Two-step: send 1 (INITIATE), then 2 (no save) or 3 (save) within 10 s. | 1 | |

- RI_DLVTYPE: 1 DIRECT, 2 PRESET_NET_VOLUME, 3 PRESET_GRS_VOLUME, 4 PRESET_AUTO_VOLUME, 5 PRESET_NET_PRICE, 6 PRESET_GRS_PRICE.
  - **CONFLICT C16:** the 0x3C page swaps the descriptions of 5 and 6 ("5=PRESET_PRS_NET (Gross Price Preset)", "6=PRESET_PRS_GRS (Net Price Preset)").

### 5.4 Error codes (ERR byte)

| Code | Meaning |
|---|---|
| 0x00 | no error |
| 0x01 | command not recognized |
| 0x02–0x06 | internal data-store read/write errors |
| 0x07 | incorrect command format |
| 0x08 | repeat command requested as first command |
| 0x0B | invalid delivery state |
| 0x0F | timer not defined |
| 0x13 | wrong password |
| 0x14 | secondary enum out of range |
| 0x15 | bad boolean |
| 0x1F–0x22 | product table errors |
| 0x23 | no active shift |
| 0x24 | last ticket status not OK |
| 0x25 | non-printed tickets memory full |
| 0x30 | direct delivery init error |
| 0x31 | preset delivery init error |
| 0x32 | delivery not configured |
| 0x33 | start type ≠ configured type |
| 0x3B | preset not allowed |
| 0x3C | net preset on non-compensated product |
| 0x3D | price preset on zero price |
| 0x3E | preset ≤ 0 or too small |
| 0x3F | bad numeric value |
| 0x40 | bad data length |
| 0x41 | enum out of range |
| 0x42 | ProdID out of range (1000–9999) |
| 0x43 | ProdID not found or inactive |
| 0x44 | no active products |
| 0x45/0x46 | product load/config errors |
| 0x47 | totalizer not found |
| 0x48 | invalid time |
| 0x49 | invalid date |
| 0x50–0x58 | E-ticket / S-ticket errors (0x56 E-ticket CRC error) |
| 0x60 | not allowed in current mode |
| 0x61/0x62 | cal bolt not in place / in place |
| 0x65/0x66 | temperature sensor errors |
| 0x80–0x84 | definition errors |
| 0x8C–0x91 | USR_DB errors |
| 0x94–0x97 | CustID errors |
| 0xA0–0xA3 | system execution errors |
| 0xB4–0xBB | G-ticket errors |
| 0xF5–0xF7 | password / shutdown / reboot 10 s timeouts |
| 0xFA | extended error (code in STAT) |
| **0xFC** | **interface busy** (retry) |
| 0xFD | system busy |
| 0xFE | shutdown in progress |
| 0xFF | data integrity error |

### 5.5 TCS delivery flows (algorithm examples, pp. 87–91)
- **Direct:**
  1. 0x37 (ProdID) → 0x3C (data 1).
  2. Loop about 1 s: 0x2B Get Gross Display.
  3. Exit loop, then 0x3D End.
  4. Poll 0x1F until 6 (TCKT_PENDING).
  5. 0x3E Print.
  6. Poll 0x1F until 1 (IDLE).
- **Preset gross:**
  1. 0x38 `<3><ProdID><Dbl>` → 0x3C (data 3).
  2. Loop: 0x1F; while ≠ 6, read 0x2B.
  3. 0x3E Print.
  4. Poll 0x1F until 1.
- Also monitor 0x4F printer state.
- **Products:** 0x90 (optional) → 0x91 N → for i: 0x94 (ProdID + status), 0x97 (name).
- **E-tickets:** 0x54 type 2 → N → 0x57 i. Then 0x5A `<2><ticket>` → N blocks → 0x5D j. Compute DOW-CRC8 over the data → 0x5E CRC (marks transferred). 0x6B is a shortcut for the last ticket.

---

## 6. UNVERIFIED / CONFLICT register

| ID | Where | Issue |
|---|---|---|
| C1 | Tech spec | Ports "3×DB25, 1×DB9, Micro USB" vs. photo (1 DB25 PRINTER, Type-C, DB9, external WiFi/BT SMAs vs. "internal" antennas). File name v1.2 vs. footer Rev 1.0. |
| C2 | Proto §1.1 example | Full-width commas `，`. Inconsistent space/trailing comma in syntax lines (SetBtPwd, SetServerIp, BoxStorage, DeleteAll). |
| C3 | Proto §1.1 | Response header shows 2 fields, text defines 3 (meter, earliest, latest). |
| C4 | Proto §1.3 | Both GetData params labelled `<data0>`; `<data3>` duplicated in the response list; GPS fields of record n labelled "1g/1h". |
| C5 | Proto §1.3 vs §1.5 | Packet serial 0..255 wrap vs. 1..100. §1.5 lines have no record-count field. Terminator `LxGetDataTs <datan>,` undefined. |
| C6 | Proto §4.1 vs §4.2 | Mode 2 = "LCR analyze storage" (BoxStatus) vs. "cmd" (SetMode, v1.86); mode 3 "print" absent from BoxStatus. |
| C7 | Proto §3.1 vs §3.2 | BT name max 8 bytes (read) vs. 16 bytes (set). |
| C8 | Proto §6.3/§6.8 | Response words `LxModifytLcrNode` (typo) and `LxFindLcrNode` (for `GetLcrNode`) break the `Lx`+request rule. Check the firmware for actual strings. §6.1 result code text is missing "0:". |
| C9 | Proto §6.4 vs §7 | `PresetGross` has 2 params (port, gallons) for LCR vs. 3 (meter, product offset, amount) for TCS. |
| C10 | Proto §7 | "meter No. 2": PandaBox port vs. TCS slave address. "1009+5" product-ID base is unexplained. App-side TCS request words and value formats are undefined. `{0x3c,"LxStart"}//Start 2` meaning (DlvType?) unclear. |
| C11 | Adding doc | `LxRdEzCmdStatus` header lists data0..3 but defines 5 fields. `SetUploadFreq` calls the port "meter number". The spontaneous-upload line has no command word, and its destination and transport are unspecified. |
| C12 | LCP Machine Code table | Value 0x07 listed both as the switch mask and "status not available (broadcast)". The "0x?0"/"0x0?" placeholders are as printed. |
| C13 | LCP Field #37 | Type given as LIST+5 (Yes/No), but the description and List 49 define 0/1/2 (required / print-if-available / never). The Adding doc shows 0/1/2 on a real meter, so treat it as a 3-value list. |
| C14 | TCS p.3 | Valid slave addresses 01…127 (250…255 reserved) vs. a "01…254" annotation (probably the CMD range). |
| C15 | TCS p.2 vs p.4 | STAT "unused, always 0" vs. full delivery/system status nibble definition. |
| C16 | TCS 0x3C | RI_DLVTYPE 5/6 descriptions swapped relative to 0x38. |
| U1 | Transport | BT SPP vs. BLE GATT, UUIDs, MTU/packet split size: unspecified. Cloud protocol (raw TCP assumed; **MQTT never mentioned**), cloud message format, keep-alive and reconnection: unspecified. |
| U2 | Meter serial | PandaBox meter-port baud rate, RS-232/RS-485 default, whether `SetRs485` is per-port, and response format: unspecified. LCR default RS-485 baud not stated. TCS baud/parity not in the available pages (**TCS PDF missing pages 8–11**). |
| U3 | Record fields | Meaning of record fields "1a data flow" and "1b totalizer" (which LCR fields). Numeric formats (decimals, GPS format). Odometer (v1.5) absent from the record. `BoxStorage` units. `BoxInfo` date format "ddhhmm". |
| U4 | Proto §4.7 | `SetApp1`/`SetApp2` semantics and responses undefined. |
| U5 | App→LCP mapping | Start/Pause/Stop/Print → LCP Cmd 0/1/2/6, PresetGross → Field #5, ModifyLcrNode → 25h, GetLcrNode → 00h scan: all inferred, not documented. |
| U6 | "Adding" frames | Trailing `0D 0A` after the LCP CRC in the quoted responses (PandaBox relay/log artefact?). The PandaBox LCP host node is 0x14 (from the examples only). |
| U7 | Modes | "Continuous / non-continuous delivery" modes do not appear in any of these documents. Source them from the firmware or the FleetPanda App docs. |
| U8 | LCR.iQ | "Allow Pump & Print with LCP Host = No" locks the register Start key for 60 s after LCP traffic, which directly affects PandaBox polling behaviour. J13/J14 pin mapping I derived from the diagram text layer needs to be verified against the drawing. |
