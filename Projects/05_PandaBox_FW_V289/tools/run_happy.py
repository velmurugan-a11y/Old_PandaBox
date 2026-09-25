"""Run one of the pandabox-tester's own sequences (default happy_flow.csv) through its /api/run
engine and wait for the result. The tester writes its CSV/TXT report under its logs/ folder.

    python tools/run_happy.py [sequence.csv]
"""
import json
import sys
import time
import urllib.request

from tcmd import post

BASE = "http://127.0.0.1:5001"


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=15) as r:
        return json.load(r)


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "happy_flow.csv"
    with urllib.request.urlopen(f"{BASE}/api/sequences/{name}", timeout=15) as r:
        text = r.read().decode()            # the tester serves the CSV as plain text
    r = post("/api/run", {"sequence": text})
    print("run:", r)
    if "error" in r:
        sys.exit(1)
    t0 = time.time()
    while True:
        time.sleep(5)
        s = get("/api/status")
        print(f"  {time.time() - t0:5.0f}s  {s['status']}  {s.get('summary')}", flush=True)
        if s["status"] != "running":
            break
    summ = s.get("summary") or {}
    sys.exit(0 if summ.get("fail", 1) == 0 else 2)


if __name__ == "__main__":
    main()
