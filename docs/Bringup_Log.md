# PandaBox Bring-up Log

This is a running record of every hardware test on the PandaBox test board: what was done, the data collected, and the conclusions. The firmware is `Projects/02_HW_Bringup`, and the raw logs are in `Projects/02_HW_Bringup/logs/`.

**Setup**
- Board: XBOX V2.5 (GD32F305VCT6), UID `33320976 000F3635 3837354E`.
- Power: **USB-C 5 V only**. No 12 V applied, no SIM, no LCR meter, no loopback plugs.
- Debug: SEGGER J-Link on SWD (J100). Console through a CH340 USB-serial adapter (COM9) at TTL level on PB6/PB7 (USART0).
- Tools: Arm GCC 14.2, J-Link V7.94a, Python 3.14 + pyserial 3.5.

---

## 2026-09-24: session 1

### Summary

| # | Peripheral | Test | Result | Key data |
|---|---|---|---|---|
| 1 | Clock | `info` | ✅ PASS | 12 MHz crystal starts; PLL = 120 MHz (`RCU_CFG0=0021040A`, SCSS = PLL) |
| 2 | Debug UART (USART0 remap) | `ping` | ✅ PASS | Two-way over COM9 at 115200 |
| 3 | External flash GD25Q256E (SPI0) | `flash id` | ✅ PASS | JEDEC `C8 40 19`, SR1 = 00, SR2 = 00 (3-byte mode), SR3 = 20, SFDP signature OK |
| 4 | External flash contents | `flash map`, `flash rd` | ✅ Read-only dump | 98 of 8192 sectors used; 6008 records decoded (see below) |
| 5 | 4G modem EC25 (UART4) + PE2 | `gsm test` | ✅ PASS | PE2 = 0: silent. PE2 = 1: `RDY` after 9.7 s, AT session OK |
| 6 | Bluetooth YC1021 (UART3) | `bt` | ✅ PASS | HCI Command Complete for Reset / Read Version / Read BD_ADDR |
| 7 | Power detect inputs | `inputs` | ✅ as expected | VBUS = 1 (USB-C present); LCR1/LCR2/DB9 12 V absent |
| 8 | ADC | `adc` | ℹ️ | V_12V = 0 mV (no 12 V connected); coin cell = **14 mV**, so no cell or a dead one |
| 9 | RTC / 32.768 kHz | `rtc` | ℹ️ not running | `RCU_BDCTL=00000018`: LXTAL off, RTC off. Backup domain was reset (fits a missing coin cell). The LXTAL start-up is not tested yet. |
| 10 | RS232 transceiver (U504) | `rs232 1/2` | ⚠️ inconclusive | RX pins high whether the RS232 supply is on or off; no loopback plug |
| 11 | RS485 transceivers (U104/U4) | `rs485 1/2` | ⚠️ inconclusive | Same: RX pins always high, so direction control isn't visible |
| 12 | Transceiver power control | `rsdiag` | ⚠️ finding | PA3 = 3300 mV and PD9 = 1 in **all four** PE5/PE6 combinations |
| 13 | LEDs | 01_LED_Sequence (earlier) | ✅ pins verified | All six pins switch in order; the visual check is still open |

### 1. Clock and console

```
SystemCoreClock = 120000000 Hz
RCU_CTL  = 03036E83 (HXTALSTB=1 PLLSTB=1)
RCU_CFG0 = 0021040A  RCU_CFG1 = 00000000  SCSS=2
Flash size = 256 KB, UID = 33320976 000F3635 3837354E
RESULT clock PASS SystemCoreClock=120000000 HXTAL=12MHz
```

- **Crystal confirmed:** a 12 MHz reference with ÷1 ×10 gives exactly 120 MHz, which confirms the crystal is **12 MHz**. It is not the 25 MHz the GD library assumes.
- **Prompt bug, fixed:** at first, newlib held back the `> ` prompt because it doesn't end in a newline. Fixed with `setvbuf(stdout, NULL, _IONBF, 0)`.

### 2. External SPI flash GD25Q256E

```
JEDEC ID = C8 40 19 (expect C8 40 19 = GigaDevice GD25Q256)
SR1 = 00 (BUSY=0 WEL=0 BP=0)  SR2 = 00 (ADS=0, 3-byte mode)  SR3 = 20
SFDP 00000000 53 46 44 50 06 01 02 FF 00 06 01 10 30 00 00 FF
```

- No block protection. 3-byte address mode is the power-on default; reads use `13h` with a 4-byte address, which works in either mode.
- SPI0 ran at 7.5 MHz for the ID and 30 MHz for the reads, both without errors.

**Contents (read only; nothing was erased or written):**

| Area | Header (24 bytes) | Used sectors | Records |
|---|---|---|---|
| Port 1 `0x0000000` | `01 00 00 00 · 00000000 · 00000000 · 00000000 · 00000000 · 5A5A5A5A` (node 1, all counters 0) | Sector 0 (header) + **sectors 2–96** (380 KB) | **6004 records** in sectors that the header no longer counts |
| Port 2 `0x1000000` | `02 00 00 00 · 00000000 · 00000000 · 00000100 · 00000004 · 5A5A5A5A` (node 2, 4 records / 0x100 bytes) | Sectors 0–1 | **4 records** |

The header layout on this build is `{u8 node + 3 pad, u32, u32, u32 end offset, u32 count, u32 flag 0x5A5A5A5A}`. That differs from the field order in Leo's source, whose V2.87 build sits at `0x0000000` (**UNVERIFIED**).

**Decoded records** (64 bytes, little-endian: epoch, 6 LCR values in tenths, 4 GPS words, 20 reserved):

| Port | Records | Dates (UTC) | Values |
|---|---|---|---|
| 1 | 6004 | 2024-09-27 (2812), 2024-09-30 (3084), 2024-10-01 (16), 2024-10-03 (64), 2024-10-04 (4), 2025-11-18 (24) | Current gross 0–50.3; flow up to 3907.2; gross totalizer #17 from 0.5 to 838 837.7; net always 0; previous gross #100 up to 838 832.7 |
| 2 | 4 | 2025-11-18 11:56:29–32 | Gross totalizer 838 832.7, no flow |

- **No GPS** in any record. All four GPS words are 0.
- **Reserved bytes** are always zero.
- **Port 1 is not a ring-buffer wrap**: it's old data whose header has been reset. The records are out of time order because the 2025 records sit at the start of the area.
- **Files:**

| File | Contents | SHA-256 |
|---|---|---|
| `logs/extflash_0x0000000-0x0000FFF.bin` | Port 1 header sector | `450feb7d…ff2c` |
| `logs/extflash_0x0002000-0x0060FFF.bin` | Port 1 data | `23037062…0e59` |
| `logs/extflash_0x1000000-0x1001FFF.bin` | Port 2 header + data | `bd85f0f5…6ada` |
| `logs/extflash_records.csv` | All 6008 decoded records | |

### 3. 4G modem EC25: PE2 low / high

| Phase | What was done | Result |
|---|---|---|
| A | PE2 = 0 for 2 s, PWRKEY pulse 600 ms (PB15), listen 15 s with `AT` every 2 s | **Silent.** PE2 does cut the modem supply. |
| B | PE2 = 1, 500 ms, PWRKEY 600 ms, wait up to 30 s | `RDY` at **t = 9.7 s**, `AT` → `OK` at 10.0 s |

AT session:

| Command | Reply | Meaning |
|---|---|---|
| `ATI` | `Quectel EC25 Revision: EC25AFAR05A07M4G` | North America AF variant |
| `AT+QGMR` | `EC25AFAR05A07M4G_30.005.30.005` | Firmware |
| `AT+CGSN` | `860858062040217` | IMEI |
| `AT+CIMI` / `AT+CPIN?` / `AT+QCCID` | `ERROR` / `+CME ERROR: 10` / `+CME ERROR: 13` | **No SIM inserted** |
| `AT+CSQ` | `+CSQ: 99,99` | No signal (no SIM) |
| `AT+CREG?` / `AT+CEREG?` | `0,4` | Not registered (no SIM) |
| `AT+IPR?` | `+IPR: 115200` | Fixed baud rate 115200 |
| `AT+QGPS?` | `+QGPS: 0` | GNSS off |
| `AT+QCFG="band"` | `0x260,0x80a,0x0` | Band configuration |
| `AT+QPOWD` / `AT+QPOWD=1` | `ERROR` | **Soft power-off refused.** The modem is switched off with PE2 = 0 instead. |

**Conclusions:**
- PE2 **is** the modem supply enable (active-high), and the PWRKEY polarity on PB15 is correct.
- The UART path through U603 works in both directions at 115200.
- The modem boots and runs normally, so its supply (VGSM) is within the EC25's range. A one-off multimeter check is still recommended.

### 4. Bluetooth YC1021

```
BT> 01 03 0C 00                  HCI_Reset
BT< 04 0E 04 01 03 0C 00         Command Complete, status 0
BT> 01 01 10 00                  HCI_Read_Local_Version_Information
BT< 04 0E 0C 01 01 10 00 7C AB CD 80 B6 C2 B2 CD
BT> 01 09 10 00                  HCI_Read_BD_ADDR
BT< 04 0E 0A 01 09 10 00 00 00 00 00 00 00
```

- The YC1021 speaks **HCI over UART (H4 framing) at 115200**, with no boot output after reset.
- The version fields are non-standard (Yichip ROM).
- **BD_ADDR is 00:00:00:00:00:00.** The chip has no identity until the MCU uploads its configuration. This matches Leo's firmware, which uploads 71 `01 10 FC` memory-write commands at boot (names, SPP record, GATT table) and derives the MAC `24:06:xx:xx:xx:xx` from the MCU UID.
- **Next:** replay Leo's upload table and check that a phone sees the device.

### 5. LCR transceivers: RS232 / RS485

`rsdiag 300`: each combination held for 300 ms. RX pins read with the internal pull-down; PA3 also measured with the ADC.

| PE5 (RS232 supply) | PE6 (RS485 supply) | PA3 = USART1 RX | PD9 = USART2 RX | PA3 voltage |
|---|---|---|---|---|
| 0 | 0 | 1 | 1 | 3300 mV |
| 1 | 0 | 1 | 1 | 3300 mV |
| 0 | 1 | 1 | 1 | 3300 mV |
| 1 | 1 | 1 | 1 | 3300 mV |

- **Something drives both RX lines high all the time**, even with both transceiver supplies "off". An unpowered transceiver can't do that.
- The most likely cause is that one transceiver (probably the RS232 chip U504, whose idle receiver output is high) stays powered whatever PE5/PE6 are set to. That would also explain why the RS485 transmit-mode test (receiver output switched off) still reads high.
- **Loopback** (DB25 14↔15) and **cross** (J1↔J2) tests gave 0 bytes, as expected with no cable fitted.
- **Status:** the transceivers are not yet confirmed working. Their power control (PE5/PE6) doesn't behave as the PCB analysis predicted.

### 6. Inputs, ADC, RTC

```
PB13 LCR1 pin13 12V: 1 (absent)   PB12 LCR2 pin13 12V: 1 (absent)
PE11 DB9 pin8  12V : 1 (absent)   PA9  USB-C VBUS    : 1 (PRESENT)
V_12V = 0 mV (PC0 x11), V_coin_cell = 14 mV (PC1 x2, PE1 on for 20 ms)
RCU_BDCTL = 00000018: LXTALEN=0 LXTALSTB=0 RTCSRC=0 RTCEN=0
```

All consistent with USB-only power and no coin cell.

---

## Open items

| # | Item | Next action |
|---|---|---|
| 1 | RS232/RS485 RX lines always high; PE5/PE6 control not visible | **Wire J1 DB25 pin 14 ↔ pin 15**, then run `rs232 1` and `rsdiag`. If the loopback echoes with PE5 = 0, the RS232 supply isn't switched. Also measure the U8/U11 3.3 V outputs with a multimeter. |
| 2 | Coin cell reads 14 mV; RTC is lost on power-off | Fit a CR1220 and re-test `adc`. Then test LXTAL start-up (write-enable backup domain, LXTAL on, RTC on). |
| 3 | `AT+QPOWD` returns ERROR | Retry some seconds after `RDY` / after `AT+CFUN?`. Check the EC25AF R05 syntax. PE2 = 0 works meanwhile. |
| 4 | No SIM | Insert a SIM with data + APN, then test network + TCP + GNSS (`AT+QGPS=1`, `AT+QGPSLOC=2`). |
| 5 | YC1021 has no configuration | Port Leo's 71-command upload table, then check phone discovery and SPP/BLE data. |
| 6 | 12 V input path not tested | Apply 12 V on DB9 pin 8, then run `adc` and `inputs`. |
| 7 | LED colours not visually confirmed | Run `led` and watch the board. |
| 8 | VGSM not measured | Measure once with a multimeter while `gsm on` (evidence so far says it's fine). |

## Board state after this session

- **Flash:** `Projects/02_HW_Bringup` at `0x08000000`. Leo's app V2.77 (APP1), APP_INFO and config pages are still intact above 0x08008000, but the bootloader region is overwritten. The full original image is in `backup/original_flash_256K.bin`.
- **Modem:** off (PE2 = 0).
- **External SPI flash:** untouched.
