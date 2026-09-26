"""Build the command-matrix results workbook in the tracker's own layout.

    python tools/make_matrix_xlsx.py <tracker.xlsx> <command_matrix.json> <out.xlsx>

Master sheet (one row per command, pass counts per meter) + one sheet per command: the tracker's cases
(type, input, pre-condition, expected) next to the actual BLE replies from the box for each meter model.
"""
import collections
import json
import sys

import openpyxl
from openpyxl.styles import Alignment, Font, PatternFill
from openpyxl.utils import get_column_letter

tracker, data, out = sys.argv[1:4]
rows = json.load(open(data))
wb_in = openpyxl.load_workbook(tracker, data_only=True)

master = [r for r in list(wb_in["Master"].iter_rows(values_only=True))[2:] if r and r[1]]
cases = {}
for ws in wb_in.worksheets[4:]:
    lst = []
    for r in ws.iter_rows(values_only=True):
        if r and isinstance(r[0], (int, float)) and r[1]:
            lst.append({"no": int(r[0]), "type": r[1], "input": r[2], "pre": r[3], "exp": r[4]})
    cases[ws.title] = lst

by = collections.defaultdict(list)
for r in rows:
    by[(r["sheet"], r["case"], r["model"])].append(r)
MODELS = ["box", "LCR-II", "LCR 600", "LCR.iQ"]
GREEN, RED, GREY = "C6EFCE", "FFC7CE", "EDEDED"

wb = openpyxl.Workbook()
ms = wb.active
ms.title = "Master"
ms.append(["PandaBox V2.901 — command test results over BLE (pandabox-tester) with LCR simulator v6 meters on Port 2"])
ms["A1"].font = Font(bold=True, size=13)
ms.append(["#", "Command", "Category", "Cases in tracker", "Box / LCR-II / LCR 600 / LCR.iQ checks", "Passed", "Failed", "Result"])
for c in ms[2]:
    c.font = Font(bold=True, color="FFFFFF")
    c.fill = PatternFill("solid", fgColor="305496")
for m in master:
    cmdname = m[1]
    rs = [r for r in rows if r["sheet"] == cmdname]
    p = sum(r["ok"] for r in rs)
    f = len(rs) - p
    models = sorted({r["model"] for r in rs}, key=MODELS.index) if rs else []
    res = "NOT RUN" if not rs else ("PASS" if f == 0 else "FAIL")
    ms.append([m[0], cmdname, m[2], m[5], ", ".join(models), p, f, res])
    ms.cell(ms.max_row, 8).fill = PatternFill("solid", fgColor={"PASS": GREEN, "FAIL": RED}.get(res, GREY))
tot_p = sum(r["ok"] for r in rows)
ms.append([])
ms.append(["", "TOTAL", "", "", "", tot_p, len(rows) - tot_p, f"{tot_p}/{len(rows)}"])
for i, w in enumerate([5, 18, 20, 10, 34, 8, 8, 10], 1):
    ms.column_dimensions[get_column_letter(i)].width = w

for m in master:
    name = m[1]
    ws = wb.create_sheet(name[:31])
    ws.append([f"{name} — {m[3]}"])
    ws["A1"].font = Font(bold=True, size=12)
    hdr = ["#", "Case type", "Tracker input", "Pre-condition", "Tracker expected"]
    for mdl in MODELS:
        hdr += [f"{mdl}: sent -> reply", f"{mdl}"]
    ws.append(hdr)
    for c in ws[2]:
        c.font = Font(bold=True, color="FFFFFF")
        c.fill = PatternFill("solid", fgColor="305496")
    for cs in cases.get(name, []):
        line = [cs["no"], cs["type"], cs["input"], cs["pre"], cs["exp"]]
        cols = []
        for mdl in MODELS:
            rs = by.get((name, cs["no"], mdl), [])
            if not rs:
                line += ["", ""]
                cols.append(None)
                continue
            txt = "\n".join(f"{r['command']} -> {' | '.join(x for x in r['response'] if x)}" + (f"  [{r['note']}]" if r["note"] else "") for r in rs)
            ok = all(r["ok"] for r in rs)
            line += [txt, "PASS" if ok else "FAIL: " + "; ".join(r["why"] for r in rs if not r["ok"])]
            cols.append(ok)
        ws.append(line)
        for j, ok in enumerate(cols):
            if ok is not None:
                ws.cell(ws.max_row, 7 + 2 * j).fill = PatternFill("solid", fgColor=GREEN if ok else RED)
    widths = [4, 9, 26, 26, 34] + [40, 10] * len(MODELS)
    for i, w in enumerate(widths, 1):
        ws.column_dimensions[get_column_letter(i)].width = w
    for row in ws.iter_rows(min_row=2):
        for c in row:
            c.alignment = Alignment(wrap_text=True, vertical="top")
    ws.freeze_panes = "C3"
wb.save(out)
print(out, f"{tot_p}/{len(rows)}")
