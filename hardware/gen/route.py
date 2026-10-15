"""Export DSN, run Freerouting headless, import the SES back. Run with KiCad's python."""
import os, subprocess, sys
import pcbnew
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from placement import BOARD_W, BOARD_H, PLACE

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.normpath(os.path.join(HERE, "..", "opentherm-gateway.kicad_pcb"))
TMP = os.path.join(HERE, "..", "build")
os.makedirs(TMP, exist_ok=True)
DSN = os.path.join(TMP, "gateway.dsn")
SES = os.path.join(TMP, "gateway.ses")
JAR = r"C:\Users\mesga\AppData\Local\freerouting\app\freerouting-executable.jar"

board = pcbnew.LoadBoard(PCB)
passes = sys.argv[1] if len(sys.argv) > 1 else "40"
POST_ONLY = passes in ("post", "stitch")  # keep the existing routing
STITCH = passes == "stitch"               # (also add the grid of ground vias)
ROUTE_ONLY = not POST_ONLY
if not POST_ONLY:
    for t in list(board.GetTracks()):
        board.Remove(t)
    if not pcbnew.ExportSpecctraDSN(board, DSN):
        raise SystemExit("DSN export failed")
    if os.path.exists(SES):
        os.remove(SES)
    subprocess.run(["java", "-jar", JAR, "-de", DSN, "-do", SES, "-mp", passes, "--gui.enabled=false"], check=False)
    if not os.path.exists(SES):
        raise SystemExit("no SES produced")
    if not pcbnew.ImportSpecctraSES(board, SES):
        raise SystemExit("SES import failed")
    board.Save(PCB)
    print("routed:", sum(1 for _ in board.GetTracks()), "track/via items")
    sys.exit(0)
# --- GND stitching vias on a grid wherever there is room ---------------------------------
mm = pcbnew.FromMM
gnd = board.FindNet("GND")
clear = mm(0.3) + mm(0.45)
pos_all = []
def free(pt, clear=clear):
    x, y = pcbnew.ToMM(pt.x), pcbnew.ToMM(pt.y)
    ux, uy = PLACE['U4'][0], PLACE['U4'][1]
    if ux - 14.5 <= x <= ux + 14.5 and y <= uy - 6.0:
        return False
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.HitTest(pt, clear if pad.GetNetname() != "GND" else mm(0.3)) and pad.GetNetname() != "GND":
                return False
            if pad.GetNetname() == "GND" and pad.HitTest(pt, mm(0.1)):
                return False
        if fp.GetReference().startswith("H") and fp.GetBoundingBox().Contains(pt):
            return False
    for t in board.GetTracks():
        if t.HitTest(pt, clear):
            return False
    for z in board.Zones():
        if z.GetIsRuleArea() and z.HitTestFilledArea(pcbnew.F_Cu, pt, 0) or (z.GetIsRuleArea() and z.GetBoundingBox().Contains(pt)):
            return False
    return True
n = 0
step = 4.0
y = 3.0
while y < BOARD_H - 1.5 and STITCH:
    x = 3.0
    while x < BOARD_W - 1.5:
        pt = pcbnew.VECTOR2I(mm(x), mm(y))
        if free(pt):
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pt)
            v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3))
            v.SetNet(gnd)
            board.Add(v)
            n += 1
        x += step
    y += step
print("stitch vias:", n, flush=True)
def fill():
    for z in board.Zones():
        z.SetNeedRefill(True)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def island_fix(tight=False):
    print("island_fix start", flush=True)
    """Add a GND via in every filled GND island that has no via/pad (returns vias added)."""
    added = 0
    gnd_vias = []
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T and t.GetNetname() == "GND":
            gnd_vias.append(t.GetPosition())
    for z in board.Zones():
        if z.GetIsRuleArea() or z.GetNetname() != "GND":
            continue
        layer = z.GetLayer()
        polys = z.GetFilledPolysList(layer)
        for i in range(polys.OutlineCount()):
            if any(polys.Contains(p, i) for p in gnd_vias):
                continue
            bb = polys.Outline(i).BBox()
            done = False
            gx = bb.GetLeft()
            step = mm(0.25)
            while gx < bb.GetRight() and not done:
                gy = bb.GetTop()
                while gy < bb.GetBottom():
                    pt = pcbnew.VECTOR2I(gx, gy)
                    if polys.Contains(pt, i) and (free(pt) or (tight and free(pt, mm(0.55)))):
                        v = pcbnew.PCB_VIA(board)
                        v.SetPosition(pt); v.SetWidth(mm(0.6)); v.SetDrill(mm(0.3)); v.SetNet(gnd)
                        board.Add(v)
                        added += 1
                        done = True
                        break
                    gy += step
                gx += step
    return added


fill()
for _ in range(4):
    n2 = island_fix()
    if not n2:
        n2 = island_fix(True)
    print("done", n2, flush=True)
    print("island vias:", n2)
    if not n2:
        break
    fill()
board.Save(PCB)
print("routed:", sum(1 for _ in board.GetTracks()), "track/via items")
