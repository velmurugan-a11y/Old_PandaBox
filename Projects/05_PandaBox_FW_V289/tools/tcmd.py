"""Send App commands to the PandaBox through the running pandabox-tester (BLE) and print replies.
   python tools/tcmd.py "BoxStatus" "GetData 1 0" ...      (tester must be connected, :5001)"""
import json, sys, urllib.request

def cmd(c, url="http://127.0.0.1:5001/api/command"):
    req = urllib.request.Request(url, data=json.dumps({"command": c}).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        return {"command": c, "error": e.read().decode()}

def post(path, body=None, timeout=100):
    req = urllib.request.Request("http://127.0.0.1:5001" + path, data=json.dumps(body or {}).encode(),
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        return {"error": e.read().decode()}

def connect(name_hint="PandaBrain"):
    """Scan (the BLE address changes every boot) and connect the tester to the box."""
    import time
    post("/api/disconnect")
    for attempt in range(6):                # the Windows BLE stack often needs a 2nd attempt
        if attempt:
            time.sleep(3)
        devs = post("/api/scan", timeout=40).get("devices", [])
        box = [d for d in devs if (d.get("name") or "").startswith(name_hint)]
        if box:
            r = post("/api/connect", {"device_name": box[0]["name"], "address": box[0]["address"], "label": "P05bench"})
            print("connect", box[0], r)
            if r.get("connected"):
                return True
    return False

if __name__ == "__main__":
    if sys.argv[1:2] == ["--connect"]:
        sys.exit(0 if connect() else 1)
    for c in sys.argv[1:]:
        r = cmd(c)
        print(f"{c:28s} -> {r.get('response', r.get('error'))}  {r.get('elapsed_ms','')}ms  {r.get('issues') or ''}")
