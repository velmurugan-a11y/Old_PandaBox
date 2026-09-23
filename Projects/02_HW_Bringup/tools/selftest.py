"""Run every peripheral handshake on the PandaBox and write a status table.

Usage: python tools/selftest.py [--port COM9] [--skip gsm]
Output: logs/status_<date>_<time>.md  (plus the usual session log / results.csv via bringup.py)
"""

import argparse
import datetime
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "..", "logs")

# (name, MCU interface + pins, command)
TESTS = [
    ("Clock 12 MHz -> 120 MHz", "HXTAL OSCIN/OSCOUT (12/13), PLL", "info"),
    ("Debug UART", "USART0 remap PB6 92 / PB7 93", "ping"),
    ("External flash GD25Q256E", "SPI0 PA5/6/7 (30-32), CS PA4 29", "flash id"),
    ("ADC 12 V / coin cell", "ADC0 IN10 PC0 15, IN11 PC1 16, PE1 98", "adc"),
    ("Power-detect inputs", "PB13 52, PB12 51, PE11 42, PA9 68", "inputs"),
    ("RTC / 32.768 kHz", "LXTAL PC14/PC15 (8/9)", "rtc"),
    ("LCR1 RS232 (U504)", "USART1 PA2 25 / PA3 26, PE5 4", "rs232 1"),
    ("LCR2 RS232 (U504)", "USART2 remap PD8 55 / PD9 56, PE5 4", "rs232 2"),
    ("LCR1 RS485 (U104)", "USART1, PE6 5, DIR PE3 2", "rs485 1"),
    ("LCR2 RS485 (U4)", "USART2, PE6 5, DIR PE4 3", "rs485 2"),
    ("LCR transceivers ack (no back-power)", "PE5 4, PE6 5, PE3 2, PE4 3, TX held low", "rsack"),
    ("4G modem EC25", "UART4 PC12 80 / PD2 83, PE2 1, PWRKEY PB15 54", "gsm test"),
    ("Bluetooth YC1021 HCI", "UART3 PC10 78 / PC11 79, RST PD4 85, EN PD5 86", "bt"),
    ("Bluetooth YC1021 full config", "UART3 (init table + 7 steps)", "bt up"),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--skip", nargs="*", default=[], help="skip tests whose command starts with these words")
    a = ap.parse_args()

    rows = []
    for name, iface, cmd in TESTS:
        if any(cmd.startswith(s) for s in a.skip):
            rows.append((name, iface, cmd, "SKIP", ""))
            continue
        out = subprocess.run([sys.executable, os.path.join(HERE, "bringup.py"), "--port", a.port, "--quiet", cmd],
                             capture_output=True, text=True)
        # bringup.py is quiet; read the RESULT line back from today's session log
        log = os.path.join(LOG_DIR, "session_%s.log" % datetime.date.today().isoformat())
        text = open(log, encoding="utf-8").read()
        block = text[text.rfind(">>> %s" % cmd):]
        results = re.findall(r"^RESULT (\S+) (\S+) ?(.*)$", block, re.M)
        if cmd == "ping":
            status, detail = ("PASS", "pong") if "pong" in block else ("FAIL", "no reply")
        elif results:
            # last RESULT of the block is the summary (bt up also prints bt_init)
            _, status, detail = results[-1]
        else:
            status, detail = "FAIL", "no RESULT line (%s)" % out.stderr.strip()[:60]
        rows.append((name, iface, cmd, status, detail))
        print("%-30s %-5s %s" % (name, status, detail))

    stamp = datetime.datetime.now().strftime("%Y-%m-%d_%H%M")
    path = os.path.join(LOG_DIR, "status_%s.md" % stamp)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# PandaBox self-test %s\n\n" % stamp.replace("_", " "))
        f.write("| Peripheral | Interface / pins (LQFP100) | Command | Result | Details |\n|---|---|---|---|---|\n")
        for r in rows:
            f.write("| %s | %s | `%s` | %s | %s |\n" % r)
    print("wrote", os.path.normpath(path))


if __name__ == "__main__":
    main()
