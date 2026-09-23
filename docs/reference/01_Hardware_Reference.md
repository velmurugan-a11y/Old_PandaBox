# PandaBox (XBOX V2.5) Hardware Reference

Board: **XBOX_V2.5** (PCB "XBOX_V2.5_2025-05-05", 4-layer FR4 1.6 mm, ENIG, panel 99 x 137.2 mm)
MCU: **GigaDevice GD32F305VCT6**, LQFP100, Cortex-M4F, 120 MHz max, **256 KB flash, 96 KB SRAM**

---

## 0. Sources and method (read this first)

| Source | What it gave | How far to trust it |
|---|---|---|
| `XBOX_V2.5 Hardware Schematic Diagram.pdf` (6 pages, but title blocks say "Sheet x of **7**") | Topology, reference designators, net names | **Not up to date.** Pages 2 and 3 are titled "iDD-213G Schematic (Main Board)" and are dated "2013 April". Sheet 7 is missing, and several circuits on the production PCB/BOM do not appear anywhere in it (see §6). |
| `Panda BOX_V2.5 PCB Layout 2025-05-05.pdf` (4 copper layers: Top / GND plane / inner signal / Bottom) | **Real copper connectivity** | **Most authoritative.** I rendered each copper layer, labelled the connected copper regions, joined the layers through the 1,699 vias and the through-hole pads, and read the pad labels (e.g. `U100x23` = U100 pin 23) to build a full pad-to-pad netlist. It agrees with the schematic wherever both show the same circuit, and it fills in the missing sheet. The netlist is saved in `pcb_netlist.txt` next to this file, one net per line. The PDF carries no net names, so net names in this document come from the schematic. |
| `PandaBox BOM V2.5.xlsx` | Fitted parts, part numbers, values | Authoritative for **what is fitted** and **values**. A reference designator missing from the BOM is treated as **not fitted (DNP)**. |
| Function diagram, Hardware Components PDF (= `pandabox (1).pdf`, identical text), working-diagram PDF, printer-setup PPTX (image-only slides), Liquid Controls drawings 81515B and 81513-040 | System context, ports, LCR cable pinout | Context only. |
| Datasheets (GD32F305, GD25Q256E, YC1021, RF5745) | Pin functions and remaps | The RF5745 is **not used on this board** (see §6). |

Any line marked **UNVERIFIED** could not be confirmed from two independent sources, or depends on an assumption that is stated next to it.

---

## 1. Block diagram summary

```
            J5 pin13 (DB25 printer) ─┐
 LCR PORT1 J1 pin13 ──D1 SM360A──────┤
 LCR PORT2 J2 pin13 ──D3 SM360A──────┼── 12V_IN ── F1 (2A fuse) ── V_12V ──┬─ D501 SMBJ36A TVS
 DB9 J3 pin8 ─────────D23 SM360A─────┘                                     ├─ R312/R314 ÷11 ─► PC0 (ADC)
                                                                           └─ U200 MP4560DN buck ─► VCC_5V (≈5.2 V)
 USB-C J8 VBUS = EXT_5V ──(R13/R14 ► PA9)                                              │
        │                                                                              │
        └──D2 SS34──┐                    ┌──D7 SS34──────────────────────────────────┘
                    └──────── "VBAT" (≈4.7 V internal rail — NOT a battery) ────────┐
   ┌───────────┬──────────────┬─────────────┬──────────────┬──────────────┬────────┘
 U300 RT9193-33  U401 TJ4330-ADJ  U2 RT9193-33  U6 RT9080-33  U8 RT9193-33  U11 RT9193-33
 always on       EN = PE2         EN = PD5       EN = PD6       EN = PE5       EN = PE6
 VMCU_3.3V       VGSM             BT_3.3V        WLAN_3V3       RS232_3.3V     RS485_3.3V
 (MCU, flash,    (EC25 VBAT)      (YC1021 +      (FC20N WiFi)   (U504 dual     (U104, U4
  U9, LEDs,                        GSR2401 FEM)                  RS232 for      RS485 for
  GNSS ant bias)                                                 LCR ports)     LCR ports)
```

| Subsystem | Part (ref) | Notes |
|---|---|---|
| Power input | 9–36 V (function diagram). Enters via J5-13, J1-13, J2-13 or J3-8, diode-ORed (SM360A), then F1 **2 A fuse** (BOM; the schematic shows a PTC), D501 SMBJ36A, bulk C1 100 µF/50 V plus C6/C40 10 µF/50 V | The docs say the box is powered either from the truck battery through the DB9 or from the LCR meter's +12 V on DB25 pin 13. |
| 5 V buck | U200 **MP4560DN** (SOIC-8), L100 SWPA6045S150MT 15 µH, D201 SS34 | FB divider R5 200 k / R6 36 k with V_FB = 0.8 V gives **≈5.24 V**. EN divider R1 100 k / R205 36 k. R203 200 k sets the frequency. Compensation is R202 36 k + C211 330 pF. R5/R6 are **not drawn in the schematic**; the values come from the PCB and BOM. |
| USB-C 5 V | J8 SHK16-B130 (Type-C, 12P) | VBUS feeds EXT_5V, then D2 SS34 into VBAT. D301 SD24C TVS. **CC1/CC2 are not connected** (see §6). |
| Internal rail "VBAT" | — | About 4.7–4.9 V: VCC_5V or EXT_5V minus a Schottky drop. The name is misleading; there is **no rechargeable battery**. The charger U302, J300 and Q305/Q306 are unpopulated. |
| MCU 3.3 V | U300 **RT9193-33GB** (BOM PN OCP2820WE33AD). The schematic says RT9080-33. | CE is driven through D6/D21 (1N4148WS) + R21 from EXT_5V or VCC_5V, so it is **always on whenever any 5 V is present**. The PWRHOLD (PC8) and watchdog (PWR_RST) diodes are not fitted. Output VMCU_3.3V feeds the MCU VDD/VDDA through beads B104 + B102 (net VDD_3.3V). |
| Modem supply | U401 **TJ4330GDP-ADJ** (SOP-8, 3 A LDO) | VGSM set by R170 6.8 k (top) and R171 1.2 k (bottom): VOUT = Vref × 6.67. **Vref UNVERIFIED** (0.6 V gives 4.0 V; 0.8 V would give 5.3 V and damage the EC25). **Measure before trusting.** D24 PT3C4V5B TVS. Bulk C13/C27 100 µF tantalum. |
| MCU | U100 **GD32F305VCT6** | **HXTAL = 12 MHz** (X101, SMD3225, ±10 ppm, CL 20 pF; C117/C118 10 pF; R123 1 M). **LXTAL = 32.768 kHz** (X1, Epson FC-135 type, SF32WK32768D31T002, CL 12.5 pF; C57/C84 10 pF). 120 MHz = 12 MHz × 10 (PLL). |
| RTC backup | J7 HYC-CR1220-2 holder, CR1220 3 V coin cell | Coin cell → D11 → MCU VBAT pin 6 (net VBACK), ORed with VMCU_3.3V through D12. C110 1 µF. Cell voltage can be measured through Q7 (P-MOS MT2305) switched by PE1. |
| LTE Cat-4 + GNSS | U1 **Quectel EC25AFA-512-STD** (EC25-AF, North America). The schematic symbol says "EC20-A"; same LGA footprint. | UART through level translator U603 **SN74LVC2G07** to UART4 (PC12/PD2). PWRKEY through Q4. DTR from PA10. SIM J18. ANT1 = LTE main, ANT2 = GNSS (active, 3.3 V bias). USB and diversity are **not** routed (USB only to test pads). |
| Wi-Fi | U801 **Quectel FC20N-Q93** | **Hosted by the EC25 over SDIO**, not by the MCU. The MCU only switches its 3.3 V supply (PD6). ANT3. |
| Bluetooth | U5 **Yichip YC1021** (QFN32, BT 3.0 BR + 5.0 BLE) + U10 **GSR2401** 2.4 GHz PA/LNA front end (QFN16) | X2 = **24 MHz** (±10 ppm, 12 pF). The function diagram's "16 MHz" is wrong. UART3 (PC10/PC11). Reset PD4, power PD5. ANT4. U10 is **not in the schematic**. |
| SPI NOR flash | U103 **GD25Q256EYIGR** (256 Mbit = 32 MB, WSON-8) | SPI0 (PA4..PA7). Needs 4-byte addressing above 16 MB. WP# = GND, HOLD# = 3.3 V, so single/dual SPI only (no quad). |
| RS232 for LCR ports | U504 **BL13232ETS** (MAX3232-class, TSSOP16; BOM also says SIT3232EEUE) | Channel 2 = LCR PORT 1 (USART1), channel 1 = LCR PORT 2 (USART2). Powered by U8 (PE5). |
| RS485 for LCR ports | U104 (port 1) and U4 (port 2) **SIT3088EESA** (SOP-8) | Powered by U11 (PE6). DE/RE driven through NPNs Q13 (PE3) and Q14 (PE4). |
| RS232 for DB9 and printer | U9 **BL13232ETS** | USART0 (PB6/PB7, remapped). **Always powered** from VMCU_3.3V. |
| 12 V presence inputs | Q10, Q9, Q11 MMBT5551 | J1-13 → PB13, J2-13 → PB12, J3-8 → PE11. All **active-low**. |
| LEDs | LED3 BL-C34S-C (4-colour through-hole array, 1 k each); LED1, LED2 3 mm red (220 R each) | Every LED sinks through an S9014W NPN, so all are **active-high** (see §2). |
| ADC | PC0 = V_12V ÷ 11 (R312 100 k / R314 10 k 1%, D302 DAN217U clamp). PC1 = coin cell ÷ 2 (R313/R315 100 k 1%, only while PE1 = 1). | |
| USB | USB-C J8 is **5 V input plus a UART0 port**, not USB. D+ → PB6 (TX) and D- → PB7 (RX) through 200 R. Native USBFS on PA11/PA12 is unconnected (R307/R308 DNP). | |
| Debug | J100 4-pin SWD: 1 GND, 2 SWCLK, 3 SWDIO, 4 3.3 V. NRST is only on test pad "SWCLK2". BOOT0 is on test pad "BOOT". | |
| Antennas | ANT1..ANT4 IPEX Gen1 (20279001E-01) to SMA bulkheads | BOM lists a GPS+4G combo antenna and two WiFi/BT sticks. The Hardware Components doc says BT uses "the second SMA from left". |
| Not present / not fitted | — | **No CAN** (U13 footprint is DNP and sits on the I2C bus; MCU CAN pins unused). **No external watchdog** (U3 PIC10F200 DNP). **No buzzer, no relays/outputs, no TF card** (PE0_TF_EN is a net label only). **No Li battery or charger.** No I2C EEPROM (U7 DNP). |

---

## 2. Complete MCU pin map (U100, GD32F305VCT6 LQFP100)

Connectivity comes from the PCB netlist. "DNP" means the part is on the PCB but not in the BOM.
Peripheral names use GD32 naming (USART0/1/2, UART3/4, SPI0, I2C0). The schematic's "UART0..UART4" labels follow the same numbering: UART0 = USART0, UART1 = USART1, UART2 = USART2, UART3 = UART3, UART4 = UART4.

| Pin | Port | Schematic net | Connected to (part.pin) | Function | Notes / polarity |
|---|---|---|---|---|---|
| 1 | PE2 | PE2_GSM_ON/OFF | R400 1 k → U401.2 (TJ4330 "EN"); R173 10 k to GND | GPIO out: **modem power (VGSM) enable** | Active-high; default off (pull-down). The schematic's U401 pinout is suspect: R172 10 k pulls U401.1 to VBAT. Which pin is really EN is **UNVERIFIED**. Check by measuring VGSM with PE2 = 0 and PE2 = 1. |
| 2 | PE3 | PE3_RS485_RE | R130 1 k → Q13 base (R128 10 k to GND). Q13 collector → U104.2 /RE + U104.3 DE, R129 10 k pull-up to RS485_3.3V | GPIO out: **RS485-1 (LCR port 1) direction** | **PE3 = 1 → receive** (DE = /RE = 0). **PE3 = 0 → transmit** (driver on, receiver off). With PE3 floating at reset, the transceiver defaults to **TRANSMIT** once RS485_3.3V is on. R127 (PA2 → Q13 base, auto-direction option) is DNP. |
| 3 | PE4 | PE4_RS485_RE | R134 1 k → Q14 → U4.2/U4.3; R133 pull-up; R132 pull-down | GPIO out: **RS485-2 (LCR port 2) direction** | Same as PE3. R131 (PD8 auto-direction) DNP. |
| 4 | PE5 | PE5_RS232_EN | R73 1 k → U8.3 CE (R121 10 k to GND) | GPIO out: **RS232_3.3V LDO enable** (powers U504) | Active-high, default off. |
| 5 | PE6 | PE6_RS485_EN | R124 1 k → U11.3 CE (R125 10 k to GND) | GPIO out: **RS485_3.3V LDO enable** (powers U104 and U4) | Active-high, default off. |
| 6 | VBAT | VBACK | D11 (coin cell) and D12 (VMCU_3.3V) cathodes; C110 1 µF | Backup domain supply | |
| 7 | PC13 | — | NC | — | |
| 8 | PC14 | — | X1.1, C84 10 pF | OSC32IN | LXTAL 32.768 kHz |
| 9 | PC15 | — | X1.2, C57 10 pF | OSC32OUT | |
| 10 | VSS_5 | GND | GND | | |
| 11 | VDD_5 | VDD_3.3V | Beads B104/B102 from VMCU_3.3V; C101..C105 100 nF | | |
| 12 | OSCIN | — | X101.3, C117 10 pF, R123 1 M | HXTAL in | **12 MHz** |
| 13 | OSCOUT | — | X101.1, C118 10 pF, R123 | HXTAL out | |
| 14 | NRST | MCU_NRST | R107 10 k to VDD_3.3V, C113 100 nF; test pad "SWCLK2" | Reset | Not on J100. |
| 15 | PC0 | PC0_ADC_12V | R312 100 k (from V_12V) / R314 10 k, C3 100 pF, D302 DAN217U clamp to 3.3 V/GND | ADC01_IN10 | V_12V = V_adc × 11 (measured after F1). |
| 16 | PC1 | PC1_ADC_VBAT | R313 100 k (from Q7 drain = switched coin cell) / R315 100 k, C4 100 pF | ADC01_IN11 | V_cell = V_adc × 2. Valid only while PE1 = 1. |
| 17 | PC2 | — | NC | | |
| 18 | PC3 | — | NC | | |
| 19 | VSSA | — | R41 0 R to GND (R40 DNP to VDD) | | The jumpers exist because the schematic symbol is an STM32F2/F4 pinout. |
| 20 | VREF- | GND | GND | | |
| 21 | VREF+ | — | R53 0 R from VDD_3.3V, C55 1 µF | | |
| 22 | VDDA | VDD_3.3V | same as VDD | | |
| 23 | PA0 | — | NC | | |
| 24 | PA1 | BT_ICE | R8 (DNP) → YC1021.17 ICE | — | **Not connected** in production. |
| 25 | PA2 | PA2_UART1_TXD | R31 200 R → U504.10 T2IN; R3 0 R → U104.4 DI; R127 (DNP) → Q13; TP2 | **USART1_TX** (default mapping) → LCR PORT 1 | TX goes to both RS232 and RS485. Which one is live depends on which LDO is on. R135 (DI to GND) is DNP. |
| 26 | PA3 | PA3_UART1_RXD | R30 200 R ← U504.9 R2OUT; R236 200 R ← U104.1 RO; TP1 | **USART1_RX** ← LCR PORT 1 | **Two drivers on one pin.** Power only one of RS232 (PE5) or RS485 (PE6) at a time. |
| 27 | VSS_4 | GND | | | |
| 28 | VDD_4 | VDD_3.3V | | | |
| 29 | PA4 | PA4_FLASH1_CS | U103.1 /CS, R110 10 k pull-up | GPIO out (or SPI0_NSS): **flash CS** | Active-low |
| 30 | PA5 | PA5_SPI0_SCK | U103.6 CLK | **SPI0_SCK** | |
| 31 | PA6 | PA6_SPI0_MISO | U103.2 SO, R111 10 k pull-up | **SPI0_MISO** | |
| 32 | PA7 | PA7_SPI0_MOSI | U103.5 SI | **SPI0_MOSI** | |
| 33 | PC4 | PC4_LED_WIFI_ORANGE | R49 1 k → Q1 (S9014W, R50 10 k) → LED3.8 | GPIO out | Active-high, orange "WiFi" LED |
| 34 | PC5 | PC5_BT_LED_BLUE | R56 1 k → Q3 (R57) → LED3.6 | GPIO out | Active-high, blue "BT" LED |
| 35 | PB0 | PB0_LED_GPS_GREEN | R52 1 k → Q2 (R54) → LED3.4 | GPIO out | Active-high, green "GPS" LED |
| 36 | PB1 | PB1_LED_PWR_RED | R43 1 k → Q16 (R44) → LED3.2 | GPIO out | Active-high, red "PWR" LED. Firmware-controlled, not hard-wired. |
| 37 | PB2/BOOT1 | — | R109 10 k to GND | BOOT1 = 0 | |
| 38–41 | PE7–PE10 | — | NC | | |
| 42 | PE11 | PE11_DB9_12V | R105 1 k ← Q11 collector (R104 10 k pull-up to 3.3 V). Q11 base: R100 100 k from J3-8, R102 10 k to GND | GPIO in: **12 V present on DB9 pin 8** | **Active-low** (12 V present → 0). Threshold about 7 V. |
| 43–46 | PE12–PE15 | — | NC | | |
| 47 | PB10 | — | NC | | |
| 48 | PB11 | — | NC | | |
| 49 | VSS_1 | — | R115 0 R to GND (symbol "VCAP1") | | |
| 50 | VDD_1 | VDD_3.3V | | | |
| 51 | PB12 | PB12_LCP2_12V | R112 1 k ← Q9 (R101 pull-up). Q9 base: R98 100 k from J2-13, R99 10 k | GPIO in: **12 V present on LCR PORT 2 pin 13** | **Active-low** |
| 52 | PB13 | PB13_LCP1_12V | R117 1 k ← Q10 (R116 pull-up). Q10 base: R113 100 k from J1-13, R114 10 k | GPIO in: **12 V present on LCR PORT 1 pin 13** | **Active-low**. On an unmodified LCRiQ, pin 13 carries RTS, not +12 V (see §3). |
| 53 | PB14 | — | NC | | |
| 54 | PB15 | PB15_GSM_PWRKEY | R47 1 k → Q4 base (R48 10 k to GND). Q4 collector → EC25.21 PWRKEY (ESD6) | GPIO out: **modem PWRKEY** | **PB15 = 1 pulls PWRKEY low** (key pressed). Default released. |
| 55 | PD8 | PD8_UART2_TXD | R37 200 R → U504.11 T1IN; R120 0 R → U4.4 DI; R131 (DNP); TP4 | **USART2_TX, full remap** → LCR PORT 2 | R10 (DI to GND) DNP |
| 56 | PD9 | PD9_UART2_RXD | R32 200 R ← U504.12 R1OUT; R75 200 R ← U4.1 RO; TP3 | **USART2_RX, full remap** ← LCR PORT 2 | Two drivers, same rule as PA3. |
| 57–62 | PD10–PD15 | — | NC | | |
| 63 | PC6 | PC6_LED_LCP1 | R17 1 k → Q5 (R18) → LED1 (220 R) | GPIO out | Active-high (LCR port 1 LED) |
| 64 | PC7 | PC7_LED_LCP2 | R19 1 k → Q6 (R26) → LED2 (220 R) | GPIO out | Active-high (LCR port 2 LED) |
| 65 | PC8 | PC8_MCU_PWRHOLD | R22 / D22 → U300 CE, **both DNP** | — | Not functional |
| 66 | PC9 | — | NC | | |
| 67 | PA8 | PA8_FLASH2_CS | NC (label only) | — | There is no second flash. |
| 68 | PA9 | PA9_MCU_VBUS | R13 36 k (from EXT_5V) / R14 100 k | GPIO in: **USB-C VBUS present** | Active-high, about 3.7 V at 5 V. The pin is 5 V tolerant. |
| 69 | PA10 | PA10_GSM_DTR | R23 4.7 k → EC25.66 DTR, R9 10 k to GND | GPIO out: **modem DTR** | Divider gives about 2.24 V high into a 1.8 V input (see §6). |
| 70 | PA11 | PA11_FS_DM | R308 (DNP) → USB-C D+ | — | Unused. The option wiring is also swapped. |
| 71 | PA12 | PA12_FS_DP | R307 (DNP) → USB-C D- | — | Unused |
| 72 | PA13 | SWDIO | J100.3, R108 10 k pull-up, test pad SWDIO | **SWDIO** | |
| 73 | NC | — | R122 (DNP) | | |
| 74 | VSS_2 | GND | | | |
| 75 | VDD_2 | VDD_3.3V | | | |
| 76 | PA14 | SWCLK | J100.2, R106 10 k pull-up, test pad SWCLK | **SWCLK** | |
| 77 | PA15 | — | NC | (JTDI) | |
| 78 | PC10 | PC10_UART3_TXD | R7 200 R → YC1021.26 GPIO07 (BT RX); test pad "BT_RX" | **UART3_TX** → Bluetooth | |
| 79 | PC11 | PC11_UART3_RXD | R12 200 R ← YC1021.25 GPIO06 (BT TX); test pad "BT_TX" | **UART3_RX** ← Bluetooth | |
| 80 | PC12 | PC12_UART4_TXD | U603.1 (1A) → U603.6 (1Y, open drain) → EC25.68 RXD, R601 4.7 k pull-up to VDD_EXT 1.8 V; TP7 | **UART4_TX** → modem | The schematic never connects this to the modem (missing sheet 7). PCB confirmed. |
| 81 | PD0 | PD0_WDI | R29 (DNP) → U3 (DNP) | — | No external watchdog |
| 82 | PD1 | PD1_WDI | R33 (DNP) → U3 (DNP) | — | |
| 83 | PD2 | (none) | U603.4 (2Y, open drain) ← U603.3 (2A) ← EC25.67 TXD; R603 4.7 k pull-up to 3.3 V; TP8 | **UART4_RX** ← modem | PCB confirmed |
| 84 | PD3 | PD3_CHG_EN | R180 (DNP) → Q12 (DNP) | — | No charger |
| 85 | PD4 | PD4_BT_RST | R51 1 k → YC1021.9 RESET, C14 10 nF | GPIO out: **BT reset, active-low** | No pull resistor; drive it. |
| 86 | PD5 | PD5_BT_EN | U2.3 CE directly; R2 10 k to VBAT (≈4.7 V), R4 100 k to GND | GPIO out: **BT_3.3V LDO enable** | **BT is ON by default** (CE pulled to about 4.3 V). Drive low to switch it off. The pin sees about 4.3 V while it is an input; PD5 is 5 V tolerant. |
| 87 | PD6 | PD6_WIFI_EN | R59 1 k → U6.3 CE (R60 10 k to GND) | GPIO out: **WLAN_3V3 enable** | Active-high, default off |
| 88 | PD7 | — | NC | | |
| 89–91 | PB3, PB4, PB5 | — | NC | (JTDO / NJTRST) | |
| 92 | PB6 | PB6_UART0_TX | R70 200 R → U9.11 T1IN (→ DB9 J3-3, → printer J5-3 through D15); R91 200 R → USB-C D+ (J8.6/8); test pad TXD0 | **USART0_TX, remap (USART0_REMAP = 1)** | |
| 93 | PB7 | PB7_UART0_RX | R74 200 R ← U9.12 R1OUT (← DB9 J3-2); R90 200 R ← USB-C D- (J8.5/7); test pad RXD0 | **USART0_RX, remap** | **Contention:** U9 is always powered and always drives R1OUT, so a UART adapter on USB-C D- fights it through 200 R + 200 R (see §6). |
| 94 | BOOT0 | BOOT0 | R103 10 k to GND; test pad "BOOT" | | Normal boot from flash |
| 95 | PB8 | (SCL) | R65 0 R → I2C SCL bus: YC1021.12 (via R63 0 R), R61 4.7 k pull-up to BT_3.3V, TP9, U7/U13 (DNP) | **I2C0_SCL, remap (I2C0_REMAP = 1)** | Pull-up is on the **switchable BT_3.3V** rail. |
| 96 | PB9 | (SDA) | R66 0 R → YC1021.13 (via R64), R62 4.7 k, TP10 | **I2C0_SDA, remap** | Not in the schematic MCU sheet; found on the PCB. |
| 97 | PE0 | PE0_TF_EN | NC | — | No TF card |
| 98 | PE1 | PE1_BATT_EN | R35 1 k → Q8 (R36 10 k) → Q7 gate (MT2305 P-MOS, R42 10 k gate pull-up) | GPIO out: **connect coin cell to the PC1 divider** | Active-high. Keep low except during a reading, to save the coin cell. |
| 99 | VSS_3 | — | R39 0 R to GND (R38 DNP to VDD) | | |
| 100 | VDD_3 | VDD_3.3V | | | |

### Peripheral and remap summary (for firmware)

| Peripheral | Pins | AFIO remap | Use |
|---|---|---|---|
| USART0 | PB6 TX / PB7 RX | `USART0_REMAP = 1` | DB9 RS232 (U9), printer J5-3 (TX through D15), USB-C "UART" |
| USART1 | PA2 TX / PA3 RX | none | LCR PORT 1 (J1): RS232 (U504 ch2) or RS485 (U104) |
| USART2 | PD8 TX / PD9 RX | `USART2_REMAP = full (11)` | LCR PORT 2 (J2): RS232 (U504 ch1) or RS485 (U4) |
| UART3 | PC10 TX / PC11 RX | none | YC1021 Bluetooth |
| UART4 | PC12 TX / PD2 RX | none | EC25 modem through SN74LVC2G07. No RTS/CTS/RI/DCD routed. |
| SPI0 | PA5 / PA6 / PA7, CS = PA4 (GPIO) | none | GD25Q256E 32 MB flash |
| I2C0 | PB8 / PB9 | `I2C0_REMAP = 1` | Shared with YC1021 I2C. EEPROM footprint DNP. |
| ADC0/1 | PC0 (IN10), PC1 (IN11) | — | V_12V, coin cell |
| SWD | PA13 / PA14 | — | JTAG pins PA15/PB3/PB4 are free; can use SWJ "SW-DP only". |
| CAN, USBFS, SDIO/EXMC | — | — | Not used |
| Clocks | HXTAL 12 MHz, LXTAL 32.768 kHz | — | **Set `HXTAL_VALUE = 12000000`.** The GD32F30x library default does not match. |

Unused GPIOs available for rework: PA0, PA8, PA15, PB3–PB5, PB10, PB11, PB14, PC2, PC3, PC9, PC13, PD7, PD10–PD15, PE0, PE7–PE10, PE12–PE15 (plus PA1, PA11, PA12, PC8, PD0, PD1, PD3, which only reach DNP parts).

---

## 3. External connectors

### J1 = LCR PORT 1, J2 = LCR PORT 2 (DB25 female, D-DMR025PF-D002)

Assumption: J1 = physical "LCR PORT 1". This follows the LCP1 net naming (J1 pin 13 → PB13_**LCP1**) and "PORT 1 on the LED/front side" in the docs. **Physical mapping UNVERIFIED.**
LCR-side meaning comes from Liquid Controls cable 81513-040 and splitter 81515B.

| DB25 pin | LCR meaning (81513-040) | J1 (PORT 1) inside the box | J2 (PORT 2) inside the box |
|---|---|---|---|
| 2 | Printer port "Transmit Data" (to meter RXD, term. 28) | R76 0 R → **J5-2**; also J2-2 and (R72 0 R) J3-2 | R82 0 R → J5-2 |
| 3 | Printer port "Receive Data" (from meter TXD, term. 27) | R77 0 R → D14 → **J5-3** | R83 0 R → D14 → J5-3 |
| 6 | DSR (meter RTS 26) | R78 0 R → J5-6 | R84 → J5-6 |
| 7 | Printer signal GND (term. 30) | R79 0 R → J5-7. **Not tied to board GND** (R126 DNP). | R85 → J5-7 |
| 11 | Secondary signal GND (term. 51) | **GND** | GND |
| 13 | +12 V from meter (term. 46). On LCRiQ this is RTS-92 unless the "red wire to +VBATT" modification is done. | D1 SM360A → 12V_IN (power in); R113 → Q10 → **PB13** (active-low detect) | D3 → 12V_IN; R98 → Q9 → **PB12** |
| 14 | Secondary TX **into the meter** (meter RXD term. 49) | B2 bead ← U504.7 **T2OUT** (← PA2 USART1_TX); R229 47 R ↔ U104.7 **RS485 B**; R231 10 k pull-down; D4 NUP2105L TVS; D9 ↔ J5-14 | B4 ← U504.14 T1OUT (← PD8); R25 47 R ↔ U4.7 B; R118 pull-down; D5; D17 ↔ J5-14 |
| 15 | Secondary RX **from the meter** (meter TXD term. 48) | B1 → U504.8 **R2IN** (→ PA3 USART1_RX); R230 47 R ↔ U104.6 **RS485 A**; R252 10 k pull-up to RS485_3.3V; D4; D10 → J5-15 | B3 → U504.13 R1IN (→ PD9); R28 47 R ↔ U4.6 A; R24 pull-up; D5; D18 → J5-15 |
| 19 | Secondary RTS (meter CTS term. 50) | R80 0 R → J5-19 | R86 → J5-19 |
| 20 | DTR (meter CTS 29) | R81 0 R → J5-20 | R87 → J5-20 |
| others, shell | — | NC | NC |

RS485 termination (R250 / R119, 120 R position) is **DNP**. Pins 14/15 carry either RS232 or RS485, chosen by which LDO the MCU turns on. **The choice is global: both ports are RS232 or both are RS485**, because U8 powers U504, which serves both ports, and U11 powers both RS485 chips. The LCRiQ printer guide shows the meter COM1 "LCP" port set to **RS232, 19200 baud**, and the printer port to RS232, 9600.

### J5 = PRINTER PORT (DB25 male, D-DMR025PM-D002)

The box re-creates the LC "serial splitter": the printer port is a pass-through from both meters' printer ports, plus the box's own USART0 TX.

| J5 pin | Connection |
|---|---|
| 2 | J1-2, J2-2 (0 R) and J3-2 (DB9 RXD, through R72 0 R). ESD2 DNP. |
| 3 | Diode-OR: D14 (from J1-3/J2-3) + D15 (from J3-3 = U9 T1OUT = PB6 USART0_TX). ESD1 DNP. |
| 6 | J1-6 / J2-6 (ESD3 DNP) |
| 7 | J1-7 / J2-7 (ESD4 DNP; R126 to GND DNP) |
| 11 | GND |
| 13 | **12V_IN bus** (F1 input side, TP11): diode-OR of J1-13, J2-13, J3-8. The box can also be powered from here. |
| 14 | D9 → J1-14, D17 → J2-14 |
| 15 | D10 ← J1-15, D18 ← J2-15 |
| 19, 20 | J1/J2 pins 19 / 20 (ESD12/13 DNP) |
| others | NC |

Diode direction assumes **pad 1 = anode**, which is consistent with the schematic for D1/D3/D23. **UNVERIFIED** for D9/D10/D14/D15/D17/D18.

### J3 = SERIAL / POWER (DB9 male, D-DMR009PM-D002, DTE)

| Pin | Signal | Inside |
|---|---|---|
| 2 | RXD (into the box) | B5 → U9.13 R1IN → U9.12 R1OUT → R74 → **PB7 (USART0_RX)**; also R72 0 R → J5-2 / J1-2 / J2-2; D13 TVS. (B7 is a DNP swap option.) |
| 3 | TXD (out of the box) | U9.14 T1OUT → B6 → pin 3 (← R70 ← **PB6 USART0_TX**); also D15 → J5-3; D13. (B8 is a DNP swap option.) |
| 5 | GND | GND |
| 8 | **+12 V power in** | D23 SM360A → 12V_IN; R100 → Q11 → **PE11** (active-low detect) |
| 1, 4, 6, 7, 9 | — | NC |

### J8 = USB-C (SHK16-B130, 12P)

| Pin | Signal | Inside |
|---|---|---|
| 2, 11 | VBUS | EXT_5V: D2 SS34 → VBAT; D6 → U300 CE; D301 SD24C TVS; R13/R14 → PA9 |
| 6, 8 | D+ | R91 200 R → **PB6 USART0_TX**; ESD500; (R308 DNP → PA11) |
| 5, 7 | D- | R90 200 R → **PB7 USART0_RX**; ESD501; (R307 DNP → PA12) |
| 3, 9 | SBU2, SBU1 | 330 pF (C43/C42) to GND; R34 DNP |
| 4, 10 | CC1, CC2 | **Not connected** |
| 1, 12, 13–16 | GND / shell | GND |

The schematic's cable sketch (5 black GND, 4 ID null, 3 green D+, 2 white D-, 1 red 5 V) implies a special USB cable wired to a 3.3 V TTL UART adapter: green = box TX, white = box RX.

### J100 = SWD header (4-pin)

| Pin | Signal |
|---|---|
| 1 | **GND** (the schematic wrongly labels it VCC_5V) |
| 2 | SWCLK / PA14 (10 k pull-up) |
| 3 | SWDIO / PA13 (10 k pull-up) |
| 4 | VMCU_3.3V |

### J18 = micro-SIM (C792-3, push-push, 6+1P)

| Pin | Signal | Inside |
|---|---|---|
| 1 | C1 VDD | EC25.14 USIM_VDD |
| 2 | C2 RST | R607 0 R → EC25.17 |
| 3 | C3 CLK | R606 0 R → EC25.16 |
| 7 | C7 DATA | R605 0 R → EC25.15; R604 10 k pull-up to USIM_VDD |
| 5 | C5 GND | GND |
| 9 | CD | **NC** (no SIM detect) |

ESD5/7/8/14 and 33 pF caps are on the SIM lines.

### J7 = CR1220 holder

Pin 1 = + (→ D11, Q7), pin 2 = GND.

### RF connectors (IPEX, 20279001E-01)

| Ref | Service | Path |
|---|---|---|
| ANT1 | LTE main | EC25.49 ANT_MAIN → L2 0 R (C610/C10 match DNP) → ESD15 |
| ANT2 | GNSS | EC25.47 → L3 0 R → C41 100 pF. **3.3 V active-antenna bias** from VMCU through R11 10 R + L1 (bead, 600 R/100 MHz per BOM; the schematic says 47 nH). Bias is always on. |
| ANT3 | Wi-Fi | FC20N.30 → L800 0 R |
| ANT4 | Bluetooth | YC1021.19 RF → L4 0 R → U10 GSR2401.5, then U10.14 → C29 15 pF → ANT4 |

EC25 ANT_DIV (pin 35) is **not connected**.

### Test points (useful for bring-up)

| Test point | Signal |
|---|---|
| TP1 / TP2 | PA3 / PA2 (USART1) |
| TP3 / TP4 | PD9 / PD8 (USART2) |
| TP7 / TP8 | PC12 / PD2 (UART4 to modem) |
| TP9 / TP10 | I2C SCL / SDA |
| TP11 | 12V_IN |
| TP13 | EC25 VDD_EXT (1.8 V) |
| TXD0 / RXD0 | PB6 / PB7 |
| BT_TX / BT_RX | PC11 / PC10 |
| ICE | YC1021 ICE |
| BT_3V3, WIFI_3V3, 3V3 | Rails |
| BOOT | BOOT0 |
| SWDIO / SWCLK | SWD |
| SWCLK2 | NRST |
| SWCLK1 | GND |
| 4G pads | 4G_VBAT, 4G_VBAT1, 4G_GND, 4G_PWRKEY, 4G_VBUS, 4G_DP, 4G_DM (EC25 USB, for firmware update / QPST), 4G_STATUS, 4G_TX, 4G_RX, USB_BOOT (EC25.115) |
| GND | GND1..GND8, TP5, TP6, TP12 |

---

## 4. Power tree and control pins

| Rail | Source | Enable / control | Loads |
|---|---|---|---|
| 12V_IN | J1-13 (D1), J2-13 (D3), J3-8 (D23), J5-13 direct | — | F1 |
| V_12V | 12V_IN through F1 2 A fuse | — | U200 buck, PC0 divider |
| VCC_5V ≈ 5.2 V | U200 MP4560DN | EN = R1/R205 divider on Vin (always on above about 5 V; threshold **UNVERIFIED**) | D7 → VBAT, D21 → U300 CE |
| EXT_5V | USB-C VBUS | — | D2 → VBAT, D6 → U300 CE, PA9 sense |
| VBAT ≈ 4.7 V | D2 / D7 OR | — | All LDOs below |
| VMCU_3.3V | U300 RT9193-33 | CE = diode-OR of 5 V sources through R21. **No MCU control** (PC8 hold and WDT paths DNP). | MCU (via B102/B104 = VDD_3.3V), U9, U103 flash, LEDs, pull-ups, GNSS antenna bias, J100 pin 4 |
| VGSM (≈3.8–4.0 V expected; **UNVERIFIED**) | U401 TJ4330-ADJ | **PE2** high = on (R173 pull-down) | EC25 VBAT_BB/VBAT_RF (pins 57–60) |
| VDD_EXT 1.8 V | EC25 pin 7 output | Present when the modem is on | U603 VCC, R601 pull-up, FC20N VIO, R145 |
| BT_3.3V | U2 RT9193-33 | **PD5** (default ON via R2 to VBAT; drive low = off) | YC1021, GSR2401, I2C pull-ups (via R94 0 R) |
| WLAN_3V3 | U6 RT9080-33 | **PD6** high = on | FC20N |
| RS232_3.3V | U8 RT9193-33 | **PE5** high = on | U504 (both LCR-port RS232 channels) |
| RS485_3.3V | U11 RT9193-33 | **PE6** high = on | U104, U4, A-line bias pull-ups |
| VBACK (MCU VBAT pin) | CR1220 via D11, VMCU via D12 | — | RTC / backup domain |

### Modem control

| Signal | Path | Polarity / notes |
|---|---|---|
| Power | PE2 → U401 EN | Active-high |
| PWRKEY | PB15 → Q4 NPN → EC25.21 | PB15 = 1 pulls PWRKEY low. EC25 power-on needs PWRKEY low for at least 500 ms after VBAT is stable (Quectel spec). Power-off: pull low again for at least 650 ms, or send AT+QPOWD. |
| RESET_N (EC25.20) | Not connected | Recover the modem by power-cycling VGSM (PE2) or with PWRKEY. |
| STATUS (EC25.61) | Test pad only | Not readable by the MCU |
| NET_STATUS / NET_MODE, RI, DCD, CTS, RTS, W_DISABLE#, AP_READY, WAKEUP_IN | Not connected | No hardware flow control |
| DTR | PA10 through a 4.7 k / 10 k divider | |
| UART | MCU PC12 → U603 → EC25 RXD (pull-up to 1.8 V); EC25 TXD → U603 → PD2 (pull-up to 3.3 V) | U603 SN74LVC2G07 is open-drain and powered from VDD_EXT, so the link only works while the modem is on. Speed is limited by the 4.7 k pull-ups: 115200 is safe; 921600 is **UNVERIFIED**. |

### Other control lines

| Function | Line | Polarity |
|---|---|---|
| Bluetooth reset | PD4 → YC1021 RESET | Active-low |
| Bluetooth UART | UART3 | — |
| BT PA/LNA control | YC1021 GPIO04 (pin 23, R69 0 R) → U10.1; GPIO05 (pin 24, R71 3.3 k) → U10.16 | Assumed TX/RX enable of an RFX2401C-compatible FEM. **UNVERIFIED**. The schematic's names "IO4_BT_RX / IO5_BT_TX" are misleading. |
| Wi-Fi | FC20N reset, SDIO and coexistence signals go to the EC25 (pins 129–138) | The MCU only switches power (PD6). |

---

## 5. BOM key parts

| Ref | Part | Description |
|---|---|---|
| U100 | GD32F305VCT6 | MCU, LQFP100 |
| U1 | Quectel EC25AFA-512-STD | LTE Cat 4 + GNSS (North America variant) |
| U801 | Quectel FC20N-Q93 | Wi-Fi module (SDIO slave of EC25) |
| U5 | YC1021 (QFN32) | Bluetooth SoC |
| U10 | GSR2401 (QFN16) | BT 2.4 GHz PA/LNA front end (not in schematic) |
| U103 | GD25Q256EYIGR | 256 Mbit SPI NOR |
| U9, U504 | BL13232ETS (BOM also: SIT3232EEUE, TSSOP16) | Dual RS232 transceivers |
| U4, U104 | SIT3088EESA (SOP-8) | RS485 transceivers |
| U603 | SN74LVC2G07DBVR (BOM PN RS2G07XH6, SOT-23-6) | Dual open-drain buffer, modem UART level shift |
| U200 | MP4560DN (SOIC-8) | 55 V / 2 A buck |
| U401 | TJ4330GDP-ADJ (SOP-8) | 3 A adjustable LDO for modem |
| U2, U8, U11, U300 | RT9193-33GB (BOM PN OCP2820WE33AD, SOT-23-5, 300 mA) | 3.3 V LDOs. The schematic says RT9080. |
| U6 | RT9080-33GJ5 (BOM PN RS1562-33S5, 600 mA) | Wi-Fi LDO |
| X101 | 12 MHz, SMD3225, ±10 ppm, CL 20 pF | MCU HXTAL |
| X1 | 32.768 kHz, FC-135, CL 12.5 pF (SF32WK32768D31T002) | MCU LXTAL |
| X2 | 24 MHz, 3225, ±10 ppm, CL 12 pF | YC1021 crystal |
| Q1–Q6, Q8, Q13, Q14, Q16 | S9014W NPN (SOT-323) | LED drivers, PWRKEY, BATT_EN, RS485 DE |
| Q9, Q10, Q11 | MMBT5551 NPN | 12 V detect |
| Q7 | MT2305 P-MOS (BOM PN MLS2305A) | Coin-cell measurement switch |
| D1, D3, D23 | SM360A (SMA) | 12 V input OR diodes |
| D2, D7, D201 | SS34 (SMB) | |
| D501 | SMBJ36A | Input TVS |
| D301 | SD24C (SOD-323) | VBUS TVS |
| D24 | PT3C4V5B | VGSM TVS |
| D4, D5, D13 | NUP2105L | Line TVS for RS485/RS232 pairs |
| D302 | DAN217U | ADC clamp |
| D6, D9–D12, D14, D15, D17, D18, D21 | 1N4148WS | |
| ESD5–9, 14–17, 500, 501 | ESD9L5.0ST5G | |
| F1 | 2 A 63 V 1206 fuse (Brightking 12H 2A) | The schematic shows PTC SMD1206P150TF. |
| L100 | SWPA6045S150MT, 15 µH | Buck inductor |
| B1–B6, B102, B104, L1 | 600 R @ 100 MHz 0402 beads | |
| C1 | 100 µF / 50 V electrolytic | |
| C6, C40 | 10 µF / 50 V 1210 (UMK325AB7106KM-P) | |
| C13, C27, C28 | 100 µF / 6.3 V tantalum (TAJB107M006) | |
| J1, J2 | D-DMR025PF-D002 | DB25 female, right angle (LCR ports) |
| J5 | D-DMR025PM-D002 | DB25 male (printer) |
| J3 | D-DMR009PM-D002 | DB9 male (serial / power) |
| J8 | SHK16-B130 | USB-C 12P |
| J18 | C792-3 | Micro-SIM push-push, H1.5 |
| J7 | HYC-CR1220-2 | Coin-cell holder |
| ANT1–4 | 20279001E-01 | IPEX Gen1 |
| LED3 | BL-C34S-C (A1844SURSYGUBUOB/S530-A3) | 4-colour LED array, through-hole |
| LED1, LED2 | A214B/SUR/S530-A3 | 3 mm red |
| Key resistor values (BOM) | | R5 200 k / R6 36 k (buck FB); R1 100 k / R205 36 k (buck EN); R203 200 k; R202 36 k; R170 6.8 k 1% / R171 1.2 k 1% (VGSM); R312 100 k 1% / R314 10 k 1%; R313 / R315 100 k 1%; R13 36 k 1% / R14 100 k; R601 / R603 4.7 k; R23 4.7 k / R9 10 k; UART series resistors 200 R; RS485 series 47 R; R123 / R55 1 M |

**Not fitted** (on the PCB, not in the BOM): U3 PIC10F200 watchdog, U7 EEPROM, U13 CAN transceiver, U302 charger, J300 battery connector, Q12, Q305, Q306, R8, R10, R20, R22, R27, R29, R33, R34, R38, R40, R67, R68, R88, R89, R93, R119, R122, R126, R127, R131, R135, R136, R155, R180, R181, R250, R307, R308, R316, R318, R322, R325, D8, D22, B7, B8, ESD1–4, ESD10–13 and several NC capacitors.
The printer-setup PPTX has no text; its slides are images, including the LCRiQ J13/J14 wiring and the pin-13 12 V modification.

---

## 6. Open questions, contradictions and risks

1. **Schematic incomplete and out of date.**
   - It is "Sheet x of 7" with only 6 sheets. Missing: U603 modem level shifter, U10 BT PA, Q13/Q14 RS485 direction drivers, R5/R6 buck feedback, SWD pull-ups R106/R108, I2C0 link R65/R66 to PB8/PB9, DB9 bead options B5–B8, R72 (J3-2 ↔ J5-2), D15.
   - The MCU symbol is an STM32-style pinout (VCAP1/VCAP2, pin 19 "VDD"). It is handled by 0 R jumpers R41 (pin 19 → GND), R39 (pin 99 → GND), R115 (pin 49 → GND), R122 (pin 73, DNP). PCB and BOM are correct for the GD32.
   - Use this document's PCB-derived netlist, not the schematic.
2. **VGSM voltage UNVERIFIED.** It depends on the TJ4330 reference voltage and on which U401 pin is EN (schematic: pin 2 = EN, driven by PE2; pin 1 has a 10 k pull-up to VBAT). **Measure VGSM with PE2 = 0 and PE2 = 1 before first power-up with the modem fitted.** The EC25 absolute maximum is 4.3 V.
3. **Schematic vs BOM value mismatches.**
   - R1 120 k → 100 k; R205 47 k → 36 k; R202 47 k → 36 k; R13 47 k → 36 k.
   - Series resistors 220 R → 200 R.
   - F1 PTC → 2 A fuse; D301 UDZSTE-17 16B → SD24C; D501 SMBJ36CA → SMBJ36A; D24 ESD56151W04 → PT3C4V5B.
   - U2/U8/U11/U300 RT9080 → RT9193.
   - Modem EC20-A → EC25AFA.
   - L1 47 nH → 600 R bead (L1 is listed with the beads in the BOM).
   - The BOM wins for fitted values.
4. **RS485 transmit path.** The schematic shows DI grounded (R135/R10 = 0 R) with the UART TX lines NC (R3/R120). The BOM fits R3/R120 0 R and omits R135/R10. So DI = UART TX, and direction is controlled only by PE3/PE4 (R127/R131 auto-direction resistors not in the BOM). **Confirm on a real board.** With PE3/PE4 floating at reset, the RS485 drivers default to transmit when powered.
5. **RS232 and RS485 share the LCR port pins.** DB25 pins 14/15 are wired to both transceivers, and both RX outputs drive the same MCU pin. Firmware must power exactly one of U8 (PE5) or U11 (PE6). The unpowered transceiver still loads the line: the RS232 receiver has about 5 k input to GND, and the RS485 bias resistors are 10 k. The mode is global to both ports.
6. **USART0 RX contention.** PB7 is driven permanently by U9 R1OUT (U9 is always powered) through 200 R, and also by USB-C D- through 200 R. A UART adapter on the USB-C port will only produce about mid-rail levels unless it is much stronger. USB-C "UART" RX is therefore likely unreliable. USB-C TX (PB6 → D+) is fine. **UNVERIFIED on hardware.**
7. **USB-C CC1/CC2 are floating** (no 5.1 k Rd). A USB-C-to-C charger will not provide VBUS; only USB-A-to-C cables power the board. The USB-C port is not USB. The native USB option (R307/R308) is also wired swapped (PA11 DM → D+, PA12 DP → D-).
8. **Modem DTR over-voltage.** PA10 through the 4.7 k / 10 k divider gives about 2.24 V into a 1.8 V-domain EC25 input. That is above typical VIH max (about 2.0 V). Keep PA10 low, or tri-state it, if DTR is not needed. **UNVERIFIED** against the EC25 absolute maximum.
9. **Modem visibility.** RESET_N, STATUS, NET_STATUS, RI and flow control are not connected to the MCU. EC25 USB is reachable only on test pads (needed for EC25 firmware updates). ANT_DIV is not connected; some carriers may require diversity.
10. **Wi-Fi is not MCU-controllable** except for power. FC20N works only through the EC25 SDIO host and EC25 firmware support.
11. **No hold-up power.** There is no Li battery or charger, and PWRHOLD / watchdog are DNP. The MCU turns off whenever 12 V (truck ignition or battery) and USB are both gone. Only the RTC survives, on the CR1220. There is no external watchdog, so use the internal FWDGT.
12. **LCR pin 13.** LCR-II provides +12 V on DB25 pin 13, but the LCRiQ wires RTS there unless the red wire is moved to +VBATT (printer-setup slides). PB12/PB13 "12 V present" readings and power-from-meter depend on this.
13. **J1-7 / J2-7** (meter printer-port signal ground) are passed only to J5-7 and not tied to board GND (R126 DNP). Verify that printer-port ground reference is intended.
14. **Diode orientation** for D9, D10, D14, D15, D17 and D18 (printer-port diode-ORs) was inferred assuming pad 1 = anode. **UNVERIFIED.**
15. **Physical port identity.** J1 = "LCR PORT 1" and J2 = "LCR PORT 2" are inferred from net names. The LED3 colour-to-pin mapping (red/green/blue/orange) is taken from net names only. **Verify on hardware.**
16. **Datasheet set mismatch.** `rf5745_data_sheet.pdf` (Qorvo WiFi FEM) matches no part on the BOM or PCB; its pinout does not fit U10's footprint. U10 is BOM "GSR2401". Its pin functions (TXEN/RXEN on pins 1/16, RF in pin 5, ANT pin 14) are assumed RFX2401C-compatible (**UNVERIFIED**). The function diagram's "16 MHz crystal" is wrong (X2 = 24 MHz).
17. **Crystal load capacitors.** X101 is specified for CL = 20 pF but has 10 pF caps (effective about 8 pF), so the frequency will run slightly high. This is fine for UART and USB tolerance, but confirm if accurate timing matters.
18. **I2C pull-ups are on BT_3.3V.** If PD5 turns Bluetooth off, the I2C0 bus loses its pull-ups and YC1021 is unpowered on the bus.
19. **PD4 (BT reset) has no pull resistor.** Its level during MCU reset is undefined, so drive it early.
20. **ISP / bootloader.** The GD32 ROM bootloader UART is on USART0 **default pins PA9/PA10**, which here are VBUS sense and modem DTR. The DB9 and USB-C use the remapped PB6/PB7. System-memory UART boot through the external ports will likely **not** work (**UNVERIFIED**). Use SWD (J100) for programming. BOOT0 is on test pad "BOOT".
21. **GD25Q256E above 16 MB** needs 4-byte address mode. Do not set the QE bit expecting quad I/O: WP# is hard-tied to GND and HOLD# to VCC.
