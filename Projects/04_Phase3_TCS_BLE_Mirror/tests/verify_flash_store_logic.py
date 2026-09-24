"""
Algorithmic cross-check of flash_store.c's design -- NOT a compile/run test
of the actual C source. This project has no host gcc/QEMU setup for the
firmware itself, so this re-implements flash_store.c's exact index/CRC
logic in Python (line-for-line matched against the C -- re-check both
together if you change one) against a simulated NOR flash chip (erase sets
bytes to 0xFF, "program" can only AND bits, matching real flash semantics),
and stress-tests it for the failure modes that matter: CRC correctness,
settings round-robin rotation, history log wraparound/eviction math, and
boot-time recovery.

Run: python tests/verify_flash_store_logic.py

A pass here means the ALGORITHM is sound. It does not catch C-specific bugs
(struct packing/sizeof mistakes, a typo'd field name, an actual SPI/timing
problem, etc.) -- that still needs the real compiled firmware on real
hardware, gated by flash_store_selftest() (see main.c).
"""

SECTOR_SIZE = 4096
RECORD_SIZE = 32
RECORDS_PER_SECTOR = SECTOR_SIZE // RECORD_SIZE  # 128, matches flash_store.c
MAGIC = 0xA5


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def check_crc_known_vector():
    # Standard CRC-16/CCITT-FALSE check value for ASCII "123456789" is 0x29B1.
    got = crc16_ccitt(b"123456789")
    assert got == 0x29B1, f"CRC16 implementation is wrong: got {got:04X}, expected 29B1"
    print("PASS: crc16_ccitt matches the standard CRC-16/CCITT-FALSE test vector (0x29B1)")


class FakeFlash:
    """Byte-array flash simulator with real NOR semantics: erase -> 0xFF,
    program can only clear bits (AND), never set them, matching gd25q.c's
    real chip behavior."""
    def __init__(self, size):
        self.data = bytearray([0xFF] * size)
        self.erase_count = 0
        self.program_count = 0

    def erase_sector(self, addr):
        base = addr - (addr % SECTOR_SIZE)
        for i in range(SECTOR_SIZE):
            self.data[base + i] = 0xFF
        self.erase_count += 1

    def program(self, addr, buf):
        self.program_count += 1
        for i, b in enumerate(buf):
            self.data[addr + i] &= b  # NOR program: AND-in, like the real chip

    def read(self, addr, length):
        return bytes(self.data[addr:addr + length])


# -------- Settings (16 x 256-byte round-robin slots in one 4KB sector) --------
SETTINGS_SLOT_SIZE = 256
SETTINGS_SLOTS = SECTOR_SIZE // SETTINGS_SLOT_SIZE  # 16
SETTINGS_BASE = 0


def settings_record_bytes(generation, name):
    # Simplified stand-in for the real ~175-byte stored_settings_t --
    # only `generation` and `name` matter for this logic test.
    payload = generation.to_bytes(4, "little") + name.encode().ljust(32, b"\0")
    crc = crc16_ccitt(bytes([MAGIC]) + payload)
    return bytes([MAGIC]) + payload + crc.to_bytes(2, "little")


def settings_record_parse(raw):
    if raw[0] != MAGIC:
        return None
    payload = raw[1:-2]
    crc_stored = int.from_bytes(raw[-2:], "little")
    if crc16_ccitt(bytes([MAGIC]) + payload) != crc_stored:
        return None
    generation = int.from_bytes(raw[0 + 1:5], "little")
    name = payload[4:36].rstrip(b"\0").decode()
    return generation, name


class SettingsStore:
    def __init__(self, flash):
        self.flash = flash
        self.next_slot, self.generation, _ = self._scan()

    def _scan(self):
        best_slot, best_gen, found = 0, 0, False
        for slot in range(SETTINGS_SLOTS):
            raw = self.flash.read(SETTINGS_BASE + slot * SETTINGS_SLOT_SIZE, 39)
            parsed = settings_record_parse(raw)
            if parsed and (not found or parsed[0] > best_gen):
                found, best_gen, best_slot = True, parsed[0], slot
        return ((best_slot + 1) % SETTINGS_SLOTS if found else 0), (best_gen if found else 0), found

    def load(self):
        best = None
        for slot in range(SETTINGS_SLOTS):
            raw = self.flash.read(SETTINGS_BASE + slot * SETTINGS_SLOT_SIZE, 39)
            parsed = settings_record_parse(raw)
            if parsed and (best is None or parsed[0] > best[0]):
                best = parsed
        return best  # (generation, name) or None

    def save(self, name):
        if self.next_slot == 0:
            self.flash.erase_sector(SETTINGS_BASE)
        self.generation += 1
        rec = settings_record_bytes(self.generation, name)
        self.flash.program(SETTINGS_BASE + self.next_slot * SETTINGS_SLOT_SIZE, rec)
        self.next_slot = (self.next_slot + 1) % SETTINGS_SLOTS


def test_settings_roundtrip():
    flash = FakeFlash(SECTOR_SIZE)
    store = SettingsStore(flash)
    assert store.load() is None, "fresh/erased flash must report no valid settings"

    names = [f"Box_{i}" for i in range(50)]
    for i, name in enumerate(names):
        store.save(name)
        # Simulate a reboot: rebuild a fresh SettingsStore from the same
        # flash contents and confirm it loads exactly what was just saved.
        reloaded = SettingsStore(flash)
        got = reloaded.load()
        assert got is not None, f"iteration {i}: expected a valid record after save"
        assert got[1] == name, f"iteration {i}: loaded {got[1]!r}, expected {name!r}"

    # 50 saves over 16 slots/sector: next_slot starts at 0 on a fresh scan,
    # so save #1 also triggers an erase (of an already-erased sector --
    # harmless, just a one-time no-op cost on virgin flash), then again
    # every 16 saves after that: saves 1, 17, 33, 49 -> 4 erases total.
    assert flash.erase_count == 4, f"expected 4 sector erases over 50 saves, got {flash.erase_count}"
    print(f"PASS: settings round-robin -- 50 saves/reboots all loaded correctly,"
          f" sector erased {flash.erase_count} times as expected")


# -------- History log (circular, erase-ahead-on-sector-boundary) --------
class HistoryLog:
    def __init__(self, flash, base, capacity_records):
        self.flash = flash
        self.base = base
        self.capacity = capacity_records
        self.write_index, self.oldest_index, self.valid_count, self.next_seq = self._scan()

    def _record_bytes(self, seq, meter):
        payload = seq.to_bytes(4, "little") + meter.to_bytes(4, "little")
        payload = payload.ljust(RECORD_SIZE - 3, b"\0")  # pad to leave room for magic+crc
        crc = crc16_ccitt(bytes([MAGIC]) + payload)
        rec = bytes([MAGIC]) + payload + crc.to_bytes(2, "little")
        assert len(rec) == RECORD_SIZE
        return rec

    def _parse(self, raw):
        if raw[0] != MAGIC:
            return None
        payload = raw[1:-2]
        crc_stored = int.from_bytes(raw[-2:], "little")
        if crc16_ccitt(bytes([MAGIC]) + payload) != crc_stored:
            return None
        seq = int.from_bytes(payload[0:4], "little")
        return seq

    def _addr(self, idx):
        return self.base + idx * RECORD_SIZE

    def _read(self, idx):
        return self._parse(self.flash.read(self._addr(idx), RECORD_SIZE))

    def _scan(self):
        idx = 0
        last_seq = 0
        while idx < self.capacity:
            seq = self._read(idx)
            if seq is None:
                break
            last_seq = seq
            idx += 1
        return idx % self.capacity, 0, idx, (last_seq + 1) if idx > 0 else 1

    def append(self, meter):
        idx = self.write_index
        addr = self._addr(idx)
        if idx % RECORDS_PER_SECTOR == 0:
            self.flash.erase_sector(addr)
            if self.valid_count >= self.capacity:
                self.oldest_index = (self.oldest_index + RECORDS_PER_SECTOR) % self.capacity
                self.valid_count -= RECORDS_PER_SECTOR

        seq = self.next_seq
        self.next_seq += 1
        self.flash.program(addr, self._record_bytes(seq, meter))

        self.write_index = (idx + 1) % self.capacity
        if self.valid_count < self.capacity:
            self.valid_count += 1
        return seq

    def read(self, cursor):
        if cursor >= self.valid_count:
            return None
        idx = (self.oldest_index + cursor) % self.capacity
        return self._read(idx)

    def erase_all(self):
        self.write_index = 0
        self.oldest_index = 0
        self.valid_count = 0


def test_history_wraparound():
    # Small region: 4 sectors x 128 records/sector = 512 capacity -- big
    # enough to be realistic, small enough to force several wraps quickly.
    capacity = 4 * RECORDS_PER_SECTOR
    flash = FakeFlash(capacity * RECORD_SIZE)
    log = HistoryLog(flash, 0, capacity)

    assert log.valid_count == 0 and log.read(0) is None, "fresh log must be empty"

    # Land exactly on a sector boundary (multiple of RECORDS_PER_SECTOR
    # beyond the initial fill) so valid_count is deterministically back to
    # `capacity` -- see the follow-up test below for what happens mid-sector.
    total_appends = capacity * 3
    seqs_written = [log.append(meter=1) for _ in range(total_appends)]

    assert log.valid_count == capacity, f"log should be full: valid_count={log.valid_count}, capacity={capacity}"

    # After wrapping, read(0)..read(capacity-1) must be exactly the last
    # `capacity` sequence numbers written, in increasing (FIFO) order.
    expected = seqs_written[-capacity:]
    got = [log.read(c) for c in range(capacity)]
    assert got == expected, "FIFO order broken after wraparound:\n" \
        f"  expected first 5 / last 5: {expected[:5]} ... {expected[-5:]}\n" \
        f"  got      first 5 / last 5: {got[:5]} ... {got[-5:]}"
    assert log.read(capacity) is None, "reading past valid_count must return nothing"
    print(f"PASS: history log survives {total_appends} appends across "
          f"{total_appends // RECORDS_PER_SECTOR} sector boundaries, "
          f"FIFO order intact, exactly the last {capacity} records retained")

    # Sector-granularity eviction check: appending partway into a sector
    # refill after a wrap should show valid_count temporarily DIP by up to
    # RECORDS_PER_SECTOR below capacity (a whole sector's old records were
    # just invalidated by one erase, not evicted one at a time) -- this is
    # expected/correct behavior for this design, not a bug, but worth
    # confirming explicitly rather than assuming.
    partial = 77
    for _ in range(partial):
        log.append(meter=1)
    expected_dip = capacity - RECORDS_PER_SECTOR + partial
    assert log.valid_count == expected_dip, \
        f"expected the documented sector-eviction dip to {expected_dip}, got {log.valid_count}"
    print(f"PASS: sector-granularity eviction dip confirmed as expected "
          f"({partial} appends into a sector refill -> valid_count={log.valid_count},"
          f" not capacity={capacity} -- correct, whole-sector eviction is by design)")

    # Boot recovery (no wrap yet): fresh log, a modest number of appends
    # (well under capacity, so the "scan from 0 until first invalid slot"
    # limitation doesn't apply), simulate reboot, confirm recovered state
    # matches pre-reboot state exactly.
    flash2 = FakeFlash(capacity * RECORD_SIZE)
    log2 = HistoryLog(flash2, 0, capacity)
    for i in range(200):
        log2.append(meter=0)
    recovered = HistoryLog(flash2, 0, capacity)
    assert recovered.valid_count == log2.valid_count == 200
    assert recovered.write_index == log2.write_index
    assert recovered.next_seq == log2.next_seq
    assert [recovered.read(c) for c in range(200)] == [log2.read(c) for c in range(200)]
    print("PASS: boot-time recovery (pre-wrap case) exactly reconstructs "
          "write_index/valid_count/next_seq and all record contents")

    # DeleteAll: logical reset, then confirm new appends correctly reclaim
    # (erase-ahead) and the log behaves like fresh from the caller's view.
    log2.erase_all()
    assert log2.valid_count == 0 and log2.read(0) is None
    new_seq = log2.append(meter=0)
    assert new_seq == 201, "next_seq must stay monotonic across DeleteAll, not reset to 0"
    assert log2.read(0) == 201
    print("PASS: DeleteAll (logical erase) resets the visible log; "
          "next append reclaims correctly and seq stays monotonic")


if __name__ == "__main__":
    check_crc_known_vector()
    test_settings_roundtrip()
    test_history_wraparound()
    print("\nALL LOGIC CHECKS PASSED")
