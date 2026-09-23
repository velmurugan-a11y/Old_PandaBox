"""Drive the PandaBox bring-up CLI over a serial port and log every exchange.

Usage:
    python tools/bringup.py [--port COM9] [--listen SECONDS] <command> [<command> ...]

Each command is sent as a line; the reply is read until the next "> " prompt
(or the per-command timeout). Everything is appended to logs/session_<date>.log
with timestamps, and RESULT lines are also collected in logs/results.csv.
"""

import argparse
import datetime
import os
import sys
import time

import serial

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "..", "logs")

# commands that take long on the board
TIMEOUTS = {"gsm": 160, "flash map": 120, "flash rd": 400, "led": 10, "rtc": 5, "bt listen": 200, "bt": 30, "rsdiag": 20, "rsack": 20}


def timeout_for(cmd):
    parts = cmd.split()
    if parts[:2] == ["bt", "listen"] and len(parts) > 2:
        return int(parts[2], 0) / 1000.0 + 10     # listen duration in ms + margin
    for key, t in TIMEOUTS.items():
        if cmd.startswith(key):
            return t
    return 5


def stamp():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM9")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--listen", type=float, default=0.0,
                    help="only listen for this many seconds (e.g. to catch the boot banner)")
    ap.add_argument("--quiet", action="store_true", help="do not echo the reply to stdout")
    ap.add_argument("commands", nargs="*")
    a = ap.parse_args()

    os.makedirs(LOG_DIR, exist_ok=True)
    log_path = os.path.join(LOG_DIR, "session_%s.log" % datetime.date.today().isoformat())
    res_path = os.path.join(LOG_DIR, "results.csv")
    new_res = not os.path.exists(res_path)

    with serial.Serial(a.port, a.baud, timeout=0.1) as ser, \
            open(log_path, "a", encoding="utf-8") as log, \
            open(res_path, "a", encoding="utf-8") as res:
        if new_res:
            res.write("time,test,status,details\n")

        def record(text):
            log.write(text)
            log.flush()
            if not a.quiet:
                sys.stdout.write(text)
                sys.stdout.flush()

        def read_until_prompt(limit):
            buf = b""
            end = time.time() + limit
            while time.time() < end:
                chunk = ser.read(4096)
                if chunk:
                    buf += chunk
                    end = max(end, time.time() + 0.5) if limit == 0 else end
                    if buf.endswith(b"> "):
                        break
            return buf.decode("utf-8", errors="replace").replace("\r", "")

        if a.listen:
            record("\n[%s] --- listen %.1f s ---\n" % (stamp(), a.listen))
            record(read_until_prompt(a.listen) + "\n")

        for cmd in a.commands:
            ser.reset_input_buffer()
            record("\n[%s] >>> %s\n" % (stamp(), cmd))
            ser.write((cmd + "\r").encode())
            t0 = time.time()
            reply = read_until_prompt(timeout_for(cmd))
            record(reply)
            record("\n[%s] <<< %.1f s\n" % (stamp(), time.time() - t0))
            for line in reply.splitlines():
                if line.startswith("RESULT "):
                    parts = line.split(" ", 3)
                    while len(parts) < 4:
                        parts.append("")
                    res.write('%s,%s,%s,"%s"\n' % (stamp(), parts[1], parts[2], parts[3]))
                    res.flush()


if __name__ == "__main__":
    main()
