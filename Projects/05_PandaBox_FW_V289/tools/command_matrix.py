"""
command_matrix.py - every case of every per-command sheet in PandaBox_Command_Test_Tracker.xlsx (41 commands),
sent through the pandabox-tester over BLE. Meter-dependent cases run once per meter model (LCR-II, LCR 600,
LCR.iQ) against the LCR simulator v6 on the Port 2 wire; box-only cases run once.

Bench: J1 empty, J2 -> USB-RS232 -> simulator (one meter, node 1). The tracker's "meter 1" is that meter
(SetPortLcrNode 0,1); where a case names a PORT, port 2 is used (the meter's port). The simulator's pulser
is driven by this script (RUN / stop), like pressing the RUN PULSER button.

    python tools/command_matrix.py [--models lcr2,lcr600,lcriq] [--out test_results/command_matrix.json]
"""
import json
import sys
import time

import tracker_suite as t
from product_suite import sim, state, wait_idle_busy, pump
from tcmd import cmd, connect

MODELS = [("lcr2", "LCR-II"), ("lcr600", "LCR 600"), ("lcriq", "LCR.iQ")]
rows = []                          # {sheet, case, model, command, response, ok, why, note}


def case(sheet, no, model, command, check, note="", wait=0.0):
    before = len(t.results)
    lines = t.run(f"{sheet}#{no}", command, check, note=note, wait=wait)
    r = t.results[before]
    rows.append({"sheet": sheet, "case": no, "model": model, "command": command, "response": lines,
                 "ok": bool(r["ok"]), "why": r["why"], "note": note})
    return lines


def fact(sheet, no, model, desc, ok, why="", resp=()):
    rows.append({"sheet": sheet, "case": no, "model": model, "command": desc, "response": list(resp),
                 "ok": bool(ok), "why": "" if ok else why, "note": ""})
    print(f"{'PASS' if ok else 'FAIL'} {sheet}#{no} [{model}] {desc} {'' if ok else '<- ' + why}")


def box_cases():
    B = "box"
    now = int(time.time())
    case("SetBoxTime", 1, B, "SetBoxTime 1748260000", r"^LxSetBoxTime 0$")
    case("BoxTime", 1, B, "BoxTime", r"^LxBoxTime 17482600\d\d", note="no GPS lock on the bench: V2.89 format <unix>")
    case("SetBoxTime", 2, B, "SetBoxTime -1", r"^LxSetBoxTime 1$")
    case("SetBoxTime", 1, B, f"SetBoxTime {now}", r"^LxSetBoxTime 0$", note="restore the clock")
    case("BoxTime", 2, B, "BoxTime", r"^LxBoxTime \d{10}$")
    case("BoxStatus", 1, B, "BoxStatus", r"^LxBoxStatus [12],0,\d+,\d+,[-0-9.]+,[NSEW],[-0-9.]+,[NSEW],[012],[012]$")
    case("BoxInfo", 1, B, "BoxInfo", r"^LxBoxInfo 2\.4,250502,2\.9\d\d,\d{6},\d{15},LCR$")
    case("RdBtName", 1, B, "RdBtName", r"^LxRdBtName PandaBrain")
    case("SetBtName", 2, B, "SetBtName AVeryLongNameOver16Bytes", r"^LxSetBtName 1$")
    case("SetBtPwd", 1, B, "SetBtPwd 1234", r"^LxSetBtPwd 0$")
    case("SetBtPwd", 2, B, "SetBtPwd 12345", r"^LxSetBtPwd 1$")
    case("SetWifiName", 1, B, "SetWifiName MyNetwork", r"^LxSetWifiName 0$")
    case("SetWifiName", 2, B, "SetWifiName AVeryLongNetworkNameHere", r"^LxSetWifiName 1$")
    case("SetWifiPwd", 1, B, "SetWifiPwd mypassword", r"^LxSetWifiPwd 0$")
    case("SetWifiPwd", 2, B, "SetWifiPwd averylongpasswordthatexceeds", r"^LxSetWifiPwd 1$")
    case("SetServerIp", 1, B, "SetServerIp 192.168.1.100", r"^LxSetServerIp 0$")
    case("SetServerIp", 2, B, "SetServerIp 999.999.999.999", r"^LxSetServerIp 1$")
    case("SetServerPort", 1, B, "SetServerPort 8080", r"^LxSetServerPort 0$")
    case("SetServerPort", 2, B, "SetServerPort 99999", r"^LxSetServerPort 1$")
    case("SetApn", 1, B, "SetApn cmnet", r"^LxSetApn 0$")
    case("SetApn", 2, B, "SetApn", r"^LxSetApn 1$")
    case("SetMode", 1, B, "SetMode 2", r"^LxSetMode 0$")
    case("SetMode", 2, B, "SetMode 1", r"^LxSetMode 0$")
    case("SetMode", 2, B, "GetData 1 0", r"^LxGetData Mode 1,", note="bridge mode: box does not serve meter data")
    case("SetMode", 3, B, "SetMode 5", r"^LxSetMode 1$")
    case("SetMode", 1, B, "SetMode 2", r"^LxSetMode 0$", note="restore")
    case("SetRs485", 1, B, "SetRs485 1", r"^LxSetRs485 1$", note="RS485 disabled in this firmware (user decision) -> refused")
    case("SetRs485", 2, B, "SetRs485 0", r"^LxSetRs485 0$")
    case("SetRs485", 3, B, "SetRs485 2", r"^LxSetRs485 1$")
    case("SetApp1", 1, B, "SetApp1", r"^LxSetApp1,1$", note="V2.89 reply string")
    case("SetApp2", 1, B, "SetApp2", r"^LxSetApp2,1$", note="V2.89 reply string")
    case("DirectDelivery", 1, B, "DirectDelivery 2,5", r"^LxStop 1$", note="TCS-only command: an LCR box refuses (V2.89 behaviour)")
    case("DirectDelivery", 2, B, "DirectDelivery 99,5", r"^LxStop 1$")
    case("Update", 1, B, "Update APP1,1024,1234", r"^LxUpdate 1$", note="OTA not implemented yet (M10)")
    case("Update", 2, B, "Update", r"^LxUpdate 1$")
    case("GetLastCmd", 1, B, "GetLastCmd 1", r"^LxGetLastCmd 1,0$", note="V2.89 fixed reply")
    case("GetLastCmd", 2, B, "GetLastCmd 99", r"^LxGetLastCmd")
    # ports (both-port expectations are adapted: J1 has no meter on this bench)
    case("SetPortLcrNode", 1, B, "SetPortLcrNode 1,2", r"^LxSetPortLcrNode 0$", wait=4.0)
    case("RdPortLcrNode", 1, B, "RdPortLcrNode", r"^LxRdPortLcrNode 1,2$")
    case("RdRegister", 1, B, "RdRegister", r"^LxRdRegister 0,[01]$", note="J1 empty -> port 1 not registered")
    case("SetPortLcrNode", 2, B, "SetPortLcrNode 1,0", r"^LxSetPortLcrNode 0$", wait=4.0)
    case("SetPortLcrNode", 3, B, "SetPortLcrNode 256,0", r"^LxSetPortLcrNode 1$")
    case("RdPortLcrNode", 2, B, "SetPortLcrNode 0,0", r"^LxSetPortLcrNode 0$", wait=2.0)
    case("RdPortLcrNode", 2, B, "RdPortLcrNode", r"^LxRdPortLcrNode 0,0$")
    case("RdRegister", 2, B, "RdRegister", r"^LxRdRegister 0,0$")


def meter_cases(model, name):
    M = name
    sim("/api/model", {"model": model, "node": 1})
    sim("/api/settings", {"busy_emulation": 1, "f25": 180, "f27": 0, "f37": 1})
    sim("/api/printer", {"online": True})
    pump(False, 60)
    time.sleep(1.0)
    t.run(M, "SetPortLcrNode 0,1", r"^LxSetPortLcrNode 0$", wait=3.0)
    case("RdRegister", 1, M, "RdRegister", r"^LxRdRegister 0,1$", note="meter on port 2 registered")
    case("RdDiagnostics", 1, M, "RdDiagnostics 1", r"^LxRdDiagnostics [01],[012],-?\d+,\d+$")
    case("RdDiagnostics", 2, M, "RdDiagnostics 99", r"Error")
    case("GetLastMtrCmd", 2, M, "GetLastMtrCmd 99", r"^LxGetLastMtrCmd 99,None,1$")
    # ---- empty storage ----
    case("DeleteAll", 1, M, "DeleteAll 1", r"^LxDeleteAll 0$")
    case("HisDataTime", 3, M, "HisDataTime 1", r"^LxHisDataTime 1,?$", note="no stored data -> empty")
    case("GetData", 3, M, "GetData 1,1", r"^LxGetDataTs 1,?$", note="storage empty")
    case("GetDataTs", 3, M, "GetDataTs 1,1700000000,1700000001", r"^LxGetDataTs 1,?$")
    # ---- switch / node ----
    case("SwitchState", 2, M, "SwitchState 2", r"^LxSwitchState 1,Run$", note="idle meter under host control: switch RUN (real 0x21)")
    case("SwitchState", 3, M, "SwitchState 3", r"Error")
    case("GetLcrNode", 1, M, "GetLcrNode 2,1,10", r"^LxFindLcrNode 2,1$")
    case("GetLcrNode", 2, M, "GetLcrNode 1,200,10", r"^LxFindLcrNode 0")
    case("GetLcrNode", 3, M, "GetLcrNode 1,1,251", r"^LxFindLcrNode 0")
    case("ModifyLcrNode", 1, M, "ModifyLcrNode 2,1,5", r"^LxModifyLcrNode 0$", wait=2.5)
    fact("ModifyLcrNode", 1, M, "meter's own node is now 5", state()["node"] == 5, "sim node not 5")
    case("ModifyLcrNode", 1, M, "GetData 5 0", r"^LxGetData 5,1,", note="meter answers on node 5")
    case("ModifyLcrNode", 1, M, "ModifyLcrNode 2,5,1", r"^LxModifyLcrNode 0$", note="restore", wait=2.5)
    case("ModifyLcrNode", 2, M, "ModifyLcrNode 2,99,5", r"^LxModifyLcrNode 1$")
    case("ModifyLcrNode", 3, M, "ModifyLcrNode 3,1,5", r"^LxModifyLcrNode 1$")
    # ---- presets ----
    case("PresetGross", 1, M, "PresetGross 1,125.5", r"^LxPresetGross 0$")
    fact("PresetGross", 1, M, "meter shows preset 125.5", state()["display"]["preset"] == "125.5", "not on meter")
    case("PresetGross", 2, M, "PresetGross 2,5,125.3", r"^LxPresetGross 1$", note="TCS form (node,product,qty) refused by an LCR box")
    case("PresetGross", 3, M, "PresetGross 3,125.5", r"^LxPresetGross 1$", note="no meter 3")
    case("PresetGross", 4, M, "PresetGross 1,-10.0", r"^LxPresetGross 1$")
    case("PresetGross", 1, M, "PresetGross 1,0", r"^LxPresetGross 0$", note="clear")
    # ---- meter control: not-running cases ----
    case("Pause", 2, M, "Pause 1", r"^LxPause 0$", note="not running: real LCR accepts as a no-op")
    case("Resume", 2, M, "Resume 1", r"^LxResume 1$", note="not paused -> refused (would otherwise start a delivery)")
    case("Print", 1, M, "Print 1", r"^LxPrint 0$", note="idle: duplicate ticket")
    wait_idle_busy()
    for c, sh in (("Start 99", "Start"), ("Pause 99", "Pause"), ("Stop 99", "Stop"), ("Print 99", "Print"), ("Resume 99", "Resume")):
        case(sh, 3 if sh in ("Start", "Pause", "Resume") else 2, M, c, rf"^Lx{sh} 1$")
    # ---- delivery with the pulser ----
    t0 = int(time.time()) - 2
    before = t.getdata(1)
    case("Start", 1, M, "Start 1", r"^LxStart 0$", note="meter queues it (rc 38), counter test")
    case("RdRegister", 1, M, "RdRegister", r"^LxRdRegister 0,1$", note="busy meter stays registered")
    wait_idle_busy()
    pump(True, 60)
    time.sleep(3.0)
    d1 = t.getdata(1)
    case("GetData", 2, M, "GetData 1,0", r"^LxGetData 1,1,\d+,\d{10},[0-9.]+,60\.0,", note="live: pulser 60 gal/min")
    case("SwitchState", 1, M, "SwitchState 2", r"^LxSwitchState 1,Run$")
    case("Start", 3, M, "Start 1", r"^LxStart 0$", note="already running: real LCR no-op rc 0")
    case("Print", 2, M, "Print 1", r"^LxPrint 1$", note="no ticket during an active delivery (rc 120)")
    case("GetLastMtrCmd", 1, M, "GetLastMtrCmd 1", r"^LxGetLastMtrCmd 1,Print,1$")
    case("Pause", 1, M, "Pause 1", r"^LxPause 0$", wait=2.0)
    fact("Pause", 1, M, "meter paused (valve closed, flow 0)", state()["state"] == "STOP", "not paused")
    case("Resume", 1, M, "Resume 1", r"^LxResume 0$", note="paused -> resumes", wait=2.5)
    d2 = t.getdata(1)
    fact("GetData", 2, M, f"gallons rising {d1.get('gross')} -> {d2.get('gross')}", d2.get("gross", 0) > d1.get("gross", 0), "not rising", [d2.get("line")])
    case("Stop", 1, M, "Stop 1", r"^LxStop 0$")
    pump(False)
    wait_idle_busy()
    end = t.getdata(1)
    ok = end.get("initial") == before.get("total") and abs(end.get("total", 0) - before.get("total", 0) - end.get("gross", 0)) < 0.15 and end.get("flow") == 0
    fact("Stop", 1, M, f"ticket math {before.get('total')} + {end.get('gross')} = {end.get('total')}", ok, "mismatch", [end.get("line")])
    case("GetLastMtrCmd", 1, M, "GetLastMtrCmd 1", r"^LxGetLastMtrCmd 1,Stop,0$")
    # ---- history ----
    case("HisDataTime", 1, M, "HisDataTime 1", r"^LxHisDataTime 1,\d{10},\d{10}")
    case("HisDataTime", 2, M, "HisDataTime 99", r"^LxHisDataTime 99,?$")
    case("BoxStorage", 1, M, "BoxStorage 1", r"^LxBoxStorage 1,[1-9]\d*,\d+,")
    case("BoxStorage", 2, M, "BoxStorage 0", r"^LxBoxStorage 1$")
    case("BoxStorage", 3, M, "BoxStorage 99", r"^LxBoxStorage 99,0,\d+,")
    h = case("GetData", 1, M, "GetData 1,1", r"^LxGetDataTs 1,0,\d+,\d{10},")
    seq = t.fields(h[0])[2] if h and t.fields(h[0])[2:3] else "0"
    case("GetDataEcho", 1, M, f"GetDataEcho {seq},0,5", r"^LxGetDataTs 1,0,\d+,\d{10},", note="ack -> next packet")
    h = case("GetData", 1, M, "GetData 1,1", r"^LxGetDataTs 1,0,")
    seq = t.fields(h[0])[2] if h and t.fields(h[0])[2:3] else "0"
    case("GetDataEcho", 2, M, f"GetDataEcho {seq},1,5", r"^LxGetDataTs 1,$", note="failure flag -> upload stops")
    case("GetData", 1, M, "GetData 1,1", r"^LxGetDataTs 1,0,")
    case("GetDataEcho", 3, M, "GetDataEcho 99,0,5", r"^LxGetDataTs 1,$", note="seq mismatch -> upload stops")
    case("GetData", 4, M, "GetData 99,1", r"Error")
    case("GetDataTs", 1, M, f"GetDataTs 1,{t0},{int(time.time()) + 5}", lambda l: (len(l) >= 2 and l[0].startswith("LxGetDataTs 1,1,"), "records"),
         note="tracker range 1700000000.. predates this clock; delivery window used")
    case("GetDataTs", 2, M, f"GetDataTs 1,{int(time.time()) + 5},{t0}", r"^LxGetDataTs 1,?$", note="end before start")
    case("DeleteAll", 2, M, "DeleteAll 99", r"^LxDeleteAll 1$")
    case("DeleteAll", 1, M, "DeleteAll 1", r"^LxDeleteAll 0$")
    case("BoxStorage", 1, M, "BoxStorage 1", r"^LxBoxStorage 1,0,\d+,", note="after DeleteAll")


def main():
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else "../test_results/command_matrix.json"
    models = sys.argv[sys.argv.index("--models") + 1].split(",") if "--models" in sys.argv else [m for m, _ in MODELS]
    box_cases()
    for model, name in MODELS:
        if model in models:
            meter_cases(model, name)
    # box reset last (the BLE link drops)
    case("SetBtName", 1, "box", "SetBtName PandaBox1", r"^LxSetBtName 0$", wait=2.0)
    case("SetBtName", 1, "box", "RdBtName", r"^LxRdBtName PandaBox1$")
    case("SetBtName", 1, "box", "SetBtName PandaBrain", r"^LxSetBtName 0$", wait=2.0, note="restore")
    case("BoxReset", 1, "box", "BoxReset", r"^LxBoxReset 0$", wait=8.0)
    ok = connect()
    fact("BoxReset", 1, "box", "box restarts and advertises again (reconnected)", ok, "no reconnect")
    if ok:
        t.run("box", f"SetBoxTime {int(time.time())}", r"^LxSetBoxTime 0$")
        t.run("box", "SetPortLcrNode 0,1", r"^LxSetPortLcrNode 0$")
    json.dump(rows, open(out, "w"), indent=1)
    passed = sum(r["ok"] for r in rows)
    print(f"\n{passed}/{len(rows)} cases passed  ({len(set(r['sheet'] for r in rows))} commands)")


if __name__ == "__main__":
    main()
