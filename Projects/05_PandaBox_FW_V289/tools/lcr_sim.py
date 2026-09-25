#!/usr/bin/env python3
"""Minimal LCR-II meter simulator on a serial port, speaking real LCP to the PandaBox.

The PandaBox polls fields 2/4/17/18/100/101 with Get-Field (cmd 0x20) and issues Start/Stop/etc
(cmd 0x24). This answers as the meter node(s) given, with a slowly rising delivery so the tester
sees flow>0 and a growing totalizer. Frame/CRC verified byte-identical to Leo's real meter.

Usage: python lcr_sim.py COM7 [--baud 19200] [--nodes 1,2]
"""
import sys, time, argparse

def crc_lcp(data):
    crc = 0x7E7E
    for b in data:
        for i in range(7, -1, -1):
            carry = crc & 0x8000
            crc = ((crc << 1) | ((b >> i) & 1)) & 0xFFFF
            if carry:
                crc ^= 0x1021
    return crc

SYNC = 0x7E
ESC = 0x1B

def build(to, frm, status, data):
    body = bytes([to, frm, status, len(data)]) + bytes(data)
    crc = crc_lcp(body)
    out = bytearray([SYNC, SYNC])
    for b in list(body) + [crc & 0xFF, crc >> 8]:
        if b in (SYNC, ESC):
            out.append(ESC)
        out.append(b)
    return bytes(out)

def parse(buf):
    """yield (to,frm,status,data,consumed_upto) for each complete frame in buf; returns leftover."""
    frames = []
    i = 0
    n = len(buf)
    while i + 1 < n:
        if buf[i] == SYNC and buf[i + 1] == SYNC:
            # unescape from i+2
            j = i + 2
            raw = []
            while j < n and len(raw) < 4 + 255 + 2:
                b = buf[j]
                if b == ESC:
                    j += 1
                    if j >= n:
                        return frames, buf[i:]
                    raw.append(buf[j])
                elif b == SYNC:
                    break
                else:
                    raw.append(b)
                j += 1
                if len(raw) >= 4 and len(raw) == 4 + raw[3] + 2:
                    break
            if len(raw) >= 6 and len(raw) == 4 + raw[3] + 2:
                to, frm, status, ln = raw[0], raw[1], raw[2], raw[3]
                data = raw[4:4 + ln]
                frames.append((to, frm, status, data))
                i = j
                continue
            else:
                return frames, buf[i:]
        i += 1
    return frames, buf[i:]

class Meter:
    def __init__(self, node):
        self.node = node
        self.gross = 0            # tenths
        self.flow = 0             # tenths gpm
        self.total = 1234560 + node * 100000
        self.running = False
        self.last = time.time()

    def tick(self):
        now = time.time()
        dt = now - self.last
        self.last = now
        if self.running:
            self.flow = 600           # 60.0 gpm
            d = int(self.flow / 60.0 * dt * 10)  # tenths added
            self.gross += d
            self.total += d
        else:
            self.flow = 0

    def field(self, f):
        self.tick()
        return {2: self.gross, 4: self.flow, 17: self.total, 18: 0,
                100: self.total - self.gross, 101: 0}.get(f, 0)

def main():
    import serial
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--baud", type=int, default=19200)
    ap.add_argument("--nodes", default="1,2")
    ap.add_argument("--seconds", type=float, default=0)   # 0 = forever
    ap.add_argument("--autostart", action="store_true")
    args = ap.parse_args()
    nodes = {int(x): Meter(int(x)) for x in args.nodes.split(",")}
    if args.autostart:
        for m in nodes.values():
            m.running = True
    s = serial.Serial(args.port, args.baud, timeout=0.02)
    print(f"LCR sim on {args.port} @ {args.baud}, nodes {list(nodes)}", flush=True)
    buf = bytearray()
    t0 = time.time()
    npoll = 0
    while args.seconds == 0 or (time.time() - t0) < args.seconds:
        d = s.read(64)
        if d:
            buf += d
            frames, leftover = parse(bytes(buf))
            buf = bytearray(leftover)
            for to, frm, status, data in frames:
                if to not in nodes or len(data) < 1:
                    continue
                m = nodes[to]
                msgid = status & 0x01
                cmd = data[0]
                if cmd == 0x20 and len(data) >= 2:          # get field
                    v = m.field(data[1]) & 0xFFFFFFFF
                    rsp = build(0x14, to, 0x80 | msgid,
                                [0x00, 0x21, (v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF])
                    s.write(rsp)
                    npoll += 1
                elif cmd == 0x24 and len(data) >= 2:        # issue command
                    k = data[1]
                    if k == 0:      m.running = True
                    elif k == 2:    m.running = False; m.gross = 0
                    elif k == 1:    m.running = False
                    s.write(build(0x14, to, 0x80 | msgid, [0x00, 0x00]))
                elif cmd == 0x21:                            # set field / preset
                    s.write(build(0x14, to, 0x80 | msgid, [0x00, 0x00]))
                elif cmd == 0x00:                            # sync / product id
                    s.write(build(0x14, to, 0x80 | msgid, [0x00] + list(b"SR200b2.05")))
                elif cmd == 0x28:                            # find node / status
                    s.write(build(0x14, to, 0x80 | msgid, [0x00, 0x00, 0x00, 0x00, 0x00, 0x00]))
        if npoll and npoll % 60 == 0:
            print(f"answered {npoll} polls; node1 gross={nodes.get(1) and nodes[1].gross}", flush=True)
            npoll += 1
    s.close()

if __name__ == "__main__":
    main()
