"""
product_suite.py - product-wise functional validation through the pandabox-tester (BLE), against the
LCR simulator v6 acting as ONE real meter on the Port 2 (J2) wire. Port 1 (J1) has nothing connected.

    python tools/product_suite.py [--out results.json] [--models lcr2,lcriq]

For each product the simulator is switched to that model (LCR-II / LCR.iQ) and node, and the box's Port 2
is pointed at it (SetPortLcrNode 0,<node>). Deliveries run like a real truck: Start (the meter answers
rc 38 and runs its ~4 s counter test), then the test turns the pulser on through the simulator (the RUN
PULSER button) and reads the gallons back over BLE.
"""
import json
import sys
import time
import urllib.request

import tracker_suite as t
from tcmd import cmd, connect

SIM = "http://127.0.0.1:5000"
PRODUCTS = [("LCR-II", "lcr2", 1), ("LCR.iQ", "lcriq", 2)]


def sim(path, body=None):
    req = urllib.request.Request(SIM + path, data=None if body is None else json.dumps(body).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


def state():
    return sim("/api/state")


def wait_idle_busy(timeout=15):
    """Wait until the meter is not busy (counter test / ticket printing), like a host would retry."""
    t0 = time.time()
    while time.time() - t0 < timeout and state()["busy"]:
        time.sleep(0.3)
    time.sleep(1.3)                 # one more box poll cycle so GetData carries fresh values


def pump(on, rate=None):
    b = {"run": on}
    if rate is not None:
        b["rate"] = rate
    sim("/api/pulser", b)


def result(tag, command, ok, why, note, lines=()):
    t.results.append({"id": tag, "command": command, "response": list(lines), "ok": bool(ok), "why": "" if ok else why, "note": note})
    print(f"{'PASS' if ok else 'FAIL'} {tag:8s} {note} {'' if ok else '<- ' + why}")


def product(name, model, node):
    tag = name
    sim("/api/model", {"model": model, "node": node})
    sim("/api/settings", {"busy_emulation": 1, "f25": 180, "f27": 0, "f37": 1})
    sim("/api/printer", {"online": True})
    pump(False, 60)
    time.sleep(1.0)
    t.run(tag, f"SetPortLcrNode 0,{node}", r"^LxSetPortLcrNode 0$", wait=2.5)
    t.run(tag, "RdPortLcrNode", rf"^LxRdPortLcrNode 0,{node}$")
    t.run(tag, "RdRegister", r"^LxRdRegister 0,1$", note="J1 empty, J2 meter registered")
    t.run(tag, "BoxStatus", rf"^LxBoxStatus 2,0,0,{node},")
    t.run(tag, "SwitchState 2", rf"^LxSwitchState {node},Run$", note="idle meter under host control reports RUN (0x21)")
    t.run(tag, "RdMtrSetting 2", rf"^LxRdMtrSetting 2,{node},180,no,clear$", note="#102/#25/#37/#27 read from the meter")
    t.run(tag, "GetLcrNode 2,1,10", rf"^LxFindLcrNode 2,{node}$", note="live meter -> its node, no scan (V2.89)")
    t.run(tag, f"GetData {node} 0", rf"^LxGetData {node},1,\d+,\d{{10}},")
    t.run(tag, f"PresetGross {node},0", r"^LxPresetGross 0$")

    # ---- node change and back ----
    t.run(tag, f"ModifyLcrNode 2,{node},9", r"^LxModifyLcrNode 0$", wait=2.5)
    t.run(tag, "RdPortLcrNode", r"^LxRdPortLcrNode 0,9$")
    ok = state()["node"] == 9
    result(tag, "sim node", ok, "simulator node not 9", "meter's own LCP node changed to 9 (25h)")
    t.run(tag, "RdMtrSetting 2", r"^LxRdMtrSetting 2,9,", note="#102 reads back 9")
    t.run(tag, f"GetData {node} 0", r"^LxGetData Error,", note="old node no longer configured")
    t.run(tag, f"ModifyLcrNode 2,9,{node}", r"^LxModifyLcrNode 0$", note="restore", wait=2.5)
    t.run(tag, "ModifyLcrNode 2,77,5", r"^LxModifyLcrNode 1$", note="no meter at node 77")

    # ---- presets ----
    t.run(tag, f"PresetGross {node},125.5", r"^LxPresetGross 0$")
    ok = state()["display"]["preset"] == "125.5"
    result(tag, "sim preset", ok, "preset not on meter", "meter shows preset 125.5 (#5)")
    t.run(tag, f"PresetGross {node},-1.0", r"^LxPresetGross 1$", note="negative -> rc 113")
    t.run(tag, "PresetGross 7,10.0", r"^LxPresetGross 1$", note="node 7 not configured")
    t.run(tag, f"PresetNet {node},10.0", r"^LxPresetNet 0$")
    t.run(tag, f"PresetGross {node},0", r"^LxPresetGross 0$")
    t.run(tag, f"PresetNet {node},0", r"^LxPresetNet 0$")

    # ---- delivery: Start / counter test / valve open no flow / pulser / pause / resume / stop ----
    t.run(tag, f"DeleteAll {node}", r"^LxDeleteAll 0$")
    t_start = int(time.time()) - 2
    before = t.getdata(node)
    tk0 = state()["display"]["ticket"]
    t.run(tag, f"Start {node}", r"^LxStart 0$", note="meter replies rc 38 (queued) -> box reports success")
    s = state()
    result(tag, "sim", s["busy"], "meter not busy", "meter busy: counter test in progress (rc 38 to every poll)")
    t.run(tag, "RdRegister", r"^LxRdRegister 0,1$", note="box keeps the busy meter online")
    wait_idle_busy()
    d = t.check_live(tag, node, False, "delivery open, valve open, pulser not turning -> 0 flow")
    ok = d.get("gross") == 0.0 and d.get("initial") == before.get("total")
    result(tag, "GetData", ok, "gross/initial wrong", "#2 = 0 and #100 = totalizer at start", [d.get("line")])
    t.run(tag, "SwitchState 2", rf"^LxSwitchState {node},Run$")
    t.run(tag, f"GetLastMtrCmd {node}", rf"^LxGetLastMtrCmd {node},Start,0$")
    pump(True, 60)
    time.sleep(3.0)
    d1 = t.check_live(tag, node, True, "RUN PULSER -> product flows")
    time.sleep(2.0)
    d2 = t.check_live(tag, node, True, "still flowing")
    ok = d2.get("gross", 0) > d1.get("gross", 0) and d2.get("total", 0) > d1.get("total", 0) and abs(d2.get("flow", 0) - 60.0) < 0.5
    result(tag, "GetData x2", ok, "not rising at 60", f"gallons rising {d1.get('gross')} -> {d2.get('gross')}, flow {d2.get('flow')}")
    t.run(tag, f"Pause {node}", r"^LxPause 0$", wait=2.5)
    result(tag, "sim", state()["state"] == "STOP", "meter not paused", "meter paused (valve closed)")
    p1 = t.check_live(tag, node, False, "paused: no flow although the pump still runs")
    time.sleep(1.5)
    p2 = t.getdata(node)
    result(tag, "GetData", p1.get("gross") == p2.get("gross"), "moved", "volume holds while paused", [p2.get("line")])
    t.run(tag, f"Start {node}", r"^LxStart 0$", note="resume (no counter test)", wait=3.0)
    t.check_live(tag, node, True, "resumed")
    pump(False)
    time.sleep(2.0)
    d3 = t.check_live(tag, node, False, "pulser stopped: delivery still open, 0 flow")
    result(tag, "sim", state()["state"] == "RUN", "not RUN", "delivery stays active with no flow (devStatus 0x01)")
    t.run(tag, f"Stop {node}", r"^LxStop 0$")
    wait_idle_busy()
    end = t.check_live(tag, node, False, "ended")
    ok = end.get("initial") == before.get("total") and abs(end.get("total", 0) - (before.get("total", 0) + end.get("gross", 0))) < 0.15
    result(tag, "GetData", ok, "final != initial + gross", f"ticket math {before.get('total')} + {end.get('gross')} = {end.get('total')}", [end.get("line")])
    s = state()
    result(tag, "sim", s["state"] == "END" and s["display"]["ticket"] == tk0 + 1, "no ticket", "meter idle, delivery ticket printed (#23 +1)")
    t.run(tag, f"GetLastMtrCmd {node}", rf"^LxGetLastMtrCmd {node},Stop,0$")

    # ---- history ----
    t.run(tag, f"HisDataTime {node}", rf"^LxHisDataTime {node},\d{{10}},\d{{10}}")
    t.run(tag, f"BoxStorage {node}", lambda l: (bool(l) and int(t.fields(l[0])[1]) > 0, "records expected"))
    h = t.run(tag, f"GetData {node},1", rf"^LxGetDataTs {node},0,\d+,\d{{10}},")
    if h and t.fields(h[0])[2:3]:
        t.run(tag, f"GetDataEcho {t.fields(h[0])[2]},0,1", rf"^LxGetDataTs {node},", note="ack -> next record")
    t.run(tag, f"GetDataTs {node},{t_start},{int(time.time()) + 5}",
          lambda l: (len(l) >= 2 and l[0].startswith(f"LxGetDataTs {node},1,"), "records in range"))
    t.run(tag, f"DeleteAll {node}", r"^LxDeleteAll 0$")

    # ---- preset Clear: meter stops exactly on the preset and ends the delivery ----
    t.run(tag, f"PresetGross {node},4.0", r"^LxPresetGross 0$")
    t.run(tag, f"Start {node}", r"^LxStart 0$")
    wait_idle_busy()
    pump(True, 120)
    time.sleep(4.5)
    wait_idle_busy()
    d = t.getdata(node)
    s = state()
    ok = d.get("gross") == 4.0 and s["state"] == "END" and s["display"]["preset"] == "—"
    result(tag, "GetData", ok, "not 4.0/END/cleared", "preset Clear: stops on 4.0, delivery ends, preset cleared", [d.get("line")])
    pump(False)

    # ---- preset Multiple: pauses at the preset, delivery stays open ----
    sim("/api/settings", {"f27": 1})
    t.run(tag, f"PresetGross {node},3.0", r"^LxPresetGross 0$")
    t.run(tag, f"Start {node}", r"^LxStart 0$")
    wait_idle_busy()
    pump(True, 120)
    time.sleep(3.5)
    d = t.getdata(node)
    s = state()
    result(tag, "GetData", d.get("gross") == 3.0 and s["state"] == "STOP", "not paused at 3.0",
           "preset Multiple: meter pauses at 3.0, delivery still open", [d.get("line")])
    pump(False)
    t.run(tag, f"Stop {node}", r"^LxStop 0$")
    wait_idle_busy()
    sim("/api/settings", {"f27": 0})

    # ---- no-flow timer ends the delivery ----
    sim("/api/settings", {"f25": 4})
    t.run(tag, f"Start {node}", r"^LxStart 0$")
    wait_idle_busy()
    pump(True, 60)
    time.sleep(2.5)                         # > 1 gal arms the timer
    pump(False)
    time.sleep(6.0)
    wait_idle_busy()
    s = state()
    result(tag, "sim", s["state"] == "END" and s["del_status"] & 0x0100, f"state {s['state']} dstat {s['del_status']:#x}",
           "no-flow timer (4 s) ended the delivery (delStatus 0x0100)")
    t.check_live(tag, node, False, "after no-flow stop")
    sim("/api/settings", {"f25": 180})

    # ---- ticket required + printer off-line: meter refuses to start ----
    sim("/api/settings", {"f37": 0})
    sim("/api/printer", {"online": False})
    t.run(tag, f"Start {node}", r"^LxStart 1$", note="Ticket Required = Yes and printer off-line -> meter refuses")
    sim("/api/printer", {"online": True})
    sim("/api/settings", {"f37": 1})

    # ---- meter cable unplugged mid-delivery ----
    t.run(tag, f"Start {node}", r"^LxStart 0$")
    wait_idle_busy()
    pump(True, 60)
    time.sleep(1.5)
    sim("/api/serial/disconnect", {})
    time.sleep(4.5)
    t.run(tag, "RdRegister", r"^LxRdRegister 0,0$", note="meter unplugged")
    t.run(tag, f"GetData {node} 0", r"^LxGetData Error,")
    t.run(tag, f"Stop {node}", r"^LxStop 1$")
    sim("/api/serial/connect", {"port": "COM7", "baud": 19200})
    time.sleep(4.0)
    t.run(tag, "RdRegister", r"^LxRdRegister 0,1$", note="plugged back in")
    t.check_live(tag, node, True, "delivery kept running on the meter")
    pump(False)
    t.run(tag, f"Stop {node}", r"^LxStop 0$")
    wait_idle_busy()


def main():
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    models = sys.argv[sys.argv.index("--models") + 1].split(",") if "--models" in sys.argv else None
    B = "BOX"
    t.run(B, f"SetBoxTime {int(time.time())}", r"^LxSetBoxTime 0$")
    t.run(B, "SetMode 2", r"^LxSetMode 0$")
    t.run(B, "SetRs485 0", r"^LxSetRs485 0$")
    t.run(B, "SetRs485 1", r"^LxSetRs485 1$", note="RS485 disabled in this firmware")
    t.run(B, "BoxInfo", r"^LxBoxInfo 2\.4,250502,2\.9\d\d,\d{6},\d{15},LCR$")
    t.run(B, "RdDiagnostics", r"^LxRdDiagnostics [01],[012],-?\d+,\d+$")

    P = "PORT1"
    t.run(P, "SetPortLcrNode 1,2", r"^LxSetPortLcrNode 0$", wait=4.5)
    t.run(P, "RdRegister", r"^LxRdRegister 0,[01]$", note="J1 empty -> port 1 not registered")
    t.run(P, "GetData 1 0", r"^LxGetData Error,")
    t.run(P, "Start 1", r"^LxStart 1$", note="no meter on J1")
    t.run(P, "SwitchState 1", r"Error")
    t.run(P, "GetLcrNode 1,1,5", r"^LxFindLcrNode 0", note="scan of J1 finds nothing")
    t.run(P, "SetPortLcrNode 2,2", r"^LxSetPortLcrNode 1$", note="same node on both ports refused")

    for name, model, node in PRODUCTS:
        if models and model not in models:
            continue
        product(name, model, node)

    W = "WRONGNODE"
    t.run(W, "SetPortLcrNode 0,7", r"^LxSetPortLcrNode 0$", wait=4.5)
    t.run(W, "RdRegister", r"^LxRdRegister 0,0$", note="no meter answers node 7")
    t.run(W, "Start 7", r"^LxStart 1$")

    passed = sum(r["ok"] for r in t.results)
    print(f"\n{passed}/{len(t.results)} checks passed")
    if out:
        json.dump(t.results, open(out, "w"), indent=1)


if __name__ == "__main__":
    main()
