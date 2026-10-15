"""Generate the KiCad schematic (opentherm-gateway.kicad_sch) from design.py.

Every part is a stock KiCad library symbol; each pin gets a local net label, so the sheet is a
netlist-faithful (not a hand-drawn) schematic. Run with plain python.
"""
import os
import re
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from design import PARTS, NETS
from sexpr import parse, KICAD

OUT = os.path.normpath(os.path.join(HERE, "..", "opentherm-gateway.kicad_sch"))
PROJECT = "opentherm-gateway"
SHEET = str(uuid.uuid4())

# part -> (library, symbol)
SYMBOL = {
    "R": ("Device", "R"), "C": ("Device", "C"), "L": ("Device", "L"), "LED": ("Device", "LED"),
    "1N4148W": ("Device", "D"), "1N5819W": ("Device", "D_Schottky"),
    "ZENER": ("Device", "D_Zener"), "TVS": ("Device", "D_TVS"),
    "MMBT2907A": ("Transistor_BJT", "MMBT3906"), "MMBT2222A": ("Transistor_BJT", "MMBT2222A"),
    "LTV-817S": ("Isolator", "LTV-817S"),
    "ESP32": ("RF_Module", "ESP32-C3-WROOM-02"),
    "AP2112K-3.3": ("Regulator_Linear", "AP2112K-3.3"),
    "USBLC6-2SC6": ("Power_Protection", "USBLC6-2SC6"),
    "USB-C": ("Connector", "USB_C_Receptacle_USB2.0_16P"),
    "TERM": ("Connector", "Screw_Terminal_01x02"),
    "SW": ("Switch", "SW_Push"), "TP": ("Connector", "TestPoint"),
    "H": ("Mechanical", "MountingHole"),
    "PWR_FLAG": ("power", "PWR_FLAG"),
}

MT3540 = '''	(symbol "MT3540"
		(exclude_from_sim no)
		(in_bom yes)
		(on_board yes)
		(property "Reference" "U" (at 0 8.89 0) (effects (font (size 1.27 1.27))))
		(property "Value" "MT3540" (at 0 -8.89 0) (effects (font (size 1.27 1.27))))
		(property "Footprint" "Package_TO_SOT_SMD:SOT-23-5" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
		(property "Datasheet" "~" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
		(property "Description" "1.2 MHz boost converter" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
		(symbol "MT3540_0_1"
			(rectangle (start -5.08 7.62) (end 5.08 -7.62) (stroke (width 0.254) (type default)) (fill (type background)))
		)
		(symbol "MT3540_1_1"
			(pin passive line (at -7.62 5.08 0) (length 2.54) (name "SW" (effects (font (size 1.27 1.27)))) (number "1" (effects (font (size 1.27 1.27)))))
			(pin power_in line (at 0 -10.16 90) (length 2.54) (name "GND" (effects (font (size 1.27 1.27)))) (number "2" (effects (font (size 1.27 1.27)))))
			(pin input line (at 7.62 -2.54 180) (length 2.54) (name "FB" (effects (font (size 1.27 1.27)))) (number "3" (effects (font (size 1.27 1.27)))))
			(pin input line (at -7.62 -2.54 0) (length 2.54) (name "EN" (effects (font (size 1.27 1.27)))) (number "4" (effects (font (size 1.27 1.27)))))
			(pin power_in line (at 0 10.16 270) (length 2.54) (name "VIN" (effects (font (size 1.27 1.27)))) (number "5" (effects (font (size 1.27 1.27)))))
		)
	)
'''


def balanced(text, start):
    depth = 0
    in_str = False
    i = start
    while i < len(text):
        c = text[i]
        if in_str:
            if c == "\\":
                i += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    raise ValueError("unbalanced")


_libs = {}


def lib_text(lib):
    if lib not in _libs:
        _libs[lib] = open(f"{KICAD}\\symbols\\{lib}.kicad_sym", encoding="utf-8").read()
    return _libs[lib]


def sym_text(lib, name):
    text = lib_text(lib)
    m = re.search(r'\n\t\(symbol "%s"\n' % re.escape(name), text)
    if not m:
        raise KeyError(f"{lib}:{name}")
    return balanced(text, m.start() + 1)


def flatten(lib, name):
    t = sym_text(lib, name)
    m = re.search(r'\n\t\t\(extends "([^"]+)"\)', t)
    if not m:
        return t
    base_name = m.group(1)
    base = flatten(lib, base_name)
    subs = []
    for sm in re.finditer(r'\n\t\t\(symbol "%s_\d+_\d+"' % re.escape(base_name), base):
        subs.append(balanced(base, sm.start() + 1))
    child = t.replace(m.group(0), "")
    extra = "".join("\n\t\t" + s.replace('"%s_' % base_name, '"%s_' % name) for s in subs)
    return child[:-1].rstrip() + extra + "\n\t)"


def lib_symbol(lib, name):
    """Return (text for the lib_symbols section, pins) with the Lib:Name id."""
    if name == "MT3540":
        t = MT3540
    else:
        t = flatten(lib, name)
    t = t.replace('(symbol "%s"' % name, '(symbol "%s:%s"' % (lib, name), 1)
    tree = parse(t)
    pins = []

    def walk(n, unit_ok=True):
        for x in n:
            if isinstance(x, list) and x:
                if x[0] == "symbol" and len(x) > 1:
                    m = re.search(r"_(\d+)_(\d+)$", x[1])
                    ok = (not m) or (m.group(1) in ("0", "1") and m.group(2) in ("0", "1"))
                    if ok:
                        walk(x)
                elif x[0] == "pin":
                    d = {}
                    for y in x:
                        if isinstance(y, list) and y:
                            d[y[0]] = y
                    at = d["at"]
                    pins.append({"num": d["number"][1], "x": float(at[1]), "y": float(at[2]),
                                 "ang": int(float(at[3])), "len": float(d["length"][1])})
                else:
                    walk(x)

    walk(tree)
    return "\t" + t.replace("\n", "\n\t") if not t.startswith("\t") else t, pins


def classify(ref, val, fp):
    if ref.startswith("TP"):
        return "TP"
    if ref.startswith("H"):
        return "H"
    if ref.startswith("SW"):
        return "SW"
    if ref in ("J1", "J2", "J5", "J6"):
        return "TERM"
    if ref == "J3":
        return "USB-C"
    if ref == "U1":
        return "MT3540"
    if ref == "U4":
        return "ESP32"
    if ref.startswith("R"):
        return "R"
    if ref.startswith("C"):
        return "C"
    if ref.startswith("L"):
        return "L"
    if ref.startswith("D"):
        if "LED" in fp:
            return "LED"
        if val.startswith("SMAJ"):
            return "TVS"
        if val in ("4V7", "13V", "4V3", "8V2"):
            return "ZENER"
        return val
    return val


def uid():
    return str(uuid.uuid4())


pin_net = {}
for net, pins in NETS.items():
    for p in pins:
        pin_net[p] = net

# ---- collect symbols --------------------------------------------------------------------
used = {}      # lib_id -> (text, pins)
items = []     # (ref, kind, lib_id, pins, value, footprint, lcsc, desc)
for ref, (val, fp, lcsc, desc) in PARTS.items():
    kind = classify(ref, val, fp)
    if kind == "MT3540":
        lib, name = "Local", "MT3540"
    else:
        lib, name = SYMBOL[kind]
    lid = f"{lib}:{name}"
    if lid not in used:
        used[lid] = lib_symbol(lib, name)
    items.append((ref, lid, used[lid][1], val, fp, lcsc, desc))
for net in ("GND",):
    if "power:PWR_FLAG" not in used:
        used["power:PWR_FLAG"] = lib_symbol("power", "PWR_FLAG")

# ---- layout ---------------------------------------------------------------------------------
ORDER = {"J": 0, "U": 1, "Q": 2, "D": 3, "L": 4, "C": 5, "R": 6, "S": 7, "T": 8, "H": 9}
items.sort(key=lambda it: (ORDER.get(it[0][0], 10), int(re.sub(r"\D", "", it[0]) or 0)))

PAPER_W = 1180.0
x0, y0 = 25.4, 38.1
x, y, rowh = x0, y0, 0.0
placed = []


def snap(v):
    return round(v / 2.54) * 2.54


for ref, lid, pins, val, fp, lcsc, desc in items:
    xs = [p["x"] for p in pins] or [0.0]
    ys = [p["y"] for p in pins] or [0.0]
    minx, maxx, miny, maxy = min(xs), max(xs), min(ys), max(ys)
    w = (maxx - minx) + 30.0
    h = (maxy - miny) + 18.0
    if x + w > PAPER_W - 20:
        x = x0
        y += rowh + 6.0
        rowh = 0.0
    cx = snap(x - minx + 15.0)
    cy = snap(y + maxy + 9.0)
    placed.append((ref, lid, pins, val, fp, lcsc, desc, cx, cy))
    x += w
    rowh = max(rowh, h)
sheet_h = y + rowh + 40.0

# ---- write ----------------------------------------------------------------------------------
out = []
out.append("(kicad_sch\n\t(version 20260101)\n\t(generator \"eeschema\")\n\t(generator_version \"10.0\")\n"
           f"\t(uuid \"{SHEET}\")\n\t(paper \"A0\")\n\t(title_block\n\t\t(title \"OpenTherm gateway\")\n"
           "\t\t(rev \"A\")\n\t\t(comment 1 \"Derived from the MPro Arduino OpenTherm shield (CERN-OHL-P-2.0)\")\n\t)\n")
out.append("\t(lib_symbols\n")
for lid, (t, _) in used.items():
    out.append(t.rstrip() + "\n")
out.append("\t)\n")

LABEL_ANG = {0: 180, 180: 0, 90: 270, 270: 90}


def prop(name, val, x, y, hide=False):
    h = " (hide yes)" if hide else ""
    return (f'\t\t(property "{name}" "{val}" (at {x:.2f} {y:.2f} 0) '
            f'(effects (font (size 1.27 1.27)){h}))\n')


def add_label(net, px, py, ang):
    la = LABEL_ANG.get(ang, 0)
    just = "left bottom" if la in (0, 90) else "right bottom"
    out.append(f'\t(label "{net}" (at {px:.2f} {py:.2f} {la}) '
               f'(effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid()}"))\n')


def add_nc(px, py):
    out.append(f'\t(no_connect (at {px:.2f} {py:.2f}) (uuid "{uid()}"))\n')


for ref, lid, pins, val, fp, lcsc, desc, cx, cy in placed:
    s = [f'\t(symbol (lib_id "{lid}") (at {cx:.2f} {cy:.2f} 0) (unit 1)\n'
         '\t\t(exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no)\n'
         f'\t\t(uuid "{uid()}")\n']
    s.append(prop("Reference", ref, cx + 3.0, cy - 4.0))
    s.append(prop("Value", val, cx + 3.0, cy + 4.0))
    s.append(prop("Footprint", fp, cx, cy, True))
    s.append(prop("Datasheet", "~", cx, cy, True))
    if lcsc:
        s.append(prop("LCSC", lcsc, cx, cy, True))
    seen = set()
    for p in pins:
        if p["num"] in seen:
            continue
        seen.add(p["num"])
        s.append(f'\t\t(pin "{p["num"]}" (uuid "{uid()}"))\n')
    s.append(f'\t\t(instances (project "{PROJECT}" (path "/{SHEET}" (reference "{ref}") (unit 1))))\n')
    s.append("\t)\n")
    out.append("".join(s))
    for p in pins:
        px, py = cx + p["x"], cy - p["y"]
        net = pin_net.get(f"{ref}.{p['num']}")
        if net:
            add_label(net, px, py, p["ang"])
        else:
            add_nc(px, py)

# power flags (one per supply net) hung on the net by label, in a free corner
fx, fy = x0 + 10.0, sheet_h + 10.0
_, flag_pins = used["power:PWR_FLAG"]
for i, net in enumerate(("GND", "+5V", "+24V", "VBUS", "5V_IN")):
    cx, cy = snap(fx + i * 25.4), snap(fy)
    out.append(f'\t(symbol (lib_id "power:PWR_FLAG") (at {cx:.2f} {cy:.2f} 0) (unit 1)\n'
               '\t\t(exclude_from_sim no) (in_bom no) (on_board no) (dnp no)\n'
               f'\t\t(uuid "{uid()}")\n'
               + prop("Reference", f"#FLG0{i + 1}", cx, cy - 3.0, True)
               + prop("Value", "PWR_FLAG", cx, cy - 3.0, True)
               + prop("Footprint", "", cx, cy, True) + prop("Datasheet", "~", cx, cy, True)
               + f'\t\t(pin "1" (uuid "{uid()}"))\n'
               + f'\t\t(instances (project "{PROJECT}" (path "/{SHEET}" (reference "#FLG0{i + 1}") (unit 1))))\n\t)\n')
    p = flag_pins[0]
    add_label(net, cx + p["x"], cy - p["y"], p["ang"])

out.append(f'\t(sheet_instances (path "/" (page "1")))\n\t(embedded_fonts no)\n)\n')
open(OUT, "w", encoding="utf-8").write("".join(out))
print("written", OUT, "symbols:", len(placed), "lib symbols:", len(used))
