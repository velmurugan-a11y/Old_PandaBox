"""
tracker_suite.py - run the PandaBox_Command_Test_Tracker (LCR_BLE-Test_V3) command cases end to end:
PC -> pandabox-tester (BLE) -> PandaBox -> RS232 Port 2 -> LCR simulator (meter 1 LCR-II node 1,
meter 2 LCR.iQ node 2) and back.

    python tools/tracker_suite.py [--out results.json]

Needs: pandabox-tester on :5001 connected to the box (tools/tcmd.py --connect), the LCR simulator on
:5000 with its serial bridge running (python app.py --serial COM7).
Every case prints PASS/FAIL with the reply; the JSON result is used to fill the tracker's result column.
"""
import json
import re
import sys
import time

from tcmd import cmd, connect

results = []


def fields(line):
    return [f.strip() for f in line.split(" ", 1)[1].rstrip(",").split(",")] if " " in line else []


def run(case_id, command, check, note="", wait=0.0):
    r = cmd(command)
    lines = r.get("response") or []
    first = lines[0] if lines else ""
    if callable(check):
        ok, why = check(lines)
    else:
        ok = bool(re.search(check, first)) if check is not None else True
        why = "" if ok else f"expected /{check}/"
    if "error" in r:
        ok, why = False, r["error"][:120]
    results.append({"id": case_id, "command": command, "response": lines, "ok": ok, "why": why,
                    "note": note, "ms": r.get("elapsed_ms"), "tester_issues": r.get("issues")})
    print(f"{'PASS' if ok else 'FAIL'} {case_id:8s} {command:36s} -> {' | '.join(lines)[:110]} {why}")
    if wait:
        time.sleep(wait)
    return lines


def sim(path, body=None):
    """LCR simulator control (:5000)."""
    import urllib.request
    req = urllib.request.Request("http://127.0.0.1:5000" + path, data=json.dumps(body or {}).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.load(r)


def getdata(node):
    lines = cmd(f"GetData {node} 0").get("response") or [""]
    f = fields(lines[0])
    try:
        return {"serial": int(f[2]), "ts": int(f[3]), "gross": float(f[4]), "flow": float(f[5]),
                "total": float(f[6]), "initial": float(f[8]), "line": lines[0]}
    except (IndexError, ValueError):
        return {"line": lines[0]}


def check_live(case_id, node, want_flow, note):
    d = getdata(node)
    ok = "flow" in d and ((d["flow"] > 0) if want_flow else (d["flow"] == 0))
    results.append({"id": case_id, "command": f"GetData {node} 0", "response": [d["line"]], "ok": ok,
                    "why": "" if ok else f"flow {'>0' if want_flow else '==0'} expected", "note": note})
    print(f"{'PASS' if ok else 'FAIL'} {case_id:8s} GetData {node} 0 ({note}) -> {d['line']}")
    return d


def main():
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    now = int(time.time())

    # ---- setup: known state ----
    run("SETUP", f"SetBoxTime {now}", r"^LxSetBoxTime 0")
    run("SETUP", "SetMode 2", r"^LxSetMode 0")
    run("SETUP", "SetRs485 0", r"^LxSetRs485 0")
    run("SETUP", "SetPortLcrNode 1,2", r"^LxSetPortLcrNode 0", wait=2.5)
    run("SETUP", "Stop 1", None)
    run("SETUP", "Stop 2", None)
    run("SETUP", "PresetGross 1,0", None)
    run("SETUP", "PresetGross 2,0", None, wait=1.0)

    # ---- box info / status ----
    run("PB-035", "BoxStatus", r"^LxBoxStatus 2,0,1,2,[-0-9.]+,[NSEW],[-0-9.]+,[NSEW],[012],[012]$")
    run("PB-040", "BoxInfo", r"^LxBoxInfo 2\.4,250502,2\.9\d\d,\d{6},\d{15},LCR$")
    run("PB-071", "BoxTime", lambda l: (bool(l) and abs(int(fields(l[0])[0]) - int(time.time())) < 30,
                                        "box time should follow SetBoxTime"))
    run("PB-042", "SetBoxTime 1748260000", r"^LxSetBoxTime 0$")
    run("PB-042", "BoxTime", lambda l: (bool(l) and abs(int(fields(l[0])[0]) - 1748260000) < 30, "time not applied"))
    run("PB-043", "SetBoxTime -1", r"^LxSetBoxTime 1$")
    run("SETUP", f"SetBoxTime {int(time.time())}", r"^LxSetBoxTime 0$")

    # ---- BT / WiFi / server ----
    run("PB-022", "RdBtName", r"^LxRdBtName PandaBrain")
    run("PB-024", "SetBtName AVeryLongNameOver16Bytes", r"^LxSetBtName 1$")
    run("PB-025", "SetWifiName MyNetwork", r"^LxSetWifiName 0$")
    run("PB-026", "SetWifiName AVeryLongNetworkNameHere", r"^LxSetWifiName 1$")
    run("PB-027", "SetWifiPwd mypassword", r"^LxSetWifiPwd 0$")
    run("PB-028", "SetWifiPwd averylongpasswordthatexceeds", r"^LxSetWifiPwd 1$")
    run("PB-029", "SetServerIp 13.205.61.50", r"^LxSetServerIp 0$")
    run("PB-030", "SetServerIp 999.9999.999.999", r"^LxSetServerIp 1$")
    run("PB-031", "SetServerPort 8181", r"^LxSetServerPort 0$")
    run("PB-032", "SetServerPort 99999", r"^LxSetServerPort 1$")
    run("PB-033", "SetApn jionet", r"^LxSetApn 0$")
    run("PB-034", "SetApn", r"^LxSetApn 1$")

    # ---- modes ----
    run("PB-036", "SetMode 2", r"^LxSetMode 0$")
    run("PB-037", "SetMode 1", r"^LxSetMode 0$")
    run("PB-037", "GetData 1 0", r"^LxGetData Mode 1,", note="bridge mode: meter data not served")
    run("PB-038", "SetMode 3", r"^LxSetMode 0$")
    run("PB-039", "SetMode 5", r"^LxSetMode 1$")
    run("SETUP", "SetMode 2", r"^LxSetMode 0$")

    # ---- RS485 / RS232 switch: the Port 2 link must drop on RS485 and come back on RS232 ----
    run("PB-044", "SetRs485 1", r"^LxSetRs485 0$", wait=3.0)
    run("PB-044", "RdRegister", r"^LxRdRegister [01],[01]",
        note="info: PE5 off / PE6 on is verified on the pins; this bench's RS232 chip still passes data")
    run("PB-045", "SetRs485 0", r"^LxSetRs485 0$", wait=3.0)
    run("PB-045", "RdRegister", r"^LxRdRegister 1,1", note="back on RS232 -> meters online")
    run("PB-046", "SetRs485 2", r"^LxSetRs485 1$")

    # ---- ports / nodes ----
    run("PB-057", "SetPortLcrNode 1,2", r"^LxSetPortLcrNode 0$")
    run("PB-060", "RdPortLcrNode", r"^LxRdPortLcrNode 1,2$")
    run("PB-058", "SetPortLcrNode 1,0", r"^LxSetPortLcrNode 0$", wait=2.0)
    run("PB-061", "RdPortLcrNode", r"^LxRdPortLcrNode 1,0$")
    run("PB-074", "RdRegister", r"^LxRdRegister 1,0", note="port 2 empty -> not registered")
    run("PB-059", "SetPortLcrNode 256,0", r"^LxSetPortLcrNode 1$")
    run("SETUP", "SetPortLcrNode 1,2", r"^LxSetPortLcrNode 0$", wait=2.5)
    run("PB-073", "RdRegister", r"^LxRdRegister 1,1$")
    run("PB-075", "SwitchState 1", r"^LxSwitchState 1,(Run|Stop)$")
    run("PB-076", "SwitchState 2", r"^LxSwitchState 2,(Run|Stop)$")
    run("PB-077", "SwitchState 3", r"Error")
    run("PB-078", "GetLcrNode 1,1,10", r"^LxFindLcrNode 1,1")
    run("PB-079", "GetLcrNode 1,200,10", r"^LxFindLcrNode (0|1,0)")
    run("PB-080", "GetLcrNode 1,1,251", r"^LxFindLcrNode (0|1,0)")
    run("PB-062", "ModifyLcrNode 1,1,5", r"^LxModifyLcrNode 0$", wait=2.0)
    run("PB-062", "RdPortLcrNode", r"^LxRdPortLcrNode 5,2$")
    run("PB-062", "GetData 5 0", r"^LxGetData 5,1,", note="meter answers on its new node 5")
    run("PB-062", "ModifyLcrNode 1,5,1", r"^LxModifyLcrNode 0$", note="restore node 1", wait=2.0)
    run("PB-063", "ModifyLcrNode 1,99,5", r"^LxModifyLcrNode 1$")
    run("PB-064", "ModifyLcrNode 3,1,5", r"^LxModifyLcrNode 1$")
    run("PB-086", "RdMtrSetting 1", r"^LxRdMtrSetting 1,1,\d+,(yes|no|skip),(clear|multiple|retain)$")
    run("PB-086", "RdMtrSetting 2", r"^LxRdMtrSetting 2,2,\d+,(yes|no|skip),(clear|multiple|retain)$")
    run("PB-087", "RdMtrSetting 3", r"Error")
    run("PB-082", "RdDiagnostics 1", r"^LxRdDiagnostics [01],[012],-?\d+,\d+$")
    run("PB-083", "RdDiagnostics 99", r"Error")
    run("PB-081", "GetLastCmd", r"^LxGetLastCmd")
    run("PB-085", "GetLastMtrCmd 99", r"^LxGetLastMtrCmd 99,None,1")

    # ---- storage, empty ----
    run("PB-020", "DeleteAll 1", r"^LxDeleteAll 0$")
    run("PB-020", "DeleteAll 2", r"^LxDeleteAll 0$")
    run("PB-021", "DeleteAll 99", r"^LxDeleteAll 1$")
    run("PB-006", "HisDataTime 1", r"^LxHisDataTime 1,?(0,0)?,?$")
    run("PB-005", "HisDataTime 99", r"^LxHisDataTime 99,?$")
    run("PB-009", "BoxStorage 99", r"^LxBoxStorage 99,0,\d+")
    run("PB-013", "GetData 1,1", r"^LxGetDataTs 1,?$", note="storage empty")
    run("PB-015", "GetData 99,1", r"Error")
    run("PB-017", "GetDataTs 1,1700000000,1700003600", r"^LxGetDataTs 1,?$")
    run("PB-018", "GetDataTs 1,1700003600,1700000000", r"^LxGetDataTs 1,?$")
    run("PB-019", "GetDataTs 1,1700000000,1700000001", r"^LxGetDataTs 1,?$")

    # ---- presets ----
    run("PB-065", "PresetGross 1,125.5", r"^LxPresetGross 0$")
    run("PB-066", "PresetGross 3,125.5", r"^LxPresetGross 1$")
    run("PB-067", "PresetGross 1,-10.0", r"^LxPresetGross 1$")
    run("PB-068", "PresetNet 1,125.5", r"^LxPresetNet 0$")
    run("PB-069", "PresetNet 3,125.5", r"^LxPresetNet 1$")
    run("PB-070", "PresetNet 1,-10.0", r"^LxPresetNet 1$")
    run("SETUP", "PresetGross 1,0", r"^LxPresetGross 0$")
    run("SETUP", "PresetNet 1,0", r"^LxPresetNet 0$")

    # ---- delivery on meter 1: Start / flow / Pause / Resume / Stop ----
    t_start = int(time.time()) - 2
    before = getdata(1)
    run("PB-052", "Pause 1", r"^LxPause 0$",
        note="V2.89 + real LCR: Pause with no delivery open is accepted (rc 0, devSt 0x21 in the golden "
             "capture); the tracker's LxPause 1 is the V3.01 firmware guard")
    run("PB-047", "Start 1", r"^LxStart 0$", wait=4.0)
    run("PB-084", "GetLastMtrCmd 1", r"^LxGetLastMtrCmd 1,Start,0$")
    run("PB-075", "SwitchState 1", r"^LxSwitchState 1,Run$", note="delivery open")
    d1 = check_live("PB-012", 1, True, "flowing after Start")
    time.sleep(2)
    d2 = check_live("PB-099", 1, True, "gross and totalizer rising")
    ok = "gross" in d1 and "gross" in d2 and d2["gross"] > d1["gross"] and d2["total"] > d1["total"]
    results.append({"id": "PB-099", "command": "GetData 1 0 x2", "response": [d1.get("line"), d2.get("line")],
                    "ok": ok, "why": "" if ok else "gross/total not increasing", "note": "volume accrues"})
    print(f"{'PASS' if ok else 'FAIL'} PB-099   gross {d1.get('gross')} -> {d2.get('gross')}, total {d1.get('total')} -> {d2.get('total')}")
    run("PB-051", "Pause 1", r"^LxPause 0$", wait=2.5)
    p1 = check_live("PB-051", 1, False, "paused: flow 0")
    time.sleep(1.5)
    p2 = getdata(1)
    ok = p1.get("gross") == p2.get("gross")
    results.append({"id": "PB-101", "command": "GetData 1 0 (paused)", "response": [p2.get("line")], "ok": ok,
                    "why": "" if ok else "gross moved while paused", "note": "no volume while paused"})
    print(f"{'PASS' if ok else 'FAIL'} PB-101   paused gross {p1.get('gross')} == {p2.get('gross')}")
    run("PB-047", "Start 1", r"^LxStart 0$", note="resume", wait=3.0)
    check_live("PB-047", 1, True, "flowing again after resume")
    run("PB-054", "Stop 1", r"^LxStop 0$", wait=2.5)
    end = check_live("PB-054", 1, False, "stopped: flow 0")
    ok = ("total" in end and "total" in before and
          abs(end["total"] - (before["total"] + end["gross"])) < 0.25 and end["initial"] == before["total"])
    results.append({"id": "PB-054", "command": "GetData 1 0 (final)", "response": [end.get("line")], "ok": ok,
                    "why": "" if ok else "final total != initial + gross", "note": "ticket math"})
    print(f"{'PASS' if ok else 'FAIL'} PB-054   initial {before.get('total')} + gross {end.get('gross')} = total {end.get('total')}")
    run("PB-084", "GetLastMtrCmd 1", r"^LxGetLastMtrCmd 1,Stop,0$")
    run("PB-048", "Start 99", r"^LxStart 1$")
    run("PB-053", "Pause 99", r"^LxPause 1$")
    run("PB-056", "Stop 99", r"^LxStop 1$")

    # ---- history after the delivery ----
    run("PB-004", "HisDataTime 1", r"^LxHisDataTime 1,\d{10},\d{10}")
    run("PB-007", "BoxStorage 1", lambda l: (bool(l) and int(fields(l[0])[1]) > 0, "records expected"))
    h = run("PB-010", "GetData 1,1", r"^LxGetDataTs 1,0,\d+,\d{10},")
    if h and fields(h[0])[2:3]:
        run("PB-010", f"GetDataEcho {fields(h[0])[2]},0,1", r"^LxGetDataTs 1,", note="ack -> next record")
    run("PB-016", f"GetDataTs 1,{t_start},{int(time.time()) + 5}",
        lambda l: (len(l) >= 2 and l[0].startswith("LxGetDataTs 1,1,"), "records in range expected"),
        note="tracker range 1700000000.. is before this box's clock; using the delivery window")

    # ---- preset delivery: auto stop at 5.0 gal ----
    run("PB-108", "PresetGross 1,5.0", r"^LxPresetGross 0$")
    run("PB-108", "Start 1", r"^LxStart 0$", wait=9.0)
    d = getdata(1)
    ok = d.get("gross") == 5.0 and d.get("flow") == 0.0
    results.append({"id": "PB-108", "command": "GetData 1 0", "response": [d.get("line")], "ok": ok,
                    "why": "" if ok else "expected gross 5.0, flow 0 (stopped at preset)", "note": "preset reached"})
    print(f"{'PASS' if ok else 'FAIL'} PB-108   preset stop -> {d.get('line')}")
    run("SETUP", "Stop 1", None)
    run("SETUP", "PresetGross 1,0", r"^LxPresetGross 0$")

    # ---- double meter delivery ----
    run("PB-095", "Start 1", r"^LxStart 0$")
    run("PB-095", "Start 2", r"^LxStart 0$", wait=4.0)
    check_live("PB-095", 1, True, "meter 1 flowing")
    check_live("PB-095", 2, True, "meter 2 flowing (independent)")
    run("PB-095", "Stop 1", r"^LxStop 0$")
    run("PB-095", "Stop 2", r"^LxStop 0$", wait=2.0)
    check_live("PB-095", 1, False, "meter 1 stopped")
    check_live("PB-095", 2, False, "meter 2 stopped")
    run("PB-007", "BoxStorage 2", lambda l: (bool(l) and int(fields(l[0])[1]) > 0, "records expected"))

    # ---- meter cable pulled during a delivery (PB-106 / PB-098): stop the simulator's serial link ----
    run("PB-106", "Start 1", r"^LxStart 0$", wait=2.0)
    sim("/api/serial/stop")
    time.sleep(4.5)                                  # > LCR_OFFLINE_POLLS failed 1 s polls
    run("PB-106", "RdRegister", r"^LxRdRegister 0,0$", note="both meters offline")
    run("PB-106", "GetData 1 0", r"^LxGetData Error,", note="no live data from an offline meter")
    run("PB-106", "Stop 1", r"^LxStop 1$", note="command not delivered")
    run("PB-106", "BoxStatus", r"^LxBoxStatus 2,", note="box still answers the app")
    sim("/api/serial/start", {"product_key": "lcr2"})
    time.sleep(3.0)
    run("PB-106", "RdRegister", r"^LxRdRegister 1,1$", note="meters back online after reconnect")
    d = check_live("PB-106", 1, True, "delivery kept running on the meter while unplugged")
    run("PB-106", "Stop 1", r"^LxStop 0$", wait=2.0)

    # ---- BT rename, then reset (both drop / change the link) ----
    run("PB-023", "SetBtName PandaBox1", r"^LxSetBtName 0$", wait=2.0)
    run("PB-023", "RdBtName", r"^LxRdBtName PandaBox1$")
    run("SETUP", "SetBtName PandaBrain", r"^LxSetBtName 0$", wait=2.0)
    run("PB-041", "BoxReset", r"^LxBoxReset 0$", wait=8.0)
    ok = connect()
    results.append({"id": "PB-041", "command": "reconnect after BoxReset", "response": [], "ok": ok,
                    "why": "" if ok else "no reconnect", "note": "box rebooted and advertises again"})
    print(f"{'PASS' if ok else 'FAIL'} PB-041   reconnect after reset")
    if ok:
        run("PB-041", "BoxInfo", r"^LxBoxInfo 2\.4,")
        run("PB-041", f"SetBoxTime {int(time.time())}", r"^LxSetBoxTime 0$")

    passed = sum(r["ok"] for r in results)
    print(f"\n{passed}/{len(results)} checks passed")
    if out:
        json.dump(results, open(out, "w"), indent=1)


if __name__ == "__main__":
    main()
