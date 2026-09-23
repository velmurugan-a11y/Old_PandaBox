"""Rebuild the external-flash dump from the session log and decode Leo's LCR delivery records.

Input : logs/session_*.log  (lines "FL <addr> <hex bytes>" printed by `flash rd`)
Output: logs/extflash_dump.bin   sparse image: only dumped ranges, gaps = 0xFF, starts at 0
        logs/extflash_records.csv decoded 64-byte records per port
        printed summary

Record layout, from Leo's app_lcr.c (little-endian, 64 bytes):
    u32 time (RTC epoch) | u32 f[6] (LCR fields #2, #4, #17, #18, #100, #101, in tenths)
    | u32 longi | u32 longimm | u32 lati | u32 latimm | 20 bytes reserved
"""

import csv
import datetime
import glob
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "..", "logs")
PORT_BASE = {1: 0x0000000, 2: 0x1000000}
FIELDS = ["GrossQty#2", "FlowRate#4", "GrossTotal#17", "NetTotal#18", "PrevGross#100", "PrevNet#101"]


def load_dump():
    mem = {}
    for path in sorted(glob.glob(os.path.join(LOG_DIR, "session_*.log"))):
        with open(path, encoding="utf-8") as f:
            for line in f:
                if not line.startswith("FL "):
                    continue
                parts = line.split()
                addr = int(parts[1], 16)
                for i, h in enumerate(parts[2:]):
                    mem[addr + i] = int(h, 16)
    return mem


def get(mem, addr, n):
    return bytes(mem.get(addr + i, 0xFF) for i in range(n))


def main():
    mem = load_dump()
    if not mem:
        print("no FL lines found")
        return
    # one .bin per contiguous dumped range (the flash is 32 MB but only ~98 sectors are used)
    addrs = sorted(mem)
    start = prev = addrs[0]
    ranges = []
    for a in addrs[1:] + [None]:
        if a is None or a != prev + 1:
            ranges.append((start, prev + 1))
            start = a
        prev = a if a is not None else prev
    for lo, hi in ranges:
        name = "extflash_0x%07X-0x%07X.bin" % (lo, hi - 1)
        with open(os.path.join(LOG_DIR, name), "wb") as f:
            f.write(bytes(mem[i] for i in range(lo, hi)))
        print("wrote %s (%d bytes)" % (name, hi - lo))
    print("dumped bytes: %d" % len(mem))

    rows = []
    for port, base in PORT_BASE.items():
        hdr = get(mem, base, 24)
        dev, a, b, c, d, flag = struct.unpack("<BxxxIIIII", hdr)
        print("port %d header @0x%07X: %s" % (port, base, hdr.hex(" ")))
        print("   devnum=%d  words=%d, %d, 0x%X, %d  flag=0x%08X" % (dev, a, b, c, d, flag))
        n_rec = 0
        dumped = sorted(a for a in mem if base + 0x1000 <= a < base + 0x1000000 and a % 64 == 0)
        for addr in dumped:
            rec = get(mem, addr, 64)
            if rec == b"\xFF" * 64:
                continue
            t, *vals = struct.unpack("<I6I4I", rec[:44])
            f6, gps = vals[:6], vals[6:]
            try:
                ts = datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
            except (OverflowError, OSError, ValueError):
                ts = "invalid"
            signed = [struct.unpack("<i", struct.pack("<I", v))[0] for v in f6]
            rows.append([port, "0x%07X" % addr, t, ts] + ["%.1f" % (v / 10.0) for v in signed]
                        + list(gps) + [rec[44:].hex()])
            n_rec += 1
        print("   records decoded: %d" % n_rec)

    out = os.path.join(LOG_DIR, "extflash_records.csv")
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["port", "addr", "epoch", "utc"] + FIELDS + ["longi", "longimm", "lati", "latimm", "reserved"])
        w.writerows(rows)
    print("wrote %s (%d rows)" % (out, len(rows)))
    for r in rows[:5] + rows[-3:]:
        print("  ", r[:10] + r[10:14])


if __name__ == "__main__":
    main()
