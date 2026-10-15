"""Fabrication outputs: Gerbers + drill (zip), BOM, pick-and-place. Run with plain python."""
import csv, os, subprocess, sys, zipfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from design import PARTS

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.normpath(os.path.join(HERE, ".."))
PCB = os.path.join(HW, "opentherm-gateway.kicad_pcb")
OUT = os.path.join(HW, "production")
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
os.makedirs(OUT, exist_ok=True)
gdir = os.path.join(OUT, "gerber")
os.makedirs(gdir, exist_ok=True)
for f in os.listdir(gdir):
    os.remove(os.path.join(gdir, f))

subprocess.run([CLI, "pcb", "export", "gerbers", "--output", gdir + os.sep,
                "--layers", "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts",
                "--no-protel-ext", PCB], check=True)
subprocess.run([CLI, "pcb", "export", "drill", "--output", gdir + os.sep, "--format", "excellon",
                "--drill-origin", "absolute", "--excellon-units", "mm",
                "--generate-map", "--map-format", "gerberx2", PCB], check=True)
with zipfile.ZipFile(os.path.join(OUT, "opentherm-gateway-gerber.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    for f in sorted(os.listdir(gdir)):
        z.write(os.path.join(gdir, f), f)

subprocess.run([CLI, "pcb", "export", "pos", "--output", os.path.join(OUT, "positions.csv"),
                "--format", "csv", "--units", "mm", "--side", "both", PCB], check=True)

groups = {}
for ref, (val, fp, lcsc, desc) in PARTS.items():
    if ref.startswith(("H", "TP")):
        continue
    key = (val, fp.split(":")[1], lcsc, desc)
    groups.setdefault(key, []).append(ref)


def refkey(r):
    return (r.rstrip("0123456789"), int(r.lstrip("ABCDEFGHIJKLMNOPQRSTUVWXYZ")))


rows = []
for (val, fp, lcsc, desc), refs in groups.items():
    refs.sort(key=refkey)
    rows.append((refkey(refs[0]), val, ",".join(refs), fp, len(refs), lcsc, desc))
rows.sort()
# one row per part number (JLCPCB rejects duplicate part numbers); designators are merged
by_code = {}
for _, val, refs, fp, qty, lcsc, desc in rows:
    by_code.setdefault(lcsc, []).append((val, refs, fp, desc))
merged = []
for lcsc, items in by_code.items():
    refs = sorted((r for _, rs, _, _ in items for r in rs.split(",")), key=refkey)
    vals = []
    for v, _, _, _ in items:
        if v not in vals:
            vals.append(v)
    notes = []
    for _, _, _, d in items:
        if d and d not in notes:
            notes.append(d)
    merged.append((refkey(refs[0]), " / ".join(vals), refs, items[0][2], lcsc, "; ".join(notes)))
merged.sort()
with open(os.path.join(OUT, "bom-jlcpcb.csv"), "w", newline="", encoding="utf-8") as f:
    w = csv.writer(f)
    w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
    for _, val, refs, fp, lcsc, note in merged:
        w.writerow([val, ",".join(refs), fp, lcsc])
try:
    with open(os.path.join(OUT, "bom.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Value", "Designators", "Footprint", "Qty", "LCSC", "Note"])
        for _, val, refs, fp, lcsc, note in merged:
            w.writerow([val, ",".join(refs), fp, len(refs), lcsc, note])
except PermissionError:
    print("bom.csv is open in another program: not updated (bom-jlcpcb.csv has the same data)")
print("outputs in", OUT)
