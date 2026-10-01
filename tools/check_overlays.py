#!/usr/bin/env python3
"""Pin the control-overlay nodes (GUI2D/device_2D.lua Ov_*) to the main display's
layout (ui/*Display.cpp: kPanR, kLfoR, kPatchL, kKnobSpacing, kLvlBarDX, kKnobCY,
kLvlHitT/B) and to hdgui_2D.lua's OV_SIZE. Every overlay must be centred on its
control, because ui/CtlOverlay.cpp takes the control's centre as half its size.
Run from the project root:  python3 tools/check_overlays.py"""
import re, sys, glob
disp = open(glob.glob("src/cpp/ui/*Display.cpp")[0]).read()
def const(name):
    m = re.search(r"\b" + name + r"\s*=\s*([0-9.]+)", disp)
    if not m: sys.exit("constant not found: " + name)
    return float(m.group(1))
kPanR, kLfoR, SP, DX = const("kPanR"), const("kLfoR"), const("kKnobSpacing"), const("kLvlBarDX")
kPatchL = float(re.search(r"kPatchL\s*=\s*([0-9]+)\s*-\s*390", disp).group(1)) - 390
kKnobCY, kHitT, kHitB = const("kKnobCY"), const("kLvlHitT"), const("kLvlHitB")
MX, MY = 390, 50
SPW, DXW = const("kKnobSpacingWide"), const("kLvlBarDXWide")      # LFO panel hidden
def knobCX(k, on): return 0.5 * ((kPanR if on else kLfoR) + kPatchL) + (k - 1) * (SP if on else SPW)
def barCX(b, on): return knobCX(0, on) - (DX if on else DXW) if b == 0 else knobCX(2, on) + (DX if on else DXW)
dev = open("GUI2D/device_2D.lua").read()
hd = open("GUI2D/hdgui_2D.lua").read()
nodes = {m.group(1): tuple(map(int, m.groups()[1:])) for m in re.finditer(
    r'front\["(Ov_\w+)"\]\s*=\s*\{\s*offset\s*=\s*\{\s*(\d+),\s*(\d+)\s*\},\s*\{\s*size\s*=\s*\{\s*(\d+),\s*(\d+)', dev)}
bad = 0
def expect(name, cx, cy):
    global bad
    x, y, w, h = nodes[name]
    if abs(x + w / 2 - (cx + MX)) > 1e-6 or abs(y + h / 2 - (cy + MY)) > 1e-6:
        print("OFF CENTRE", name, (x + w / 2, y + h / 2), "want", (cx + MX, cy + MY)); bad += 1
    m = re.search(name + r"\s*=\s*\{\s*(\d+),\s*(\d+)\s*\}", hd)
    if not m or (int(m.group(1)), int(m.group(2))) != (w, h):
        print("OV_SIZE mismatch", name); bad += 1
for on, tag in ((True, "On"), (False, "Off")):
    for k, n in enumerate(("Amount", "Tone", "Style")):
        expect(f"Ov_Knob_{n}_{tag}", knobCX(k, on), 0.5 * (kHitT + kHitB))   # label + knob, like the bars
    for b, n in enumerate(("In", "Out")):
        expect(f"Ov_Bar_{n}_{tag}", barCX(b, on), 0.5 * (kHitT + kHitB))
print("overlays OK" if bad == 0 else f"{bad} problem(s)")
sys.exit(1 if bad else 0)
