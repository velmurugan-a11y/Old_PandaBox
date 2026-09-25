# V2.89 reverse-engineering notes (working file)

Source: `From Leo/05_Firmware_Binaries/X-Box_V2.89_APP1.bin`, disassembled with `re/annotate.py`.
Addresses are absolute (APP1 link address). "fn_X" = function at X.
These notes supersede `docs/reference/02_Legacy_Firmware_Reference.md` wherever the two disagree (that doc was written from the 2023 source + V2.87).

## 0. Toolchain facts
- Built with Keil armcc: `__main` at 0x08008150 → scatter-load 0x08008744 → `main` 0x08016254. `SystemInit` 0x0800B714.
- Initial SP 0x20003950. Config struct `T_BOX_PARAM` in RAM at 0x2000295C.

## 1. main (0x08016254)
```
HAL_Init 0800A854; SYS_Init 0800B61C; DRV_Init 0800A07C; APP_Init 08009D08; CLI_Init 08009E24
loop: 0800A6D4 (feed FWDGT) ; 0800A6CC (HAL_DoEvent) ; 0800A150 (EVT) ; 0800B164 (MQ) ; 0800B8E4 (TMR)
```
HAL_Init: MCU_Init 0800B09A, HWEVT 0800AB3C, GPIO 0800A614, UART 0800BC00, RTC 0800B3D0, IT 0800AC68, SPI 0800B5E4, ADC 08008968, FLASH (nop) 0800A530, WDG 0800C642, ASSERT uart = USART0.

## 2. HAL enums
GPIO output enum (HAL_GpioSet/Reset/Get index → pin), table at 0x08015D02:

| idx | pin | use |
|---|---|---|
| 0 | PC4 | LED WiFi |
| 1 | PB0 | LED GPS |
| 2 | PC5 | LED BT |
| 3 | PB1 | LED PWR |
| 4 | PC6 | LED LCR1 |
| 5 | PC7 | LED LCR2 |
| 6 | PE2 | GSM power (VGSM) |
| 7 | PB15 | GSM PWRKEY |
| 8 | PD5 | BT enable |
| 9 | PD4 | BT reset (high = run) |
| 10 | PD6 | WiFi enable |
| 11 | PA4 | SPI flash CS |
| 12 | PE3 | RS485 dir port1 |
| 13 | PE4 | RS485 dir port2 |

GPIO input enum: 0 → PB13 (port1 12 V detect), 1 → PB12 (port2). ADC enum: 0 → ch10 (PC0, 12 V), 1 → ch11 (PC1).
UART enum = peripheral index (0 USART0 … 4 UART4). UART config: baud ∈ {2400,4800,9600,19200,38400,115200}, parity enum (0 none, 1 → 0x600 odd?, 2 → 0x400 even), stop (0 → 1 bit, 1 → 0x1000 = 2 bits).

GPIO init (0x08015998, then 0x0801594C):
- Out PP 50 MHz: PA10, PD6, PD5, PD0, PD4, PE2, PB15, PB1, PB0, PC5, PC4, PC6, PC7, PE3, PE4, PE5, PE6. All left **low** (ODR reset) except **PE5 = 1** (RS232 on), PE6 = 0.
- In floating: PB12, PB13. Analog: PC0, PC1.
- Note: BT (PD5) is **off** and in reset (PD4) until APPBT_Init.

## 3. DBG / ASSERT
- `DBG(level, file, line, fmt, …)` = fn_08009EBC; header `"\r\n%c,%d,%s,%04d:"` → `\r\nD,<ms>,<basename>,<line>:<text>`. Levels: 0 = 'D' (debug), 1 = 'I' (info) … (DEBUG/INFO/WARNING/ERROR/ASSERT). Basename = after last '\\'.
- ASSERT fn_08009D9C: prints `"\r\nASSERT: %s,%04d"` (file basename, line) on the debug UART then **hangs** (watchdog resets).

## 4. APP_Init (0x08009D08, app.c)
1. `SYS_SetSysMsgCallback(fn_0800D928)`.
2. `DBG I "---APP Init Start!---"`.
3. LED 3, 1, 0, 2 → mode 5 (LED driver enum, see §7).
4. One-shot timer 3000 ms → fn_0800D8B0 (module inits), started.

## 5. app_bt.c
**APPBT_Init (0x080089E8):** GPIO9 (PD4 BT reset) = 1, 10 ms, GPIO8 (PD5 BT power) = 1, 100 ms; UART3 115200 8N1, pack interval **20 ms**, RX cb fn_0800E074; event → fn_0800DD1C; repeat timer 500 ms → fn_0800D840 (started); post event; table length = u16 LE at 0x08018B9C (0x278F = 10127), state = 2; `DBG I "BT Program is update......"`.

**APPBT_SendDataToBt(buf,len) (0x08008ADC):** only if online flag (0x20000058) and len ≤ 500. Appends `\r\n` unless last char is `;`.
- SPP mode (0x2000006E == 0): chunks of 127: `01 05 <n> data`.
- BLE mode: chunks of 125: `01 09 <n+2> 2A 00 data`.
- All chunks are concatenated and written to UART3 **in one write** (no per-chunk ack wait).

**RSSI request (0x08008C94):** if online, send `01 67 00` to UART3, `DBG I "get blue rssi!"`.

**APPBT_SetBtName (0x08008CE8):** len ≤ 16 else err 3; strips one trailing ','; copies into cfg BtName (+0x24), flag[2] = 0x5A, save.
**APPBT_SetBtPwd (0x08008D90):** len must be exactly 4 else 3; stores 4 bytes at cfg +0x34, flag[3] = 0x5A, save.
Callbacks: fn_08008DE0 sets data-received cb (0x20000064) = fn_080125D8 (app command parser); fn_08008DEC sets send-done cb (0x20000068) = fn_0800FC50.

## 6. app_cfg.c
- RAM copy 0x2000295C (116 B). `APPCFG_GetBoxParam(out)` (0x08008DF8): strips `\r\n` inside SSID (+4), WiFi pwd (+0x14), BT name (+0x24) when their flag is 0x5A; copies out.
- `APPCFG_SetBoxParam(in)` (0x08008FEC): copy in, same CR/LF strip, write flash page (offset 0, 116 B).
- `APPCFG_Init` (0x08008E94): read 116 B, print SSID/PWD/BT NAME/BT PWD/HeatTime (level I) for flags == 0x5A; HeatTime default 1000 when flag[0x6F] ≠ 0x5A.
- `APPCFG_ReDefaultBoxParam` (0x08008F9C): zero, IP "118.89.111.211" at +0x47, byte +0x55 = 0, port "80" at +0x56, write.
- Send mode byte 0x200000C3 (0 = BT …): setter 0x08009088, getter 0x08008E88.

## 7. app_ec20.c (first part)
**APPEC20_Init (0x080090F0):** GPIO10 (PD6 WiFi) = 1, GPIO6 (PE2 GSM) = 1, 100 ms, GPIO7 (PWRKEY) = 1, 600 ms, = 0. UART4 115200 8N1, pack **20 ms**, callbacks RX fn_0800F360 / 2nd fn_0800FC1C. Events: 0x20000078 → fn_0800F804 (send cmd), 0x20000079 → fn_0800E558 (GPS). One-shot 10000 ms → fn_0800F7E0 (cmd timeout, id 0x20000075). Repeat timer period = cfg HeatTime → fn_0800E5B8 (heartbeat, id 0x20000076). Repeat 15000 ms → fn_0800E3C4 (CSQ, id 0x20000077). LED 3 → mode 5, LED 2 → mode 1. `DBG D "EC20_Init OK!"`.

**Send to server (0x0800923C):** payload copied; unless it starts with `wr`: append `,` if last char is not `,`/`;`, then 15-byte IMEI (0x20002670), then `\r\n`. Sends `AT+QISEND=0,<len>\r\n`, waits 5 ms, sends payload (no wait for `>`). Logs `send to wifi len = …` (sic) at I.
**Send to WiFi (0x0800934C):** requires 0x2000007A (reply-to-wifi flag) and 0x2000007B (wifi client connected), else returns 122. Appends `\r\n` unless last char `;`. Sends `AT+QDATAFWD=0,3,<2*len>,"<HEX>",1\r\n` after waiting ≤100 ms for UART4 busy flag 0x20000087 to clear; sets busy; clears 0x2000007A.
**GPS request (0x080094BC):** if init index (0x20000074) > 8 post GPS event.
**Set4GApn (0x080094DC):** len ≤ 15 else 3; strip trailing ','; store at +0x5E; save (no flag).
**SetHeatTime (0x08009544):** len ≤ 5 else 3; value must be 100 < v < 90000 else 5; store +0x70, flag +0x6F = 0x5A, save, restart heartbeat timer with the new period.
**SetServerIp (0x080095FC):** 10 ≤ len ≤ 20 else 4; strip trailing ','; store at +0x47 (NUL-terminated); save.
**SetServerPort (0x080096C4):** 2 ≤ len ≤ 6 else 4; strip ','; store at +0x56; save.
**SetWifiPassword (0x0800978C):** 8 ≤ len ≤ 15 else 3; store at +0x14 (no comma strip); flag[1] = 0x5A; save.
**SetWifiSsid (0x080097E2):** len ≤ 15 else 3; store at +4 (no comma strip → the `panda007,` bug); flag[0] = 0x5A; save.
Setter 0x080095E4: data cb (0x2000007C) = fn_080125D8. 0x080095F0: wifi send-done cb (0x20000080) = fn_080144A8.
fn_0800983C(port, src): copies 3 u32 (stride 8) into 0x20002690[port*3] (GPS snapshot per port?).

## 8. app_lcr.c init (0x08009868)
- USART1 and USART2: 19200 8N1, pack **50 ms**, callbacks RX fn_0800FCB8 (both), tx-done fn_0800FC28 / fn_0800FC3C.
- Events: 0x20000097 → fn_080138E0 (send next poll cmd), 0x20000098 → fn_080134C0 (save record), 0x20000099 → fn_08013A9C (send history), 0x2000009A → fn_08013250 (posted at end of init).
- Timers: repeat 1000 ms fn_0801443C (poll tick, id 0x90); one-shot **200 ms** fn_080118B8 (field reply timeout, id 0x91); one-shot **200 ms** fn_0801429C (control-cmd retry, id 0x94); repeat 1000 ms fn_0801213C (link check, id 0x95, **started**); one-shot **60000 ms** fn_08014028 (id 0x96, **started**); one-shot 1000 ms fn_080134B4 (reboot, id 0x92).
- **External flash header (V2.89 format, differs from older docs):** 24 bytes read from address 0 into 0x20002898:
  `+0 u8 DevNum0, +1 u8 DevNum1, +4 u32 RdAddr, +8 u32 StartAddr, +12 u32 EndAddr, +16 u32 DataCnt, +20 u32 magic 0xA5A5A5A5`.
  One ring for **both ports over the whole chip**; max DataCnt 0x7FFC0 (524 224 = (32 MB − 4 KB)/64). Records at 0x1000…
  Boot: magic wrong → zero the RAM header. StartAddr = 0xFFFFFFFF → zero Start/End/Cnt/Rd. Else if DataCnt < max: scan the sector at (EndAddr+4096)&~0xFFF … actually scans 64 records of the sector after EndAddr rounded, stops at 0xFFFFFFFF time; keeps max timestamp; EndAddr = found − 4096 …; recompute DataCnt. WrAddr (0x20002790+0x104) = EndAddr.
  Prints `WrAddr=%x EndAddr=%x StartAddr=%x DevNum%d--%d DataCnt=%d RdAddr=%x` (D), 20 ms, `APPLCR_Init OK!` (I).
- Registers command-parser fn_080125D8 for EC20 + BT, BT send-done fn_0800FC50, WiFi send-done fn_080144A8.
- RTC: `SysTime Second = %d` (D); if newest record time > RTC, set RTC to it.
- Sends `TEST LCR1!` on USART1, 10 ms, `TEST LCR2!` on USART2.
- `ADC = %d!` (I) with ADC ch 0 (12 V raw), 10 ms, **sends LxBoxInfo** (fn_08010E10) at boot, clears 120 B at 0x200028E4, posts event 0x9A.
