"""
product_suite.py - product-wise functional validation through the pandabox-tester (BLE).

Bench wiring: Port 1 (J1) empty; Port 2 (J2) -> USB-RS232 COM7 -> LCR simulator, which answers two
meters on that wire: node 1 = LCR-II, node 2 = LCR.iQ. One meter per port, so each product is tested
by pointing Port 2 at its node (SetPortLcrNode 0,<node>).

    python tools/product_suite.py [--out results.json]

Sections: box functions, Port 1 empty, then for each product: identity/settings, node scan, node
change (ModifyLcrNode) and back, presets, delivery (Start/flow/Pause/Resume/Stop/ticket math), preset
auto-stop, history (HisDataTime/BoxStorage/GetData n,1/GetDataEcho/GetDataTs/DeleteAll), wrong node,
meter unplugged mid-delivery; finally BT rename and BoxReset.
"""
import json
import sys
import time

import tracker_suite as t
from tcmd import cmd, connect

PRODUCTS = [("LCR-II", 1, 60.0), ("LCR.iQ", 2, 45.0)]     # name, simulator node, pump gal/min


def product(name, node, pump):
    tag = f"{name}"
    t.run(tag, f"SetPortLcrNode 0,{node}", r"^LxSetPortLcrNode 0$", wait=2.5)
    t.run(tag, "RdPortLcrNode", rf"^LxRdPortLcrNode 0,{node}$")
    t.run(tag, "RdRegister", r"^LxRdRegister 0,1$", note="Port 1 empty, Port 2 registered")
    t.run(tag, "BoxStatus", rf"^LxBoxStatus 2,0,0,{node},")
    t.run(tag, "SwitchState 2", rf"^LxSwitchState {node},Stop$", note="idle meter")
    t.run(tag, "RdMtrSetting 2", rf"^LxRdMtrSetting 2,{node},\d+,(yes|no|skip),(clear|multiple|retain)$")
    t.run(tag, "GetLcrNode 2,1,10", rf"^LxFindLcrNode 2,{node}$",
          note="meter online -> its node at once, no scan (V2.89)")
    t.run(tag, "GetLcrNode 2,5,10", rf"^LxFindLcrNode 2,{node}$", note="V2.89 answers the live node even outside the range")
    t.run(tag, f"GetData {node} 0", rf"^LxGetData {node},1,\d+,\d{{10}},")
    t.run(tag, "Stop %d" % node, None)
    t.run(tag, f"PresetGross {node},0", r"^LxPresetGross 0$")

    # ---- node change and back ----
    t.run(tag, f"ModifyLcrNode 2,{node},9", r"^LxModifyLcrNode 0$", wait=2.5)
    t.run(tag, "RdPortLcrNode", r"^LxRdPortLcrNode 0,9$")
    t.run(tag, "RdRegister", r"^LxRdRegister 0,1$", note="meter answers on its new node 9")
    t.run(tag, "GetData 9 0", r"^LxGetData 9,1,")
    t.run(tag, f"GetData {node} 0", r"^LxGetData Error,", note="old node no longer configured")
    t.run(tag, "RdMtrSetting 2", r"^LxRdMtrSetting 2,9,", note="#102 LCRNode reads back 9")
    t.run(tag, "Start 9", r"^LxStart 0$", wait=2.0)
    t.check_live(tag, 9, True, "delivery on the new node")
    t.run(tag, "Stop 9", r"^LxStop 0$", wait=1.5)
    t.run(tag, f"ModifyLcrNode 2,9,{node}", r"^LxModifyLcrNode 0$", note="restore", wait=2.5)
    t.run(tag, "RdPortLcrNode", rf"^LxRdPortLcrNode 0,{node}$")
    t.run(tag, f"ModifyLcrNode 2,77,5", r"^LxModifyLcrNode 1$", note="no meter at node 77")
    t.run(tag, f"ModifyLcrNode 1,1,5", r"^LxModifyLcrNode 1$", note="Port 1 empty")

    # ---- presets ----
    t.run(tag, f"PresetGross {node},125.5", r"^LxPresetGross 0$")
    t.run(tag, f"PresetNet {node},125.5", r"^LxPresetNet 0$")
    t.run(tag, f"PresetGross {node},-1.0", r"^LxPresetGross 1$")
    t.run(tag, "PresetGross 7,10.0", r"^LxPresetGross 1$", note="node 7 not configured")
    t.run(tag, f"PresetGross {node},0", r"^LxPresetGross 0$")
    t.run(tag, f"PresetNet {node},0", r"^LxPresetNet 0$")

    # ---- delivery ----
    t.run(tag, f"DeleteAll {node}", r"^LxDeleteAll 0$")
    t.run(tag, f"HisDataTime {node}", rf"^LxHisDataTime {node},?$", note="empty after DeleteAll")
    t_start = int(time.time()) - 2
    before = t.getdata(node)
    t.run(tag, f"Start {node}", r"^LxStart 0$", wait=4.0)
    t.run(tag, "SwitchState 2", rf"^LxSwitchState {node},Run$")
    t.run(tag, f"GetLastMtrCmd {node}", rf"^LxGetLastMtrCmd {node},Start,0$")
    d1 = t.check_live(tag, node, True, "flowing")
    time.sleep(2)
    d2 = t.check_live(tag, node, True, "still flowing")
    ok = d2.get("gross", 0) > d1.get("gross", 0) and d2.get("total", 0) > d1.get("total", 0)
    near = abs(d2.get("flow", 0) - pump) <= 0.5
    t.results.append({"id": tag, "command": "GetData x2", "response": [d1.get("line"), d2.get("line")],
                      "ok": ok and near, "why": "" if ok and near else f"volume must rise, flow ~{pump}",
                      "note": f"gross/total rise, flow {d2.get('flow')} ~ pump {pump}"})
    print(f"{'PASS' if ok and near else 'FAIL'} {tag:8s} rising {d1.get('gross')}->{d2.get('gross')}, flow {d2.get('flow')}")
    t.run(tag, f"Pause {node}", r"^LxPause 0$", wait=2.5)
    p1 = t.check_live(tag, node, False, "paused: flow 0")
    time.sleep(1.5)
    p2 = t.getdata(node)
    ok = p1.get("gross") == p2.get("gross")
    t.results.append({"id": tag, "command": "GetData (paused)", "response": [p2.get("line")], "ok": ok,
                      "why": "" if ok else "volume moved while paused", "note": "volume holds while paused"})
    print(f"{'PASS' if ok else 'FAIL'} {tag:8s} paused gross {p1.get('gross')} == {p2.get('gross')}")
    t.run(tag, f"Start {node}", r"^LxStart 0$", note="resume", wait=3.0)
    t.check_live(tag, node, True, "resumed")
    t.run(tag, f"Stop {node}", r"^LxStop 0$", wait=2.5)
    end = t.check_live(tag, node, False, "stopped")
    ok = ("total" in end and "total" in before and end["initial"] == before["total"] and
          abs(end["total"] - (before["total"] + end["gross"])) < 0.25)
    t.results.append({"id": tag, "command": "GetData (final)", "response": [end.get("line")], "ok": ok,
                      "why": "" if ok else "final != initial + gross", "note": "ticket math"})
    print(f"{'PASS' if ok else 'FAIL'} {tag:8s} {before.get('total')} + {end.get('gross')} = {end.get('total')}")
    t.run(tag, f"GetLastMtrCmd {node}", rf"^LxGetLastMtrCmd {node},Stop,0$")
    t.run(tag, "SwitchState 2", rf"^LxSwitchState {node},Stop$")

    # ---- history ----
    t.run(tag, f"HisDataTime {node}", rf"^LxHisDataTime {node},\d{{10}},\d{{10}}")
    t.run(tag, f"BoxStorage {node}", lambda l: (bool(l) and int(t.fields(l[0])[1]) > 0, "records expected"))
    h = t.run(tag, f"GetData {node},1", rf"^LxGetDataTs {node},0,\d+,\d{{10}},")
    if h and t.fields(h[0])[2:3]:
        t.run(tag, f"GetDataEcho {t.fields(h[0])[2]},0,1", rf"^LxGetDataTs {node},", note="ack -> next record")
    t.run(tag, f"GetDataTs {node},{t_start},{int(time.time()) + 5}",
          lambda l: (len(l) >= 2 and l[0].startswith(f"LxGetDataTs {node},1,"), "records in range"))
    t.run(tag, f"GetDataTs {node},1700000000,1700000001", rf"^LxGetDataTs {node},?$", note="empty range")
    t.run(tag, f"DeleteAll {node}", r"^LxDeleteAll 0$")
    t.run(tag, f"BoxStorage {node}", rf"^LxBoxStorage {node},0,")

    # ---- preset auto-stop ----
    t.run(tag, f"PresetGross {node},4.0", r"^LxPresetGross 0$")
    t.run(tag, f"Start {node}", r"^LxStart 0$", wait=10.0)
    d = t.getdata(node)
    ok = d.get("gross") == 4.0 and d.get("flow") == 0.0
    t.results.append({"id": tag, "command": "GetData (preset)", "response": [d.get("line")], "ok": ok,
                      "why": "" if ok else "expected stop at 4.0", "note": "preset reached"})
    print(f"{'PASS' if ok else 'FAIL'} {tag:8s} preset -> {d.get('line')}")
    t.run(tag, f"Stop {node}", None)
    t.run(tag, f"PresetGross {node},0", r"^LxPresetGross 0$")

    # ---- meter unplugged mid-delivery ----
    t.run(tag, f"Start {node}", r"^LxStart 0$", wait=2.0)
    t.sim("/api/serial/stop")
    time.sleep(4.5)
    t.run(tag, "RdRegister", r"^LxRdRegister 0,0$", note="meter unplugged")
    t.run(tag, f"GetData {node} 0", r"^LxGetData Error,")
    t.run(tag, f"Stop {node}", r"^LxStop 1$")
    t.sim("/api/serial/start", {"product_key": "lcr2"})
    time.sleep(3.0)
    t.run(tag, "RdRegister", r"^LxRdRegister 0,1$", note="plugged back in")
    t.check_live(tag, node, True, "delivery kept running on the meter")
    t.run(tag, f"Stop {node}", r"^LxStop 0$", wait=2.0)


def main():
    out = sys.argv[sys.argv.index("--out") + 1] if "--out" in sys.argv else None
    B = "BOX"
    t.run(B, f"SetBoxTime {int(time.time())}", r"^LxSetBoxTime 0$")
    t.run(B, "SetMode 2", r"^LxSetMode 0$")
    t.run(B, "SetRs485 0", r"^LxSetRs485 0$")
    t.run(B, "BoxInfo", r"^LxBoxInfo 2\.4,250502,2\.9\d\d,\d{6},\d{15},LCR$")
    t.run(B, "BoxTime", lambda l: (bool(l) and abs(int(t.fields(l[0])[0]) - int(time.time())) < 30, "clock"))
    t.run(B, "RdBtName", r"^LxRdBtName PandaBrain")
    t.run(B, "RdDiagnostics", r"^LxRdDiagnostics [01],[012],-?\d+,\d+$")
    t.run(B, "SetWifiName MyNetwork", r"^LxSetWifiName 0$")
    t.run(B, "SetWifiPwd mypassword", r"^LxSetWifiPwd 0$")
    t.run(B, "SetServerIp 13.205.61.50", r"^LxSetServerIp 0$")
    t.run(B, "SetServerPort 8181", r"^LxSetServerPort 0$")
    t.run(B, "SetApn jionet", r"^LxSetApn 0$")
    t.run(B, "SetMode 1", r"^LxSetMode 0$")
    t.run(B, "GetData 2 0", r"^LxGetData Mode 1,", note="bridge mode")
    t.run(B, "SetMode 2", r"^LxSetMode 0$")

    P = "PORT1"                                     # nothing on J1
    t.run(P, "SetPortLcrNode 1,2", r"^LxSetPortLcrNode 0$", wait=4.5)
    t.run(P, "RdRegister", r"^LxRdRegister 0,1$", note="J1 empty -> port 1 not registered")
    t.run(P, "GetData 1 0", r"^LxGetData Error,")
    t.run(P, "Start 1", r"^LxStart 1$", note="no meter on J1")
    t.run(P, "SwitchState 1", r"Error")
    t.run(P, "RdMtrSetting 1", r"Error")
    t.run(P, "GetLcrNode 1,1,5", r"^LxFindLcrNode 0", note="scan of J1 finds nothing")
    t.run(P, "BoxStatus", r"^LxBoxStatus 2,0,1,2,")
    t.run(P, "SetPortLcrNode 2,2", r"^LxSetPortLcrNode 1$", note="both ports on one node is refused")
    t.run(P, "RdPortLcrNode", r"^LxRdPortLcrNode 1,2$", note="unchanged after the refusal")

    for name, node, pump in PRODUCTS:
        product(name, node, pump)

    W = "WRONGNODE"
    t.run(W, "SetPortLcrNode 0,7", r"^LxSetPortLcrNode 0$", wait=4.5)
    t.run(W, "RdRegister", r"^LxRdRegister 0,0$", note="no meter answers node 7")
    t.run(W, "Start 7", r"^LxStart 1$")
    t.run(W, "SetPortLcrNode 0,1", r"^LxSetPortLcrNode 0$", wait=2.5)
    t.run(W, "RdRegister", r"^LxRdRegister 0,1$")

    R = "BOX"
    t.run(R, "SetBtName PandaBox1", r"^LxSetBtName 0$", wait=2.0)
    t.run(R, "RdBtName", r"^LxRdBtName PandaBox1$")
    t.run(R, "SetBtName PandaBrain", r"^LxSetBtName 0$", wait=2.0)
    t.run(R, "BoxReset", r"^LxBoxReset 0$", wait=8.0)
    ok = connect()
    t.results.append({"id": R, "command": "reconnect after BoxReset", "response": [], "ok": ok,
                      "why": "" if ok else "no reconnect", "note": ""})
    print(f"{'PASS' if ok else 'FAIL'} BOX      reconnect after reset")
    if ok:
        t.run(R, f"SetBoxTime {int(time.time())}", r"^LxSetBoxTime 0$")
        t.run(R, "RdPortLcrNode", r"^LxRdPortLcrNode ", note="after reset")

    passed = sum(r["ok"] for r in t.results)
    print(f"\n{passed}/{len(t.results)} checks passed")
    if out:
        json.dump(t.results, open(out, "w"), indent=1)


if __name__ == "__main__":
    main()
