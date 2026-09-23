# PandaBox (XBOX V2.5): Hardware and Firmware Master Note

This is a one-page-per-topic summary of everything Leo left us: schematic, PCB, BOM, protocol documents, partial source code, binaries, and a flash dump from a real board. The details and evidence are in `docs/reference/`:

| File | Contents |
|---|---|
| [01_Hardware_Reference.md](reference/01_Hardware_Reference.md) | Full 100-pin MCU pin map, connectors, power tree, BOM, risks |
| [02_Legacy_Firmware_Reference.md](reference/02_Legacy_Firmware_Reference.md) | Leo's software architecture, flash map, boot/OTA flow, binary analysis |
| [03_Protocol_Reference.md](reference/03_Protocol_Reference.md) | App/Cloud ASCII protocol, LCR LCP protocol, TCS 3000 protocol |
| [pcb_netlist.txt](reference/pcb_netlist.txt) | Pad-to-pad netlist extracted from the 4 PCB copper layers |

Items marked **UNVERIFIED** haven't been confirmed by two sources or on real hardware.

> **The schematic PDF is out of date.** It says "7 sheets" but has 6, two pages are from a 2013 "iDD-213G" design, and several fitted parts are missing from it (modem level shifter U603, BT PA U10, RS485 direction transistors Q13/Q14). The pin map below comes from the **PCB copper plus the BOM**, which were cross-checked against the schematic wherever both show a circuit.

---

## 1. System overview

```
                    ┌──────────── PandaBox (XBOX V2.5) ────────────┐
 Truck 12/24 V ─DB9─┤ 9–36 V in → 5 V buck → LDOs (each switchable)│
 LCR meter 1 ─DB25──┤ USART1 ─ RS232 / RS485 ┐                     │
 LCR meter 2 ─DB25──┤ USART2 ─ RS232 / RS485 ┤   GD32F305VCT6      │── ANT1 LTE  ┐ Quectel EC25AFA
 Printer    ─DB25──┤ pass-through + USART0 TX │   120 MHz, 256K/96K │── ANT2 GNSS ┘ (UART4)
 PC / debug ─DB9───┤ USART0 RS232 (CLI 115200) │   12 MHz HXTAL      │── ANT3 WiFi   FC20N (hosted by EC25)
 USB-C ────────────┤ 5 V in + USART0 TTL       │   32.768 kHz RTC    │── ANT4 BT     YC1021 + GSR2401 PA (UART3)
 SWD J100 ─────────┤ PA13/PA14                 │   32 MB SPI flash   │
                    └──────────────────────────────────────────────┘
        Driver App (phone) ←BT / WiFi→ PandaBox ←4G TCP→ Cloud server
```

**What the box does:** it reads fuel delivery data (gross/net quantity, totalizers, flow) from up to **two Liquid Controls LCR meters** (LCR II, LCR 600, LCR.iQ), or a TCS 3000. It adds GPS and time, stores each record in 32 MB of flash, and sends the data to the **Driver App** over Bluetooth or WiFi and to the **cloud** over 4G TCP. The App can also **start, pause, stop and print** deliveries and set presets through the box.

---

## 2. Hardware

### 2.1 Key parts

| Function | Part | Ref |
|---|---|---|
| MCU | GigaDevice **GD32F305VCT6**, LQFP100, Cortex-M4F, 120 MHz, 256 KB flash, 96 KB SRAM | U100 |
| Main crystal | **12 MHz** (not the library's 25 MHz default) | X101 |
| RTC crystal / backup | 32.768 kHz; CR1220 coin cell | X1, J7 |
| 4G LTE Cat 4 + GNSS | **Quectel EC25AFA** (North America). The schematic symbol says EC20. | U1 |
| WiFi | Quectel FC20N, controlled by the EC25 over SDIO (the MCU only switches its power) | U801 |
| Bluetooth | **Yichip YC1021** (BT 3.0 + BLE 5.0) + GSR2401 PA/LNA, 24 MHz crystal | U5, U10 |
| External flash | **GD25Q256E**, 32 MB SPI NOR (needs 4-byte addressing above 16 MB) | U103 |
| RS232 | BL13232 ×2: U9 for DB9/printer (always on), U504 for both LCR ports | U9, U504 |
| RS485 | SIT3088 ×2 (one per LCR port) | U104, U4 |
| Power | MP4560 buck (≈5.2 V), TJ4330-ADJ modem LDO, RT9193-33 LDOs ×4, RT9080-33 (WiFi) | |
| **Not fitted** | CAN, external watchdog, buzzer, relays, Li battery/charger, I2C EEPROM, native USB | |

### 2.2 MCU pin map (all used pins)

| Pin | GPIO | Signal | Direction / polarity | Peripheral |
|---|---|---|---|---|
| 1 | PE2 | Modem power (VGSM LDO enable) | Out, **high = on** | GPIO |
| 2 | PE3 | RS485 port 1 direction | Out, **high = receive**, low = transmit | GPIO |
| 3 | PE4 | RS485 port 2 direction | Out, **high = receive**, low = transmit | GPIO |
| 4 | PE5 | RS232 transceiver power (both LCR ports) | Out, high = on | GPIO |
| 5 | PE6 | RS485 transceiver power (both LCR ports) | Out, high = on | GPIO |
| 8/9 | PC14/PC15 | 32.768 kHz crystal | | LXTAL |
| 12/13 | OSCIN/OSCOUT | 12 MHz crystal | | HXTAL |
| 15 | PC0 | 12 V input sense (V = ADC × 11) | Analog | ADC IN10 |
| 16 | PC1 | Coin-cell sense (V = ADC × 2, only while PE1 = 1) | Analog | ADC IN11 |
| 25 | PA2 | LCR port 1 TX | | **USART1_TX** |
| 26 | PA3 | LCR port 1 RX | | **USART1_RX** |
| 29 | PA4 | SPI flash CS | Out, active-low | GPIO |
| 30–32 | PA5/PA6/PA7 | SPI flash SCK/MISO/MOSI | | **SPI0** |
| 33 | PC4 | LED WiFi (orange) | Out, **high = on** | GPIO |
| 34 | PC5 | LED BT (blue) | Out, high = on | GPIO |
| 35 | PB0 | LED GPS (green) | Out, high = on | GPIO |
| 36 | PB1 | LED PWR (red) | Out, high = on | GPIO |
| 42 | PE11 | 12 V present on DB9 pin 8 | In, **low = present** | GPIO |
| 51 | PB12 | 12 V present on LCR port 2 pin 13 | In, low = present | GPIO |
| 52 | PB13 | 12 V present on LCR port 1 pin 13 | In, low = present | GPIO |
| 54 | PB15 | Modem PWRKEY (through NPN) | Out, **high = key pressed** | GPIO |
| 55 | PD8 | LCR port 2 TX | | **USART2_TX (full remap)** |
| 56 | PD9 | LCR port 2 RX | | **USART2_RX (full remap)** |
| 63 | PC6 | LED LCP1 | Out, high = on | GPIO |
| 64 | PC7 | LED LCP2 | Out, high = on | GPIO |
| 68 | PA9 | USB-C VBUS present | In, high = present | GPIO |
| 69 | PA10 | Modem DTR (divider; see risks) | Out | GPIO |
| 72/76 | PA13/PA14 | SWDIO/SWCLK | | SWD |
| 78 | PC10 | Bluetooth RX (MCU TX) | | **UART3_TX** |
| 79 | PC11 | Bluetooth TX (MCU RX) | | **UART3_RX** |
| 80 | PC12 | Modem RXD (MCU TX, via U603) | | **UART4_TX** |
| 83 | PD2 | Modem TXD (MCU RX, via U603) | | **UART4_RX** |
| 85 | PD4 | Bluetooth reset | Out, **low = reset** (no pull, drive it early) | GPIO |
| 86 | PD5 | Bluetooth power | Out, **on by default** (pulled up); low = off | GPIO |
| 87 | PD6 | WiFi power | Out, high = on | GPIO |
| 92 | PB6 | Debug/DB9/printer TX | | **USART0_TX (remap)** |
| 93 | PB7 | Debug/DB9 RX | | **USART0_RX (remap)** |
| 95/96 | PB8/PB9 | I2C to YC1021 (pull-ups on BT_3.3V) | | I2C0 (remap), unused by the firmware |
| 98 | PE1 | Connect coin cell to the PC1 divider | Out, high = on (keep low) | GPIO |
| 94 | BOOT0 | 10 k to GND | | Boot from flash |

**Peripheral summary:**

| Peripheral | Use | Pins |
|---|---|---|
| USART0 | Debug CLI and bootloader upgrade, 115200 | PB6/PB7 (`gpio_pin_remap_config(GPIO_USART0_REMAP)`) |
| USART1 | LCR port 1, 19200 8N1 | PA2/PA3 |
| USART2 | LCR port 2, 19200 8N1 | PD8/PD9 (`GPIO_USART2_FULL_REMAP`) |
| UART3 | Bluetooth, 115200 | PC10/PC11 |
| UART4 | EC25 modem, 115200 | PC12/PD2 |
| SPI0 | GD25Q256 external flash | PA5–PA7, CS on PA4 |

Other peripherals used: ADC0, RTC, FWDGT, TIMER2. No DMA, CAN or USB.

### 2.3 External connectors

| Connector | Key pins |
|---|---|
| **J1 LCR PORT 1 / J2 LCR PORT 2** (DB25 F) | 14 = TX to meter, 15 = RX from meter (RS232 **or** RS485 A/B on the same pins), 11 = GND, 13 = +12 V from meter (power in and detect), 2/3/6/7/19/20 = meter printer-port pass-through to J5 |
| **J5 PRINTER** (DB25 M) | Pass-through of both meters' printer ports, plus the box's USART0 TX on pin 3; pin 13 = 12 V bus |
| **J3 DB9** (M) | 2 = RXD → USART0, 3 = TXD ← USART0, 5 = GND, **8 = +12 V power in** |
| **J8 USB-C** | 5 V power in, plus USART0 TTL on D+ (TX) / D- (RX). **It is not USB.** CC pins are floating, so only USB-A-to-C cables power it. |
| **J100 SWD** | 1 GND, 2 SWCLK, 3 SWDIO, 4 3.3 V. The schematic wrongly labels pin 1 as 5 V. |
| J18 | Micro-SIM, no card detect |

### 2.4 Power tree

```
12V_IN (DB9-8 | LCR1-13 | LCR2-13 | PRN-13, diode-ORed) → 2 A fuse → MP4560 buck → 5.2 V ─┐
USB-C VBUS ───────────────────────────────────────────────────────────────────────────────┤
                                                                         "VBAT" ≈ 4.7 V ──┤
  ├─ U300 3.3 V  VMCU   always on (MCU, flash, LEDs, U9)
  ├─ U401 VGSM   modem  EN = PE2   ⚠ output voltage UNVERIFIED (4.0 V or 5.3 V; EC25 max 4.3 V)
  ├─ U2   3.3 V  BT     EN = PD5 (default ON)
  ├─ U6   3.3 V  WiFi   EN = PD6
  ├─ U8   3.3 V  RS232  EN = PE5
  └─ U11  3.3 V  RS485  EN = PE6
```

There's no battery backup: the box turns off as soon as 12 V and USB are both gone. Only the RTC keeps running, on the coin cell.

### 2.5 Hardware rules the firmware must follow

1. **`HXTAL_VALUE = 12000000`** and the PLL set for 12 MHz → 120 MHz. The GD library's default CL settings assume 25 MHz and would **overclock the chip to 250 MHz**.
2. **Power only one of RS232 (PE5) or RS485 (PE6).** Both transceivers drive the same RX pin, and the choice applies to both LCR ports.
3. **Drive PE3/PE4 high (receive) before powering RS485.** Otherwise the drivers come up in transmit and hold the bus.
4. **Modem UART only works while the modem is powered.** The level shifter U603 runs from the modem's 1.8 V output.
5. **Modem power-on sequence:** PE2 = 1, wait for the supply to settle, then PB15 = 1 for ≥ 500 ms, then PB15 = 0. Leo's code uses 600 ms. There's no RESET or STATUS line to the MCU, so recover the modem by cycling PE2.
6. Drive PD4 (BT reset) to a defined level right after reset.
7. Program over SWD only. The ROM bootloader's UART pins (PA9/PA10) are used for other things.

---

## 3. Leo's legacy firmware

### 3.1 Architecture: super-loop, no RTOS

```c
main(): HAL_Init → SYS_Init → DRV_Init → APP_Init → CLI_Init
while(1){ HAL_FeedWatchDog(); HAL_DoEvent(); EVT_DoEvent(); MQ_ProcessMsg(); TMR_ProcessTimeout(); }
```

| Layer | What it does | Source provided? |
|---|---|---|
| HAL | GPIO, UART (RX split into packets by an idle gap: 20 ms modem, 50 ms LCR), RTC, SPI, ADC, flash, watchdog, jump-to-app | ❌ |
| SYS | Debug log (`DBG`), software timers (`TMR`), deferred events (`EVT`), message queue (`MQ`), reboot reasons | ❌ |
| DRV | LED patterns, GD25Q256 driver | ❌ |
| APP | `app_cfg` (settings), `app_ec20` (modem/GPS/WiFi), `app_lcr` (meter polling and App commands), `app_bt`, `update` (OTA) | ✅ cfg, ec20, lcr, update (partial); ❌ app.c, app_bt.c |
| CLI | Serial command shell on USART0 | ❌ |

Source files recovered: `main_init.c`, `app_cfg.c`, `app_ec20.c`, `app_lcr.c`, `update.c`, which is **roughly 30 % of the code**. Everything else has to be rewritten; the full list is in [02_Legacy_Firmware_Reference.md §5](reference/02_Legacy_Firmware_Reference.md).

### 3.2 Internal flash map (from the source, all binaries, and the board dump)

| Address | Size | Content |
|---|---|---|
| `0x08000000` | 30 KB | **Bootloader** (≈26 KB used). Clock runs directly on the 12 MHz crystal, no PLL. |
| `0x08007800` | 2 KB | **APP_INFO**: 24 bytes, a flag, length and checksum for each app slot |
| `0x08008000` | 100 KB | **APP1** slot (only 98 KB usable, because the last page is never erased) |
| `0x08021000` | 100 KB | **APP2** slot (A/B backup) |
| `0x0803A000` | 22 KB | Unused |
| `0x0803F800` | 2 KB | **User config** `T_BOX_PARAM` (116 bytes: WiFi SSID/password, BT name/password, server IP/port, APN, work mode, heartbeat time) |

**APP_INFO flags:**

| Value | ASCII | Meaning |
|---|---|---|
| `0x5A5A5A5A` | "ZZZZ" | Slot is valid; boot it |
| `0x62616E6B` | "bank" | Valid backup |
| `0x61707031` / `0x61707032` | "app1" / "app2" | Upgrade pending for that slot |

The checksum is a 32-bit sum of all bytes in the image.

### 3.3 Boot and upgrade flow

```
Reset → Bootloader (12 MHz) → prints "update" on USART0 → waits 200 ms for a PC tool
   ├─ PC tool sends "update app1|app2 <len> <sum>" → XMODEM-CRC (128 B) over USART0 → program slot
   ├─ APP_INFO says "app1"/"app2" (unfinished upgrade) → resume the UART upgrade
   └─ otherwise boot APP1 (flag OK), else APP2 (OK), else whichever is "bank"
          → check stack pointer → deinit UARTs → VTOR = slot, MSP = slot[0] → jump to slot[1]
App (120 MHz PLL) → OTA from the server: "Update APP<n>,<len>,<sum>" → XMODEM over 4G (or BT)
   → writes the *other* slot → checksum OK → new slot = OK, old slot = "bank" → reboot
```

### 3.4 The binaries

| File | What it really is |
|---|---|
| `Bootlaod_APP_V2.64.bin` | **Merged factory image**: bootloader + APP_INFO ("APP1 OK") + **app V2.62** (2024-06-30) at `0x08008000`. Flash it at `0x08000000`. |
| `X-Box_V2.87_APP1.bin` | App **V2.87** (2026-06-28), linked for `0x08008000` |
| `X-Box_V2.87_APP2.bin` | The same V2.87, linked for `0x08021000` |
| Board dump (`backup/original_flash_256K.bin`) | Bootloader, **app V2.77** (2025-10-31) in APP1, and a **clock-diagnostic test program** in APP2 (not Leo's app) |

**Board config read from the dump:**

| Setting | Value |
|---|---|
| Server | 34.121.179.10:8080 |
| WiFi SSID | "panda007," (the comma is stored as-is, a bug) |
| WiFi password | Default `123456789` |
| BT name | Default "PandaBrain" |

The **default server compiled into the binaries is 118.89.111.211:80**, which is not the FleetPanda server.

### 3.5 What each module does

- **Modem (UART4, EC25):** runs 15 AT commands at start-up:
  1. `ATE0`, `AT+QGMR`, `AT+CGSN` (IMEI), `AT+CIMI` (IMSI, also used to detect the SIM)
  2. WiFi hotspot `AT+QWSSID=TBOX_APP`, `AT+QWAUTH=5,4,"123456789"`, `AT+QWIFI=1`, `AT+QWTOCLIEN=1,5553`
  3. `AT+CSQ`, `AT+QIACT=1`, `AT+QICSGP=…` (APN; runs *after* QIACT, which is a bug)
  4. `AT+QIOPEN=1,0,"TCP",ip,port`
  5. GPS: `AT+QGPS=1`, `AT+QGPSCFG="nmeasrc",1`, `AT+QDATAFWDHEX=1`

  After start-up it sends a `Heart,…` heartbeat, checks signal (`AT+CSQ`) every 15 s, reads GPS with `AT+QGPSGNMEA="RMC"`, and reconnects when the socket drops.
- **LCR (USART1/2, 19200):** three work modes:
  - **Bridge:** transparent pass-through between the App and the meter.
  - **CMD:** the box polls each meter every 1 s for fields #2 GrossQty, #4 FlowRate, #17/#18 totalizers and #100/#101 previous totals. It stores a 64-byte record in external flash when the values change.
  - **Idle:** polling stopped.

  It relays the App's Start/Pause/Stop/Print/Preset commands to the meter as LCP frames.
- **External flash:** 16 MB ring buffer per port, 64-byte records `{time, 6 × LCR values, GPS}`, header in sector 0. Capacity is 262,080 records per port.
- **Bugs to fix in the rewrite:**
  - Port 2 records are never saved.
  - The Start/Stop address check is inverted.
  - The APN is set after activating the data connection.
  - SSID commas are not stripped.
  - `app_init` is duplicated.

---

## 4. Protocols

### 4.1 App / Cloud ↔ PandaBox: ASCII lines, protocol Rev 1.86

```
Request : <Cmd> p0,p1,...,\r\n        Response: Lx<Cmd> result/fields,\r\n     (0 = OK, 1 = fail)
```

There's no binary framing and no CRC. The same command set works over BT, WiFi (TCP port 5553 on the box's hotspot) and the 4G TCP socket, with about 40 commands:

| Group | Commands |
|---|---|
| History | `HisDataTime`, `BoxStorage`, `GetData m,mode,`, `GetDataEcho`, `GetDataTs m,t0,t1,`, `DeleteAll` |
| Configuration | `SetBtName`, `SetBtPwd`, `RdBtName`, `SetWifiName`, `SetWifiPwd`, `SetServerIp`, `SetServerPort`, `SetApn` |
| Box | `BoxStatus`, `BoxInfo`, `BoxReset`, `SetBoxTime`, `BoxTime`, `SetMode 1/2/3`, `SetRs485 0/1`, `SetApp1/2` |
| Meter control | `Start m`, `Pause m`, `Stop m`, `Print m`, `PresetGross port,gal,` |
| Meter setup | `SetPortLcrNode`, `RdPortLcrNode`, `ModifyLcrNode`, `GetLcrNode`, `RdRegister`, `SwitchState` |
| Added in V2.87 | `RdCalib`/`SetCalib`, `SetHeatTime`, `RdDiagnostics`, `PresetNet` |
| TCS 3000 | `DirectDelivery`, `PresetGross 2,5,125.3`, and the Get… totalizer commands |
| Requested by FleetPanda, not built | `SetUploadFreq`, `RdEzCmdStatus`, `SetNoFlow`, `SetTicketReqd`, `SetPresetType` |

The record line is `LxGetData m,n,seq,{time,a,b,c(#17),d(#18),e(#100),f(#101),lon,lat,}×n`.

### 4.2 Box ↔ LCR meter: Liquid Controls LCP

```
7E 7E | to | from(0x14 = PandaBox) | status | len | msgID data… | CRC_lo CRC_hi
```

- **Escaping:** 0x7E and 0x1B are escaped as `1B xx`, including in the CRC.
- **CRC:** CRC-16 with poly 0x1021, init 0x7E7E, data bits shifted into the LSB. It is **not** standard CCITT.
- **Numbers** are big-endian. Meter nodes run 1–250 (default 250).
- **Key messages:** `00` Product ID, `20/21` Get/Set Field, `24` Issue Command (0 start, 1 pause, 2 end + ticket, 6 print), `25` Set Address, `28` Delivery Status (**use this for polling**).
- **Gotcha:** field #27 (0x1B) must be escaped. That's why FleetPanda's "query Preset Type" attempt failed.

### 4.3 Box ↔ TCS 3000

```
7E | DEST | SRC | FLAG(0x20 set / 0x40 get) | CMD | BTCNT | data | CRC8 (Dallas 1-Wire)
```

The response adds ERR and STAT bytes. The delivery flow is: configure (0x37 direct / 0x38 preset) → begin 0x3C → poll 0x2B / 0x1F → end 0x3D → print 0x3E. The baud rate is not in the documents (pages 8–11 of the PDF are missing).

---

## 5. Development environment on this PC

| Tool | Version | Notes |
|---|---|---|
| **Arm GNU Toolchain** | 14.2.Rel1 | `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\`. Used by `Projects/01_LED_Sequence/build.sh`. |
| **Keil MDK** | 5.43a, **MDK-ARM Lite** edition, Arm Compiler 6.24 | `C:\Users\Velu\AppData\Local\Keil_v5`. ⚠ **Lite can only build programs up to 32 KB.** The full PandaBox app is about 80 KB, so it won't build in Lite; that needs an MDK license (Community edition is non-commercial only) or GCC. Keil's `TOOLS.INI` points to GCC **14.3** rel1, which isn't installed (14.2 is). |
| Keil packs | GigaDevice **GD32F30x_DFP 2.5.0 and 2.6.0** (includes `GD32F30x_CL.FLM`), ARM CMSIS 6.2/6.3 | Packs folder: `%LOCALAPPDATA%\Arm\Packs` |
| **SEGGER J-Link** | V7.94a (`C:\Program Files\SEGGER\JLink`), V9.58 (`…\JLink_V958`) | Probe connects fine: SWD 4 MHz, VTref 3.31 V, device `GD32F305VC` |
| GD32 firmware library | V3.0.3 | In the repo: `GD32F30x_Firmware_Library/` |
| Serial | **CH340 USB-serial on COM9**, Tera Term 4.106 | Use it for the USART0 debug CLI (115200) through the DB9 (via RS232) or a TTL adapter |
| Other | VS Code 1.138, Python 3.14, Git 2.55, GitHub CLI 2.101 | |

**Current state of the test board:** my LED test (`Projects/01_LED_Sequence`) is flashed at `0x08000000`, which overwrote the first 2 KB (the bootloader). The rest of the original flash (APP_INFO, app V2.77, config) is still there. To restore the board exactly as it was, program `backup/original_flash_256K.bin` at `0x08000000`.

---

## 6. Open items to confirm on hardware or with the team

| # | Item | How to resolve |
|---|---|---|
| 1 | **Modem supply voltage (VGSM)**: 4.0 V or 5.3 V? The EC25 is damaged above 4.3 V. | Measure U401's output with PE2 = 0 and PE2 = 1 before the modem firmware work. This matters most. |
| 2 | Which LCR port is physically J1 vs J2; LED colours vs pins | Look at the board and light each LED. The LED colours were confirmed in the LED test. |
| 3 | Bluetooth mode: SPP (Classic) or BLE? What does the App use? | Ask the App team. The binaries contain "YichipSmartSPP" and "YichipSmartLE", so both exist. |
| 4 | Cloud protocol: plain TCP line protocol, as the documents say, or something else? What format does the server at 34.121.179.10:8080 expect? | Ask the backend team |
| 5 | "Continuous / non-continuous delivery" modes aren't in any of Leo's documents | Check the Notion/Slack docs or ask the product team |
| 6 | RS232 or RS485 for the LCR ports in the field? The printer guide says RS232 at 19200; V2.87 defaults to RS485. | Check `SetRs485` usage and customer setups |
| 7 | Where did the clock-diagnostic program in APP2 come from? | Ask the team. The original APP2 contents may be lost (not important, since APP1 was the one booting). |
| 8 | TCS 3000 baud and parity (PDF pages 8–11 missing) | Get the full document from the vendor |
| 9 | PandaBox protocol inconsistencies (16 conflicts) | Listed in [03_Protocol_Reference.md §6](reference/03_Protocol_Reference.md) |

---

## 7. Proposed plan for the new firmware (step by step)

Each step is built, flashed and checked on the board before starting the next.

| Step | Deliverable | How it's checked |
|---|---|---|
| 0 ✅ | Toolchain, J-Link, 6-LED chase (`Projects/01_LED_Sequence`) | Done: pins sampled over SWD |
| 1 | **Base project**: correct 12 MHz → 120 MHz clock on the crystal, SysTick, pin map (`board.h`), all LDO/enable pins in safe default states, watchdog | Read the clock registers; measure the rails |
| 2 | **Debug UART + CLI** on USART0 (PB6/PB7 remap, 115200) with a `printf` log | Tera Term on COM9 |
| 3 | **Core services**: software timers, events, UART RX packet driver (idle-gap), ring buffers | Unit tests over the CLI |
| 4 | **Internal flash**: config page (`T_BOX_PARAM`-compatible) and the APP_INFO layout | Save and read back across reboots |
| 5 | **External SPI flash** GD25Q256 (ID, 4-byte addressing, erase/program/read) | JEDEC ID check, read-back test |
| 6 | **RTC** + coin cell + ADC (12 V and battery) | CLI readings compared with a multimeter |
| 7 | **LCR port driver**: RS232/RS485 switching, LCP framing, CRC, escaping | Connect a real meter: Get Product ID / Get Field #17 |
| 8 | **Meter polling + record storage** (fixing the port-2 bug) | Delivery on the meter produces records |
| 9 | **Bluetooth** (YC1021) + App ASCII protocol | Existing Driver App connects and reads data |
| 10 | **4G modem** (only after open item #1): power sequence, AT engine, TCP to the server, GPS | Data arrives at the server, GPS fix |
| 11 | **WiFi hotspot** (EC25 + FC20N) | Phone connects to TBOX_APP |
| 12 | **Bootloader + A/B OTA**, compatible with Leo's flash map | Upgrade over UART, then over 4G |
| 13 | TCS 3000 support, FleetPanda's new commands, delivery modes | Per customer |
