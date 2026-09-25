# V2.89 RE notes, part 2 (app.c, LED, BT runtime, EC20 runtime)

## 9. app.c
- `appInitTimeOut` (0x0800D8B0, fires 3 s after boot): LEDs 3,1,0,2 → mode 0 (off); `APPCFG_Init`; `APPEC20_Init`; `APPBT_Init`; `APPLCR_Init`; kill the init timer; `DBG I "---APP Init Ok!---"`.
- `appProcessSysMsg` (0x0800D928): no-op.

## 10. LED driver (drv/led.c)
- LED enum: 0 = WiFi PC4 (GPIO0), 1 = BT PC5 (GPIO2), 2 = GPS PB0 (GPIO1), 3 = PWR PB1 (GPIO3). High = on. LCR LEDs (PC6/PC7) are driven by app_lcr directly.
- 100 ms one-shot timer that restarts itself while any LED is animating.
- State per LED (12 B at 0x20002D1C): `mode`, `pattern`, `count` (toggles left, −1 = forever), `tick`.
- `LED_Play(led, mode)` (0x0800AFE0). Modes: 0 off; 1 on; 2 blink, toggle every 1.3 s; 3 every 0.9 s; 4 every 0.5 s; 5 every 0.1 s (fast); 6..9 = 1..4 fast flashes (0.1 s) separated by a 1.3 s pause, repeating; 10/11 = hold.
- `LED_Flash(led, n)` (0x0800AE38): mode 11, 2n fast toggles then the LED **ends ON**. Activity blink: BT RX → LED1 ×3, WiFi data → LED0 ×3, TCP recv → LED3 ×3, GPS RMC → LED2 ×3.
- Pattern period table (0x0801B46C): {0,0,13,9,5,1,0,64} ticks of 100 ms.
- When a pattern's count reaches 0 the LED is set ON.

## 11. app_bt.c runtime
- 500 ms resend timer (0x0800D840): `DBG D "appBtReSendCmdTimeOut %d"`; while counter < 3 re-post the BT event; counter++ (reset on each ack).
- **Init table upload** (event 0x0800DD1C, first RX cb 0x0800E074): table at 0x08018B9C = u16 LE total length (incl. these 2 bytes), then records `[n][n bytes]`; one record per event, written to UART3 **byte by byte**. Ack = HCI event with byte[0]=04, [1]=0E, [5]=0xFC, [6]=0 (if the RX chunk is exactly 8 bytes, skip its first byte). Ack → counter = 0, post next.
- **Config commands** after the table; step index 0..6 → command table 0x0801B32B = `03 04 0C 0D 00 01 02`. Frame `01 <cmd> <len> <data>`:
  - 0x03 BT name: cfg BtName if flag[2]==0x5A else "PandaBrain".
  - 0x04 BLE name: same name + "BLE"; if name length ≥ 13 → first 13 chars + "BLE".
  - 0x0C: `00`.
  - 0x0D PIN: 4 bytes cfg BtPwd if flag[3]==0x5A else "1234".
  - 0x00 BT address: u32 (UID word0 + 4) little-endian, then `11 25`. Log `BT_MAC=24:06:%.2X:%.2X:%.2X:%.2X` (I) with bytes b3,b2,b1,b0.
  - 0x01 BLE address: (UID word0 + 5) LE, `11 25`. Log `BLE_MAC=…` (I).
  - 0x02: `07`.
  Logs `"\n\r-----Send---%d---\n\r"` (D) + hex dump (D). Restart 500 ms resend timer.
  Ack `02 06 02 <cmd>` matching the current step → `DBG I "set ok %0x"`, step++; after step 7: `DBG I "BT Program is update ok!"`, RX cb → 0x0800D93C, stop resend timer.
- **Data RX (0x0800D93C):** stop resend timer; LED_Flash(BT, 3). Scans frames in the chunk:
  - `02 07 <n> data` → SPP data (mode = 0).
  - `02 08 <n> <hh hh> data(n−2)` → BLE data (mode = 1).
  - `02 06 02 05|09 …` → TX ack → call send-done cb (app_lcr history pacing).
  - `02 06 03 67 ?? <rssi>` → RSSI (signed) `DBG I "bt rssi %d"`. `02 06 02 67` → `"bt is disconnect!"`.
  - `02 00` → online, SPP, `"---bt is online---"` (D); `02 02` → online, BLE, `"---ble is online---"` (D); both → LED BT on.
  - `02 03` / `02 05` → offline, `"---bt is offline---"` (D), LED BT off.
  - else `DBG D "u8ReadLen00 = %d"`.
  Then if data was collected: reply-to-wifi = 0, send mode = BT(0), call the command parser (0x080125D8); if payload contains `Update` → BT OTA request (0x0800E1B0). Log `Rcv bt data: len = %d, %s` (hex, I) when < 128 B.
- BT OTA request (0x0800E1B0): tokens separated by ' ' or ','. `Update app1|app2 <len> <sum>`; refuse if that slot flag is 0x5A5A5A5A (`APPn is running, Can not update!`, E); else flag 'app1'/'app2', len, sum → erase/write APP_INFO, 50 ms, start update type 3 (BT).

## 12. app_ec20.c runtime
AT init table (0x0801B334, each expects "OK"): `ATE0, QGMR, CGSN, CIMI, QWSSID=TBOX_APP, QWAUTH=5,4,"123456789", QWIFI=1, QWTOCLIEN=1,5553, CSQ, QIACT=1, QICSGP=1,1,"MOBILE","","",1, QIOPEN, QGPS=1, QGPSCFG="nmeasrc",1, QDATAFWDHEX=1`.
- Send event (0x0800F804), index 0x20000074:
  - QIOPEN / QIACT skipped when no SIM (0x20000085 == 0): `"No sim, jump this cmd QIOPEN!"` / `"…QIACT!"` (D).
  - idx 0 → `"ATE0\r\n"`; QWSSID with cfg SSID if flag[0]==0x5A; QWAUTH with cfg pwd if flag[1]==0x5A; QIOPEN with cfg IP/port if IP valid else `"118.89.111.211",9090`; QICSGP always with cfg APN; others `"AT+%s\r\n"`.
  - Cmd type (0x20000089): CIMI → 8, CGSN → 16, CSQ → 6, QWTOCLIEN → 5.
  - `DBG I "Send to EC20, %s---%d"`.
  - After idx 15: stop cmd timer, RX cb → 0x0800E794; if TCP state 2: send `write(IMEI,<imei>)` via the server path, LED PWR on, start heartbeat. `DBG D "---EC25 Init End !---"`.
- Cmd timeout (0x0800F7E0, 10 s default): re-post send event while idx < 15.
- Init RX (0x0800F360): skip a leading 0x00; `DBG D "WIFI----%d----%s-----"`; stop cmd timer.
  - `RDY` → restart cmd timer, idx = 0.
  - `+CGSN` → IMEI = 15 chars after the first '"'; `IMEI:%s` (I); stored into cfg if different.
  - `+QIOPEN` → '0' → TCP state 2 `"tcp is connect!"`, start heartbeat.
  - idx < 15 and reply contains "OK": type 8 CIMI → 15 digits `CIMI:%s` (D), SIM present, start CSQ timer, LED PWR mode 3; type 16 CGSN → IMEI digits; type 6 CSQ → g_u8Csq. idx++, retry = 0, 100 ms, post send; timeout 10 s if the next cmd contains QWIFI; restart timer.
  - no match: retry < 2: `+CME ERROR` → no SIM; `ERROR` → timeout 2 s else 5 s; `DBG E "send time out!"`. retry ≥ 2 → `DBG E "EC20 Cmd fail %d!"`, idx++ (unless 0), post.
- UART4 TX-done (0x0800FC1C): busy flag = 0.
- Runtime RX (0x0800E794, 2..256 B):
  - `SEND OK` → wifi-send-done cb, send mode = 4G(2), flag 8A = 1.
  - `OK` while wifi client connected → wifi-send-done cb.
  - `CSQ` → 2 digits → g_u8Csq (D).
  - `QWIFIND` → char +9 after "QW" == '1' → WiFi client connected, LED WiFi on; else off.
  - `QDATAFWD` (16..254 B): LED_Flash(WiFi,3); reply-to-wifi = 1, wifi connected = 1; hex payload decoded; '~' frames split across packets; send mode = WiFi(1); parser called.
  - `+QIURC`: `recv` → LED_Flash(PWR,3), send `AT+QIRD=0,256\r\n`; `close` → TCP state 4, `"TCP is close!"`, LED PWR mode 3, CSQ timer → 1 s.
  - `+QIRD: <n>`: n == 0 → closed; else payload: send mode = 4G; `Update Imei …i,<15 digits>` → `imei is ok!`, reply `LxUpdate 0`; `Update App …pp<n>,<len>,<sum>` → needs imei-ok (`please send imei!`), `Update APP%d,%d,%d` (D), APP_INFO check, stop heartbeat, write APP_INFO, 100 ms, start OMS update (type 1); else command parser. Then TCP state 2, restart heartbeat.
  - `+QIOPEN` → '0' → connected (state 2); else state 4 `tcp is close!`.
  - `+QISTATE` → state digit after the 5th comma; if ≠ 2 → state 4, LED PWR 3.
  - `GPGGA` (> 50 B) and `GPRMC` (> 50 B, LED_Flash(GPS,3)) fixed-offset parse → `T_GPS_DATA` at 0x2000265C: +0 lat (×2 |1 if N), +4 lon (×2 |1 if W), +8 lat frac, +12 lon frac, +16 time.
- CSQ timer (0x0800E3C4, 15 s): if UART4 idle: TCP state 4 or > 5 → reconnect (`AT+QICLOSE=0` first, then `AT+QIOPEN…`), `reconect server , %s` (I). Else `AT+CSQ`.
- Heartbeat timer (0x0800E5B8, period = HeatTime): only in TCP state 2; waits for UART4 idle; alternates `AT+QISTATE?` (`---check server status---` I) and `Heart,<rtc>,<p1 v0>,<p1 v2>,<p1 v4>,<p2 v0>,<p2 v2>,<p2 v4>` (`%d.%01d`) via the server path.
- GPS event (0x0800E558): if UART4 idle send `AT+QGPSGNMEA="RMC"\r\n` else re-post.
