format_version = "2.0"

re_edit = { version = "1.1.0" }

--------------------------------------------------------------------------
-- Saturator panel graphics — Ekss "Basics" series (sibling to Degrader).
--   Panel 3770 x 345 (1U). SDK 5 touch-screen layout, same geometry as the
--   Degrader: one C++ display (Main_Display, 2440 x 250) holds the model
--   selector, the LFO and the Amount / Tone / Character knobs.
--   Input meter 45 x 155 (9 x 31 native). CV trim TrimKnob_64frames 100 x 120.
--   Shine / display / patch-name PNGs are the Degrader's, byte for byte.
--------------------------------------------------------------------------

front = {}
front["Panel_front_bg"]    = { { path = "Panel_Front" } }
front["TapeFront"]         = { offset = { 3135,  25 }, { path = "Tape_Horizontal_1frames" } }
-- Bypass strip: SAME position and art in every Ekss effect device.
front["OffOnBypass"]       = { offset = {  160,  45 }, { path = "Switch_02vert_3frames", frames = 3 } }
-- Vertical stereo input meter under the switch (Lua custom_display, 45 x 155).
-- It starts under the bypass switch's 55 px drop shadow, which draws on top.
front["Input_Meter"]       = { offset = {  165, 170 }, { path = "Input_Meter" } }
front["Input_Meter_Shine"] = { offset = {  165, 170 }, { path = "Input_Meter" } }
-- SDK 5 C++ touch screen (display_id 1). Fills the black window in the panel
-- art; the shine sits 5 px outside it, drawn on top.
front["Main_Display"]      = { offset = {  390,  50 }, { path = "Main_Display" } }
front["Main_Display_Shine"]= { offset = {  385,  45 }, { path = "disp_area_shine" } }
-- Stock patch widgets, drawn OVER the display (SDK 5 allows widget overlap).
-- The name sits on the red well in Panel_Front.png.
front["PatchName_Disp"]    = { offset = { 2309, 110 }, { path = "PatchName_Disp" } }
front["PatchBrowseGroup"]  = { offset = { 2500, 185 }, { path = "PatchBrowseGroup" } }

-- CONTROL OVERLAYS (2026-09-27, src/cpp/ui/CtlOverlay.cpp): transparent C++
-- displays over the knobs, the IN / OUT bars and the LFO rate, one PropertyBox
-- each, so alt-click opens the automation lane (the PCFX AMT fader pattern).
-- Centred on the main display's knob / bar centres (knobCX / lvlBarCX), for the
-- LFO panel shown (_On) and hidden (_Off). tools/check_overlays.py recomputes these.
front["Ov_Knob_Amount_On"] = { offset = { 1711, 70 }, { size = { 154, 230 } } }
front["Ov_Knob_Tone_On"] = { offset = { 1884, 70 }, { size = { 154, 230 } } }
front["Ov_Knob_Style_On"] = { offset = { 2057, 70 }, { size = { 154, 230 } } }
front["Ov_Bar_In_On"] = { offset = { 1617, 70 }, { size = { 92, 230 } } }
front["Ov_Bar_Out_On"] = { offset = { 2213, 70 }, { size = { 92, 230 } } }
front["Ov_Knob_Amount_Off"] = { offset = { 1431, 70 }, { size = { 155, 230 } } }
front["Ov_Knob_Tone_Off"] = { offset = { 1691, 70 }, { size = { 155, 230 } } }
front["Ov_Knob_Style_Off"] = { offset = { 1951, 70 }, { size = { 155, 230 } } }
front["Ov_Bar_In_Off"] = { offset = { 1283, 70 }, { size = { 91, 230 } } }
front["Ov_Bar_Out_Off"] = { offset = { 2163, 70 }, { size = { 91, 230 } } }
front["Ov_Rate"] = { offset = { 1400, 50 }, { size = { 156, 55 } } }

back = {}
back["Panel_back_bg"]  = { { path = "Panel_Back" } }
-- Device-name tape over the Saturator logo (Figma: 290, 25). The Degrader's
-- tape spot (2730, 50) is the CV OUT box on this panel.
back["TapeBack"]       = { offset = {  290,  25 }, { path = "Tape_Horizontal_1frames" } }
-- CV inputs: trim (100 px) + jack (75 px), each pair centred on its label in
-- Panel_Back.png (same label positions as the Degrader: AMOUNT, TONE, CHARACTER).
back["Amount_CV_Trim"]    = { offset = { 1045, 125 }, { path = "TrimKnob_64frames", frames = 64 } }
back["Amount_CV_Jack"]    = { offset = { 1155, 135 }, { path = "SharedCVJack_3frames", frames = 3 } }
back["Tone_CV_Trim"]      = { offset = { 1283, 125 }, { path = "TrimKnob_64frames", frames = 64 } }
back["Tone_CV_Jack"]      = { offset = { 1393, 135 }, { path = "SharedCVJack_3frames", frames = 3 } }
back["Character_CV_Trim"] = { offset = { 1550, 125 }, { path = "TrimKnob_64frames", frames = 64 } }
back["Character_CV_Jack"] = { offset = { 1660, 135 }, { path = "SharedCVJack_3frames", frames = 3 } }
-- Main audio I/O.
back["Input_Left"]     = { offset = { 1901, 130 }, { path = "SharedAudioJack_3frames", frames = 3 } }
back["Input_Right"]    = { offset = { 2031, 130 }, { path = "SharedAudioJack_3frames", frames = 3 } }
back["Output_Left"]    = { offset = { 2305, 130 }, { path = "SharedAudioJack_3frames", frames = 3 } }
back["Output_Right"]   = { offset = { 2435, 130 }, { path = "SharedAudioJack_3frames", frames = 3 } }
-- CV OUT / LFO: level trim (100 px) + jack (75 px), under the "LFO" label.
-- Same coordinates on the Degrader.
back["LFO_CV_Out_Jack"] = { offset = { 2780, 135 }, { path = "SharedCVJack_3frames", frames = 3 } }

-- Routing icons (signal-flow graphics, static): a 145 x 85 group at (3080, 200),
-- two 65 x 85 icons. Same coordinates on both devices (Erik, 2026-09-22).
back["Routing_Icon_02"] = { offset = { 3080, 200 }, { path = "Routing_Icon_02" } }
back["Routing_Icon_03"] = { offset = { 3160, 200 }, { path = "Routing_Icon_03" } }

folded_front = {}
folded_front["Panel_folded_front_bg"] = { { path = "Panel_Folded_Front" } }
folded_front["TapeFoldedFront"]       = { offset = { 1070, 25 }, { path = "Tape_Horizontal_1frames" } }
folded_front["OffOnBypass"]           = { offset = {  160, 21 }, { path = "Switch_02vertFolded_3frames", frames = 3 } }
-- Patch name on the red well in Panel_Folded_Front.png, same 475 x 65 frame
-- as the front panel's.
folded_front["PatchName_Folded"]      = { offset = { 2005, 40 }, { path = "PatchName_Disp" } }
folded_front["Folded_Shine"]          = { offset = { 1585, 25 }, { path = "Folded_Shine" } }
folded_front["PatchBrowseGroup"]      = { offset = { 2510, 20 }, { path = "PatchBrowseGroup" } }

folded_back = {}
folded_back["Panel_folded_back_bg"] = { { path = "Panel_Folded_Back" } }
folded_back["TapeFoldedBack"]       = { offset = { 1070, 40 }, { path = "Tape_Horizontal_1frames" } }
folded_back["CableOrigin"]          = { offset = { 1880, 75 } }
