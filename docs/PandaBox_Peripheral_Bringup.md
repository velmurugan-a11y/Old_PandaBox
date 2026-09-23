# PandaBox (XBOX V2.5): Peripherals, Pins and Handshake Plan

This is the working reference for the **hardware bring-up / handshake firmware**. For each part on the board it lists how it connects to the GD32F305VCT6 (pins with LQFP100 pin numbers), what powers and controls it, its protocol settings, and **the smallest exchange that proves it is alive** ("handshake"). Each handshake has a pass condition.

Sources: [PandaBox_Master_Note.md](PandaBox_Master_Note.md) and `docs/reference/` (the pin map comes from the PCB copper and the BOM, because the schematic is out of date), the datasheets in `From Leo/02_Datasheets`, and Leo's V2.87 binary. Items marked **UNVERIFIED** still need confirming on hardware.

---

## 0. Overview: every peripheral on the MCU

| # | Device | Part (ref) | MCU peripheral | Data pins (LQFP100 pin) | Control / power pins | Protocol |
|---|---|---|---|---|---|---|
| 1 | Main clock | 12 MHz crystal (X101) | HXTAL → PLL | OSCIN 12, OSCOUT 13 | – | 12 MHz → 120 MHz |
| 2 | RTC clock + backup | 32.768 kHz (X1), CR1220 (J7) | LXTAL, RTC, BKP | PC14 8, PC15 9, VBAT 6 | – | – |
| 3 | Status LEDs ×6 | BL-C34S-C + 2× red, S9014 drivers | GPIO | PB1 36, PB0 35, PC4 33, PC5 34, PC6 63, PC7 64 | – | Active-high |
| 4 | Debug / DB9 / printer serial | BL13232 (U9) | **USART0 (remap)** | TX PB6 92, RX PB7 93 | Always powered | RS232 (DB9) / TTL (USB-C), 115200 8N1 |
| 5 | LCR port 1 serial | BL13232 ch2 (U504) / SIT3088 (U104) | **USART1** | TX PA2 25, RX PA3 26 | RS232 power PE5 4, RS485 power PE6 5, RS485 direction PE3 2 | RS232 or RS485, 19200 8N1, LCP |
| 6 | LCR port 2 serial | BL13232 ch1 (U504) / SIT3088 (U4) | **USART2 (full remap)** | TX PD8 55, RX PD9 56 | PE5 4, PE6 5, RS485 direction PE4 3 | Same as port 1 |
| 7 | External flash 32 MB | GD25Q256E (U103) | **SPI0** | SCK PA5 30, MISO PA6 31, MOSI PA7 32 | CS PA4 29 (active-low) | SPI mode 0/3, 30 MHz max (MCU limit) |
| 8 | Bluetooth | Yichip YC1021 (U5) + GSR2401 PA (U10) | **UART3** | MCU TX PC10 78 → BT, MCU RX PC11 79 ← BT | Reset PD4 85 (low = reset), power PD5 86 (default ON) | UART 115200, Yichip HCI-style frames |
| 9 | BT side-bus (unused) | YC1021 I2C | I2C0 (remap) | SCL PB8 95, SDA PB9 96 | Pull-ups on the BT power rail | I2C |
| 10 | 4G LTE + GNSS | Quectel EC25AFA (U1) | **UART4** | MCU TX PC12 80 → modem RXD, MCU RX PD2 83 ← modem TXD (through U603 level shifter) | Power PE2 1, PWRKEY PB15 54 (high = press), DTR PA10 69 | AT commands, 115200 8N1 |
| 11 | WiFi | Quectel FC20N (U801), hosted by the EC25 over SDIO | (through EC25 AT) | – | Power PD6 87 | EC25 `AT+QWIFI…` |
| 12 | Supply monitor | 12 V divider ÷11 | **ADC0 IN10** | PC0 15 | – | Analog |
| 13 | Coin-cell monitor | ÷2 divider + P-MOS switch | **ADC0 IN11** | PC1 16 | Enable PE1 98 (high = connect) | Analog |
| 14 | 12 V present, LCR port 1 | Q10 | GPIO in | PB13 52 | – | Low = 12 V present |
| 15 | 12 V present, LCR port 2 | Q9 | GPIO in | PB12 51 | – | Low = 12 V present |
| 16 | 12 V present, DB9 | Q11 | GPIO in | PE11 42 | – | Low = 12 V present |
| 17 | USB-C 5 V present | R13/R14 divider | GPIO in | PA9 68 | – | High = present |
| 18 | Watchdog | internal | FWDGT | – | – | – |
| 19 | Debug / programming | J100 | SWD | SWDIO PA13 72, SWCLK PA14 76 | NRST 14 (test pad only) | SWD |

**Not on the board:** CAN, external watchdog, buzzer, relays, battery charger, I2C EEPROM, native USB, SD card.

**Free pins** (for a debug scope output or rework): PA0, PA8, PA15, PB3–PB5, PB10, PB11, PB14, PC2, PC3, PC9, PC13, PD7, PD10–PD15, PE0, PE7–PE10, PE12–PE15.

---

## 1. Safe power-on state: GPIO init, before anything else

The bring-up firmware must put every control pin into this state in the first lines of `main()`:

| Pin | Pin # | Mode | Initial level | Why |
|---|---|---|---|---|
| PE2 modem power | 1 | Push-pull out | **0** (off) | Keep the modem off until VGSM is measured (§10) |
| PB15 PWRKEY | 54 | Push-pull out | 0 (released) | |
| PA10 modem DTR | 69 | Push-pull out | 0 | High gives about 2.24 V into a 1.8 V pin |
| PD6 WiFi power | 87 | Push-pull out | 0 | |
| PD5 BT power | 86 | Push-pull out | 1 (on) or 0 | The pin floats on by default; drive it deliberately |
| PD4 BT reset | 85 | Push-pull out | **0**, then 1 when BT is started | The pin has no pull resistor |
| PE3 / PE4 RS485 direction | 2 / 3 | Push-pull out | **1** (receive) | Otherwise the RS485 drivers power up transmitting |
| PE5 RS232 power | 4 | Push-pull out | 0 | Never on together with PE6 |
| PE6 RS485 power | 5 | Push-pull out | 0 | Never on together with PE5 |
| PE1 coin-cell switch | 98 | Push-pull out | 0 | Saves the coin cell |
| PA4 flash CS | 29 | Push-pull out | **1** (deselected) | |
| 6 LED pins | 33–36, 63, 64 | Push-pull out | 0 | |
| PB12, PB13, PE11 | 51, 52, 42 | Input, no pull | – | External pull-ups already fitted |
| PA9 | 68 | Input, no pull | – | |
| AFIO | – | `GPIO_USART0_REMAP`, `GPIO_USART2_FULL_REMAP`, `GPIO_SWJ_SWDPENABLE_REMAP` (SW-DP only; frees PA15/PB3/PB4) | | |

---

## 2. Handshake tests, one per peripheral

Each test is a CLI command in the bring-up firmware (see §4). The order below is the order to bring them up in.

### 2.1 Clock: 12 MHz crystal → 120 MHz

| Item | Value |
|---|---|
| Configuration | `HXTAL_VALUE = 12000000`. The simple recipe for the GD32F30x CL: PREDV0SEL = HXTAL, PREDV0 = /1, PLL source = PREDV0, PLLMF = ×10, giving **120 MHz**. AHB /1, APB2 /1 (120 MHz), APB1 /2 (60 MHz). Wait states: FMC WS = 3. Leo's recipe gives the same result through PLL1 (/3 ×10 /10 ×30). |
| Handshake | HXTALSTB goes to 1 within the timeout. PLLSTB = 1. SCSS = PLL. `SystemCoreClock` = 120 000 000. |
| Pass | All four bits correct, and the SysTick 1 s tick matches the RTC second (see §2.2) to within 0.1 %. |
| Failure handling | If HXTAL doesn't start, fall back to the internal 8 MHz oscillator → 120 MHz and print a warning, so the board still boots. |

### 2.2 RTC + 32.768 kHz crystal + coin cell

| Item | Value |
|---|---|
| Configuration | Enable PMU/BKP clocks and backup-domain write access. LXTAL on, then RTC source = LXTAL, prescaler 32767 → 1 Hz. |
| Handshake | `LXTALSTB` = 1 within 5 s. The RTC counter increments by 1 per second. After cutting 12 V and USB, the RTC keeps counting on the CR1220. |
| Pass | 10 s on the RTC = 10 s ± 20 ms on SysTick. Time survives a power cycle. |

### 2.3 LEDs ✅ done

Tested in `Projects/01_LED_Sequence`: every pin switches correctly. It still needs a visual check that the colours match the pins.

### 2.4 Debug serial: USART0 (DB9 / USB-C)

| Item | Value |
|---|---|
| Pins | TX PB6 (92), RX PB7 (93), **remapped** |
| Paths | **DB9:** pin 3 = TX out, pin 2 = RX in, pin 5 = GND, RS232 levels (U9 always on). **USB-C:** D+ = TX, D− = RX at 3.3 V TTL, through a special cable. |
| Settings | 115200 8N1 (Leo used the same) |
| Handshake | On boot print a banner (`PandaBox bring-up vX, SystemCoreClock=…, reset reason=…`). The CLI then answers `ping` → `pong`. |
| Pass | Banner visible in Tera Term (COM9), and commands echo back. |
| Note | USB-C RX may be unreliable: U9 drives the same MCU pin (PB7) all the time. **Use the DB9 with an RS232 adapter for RX.** The CH340 on COM9 must be an **RS232-level** adapter to use the DB9; a TTL CH340 needs the USB-C wiring. Which is it? |

### 2.5 External flash: GD25Q256E on SPI0

| Item | Value |
|---|---|
| Pins | CS PA4 (29, GPIO, active-low), SCK PA5 (30), MISO PA6 (31), MOSI PA7 (32) |
| SPI settings | Master, mode 0 (CPOL 0, CPHA 0), MSB first, 8-bit. Prescaler /4 = **30 MHz** (APB2 120 MHz). That is the GD32F305's SPI maximum; the flash itself allows 80 MHz. Start at /16 = 7.5 MHz for bring-up. WP# is tied to GND and HOLD# to 3.3 V, so single SPI only (no quad). |
| Handshake 1 (read-only) | `9Fh` Read JEDEC ID → expect **`C8 40 19`** (GigaDevice, SPI NOR, 256 Mbit). Also `05h`/`35h`/`15h` status registers: check the BUSY bit and ADS (S8, the 3- or 4-byte address mode). |
| Handshake 2 (4-byte mode) | `06h` write-enable → `B7h` enter 4-byte mode → `35h` → ADS = 1. Then `13h` read with a 4-byte address above 16 MB. |
| Handshake 3 (write test) | ⚠ **Leo's delivery history is stored in this flash** (16 MB per LCR port, a header in sector 0 at `0x0000000` and at `0x1000000`). Before any erase, read those two headers and ask whether the data matters. Then run the write test in **one sector only**, for example the last 4 KB at `0x1FFF000`: `20h`/`21h` sector erase → `12h` page program 256 B → `13h` read back and compare. |
| Pass | ID = C8 40 19, the ADS bit follows B7h/E9h, and the read-back matches 256/256 bytes. |

### 2.6 RS232 on the LCR ports: U504 (both ports)

| Item | Value |
|---|---|
| Pins | Port 1: USART1 TX PA2 (25), RX PA3 (26). Port 2: USART2 TX PD8 (55), RX PD9 (56), full remap. |
| Power | **PE5 (4) = 1**, and **PE6 (5) = 0**: RS485 off, otherwise two drivers fight on the RX pin |
| Connector | DB25 J1/J2: **pin 14 = TX to meter, pin 15 = RX from meter, pin 11 = GND** |
| Settings | 19200 8N1, the LCR default (Leo used the same) |
| Handshake A (no meter) | Loopback plug: short **DB25 pin 14 ↔ pin 15** on J1. Send `PANDA-LOOP-0123456789` on USART1 and receive the same bytes back. Repeat on J2 with USART2. |
| Handshake B (with meter) | LCP **Get Product ID** to the meter node (default 250): `7E 7E FA 14 02 01 00 <crc>` → reply `7E 7E 14 FA 80 … 00 02 "SR…"`, where product ID 02 = LCR. This needs the LCP driver (CRC 0x1021 / init 0x7E7E, escaping of 0x7E/0x1B). |
| Pass | A: 100 % loopback at 19200 and 115200. B: a valid CRC reply from the meter. |

### 2.7 RS485 on the LCR ports: U104 (port 1), U4 (port 2)

| Item | Value |
|---|---|
| Pins | Same UARTs as §2.6. Direction: **PE3 (2) for port 1, PE4 (3) for port 2. High = receive, low = transmit.** |
| Power | **PE6 (5) = 1**, and PE5 (4) = 0. Set PE3/PE4 **high first**, then turn PE6 on. |
| Connector | DB25 pin 15 = **A**, pin 14 = **B** (B has a 10 k pull-down, A has a 10 k pull-up). No 120 Ω termination fitted. |
| Transmit sequence | Direction = TX (0) → write bytes → wait for the UART **TC** (transmission complete) flag, not TBE → direction = RX (1). Nothing is automatic, because the auto-direction resistors aren't fitted. |
| Handshake A (no meter) | Cable **J1 pin 14 ↔ J2 pin 14, J1 pin 15 ↔ J2 pin 15**. Send a pattern from port 1 and receive it on port 2, then the reverse. |
| Handshake B | LCP Get Product ID to a meter wired for RS485, which is the V2.87 default. |
| Pass | Both directions error-free at 19200. |

### 2.8 Bluetooth: Yichip YC1021 on UART3

| Item | Value |
|---|---|
| Pins | MCU TX **PC10 (78)** → BT GPIO07 (pin 26). MCU RX **PC11 (79)** ← BT GPIO06 (pin 25). Test pads BT_RX / BT_TX. |
| Control | **PD5 (86)**: BT 3.3 V regulator, on by default, low = off. **PD4 (85)**: YC1021 RESET, **active-low**. |
| Settings | 115200 8N1 (from Leo's firmware). No RTS/CTS. |
| How Leo starts it | Found in the V2.87 binary at `0x0801AD50–0x0801B423`. The YC1021 runs from its own ROM. At every boot the MCU **uploads the configuration into BT RAM** as a table of **71 commands**, each sent as `[len] 01 10 FC <n> <payload>`, an HCI-style vendor command with opcode **0xFC10 = write memory**, payload `<count> <addr_lo> <addr_hi> <data…>`. The table holds about 1.6 KB: the device names **"YichipSmartSPP"** (Classic) and **"YichipSmartLE"** (BLE), the SPP service record ("SPP slave"), a BLE GATT table, the version tag "HV1.001", and RF/radio settings. It ends with one **0xFC04** command, probably "start/run" (**UNVERIFIED**). The name is later changed to the saved BT name ("PandaBrain" by default). |
| Runtime data frames (from Leo's source) | MCU → BT data: `01 05 <len> <data>`. BT → MCU data: `02 07 <len> <data>`. `02 06` = "send OK". Link status strings: "bt is online", "ble is online", "bt is offline". |
| Handshake plan | 1. PD5 = 1, PD4 = 0 for 10 ms, then PD4 = 1, then wait 50 ms. 2. Send the 71-command table (copied from V2.87) one command at a time, waiting for each reply. The reply format is **UNVERIFIED**; an HCI "Command Complete" (`04 0E …`) is expected. 3. A phone scan shows **"YichipSmartSPP"** or **"YichipSmartLE"**. 4. Connect with a serial Bluetooth app and send text, which should arrive as `02 07 len data`. |
| First step | **Capture the real traffic** before writing the driver. Flash Leo's firmware back (`backup/original_flash_256K.bin`), connect the CH340's RX to test pad **BT_RX** (MCU→BT) and then **BT_TX** (BT→MCU) at 115200, and record the boot. That shows the exact replies and timing. |
| Pass | Phone sees the device, connects, and data flows both ways. |

### 2.9 4G modem: Quectel EC25AFA on UART4

| Item | Value |
|---|---|
| Pins | MCU TX **PC12 (80)** → U603 → EC25 RXD (pin 68). MCU RX **PD2 (83)** ← U603 ← EC25 TXD (pin 67). Test pads TP7/TP8. |
| Control | **PE2 (1)**: modem supply VGSM, high = on. **PB15 (54)**: PWRKEY, high = key pressed (through an NPN). **PA10 (69)**: DTR, keep low. **Not connected:** RESET, STATUS, RI, DCD, RTS/CTS. |
| ⚠ Before the first test | **Measure VGSM** (U401 output or the 4G_VBAT test pad) with the modem powered from a bench supply or with PE2 held high through the CLI. **It must be 3.3–4.3 V.** The regulator's reference voltage is unconfirmed, so it could be about 5.3 V, which would destroy the EC25. |
| Settings | 115200 8N1, the EC25 default. The UART works **only while the modem is on**: U603 is powered from the modem's 1.8 V. |
| Power-on sequence | PE2 = 1 → wait 100 ms → PB15 = 1 for **600 ms** → PB15 = 0 → wait for **`RDY`** (typically 10–13 s after the key press). |
| Handshake | `AT` → `OK`. `ATE0` → `OK`. `ATI` → `Quectel EC25 … Revision: EC25AF…`. `AT+CGSN` → 15-digit IMEI. `AT+CPIN?` → `+CPIN: READY` (needs a SIM). `AT+CSQ` → `+CSQ: <rssi>,99` with rssi 10–31. `AT+CREG?`/`AT+CEREG?` → registered. |
| Network handshake | `AT+QICSGP=1,1,"<APN>","","",1` → `AT+QIACT=1` → `AT+QIOPEN=1,0,"TCP","<server>",<port>,0,0` → `+QIOPEN: 0,0`. Set the APN **before** QIACT; Leo's code does it the other way round, which is a bug. |
| Power-off | `AT+QPOWD` → `POWERED DOWN` → PE2 = 0 |
| Pass | IMEI read, SIM ready, signal at least 10, registered, TCP connected. |

### 2.10 GNSS (inside the EC25)

| Item | Value |
|---|---|
| Antenna | ANT2, active; 3.3 V bias always on |
| Handshake | `AT+QGPS=1` → `OK`. Then `AT+QGPSLOC=2` every 1 s; `+CME ERROR: 516` means no fix yet. |
| Pass | A fix outdoors within 60 s (cold start), giving `+QGPSLOC: <utc>,<lat>,<lon>,…`. Leo used `AT+QGPSGNMEA="RMC"`. |

### 2.11 WiFi: FC20N (hosted by the EC25)

| Item | Value |
|---|---|
| Control | **PD6 (87)** = 1 powers WLAN_3V3. Everything else goes through EC25 AT commands. The FC20N is on the EC25's SDIO bus, not the MCU's. |
| Handshake (Leo's sequence) | `AT+QWSSID=TBOX_APP` → `AT+QWAUTH=5,4,"123456789"` → `AT+QWIFI=1` → `AT+QWTOCLIEN=1,5553`. Leo's code then forwards data with `AT+QDATAFWDHEX=1` / `AT+QDATAFWD=…`. |
| Pass | A phone sees the SSID **TBOX_APP**, joins with `123456789`, and reaches TCP port 5553. |
| Note | The exact meaning of `QWAUTH` / `QWTOCLIEN` / `QDATAFWD` needs the Quectel EC25 WiFi AT manual, which we don't have yet. |

### 2.12 ADC: 12 V supply and coin cell

| Item | Value |
|---|---|
| Pins | PC0 (15) = ADC0 IN10, **V_12V = Vadc × 11**. PC1 (16) = IN11, **V_cell = Vadc × 2**, valid only while **PE1 (98) = 1**. |
| Settings | ADC clock: 40 MHz max per the GD32F305 datasheet, and accuracy is specified at 14 MHz. Use APB2 /8 = **15 MHz**, sampling 239.5 cycles (the dividers have 100 k source impedance), VREF+ = 3.3 V. Calibrate before use. |
| Handshake | Read both channels 16× and average. |
| Pass | 12 V reading within ±3 % of a multimeter. Coin cell 2.8–3.2 V. |

### 2.13 Digital inputs (power-source detect)

| Pin | Signal | Pass |
|---|---|---|
| PB13 (52) | LCR port 1 pin 13 has 12 V | Reads 0 when 12 V is applied to J1 pin 13, else 1 |
| PB12 (51) | LCR port 2 pin 13 has 12 V | Same on J2 |
| PE11 (42) | DB9 pin 8 has 12 V | Same on J3 |
| PA9 (68) | USB-C VBUS present | Reads 1 with USB-C 5 V |

### 2.14 Watchdog

FWDGT from the internal 40 kHz oscillator, prescaler /64, reload about 1250, giving **≈2 s**. Handshake: `wdt test` stops feeding it, and the board must reboot with reset reason = watchdog, read from RCU_RSTSCK.

---

## 3. Protocol cheat sheet

| Link | Frame |
|---|---|
| App ⇄ Box (BT / WiFi / 4G TCP) | ASCII line `Cmd p0,p1,\r\n` → reply `LxCmd 0,\r\n` (0 = OK, 1 = fail). About 40 commands, listed in `docs/reference/03_Protocol_Reference.md`. |
| Box ⇄ LCR meter (LCP) | `7E 7E to from(0x14) status len msgID data CRClo CRChi`. CRC-16 poly 0x1021, init 0x7E7E, data bits shifted into the LSB. 0x7E and 0x1B are escaped as `1B xx`. Big-endian values. |
| Box ⇄ TCS 3000 | `7E dest src flag cmd btcnt data CRC8` (Dallas 1-Wire CRC-8). The reply adds ERR and STAT bytes. |
| Box ⇄ EC25 | AT commands + URCs (`RDY`, `+QIURC: "recv",0`, …), CR/LF terminated |
| Box ⇄ YC1021 | Yichip HCI-style: config `01 10 FC len …`; data TX `01 05 len data`; data RX `02 07 len data` |
| Box ⇄ GD25Q256E | SPI NOR: 9Fh ID, 05h/35h/15h status, 06h WREN, B7h/E9h 4-byte mode on/off, 13h read, 12h program, 21h sector erase (4-byte address) |

---

## 4. Proposed bring-up firmware: `Projects/02_HW_Bringup`

It's one image, flashed at `0x08000000` over SWD, with a serial CLI on USART0 (115200). Each test is a separate command, so one peripheral can be tested at a time and a failure in one doesn't block the others.

```
Projects/02_HW_Bringup/
  src/board.h          pin map from §0/§1 (single source of truth)
  src/board.c          safe GPIO init (§1), clocks (§2.1)
  src/cli.c            line editor + command table
  src/uart.c           USART0/1/2, UART3/4, interrupt RX ring buffers
  src/drv_flash.c      GD25Q256E (§2.5)
  src/drv_rs.c         RS232/RS485 switching + direction (§2.6/§2.7)
  src/lcp.c            LCP frame, CRC, escaping (meter handshake)
  src/drv_bt.c         YC1021 reset + config upload (§2.8)
  src/drv_ec25.c       power sequence + AT engine (§2.9–§2.11)
  src/drv_adc.c, rtc.c, wdt.c
  build.sh / flash.sh  GCC + J-Link, as in 01_LED_Sequence
```

| CLI command | What it does |
|---|---|
| `info` | Clocks, reset reason, flash size, unique ID |
| `led <n> on/off`, `led chase` | LEDs |
| `rtc`, `rtc set <epoch>` | RTC |
| `adc` | 12 V and coin cell |
| `inputs` | PB12/PB13/PE11/PA9 |
| `flash id`, `flash hdr`, `flash test <addr>` | External flash (§2.5) |
| `rs232 loop <1/2>`, `rs485 loop` | Serial port loopback tests |
| `lcp id <port> <node>`, `lcp scan <port>` | Meter handshake |
| `bt reset`, `bt init`, `bt raw` | Bluetooth, including a raw UART3 pass-through to sniff and debug |
| `modem on/off`, `at <cmd>`, `modem raw`, `gps`, `wifi on` | EC25 |
| `wdt test` | Watchdog |
| `selftest` | Runs every test that is safe without extra hardware |

Build with **GCC**. Keil MDK Lite's 32 KB limit is enough for this bring-up image. The full application (about 80 KB) needs GCC or a paid Keil license.

---

## 5. What's needed from the bench before starting

| Needed | For |
|---|---|
| **Multimeter reading of VGSM** (EC25 supply) | Required before §2.9 |
| RS232-level USB adapter for the DB9, or a TTL adapter + USB-C cable | §2.4 debug console. Is the CH340 on COM9 RS232 or TTL? |
| DB25 loopback plug (pin 14 ↔ 15) and a J1 ↔ J2 cable | §2.6 / §2.7 without a meter |
| An LCR meter (LCR-II / LCR 600 / LCR.iQ) and its node address, if available | LCP handshake |
| Micro-SIM with data plan + APN | §2.9 network test |
| GNSS antenna on ANT2, LTE antenna on ANT1, BT antenna on ANT4 | §2.8–§2.10 |
| Decision: may the external flash's delivery history be erased? | §2.5 write test |
| 12 V bench supply into DB9 pin 8 (or LCR pin 13) | Realistic power and the ADC test |
