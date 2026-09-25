"""Annotate the objdump output of Leo's V2.89 app with literal values, strings and call targets.

Usage: python annotate.py            (reads v289_app1.bin + v289_raw.dis, writes v289.dis + funcs.txt)
"""
import re, struct, collections

BASE = 0x08008000
BIN = open('v289_app1.bin', 'rb').read()
END = BASE + len(BIN)


def u32(a):
    o = a - BASE
    return struct.unpack_from('<I', BIN, o)[0] if 0 <= o <= len(BIN) - 4 else None


def cstr(a, maxlen=120):
    o = a - BASE
    if not (0 <= o < len(BIN)):
        return None
    e = BIN.find(b'\0', o, o + 400)
    if e < 0 or e - o < 2:
        return None
    s = BIN[o:e]
    if not all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        return None
    return s[:maxlen].decode().encode('unicode_escape').decode()


PERIPH = {
    0x40013800: 'USART0', 0x40004400: 'USART1', 0x40004800: 'USART2', 0x40004C00: 'UART3', 0x40005000: 'UART4',
    0x40013000: 'SPI0', 0x40022000: 'FMC', 0x40003000: 'FWDGT', 0x40002800: 'RTC', 0x40006C00: 'BKP',
    0x40012400: 'ADC0', 0x40000400: 'TIMER2', 0x40021000: 'RCU', 0x40007000: 'PMU', 0x40010000: 'AFIO',
    0x40010400: 'EXTI', 0x40010800: 'GPIOA', 0x40010C00: 'GPIOB', 0x40011000: 'GPIOC', 0x40011400: 'GPIOD',
    0x40011800: 'GPIOE', 0xE000ED08: 'SCB_VTOR', 0x1FFFF7E0: 'FLASH_SIZE', 0x1FFFF7E8: 'UID',
}


def describe(v):
    if v is None:
        return ''
    s = cstr(v)
    if s is not None:
        return '"%s"' % s
    if v in PERIPH:
        return PERIPH[v]
    for b, n in PERIPH.items():
        if b <= v < b + 0x400 and b >= 0x40000000:
            return '%s+0x%X' % (n, v - b)
    if 0x20000000 <= v < 0x20018000:
        return 'ram_%08X' % v
    if BASE <= (v & ~1) < END:
        return 'fn_%08X' % (v & ~1) if v & 1 else 'rom_%08X' % v
    return ''


lines = open('v289_raw.dis').read().splitlines()
calls = collections.Counter()
callers = collections.defaultdict(set)
ptrs = set()
out = []
ldr_re = re.compile(r'^\s*([0-9a-f]+):\s+[0-9a-f ]+\s+ldr(?:\.w)?\s+(r\d+|sl|fp|ip|lr), \[pc, #(-?\d+)\]')
bl_re = re.compile(r'^\s*([0-9a-f]+):.*\bbl\s+([0-9a-f]+)')
for ln in lines:
    m = ldr_re.match(ln)
    if m:
        pc = int(m.group(1), 16)
        lit = ((pc + 4) & ~3) + int(m.group(3))
        v = u32(lit)
        d = describe(v)
        ln = '%s\t; =0x%08X %s' % (ln, v if v is not None else 0, d)
    m = re.search(r'\(adr(?:\.w)? \w+, ([0-9a-f]+) ', ln)
    if m:
        a = int(m.group(1), 16)
        d = describe(a)
        ln = '%s\t; =0x%08X %s' % (ln, a, d)
    m = bl_re.match(ln)
    if m:
        t = int(m.group(2), 16)
        calls[t] += 1
        callers[t].add(int(m.group(1), 16))
    out.append(ln)

# also treat code pointers loaded from literal pools as function entries
for ln in out:
    m = re.search(r'; =0x([0-9A-F]{8}) fn_([0-9A-F]{8})', ln)
    if m:
        t = int(m.group(2), 16)
        if t not in calls:
            calls[t] += 0
            ptrs.add(t)
# label function entries (BL targets)
labelled = []
for ln in out:
    m = re.match(r'^\s*([0-9a-f]+):', ln)
    if m and int(m.group(1), 16) in calls:
        a = int(m.group(1), 16)
        labelled.append('\n; ===== fn_%08X  (called %d x)' % (a, calls[a]))
    labelled.append(re.sub(r'\bbl\s+([0-9a-f]+) <[^>]*>', lambda mm: 'bl fn_%08X' % int(mm.group(1), 16), ln))
open('v289.dis', 'w').write('\n'.join(labelled))

# per-function string summary: which strings each function references
funcs = sorted(calls)
refs = collections.defaultdict(list)
cur = None
for ln in labelled:
    m = re.match(r'^; ===== fn_([0-9A-F]+)', ln.strip())
    if m:
        cur = int(m.group(1), 16)
        continue
    if cur and '; =0x' in ln and '"' in ln:
        refs[cur].append(ln.split('; =0x', 1)[1][9:])
with open('funcs.txt', 'w') as f:
    for a in funcs:
        f.write('fn_%08X calls=%d  %s\n' % (a, calls[a], ' | '.join(refs[a])[:600]))
print('functions:', len(funcs))
