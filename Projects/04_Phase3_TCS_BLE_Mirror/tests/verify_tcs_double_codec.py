"""
Algorithmic cross-check of tcs.c's tcs_double_bits_to_tenths()/
tcs_tenths_to_double_bits() -- pure-integer IEEE754-double <-> tenths
codec (no `double`/`float` C type used in tcs_double_bits_to_tenths(),
per this project's no-floats convention). This is the exact logic that
was designed and verified here in Python BEFORE being transcribed to C
-- re-check both together if you change one. Cross-checked against
Python's own struct-based double handling (IEEE754-correct by
construction), so a pass here means the bit-manipulation algorithm is
correct; it does not substitute for testing tcs_get_data()/tcs_start()
against a real TCS meter (see the port plan's Phase 3 verification).

Run: python tests/verify_tcs_double_codec.py
"""
import struct
import random

MANT_MASK = (1 << 52) - 1


def bitlen(x: int) -> int:
    return x.bit_length()


def decode_wire_to_tenths(wire: bytes) -> int:
    """Mirrors the planned C tcs_double_bits_to_tenths() exactly."""
    bits = int.from_bytes(wire, "big")
    sign = (bits >> 63) & 1
    exp = (bits >> 52) & 0x7FF
    mant = bits & MANT_MASK

    if exp == 0 or exp == 0x7FF:
        return 0  # zero/subnormal/Inf/NaN -- not a real meter reading

    significand = (1 << 52) | mant  # 53-bit
    shift = exp - 1075
    numerator = significand * 10

    if shift >= 0:
        return 0  # unreachable for any realistic meter value

    rshift = -shift
    if rshift >= 64:
        tenths_mag = 0
    else:
        rounding = (1 << (rshift - 1)) if rshift > 0 else 0
        tenths_mag = (numerator + rounding) >> rshift

    return -tenths_mag if sign else tenths_mag


def encode_tenths_to_wire(tenths: int) -> bytes:
    """Mirrors the planned C tcs_tenths_to_double_bits() exactly."""
    sign = 1 if tenths < 0 else 0
    mag = -tenths if sign else tenths

    if mag == 0:
        bits = 0
    else:
        mbits = bitlen(mag)
        shift_up = 63 - mbits
        quotient = (mag << shift_up) // 10  # floor(value * 2^shift_up)

        q_bits = bitlen(quotient)
        norm_shift = q_bits - 53
        if norm_shift > 0:
            rounding = 1 << (norm_shift - 1)
            normalized = (quotient + rounding) >> norm_shift
            if normalized >= (1 << 53):  # rounding overflowed into the next power of 2
                normalized >>= 1
                norm_shift += 1
        elif norm_shift < 0:
            normalized = quotient << (-norm_shift)
        else:
            normalized = quotient

        exp_field = 1075 + norm_shift - shift_up
        assert 1 <= exp_field <= 2046, f"exponent out of normal range: {exp_field} for tenths={tenths}"
        bits = (sign << 63) | (exp_field << 52) | (normalized & MANT_MASK)

    return bits.to_bytes(8, "big")


def check_decode_against_python(value: float, expected_tenths: int):
    wire = struct.pack(">d", value)  # big-endian, matches the confirmed wire format
    got = decode_wire_to_tenths(wire)
    assert got == expected_tenths, f"decode({value}) = {got}, expected {expected_tenths}"


def check_encode_roundtrip(tenths: int):
    wire = encode_tenths_to_wire(tenths)
    # Cross-check: what double does this bit pattern represent (per Python/IEEE754)?
    (as_double,) = struct.unpack(">d", wire)
    decoded_back = decode_wire_to_tenths(wire)
    assert decoded_back == tenths, \
        f"encode/decode round trip broken for tenths={tenths}: " \
        f"encoded as {as_double!r}, decoded back as {decoded_back}"


if __name__ == "__main__":
    # 1. Decode: real struct.pack values -> known tenths
    known = [
        (0.0, 0), (1.0, 10), (0.5, 5), (100.0, 1000), (1234.5, 12345),
        (1989.4, 19894), (73767.4, 737674), (0.1, 1), (999999.9, 9999999),
        (3366600.9, 33666009),
    ]
    for value, expected in known:
        check_decode_against_python(value, expected)
    print(f"PASS: decode matches Python/struct for {len(known)} known real-world-shaped values")

    # 2. Decode: negative values
    for value, expected in known[1:]:
        check_decode_against_python(-value, -expected)
    print("PASS: decode handles negative values (sign bit) correctly")

    # 3. Decode: zero, subnormal, inf, nan all safely return 0 (documented limitation)
    assert decode_wire_to_tenths(struct.pack(">d", 0.0)) == 0
    assert decode_wire_to_tenths(struct.pack(">d", -0.0)) == 0
    assert decode_wire_to_tenths(struct.pack(">d", float("inf"))) == 0
    assert decode_wire_to_tenths(struct.pack(">d", float("-inf"))) == 0
    assert decode_wire_to_tenths(struct.pack(">d", float("nan"))) == 0
    assert decode_wire_to_tenths(struct.pack(">d", 5e-320)) == 0  # subnormal
    print("PASS: zero/subnormal/Inf/NaN all safely decode to 0 (documented limitation, not a crash/garbage value)")

    # 4. Encode/decode round trip across a wide, realistic range (gallons, tenths-scaled)
    random.seed(1234)
    test_values = list(range(0, 2000)) + [random.randint(0, 50_000_000) for _ in range(5000)]
    test_values += [-v for v in test_values if v != 0]
    for tenths in test_values:
        check_encode_roundtrip(tenths)
    print(f"PASS: encode->decode round trip exact for {len(test_values)} values "
          f"(0 to 5,000,000.0 gallons range, both signs)")

    # 5. Encode: spot-check a few values against Python's own float division for sanity
    for tenths in [0, 1, 5, 10, 12345, 9999999, -12345]:
        wire = encode_tenths_to_wire(tenths)
        (as_double,) = struct.unpack(">d", wire)
        expected = tenths / 10.0
        assert abs(as_double - expected) < 1e-6, f"encode({tenths}) = {as_double}, expected ~{expected}"
    print("PASS: encoded doubles match tenths/10.0 to within float precision")

    print("\nALL TCS DOUBLE CODEC CHECKS PASSED")
