#ifndef YC1021_H
#define YC1021_H

#include <stdint.h>

/*
 * yc1021.c -- driver for the real BT/BLE chip on this board: a Yichip
 * YC1021 (QFN32) at board reference U5, confirmed via a firmware
 * extraction the user pulled from a real X-Box V2.86 host application
 * image ("yichip_yc1021_bt_firmware.bin", the actual module firmware
 * the host flashes to it over UART3) -- matching identifying strings
 * ("YichipSmartSPP", "YichipSmartLE", "HV1.001", "SPP slave") verified
 * byte-for-byte at their documented offsets, plus surrounding bytes
 * that read as standard Bluetooth HCI UART framing (0x01=HCI Command,
 * 0x02=ACL Data, 0x04=HCI Event), consistent with the [0x01]/[0x02]/
 * [0x04]-prefixed frames reconstructed from app_bt.c disassembly
 * earlier this session.
 *
 * This corrects an earlier misidentification this session: a physical
 * board inspection initially read U5 as an "FC20N" chip, which led to a
 * full rewrite around Quectel AT commands (see git history / session
 * notes for ble_fc20n.c, since removed). The firmware-extraction
 * evidence here is far stronger (pulled from the compiled host binary
 * with matching schematic reference designators) and clarifies that
 * FC20N is a real but SEPARATE chip at U801, serving WiFi -- not U5,
 * not BT. U5 is genuinely a YC1021.
 *
 * PROTOCOL CONFIRMED ON REAL HARDWARE: the chip speaks plain HCI ("H4",
 * a bare [0x01]=command/[0x04]=event type-byte prefix, no SLIP framing)
 * over UART3, NOT HCI-H5 as earlier attempts assumed -- see yc1021.c's
 * header comment for how this was proven (a reference bring-up rig on
 * this same board got clean HCI_Reset/Read_Local_Version/Read_BD_ADDR
 * responses). This driver currently only runs that same bring-up
 * handshake to confirm the link is alive; BLE advertising/GATT and the
 * App ASCII protocol (BoxInfo etc.) are follow-on work, not yet done.
 */
void yc1021_init(void);      /* resets + enables the YC1021, brings up its UART */
void yc1021_poll(void);      /* call repeatedly from the main loop */
int  yc1021_is_synced(void); /* 1 once online (BT or BLE connected) has been observed */

#endif /* YC1021_H */
