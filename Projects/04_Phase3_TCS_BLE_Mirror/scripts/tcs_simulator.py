"""
tcs_simulator.py -- Simulates one LCR meter node on COM7 at 19200 baud.

Listens for TCS binary frames from the PandaBox, responds to all commands
the Phase-3 firmware sends so the happy-flow end-to-end test passes.

TCS frame format (TX from PandaBox):
    [0x7E][dest][src=0x41][flag][cmd][len][data...][crc8]
    Escaping: 0x7E and 0x7D bytes in the payload are prefixed with 0x7D.
    CRC-8 computed over all bytes including 0x7E start, excluding CRC.

Firmware opcodes confirmed from tcs.c source:
    Boot scan:
        flag=0x20, cmd=0xA9  Scan node (tcs_scan_node step 1)
        flag=0x40, cmd=0x0C  Status (tcs_scan_node step 2)
    Start (fire-and-forget, no reply needed but we ack for cleanliness):
        flag=0x20, cmd=0xA9  Clear transaction (reuses scan opcode)
        flag=0x20, cmd=0x37  Configure direct delivery
        flag=0x20, cmd=0x38  Configure preset delivery
        flag=0x20, cmd=0x3C  Start delivery
    Stop/Pause/Resume/Print (single-frame, ack expected):
        flag=0x20, cmd=0x3D  Stop
        flag=0x20, cmd=0x39  Pause
        flag=0x20, cmd=0x3A  Resume
        flag=0x20, cmd=0x3E  Print
    GetData field reads (flag=0x40, ack expected with 8-byte IEEE-754 double at data[2]):
        cmd=0x42  flow_tenths
        cmd=0x2B  gross_tenths (current delivery)
        cmd=0x1E  system_gross_tenths (totalizer)
        cmd=0x1C  net_totalizer_tenths

Reply data format for field reads (10 bytes total):
    [0x00][0x00][8-byte big-endian IEEE-754 double]
    Firmware reads the double from rx_buf[8] (0-indexed from frame start),
    which corresponds to data[2] — the two leading 0x00 bytes are required.

Usage:
    python scripts/tcs_simulator.py COM7
    python scripts/tcs_simulator.py COM7 --node 1 --baud 19200
"""

import serial
import sys
import time
import struct
import argparse

# Dallas/Maxim CRC-8 table (copied from tcs.c)
CRC_TABLE = [
    0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32, 163, 253, 31, 65,
    157, 195, 33, 127, 252, 162, 64, 30, 95, 1, 227, 189, 62, 96, 130, 220,
    35, 125, 159, 193, 66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
    190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158, 29, 67, 161, 255,
    70, 24, 250, 164, 39, 121, 155, 197, 132, 218, 56, 102, 229, 187, 89, 7,
    219, 133, 103, 57, 186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
    101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69, 198, 152, 122, 36,
    248, 166, 68, 26, 153, 199, 37, 123, 58, 100, 134, 216, 91, 5, 231, 185,
    140, 210, 48, 110, 237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
    17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49, 178, 236, 14, 80,
    175, 241, 19, 77, 206, 144, 114, 44, 109, 51, 209, 143, 12, 82, 176, 238,
    50, 108, 142, 208, 83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
    202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234, 105, 55, 213, 139,
    87, 9, 235, 181, 54, 104, 138, 212, 149, 203, 41, 119, 244, 170, 72, 22,
    233, 183, 85, 11, 136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
    116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84, 215, 137, 107, 53,
]

TCS_START = 0x7E
TCS_ESC   = 0x7D
TCS_HOST  = 0x41  # PandaBox host address (src in TX frames, dest in RX frames)


def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc = CRC_TABLE[crc ^ b]
    return crc


def escape_byte(b: int) -> bytes:
    if b == TCS_START or b == TCS_ESC:
        return bytes([TCS_ESC, b])
    return bytes([b])


def build_frame(dest: int, src: int, flag: int, cmd: int, data: bytes) -> bytes:
    """Build a TCS frame with CRC and escaping."""
    raw = bytes([TCS_START])
    crc = CRC_TABLE[0 ^ TCS_START]

    def add(b: int):
        nonlocal crc, raw
        raw += escape_byte(b)
        crc = CRC_TABLE[crc ^ b]

    add(dest)
    add(src)
    add(flag)
    add(cmd)
    add(len(data))
    for b in data:
        add(b)
    raw += escape_byte(crc)
    return raw


def tenths_to_double_bytes(tenths: int) -> bytes:
    """Convert integer tenths-of-a-unit to big-endian IEEE-754 double."""
    value = tenths / 10.0
    return struct.pack('>d', value)


def parse_frame(buf: bytearray):
    """
    Parse one TCS frame from buf (raw bytes, NOT de-escaped — matches
    the real firmware's rx_feed() which also does NOT de-escape on receive).
    Returns (frame_dict, bytes_consumed) or (None, 0).
    """
    if len(buf) < 7:
        return None, 0
    if buf[0] != TCS_START:
        return None, 0

    # Collect unescaped header bytes (dest src flag cmd len) = 5 bytes
    unesc = bytearray()
    i = 1
    while i < len(buf) and len(unesc) < 5:
        if buf[i] == TCS_ESC and i + 1 < len(buf):
            unesc.append(buf[i + 1])
            i += 2
        else:
            unesc.append(buf[i])
            i += 1

    if len(unesc) < 5:
        return None, 0  # incomplete header

    dest, src, flag, cmd, data_len = unesc[0], unesc[1], unesc[2], unesc[3], unesc[4]
    total_unesc_needed = 5 + data_len + 1  # header(5) + data + crc

    while i < len(buf) and len(unesc) < total_unesc_needed:
        if buf[i] == TCS_ESC and i + 1 < len(buf):
            unesc.append(buf[i + 1])
            i += 2
        else:
            unesc.append(buf[i])
            i += 1

    if len(unesc) < total_unesc_needed:
        return None, 0  # incomplete

    payload = bytes(unesc[5:5 + data_len])
    frame_crc = unesc[5 + data_len]

    check_data = bytes([TCS_START]) + bytes(unesc[:5 + data_len])
    computed = crc8(check_data)

    frame = {
        'dest': dest, 'src': src, 'flag': flag, 'cmd': cmd,
        'data': payload, 'crc_ok': (computed == frame_crc),
        'raw_bytes_consumed': i + 1,  # +1 for the leading 0x7E
    }
    return frame, i + 1


# Simulated meter state
class MeterState:
    def __init__(self, node: int):
        self.node = node
        self.measuring = False
        self.paused = False
        self.counter = 0
        # Simulated values in tenths-of-a-gallon / tenths-of-gal-per-min
        self._flow_tenths = 0          # idle until Start
        self._gross_tenths = 0         # current delivery
        self._sys_gross_tenths = 50000  # running totalizer (5000.0 gal)
        self._net_total_tenths = 0
        self._last_tick = time.time()

    def tick(self):
        """Advance simulated flow/totals each call."""
        now = time.time()
        dt = now - self._last_tick
        self._last_tick = now
        if self.measuring and not self.paused:
            # 5.0 gal/min = 50 tenths/min = 50/60 tenths/sec
            rate = 50.0 / 60.0  # tenths per second
            delta = int(rate * dt)
            self._gross_tenths += delta
            self._sys_gross_tenths += delta
            self._flow_tenths = 50  # 5.0 gal/min
        else:
            self._flow_tenths = 0

    def start(self):
        self.measuring = True
        self.paused = False
        self._gross_tenths = 0
        self.counter += 1
        print(f"  [STATE] delivery #{self.counter} started")

    def stop(self):
        self.measuring = False
        self.paused = False
        print(f"  [STATE] delivery stopped, gross={self._gross_tenths/10:.1f} gal")

    def pause(self):
        self.paused = True
        print(f"  [STATE] delivery paused")

    def resume(self):
        self.paused = False
        print(f"  [STATE] delivery resumed")

    def field_bytes(self, cmd: int) -> bytes:
        """Return 10-byte response data: [0x00][0x00][8-byte double]."""
        self.tick()
        field_map = {
            0x42: self._flow_tenths,         # flow rate
            0x2B: self._gross_tenths,         # current delivery gross
            0x1E: self._sys_gross_tenths,     # system gross totalizer
            0x1C: self._net_total_tenths,     # net totalizer
        }
        tenths = field_map.get(cmd, 0)
        d = tenths_to_double_bytes(tenths)    # 8 bytes, big-endian IEEE-754
        return bytes([0x00, 0x00]) + d        # 10 bytes total (firmware reads double from data[2])


def handle_frame(frame: dict, state: MeterState, port: serial.Serial) -> None:
    dest = frame['dest']
    src  = frame['src']
    flag = frame['flag']
    cmd  = frame['cmd']
    data = frame['data']
    ok   = frame['crc_ok']

    cmd_names = {
        0xA9: 'Scan/Clear',
        0x0C: 'Status',
        0x37: 'ConfigDirect',
        0x38: 'ConfigPreset',
        0x3C: 'StartDelivery',
        0x3D: 'Stop',
        0x39: 'Pause',
        0x3A: 'Resume',
        0x3E: 'Print',
        0x42: 'GetFlow',
        0x2B: 'GetGross',
        0x1E: 'GetSysGross',
        0x1C: 'GetNetTotal',
    }
    name = cmd_names.get(cmd, f'0x{cmd:02X}')
    print(f"  RX dest=0x{dest:02X} src=0x{src:02X} flag=0x{flag:02X} cmd={name} "
          f"len={len(data)} crc={'OK' if ok else 'FAIL'}")

    if not ok:
        print("  [CRC fail, ignoring]")
        return

    if dest != state.node and dest != 0xFF:
        return  # not for us

    resp = None

    if cmd == 0xA9:
        # Scan / Clear transaction — respond with our node address
        resp_data = bytes([state.node])
        resp = build_frame(TCS_HOST, state.node, 0x20, 0xA9, resp_data)
        print(f"  -> Scan/Clear ack for node {state.node}")

    elif cmd == 0x0C:
        # Status request
        status = 0x0100  # node type=LCR-II
        if state.measuring:
            status |= 0x01
        if state.paused:
            status |= 0x02
        sd = bytes([state.node, 0x00, (status >> 8) & 0xFF, status & 0xFF])
        resp = build_frame(TCS_HOST, state.node, 0x40, 0x0C, sd)
        print(f"  -> Status 0x{status:04X}")

    elif cmd in (0x37, 0x38):
        # Configure delivery — ack (firmware doesn't wait but we reply anyway)
        resp = build_frame(TCS_HOST, state.node, flag | 0x40, cmd, bytes([0x00]))
        print(f"  -> Config delivery ack")

    elif cmd == 0x3C:
        # Start delivery
        state.start()
        resp = build_frame(TCS_HOST, state.node, flag | 0x40, cmd, bytes([0x00]))
        print(f"  -> StartDelivery ack")

    elif cmd == 0x3D:
        # Stop
        state.stop()
        resp = build_frame(TCS_HOST, state.node, flag | 0x40, cmd, bytes([0x00]))
        print(f"  -> Stop ack")

    elif cmd == 0x39:
        # Pause
        state.pause()
        resp = build_frame(TCS_HOST, state.node, flag | 0x40, cmd, bytes([0x00]))
        print(f"  -> Pause ack")

    elif cmd == 0x3A:
        # Resume
        state.resume()
        resp = build_frame(TCS_HOST, state.node, flag | 0x40, cmd, bytes([0x00]))
        print(f"  -> Resume ack")

    elif cmd == 0x3E:
        # Print ticket
        resp = build_frame(TCS_HOST, state.node, flag | 0x40, cmd, bytes([0x00]))
        print(f"  -> Print ack")

    elif cmd in (0x42, 0x2B, 0x1E, 0x1C):
        # GetData field read — respond with 10-byte payload: [0x00][0x00][double]
        fdata = state.field_bytes(cmd)
        resp = build_frame(TCS_HOST, state.node, flag | 0x40, cmd, fdata)
        tenths = {0x42: state._flow_tenths, 0x2B: state._gross_tenths,
                  0x1E: state._sys_gross_tenths, 0x1C: state._net_total_tenths}.get(cmd, 0)
        print(f"  -> Field 0x{cmd:02X} = {tenths/10:.1f}")

    else:
        print(f"  [unknown cmd 0x{cmd:02X}, no reply]")

    if resp:
        time.sleep(0.002)  # 2ms turnaround
        port.write(resp)
        port.flush()
        print(f"  TX {len(resp)} bytes: {resp.hex()[:24]}{'...' if len(resp) > 12 else ''}")


def main():
    parser = argparse.ArgumentParser(description="TCS meter simulator (Phase-3 firmware opcodes)")
    parser.add_argument("port", help="Serial port (e.g. COM7)")
    parser.add_argument("--node", type=int, default=1, help="Node address to simulate (default 1)")
    parser.add_argument("--baud", type=int, default=19200, help="Baud rate (default 19200)")
    args = parser.parse_args()

    print(f"TCS simulator v2 — node={args.node} on {args.port} at {args.baud} baud")
    print("Handles: Scan/Clear(0xA9) Status(0x0C) Start(0x3C) Stop(0x3D)")
    print("         Pause(0x39) Resume(0x3A) Print(0x3E)")
    print("         GetFlow(0x42) GetGross(0x2B) GetSysGross(0x1E) GetNetTotal(0x1C)")
    print("Press Ctrl+C to stop.\n")

    state = MeterState(args.node)
    buf = bytearray()

    with serial.Serial(args.port, args.baud, bytesize=8, parity='N', stopbits=1,
                       timeout=0.05, rtscts=False, dsrdtr=False) as ser:
        while True:
            chunk = ser.read(64)
            if chunk:
                buf += chunk
                print(f"RX raw ({len(chunk):2d}B): {chunk.hex()}")

            while True:
                start = buf.find(TCS_START)
                if start < 0:
                    buf.clear()
                    break
                if start > 0:
                    print(f"  [skipping {start} garbage bytes before 0x7E]")
                    buf = buf[start:]

                frame, consumed = parse_frame(buf)
                if frame is None:
                    break  # need more data
                buf = buf[consumed:]
                handle_frame(frame, state, ser)


if __name__ == "__main__":
    main()
