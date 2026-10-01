# Saturator — project rules for Claude

Scaffolded from the Ekssperimental Sounds RE template. Same conventions as
FreakShift. Read these before touching motherboard / GUI / DSP.

## texts.lua is mandatory

Any time a property is added or renamed in `motherboard_def.lua`, every
`jbox.ui_text("KEY")` reference must have a matching entry in
`Resources/English/texts.lua`:

```lua
["KEY"] = "Display Text",
```

Missing keys cause a hard build/load error. Verify with:

```
grep -oE 'ui_text\("[^"]+"\)' motherboard_def.lua | sort -u
```

and cross-check each against `Resources/English/texts.lua`.

## Property tags

- Start at 1000+ (0–127 and scattered transport/CV tags are SDK-reserved).
- Never reuse a retired tag.

## Do NOT remove user-facing controls without an explicit instruction

Never delete a knob, switch, jack, label, property, or binding on your own.
If a control seems redundant, ask first. A leftover knob is a far smaller
problem than a missing one (a missing widget can render a DSP feature mute).

## Checklist for adding a user-facing property

1. `motherboard_def.lua` — define property (owner, default, ui_type, property_tag)
2. `Resources/English/texts.lua` — add all ui_text keys
3. Front-panel controls live in the C++ touch screen
   (`src/cpp/ui/SaturatorDisplay.cpp`): add the property to the hdgui
   `values` list AND the matching `kV_*` enum (same order), draw it, give it a
   gesture and a PropertyBox (size the `sBox` array for it). Back-panel
   controls: `GUI2D/device_2D.lua` node + `GUI2D/hdgui_2D.lua` widget, and
   list any new PNG in `GUI2D/gui_2D.cmake`.
4. Add the property to `remote_implementation_chart` (+ its `:short8` /
   `:short4` texts).
5. `src/cpp/Saturator.h` — kTag_* enum + TJBox_PropertyRef + cached value
6. `src/cpp/Saturator.cpp` — JBox_MakePropertyRef in ensureInit(), refresh in
   the diff loop, use in renderBatch()

## SDK 5 touch screen = a port of the Degrader's

`src/cpp/ui/SaturatorDisplay.cpp` is `DegraderDisplay.cpp` (2026-09-22) with the
red palette, CLEAN/WARM/GRITTY/HEAVY, default model ES83 and the Saturator's own
pictograms (one schematic symbol per model). Layout, gestures, PropertyBoxes,
LFO section and knobs are shared logic: when one device's copy changes, port
the change to the other. `SaturatorShared.h` mirrors `DegraderShared.h` the
same way (LFO maths identical; model tables differ).

## Offline checks

- DSP torture + LFO test: `tools/sat_test.cpp` (build line in its header).
- texts.lua: every `ui_text` key resolves (see the reason-re-development skill).

## Knob naming (2026-09-23)

The third knob is labelled **style** on the panel, in tooltips, in automation
lanes and in Remote. The PROPERTY is still `/custom_properties/Character`
(tag unchanged), and the texts.lua KEYS are still "Character" -- only the
values changed. Same for `Character_CV`, `LFO_Depth_Character` and
`LFO_Phase_Character`. Don't rename the ids: patches and Remote maps key off
them. The back-panel art still reads CHARACTER over the CV jack.

## Control overlays (2026-09-27)

The knobs, the IN / OUT bars and the LFO rate each have a transparent C++
overlay display on top of the main display (`src/cpp/ui/CtlOverlay.cpp`,
display_id 2..12, declared LAST in `GUI2D/hdgui_2D.lua`). Each carries ONE
PropertyBox (tooltip, automation rectangle, alt-click lane, Remote) and does
the drag itself; the main display only draws. It is the PCFX AMT-fader
pattern, because alt-click never worked from the main display's boxes.
- Knobs and bars exist twice (LFO panel shown / hidden), switched by the
  visibility functions in `GUI2D/gui_functions.lua`.
- Move a knob or bar in the main display -> move its `Ov_*` node in
  `GUI2D/device_2D.lua` and `OV_SIZE` in `hdgui_2D.lua`, then run
  `python3 tools/check_overlays.py`.
- The bar drag state lives in gui_owner `Lvl_AncY/V/S`, `Lvl_Grab` (no statics).
- The value readout in the label row comes from the DSP: rt_owner
  `UI_EditMask` (`Device::updateEditMask`, 1.5 s hold per control).
