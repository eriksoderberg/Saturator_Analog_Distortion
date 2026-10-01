-- gui_functions.lua -- pure functions referenced from hdgui_2D.lua (2026-09-27).
-- Run inside the draw VM: no host access, no side effects, no globals.
-- params follow each widget's visibility `values`:
--   [1] /custom_properties/LFO_On (0 / 1)   [2] /custom_properties/UI_ModeList (0..5)

-- Knob / bar overlays for the LFO panel SHOWN: LFO on, and the LFO shape popup
-- (UI_ModeList 5), which covers the knob row, closed.
function ekss_ov_lfo_on(params)
  return (params[1] or 0) > 0.5 and math.floor((params[2] or 0) + 0.5) ~= 5
end
-- ... for the LFO panel HIDDEN.
function ekss_ov_lfo_off(params)
  return (params[1] or 0) <= 0.5
end
-- LFO rate overlay: shown with the LFO panel.
function ekss_ov_rate(params)
  return (params[1] or 0) > 0.5
end
