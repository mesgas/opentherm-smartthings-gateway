"""Build the OpenTherm gateway PCB (unrouted) with KiCad's python.

Usage:  "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" build_pcb.py
Output: ../opentherm-gateway.kicad_pcb
"""
import os
import sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from design import PARTS, NETS
from placement import BOARD_W, BOARD_H, PLACE

FP_DIR = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"
OUT = os.path.normpath(os.path.join(HERE, "..", "opentherm-gateway.kicad_pcb"))
mm = pcbnew.FromMM


def V(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


board = pcbnew.BOARD()
ds = board.GetDesignSettings()
board.SetCopperLayerCount(4)
ds = board.GetDesignSettings()

# ---- nets -------------------------------------------------------------------------
nets = {}
for name in NETS:
    n = pcbnew.NETINFO_ITEM(board, name)
    board.Add(n)
    nets[name] = n
pin_net = {}
for net, pins in NETS.items():
    for p in pins:
        pin_net[p] = net

# ---- footprints -------------------------------------------------------------------
for ref, (val, fpname, lcsc, desc) in PARTS.items():
    lib, name = fpname.split(":")
    fp = pcbnew.FootprintLoad(os.path.join(FP_DIR, lib + ".pretty"), name)
    if fp is None:
        raise SystemExit("footprint not found: " + fpname)
    for pad in list(fp.Pads()):
        if pad.GetDrillSize().x and pad.GetDrillSize().x < mm(0.3):
            fp.Remove(pad)
    fp.SetReference(ref)
    fp.SetValue(val)
    x, y, rot, side = PLACE[ref]
    board.Add(fp)
    if side == "B":
        fp.Flip(V(x, y), False)
    fp.SetPosition(V(x, y))
    fp.SetOrientationDegrees(rot)
    for pad in fp.Pads():
        key = f"{ref}.{pad.GetNumber()}"
        if key in pin_net:
            pad.SetNet(nets[pin_net[key]])
    # keep reference small on the silkscreen
    fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
    fp.Reference().SetTextThickness(mm(0.12))
    fp.Value().SetVisible(False)

# ---- board outline -------------------------------------------------------------------
def edge(x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(x1, y1))
    s.SetEnd(V(x2, y2))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(mm(0.1))
    board.Add(s)


edge(0, 0, BOARD_W, 0)
edge(BOARD_W, 0, BOARD_W, BOARD_H)
edge(BOARD_W, BOARD_H, 0, BOARD_H)
edge(0, BOARD_H, 0, 0)

# ---- design rules / net classes -------------------------------------------------------
ds.m_TrackMinWidth = mm(0.15)
ds.m_ViasMinSize = mm(0.6)
ds.m_ViasMinAnnulus = mm(0.15)
ds.m_MinThroughDrill = mm(0.3)
ns = ds.m_NetSettings
default = ns.GetDefaultNetclass()
default.SetTrackWidth(mm(0.25))
default.SetClearance(mm(0.2))
default.SetViaDiameter(mm(0.6))
default.SetViaDrill(mm(0.3))
power = pcbnew.NETCLASS("Power")
power.SetTrackWidth(mm(0.5))
power.SetClearance(mm(0.2))
power.SetViaDiameter(mm(0.8))
power.SetViaDrill(mm(0.4))
ns.SetNetclass("Power", power)
for n in ("+5V", "+3V3", "+24V", "VBUS", "5V_IN", "SW"):
    ns.SetNetclassPatternAssignment(n, "Power")

# ---- ground pours -------------------------------------------------------------------
gnd = nets["GND"]
for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(gnd)
    z.SetIsRuleArea(False)
    z.SetMinThickness(mm(0.2))
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    z.SetLocalClearance(mm(0.3))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    z.SetThermalReliefGap(mm(0.3))
    z.SetThermalReliefSpokeWidth(mm(0.4))
    o = 0.4
    for (x, y) in [(o, o), (BOARD_W - o, o), (BOARD_W - o, BOARD_H - o), (o, BOARD_H - o)]:
        z.AppendCorner(V(x, y), -1)
    board.Add(z)

# (the antenna keepout comes with the ESP32-C3-WROOM-02 footprint)

# ---- silkscreen texts ------------------------------------------------------------------
def text(x, y, s, size=1.0, layer=pcbnew.F_SilkS):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(V(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(0.15))
    board.Add(t)


text(24.0, 2.0, "OpenTherm gateway rev A", 0.9)
text(24.0, 4.0, "CERN-OHL-P-2.0 - based on MPro OpenTherm shield", 0.8)

board.Save(OUT)
print("saved", OUT)
print("footprints:", len(PARTS), "nets:", len(NETS))
