
format_version = "2.0"

re_edit = { version = "1.1.0" }

--------------------------------------------------------------------------
-- front
--------------------------------------------------------------------------
front_widgets = {}

front_widgets[#front_widgets + 1] = jbox.device_name {
  graphics = { node = "TapeFront" },
}

-- Vertical stereo input meter under the bypass switch. 45 x 155 panel px -> 9 x 31 native (panel/5).
front_widgets[#front_widgets + 1] = jbox.custom_display {
  graphics = { node = "Input_Meter" },
  values = {
    "/custom_properties/Input_MeterL",       -- [1] L bar
    "/custom_properties/Input_MeterR",        -- [2] R bar
    "/custom_properties/Input_MeterL_Peak",   -- [3] L peak ghost
    "/custom_properties/Input_MeterR_Peak",   -- [4] R peak ghost
  },
  display_width_pixels  = 45/5,
  display_height_pixels = 155/5,
  show_remote_box       = false,
  show_automation_rect  = false,
  draw_function         = "Input_Meter_Draw",
}
front_widgets[#front_widgets + 1] = jbox.static_decoration {
  graphics = { node = "Input_Meter_Shine" }, blend_mode = "normal",
}

-- Bypass strip AFTER the meter: the switch art's bottom 55 px is shadow and
-- overlaps the top of the meter, so it must draw on top of it.
front_widgets[#front_widgets + 1] = jbox.sequence_fader {
  graphics    = { node = "OffOnBypass", hit_boundaries = { left = 0, top = 0, right = 0, bottom = 55 } },
  value       = "/custom_properties/builtin_onoffbypass",
  orientation = "vertical",
  handle_size = 50,
  inset1      = 0,
  inset2      = 60,
}


-- The touch screen: SDK 5 C++ custom display, src/cpp/ui/SaturatorDisplay.cpp.
-- It draws the CLEAN/WARM/GRITTY/HEAVY model selector, the animated pictogram, the LFO
-- section and the Amount/Tone/Character knobs. Host PropertyBoxes (alt-click
-- automation, right-click menu, Remote) are set up in C++ (DisplaySetup).
-- Coordinate system = the node size, so 1 unit = 1 panel px.
-- values: ORDER IS THE CONTRACT with the kV_* enum in SaturatorDisplay.cpp.
front_widgets[#front_widgets + 1] = jbox.custom_display {
  graphics              = { node = "Main_Display" },
  background            = "transparent",   -- without this it paints black (PCFX)
  display_width_pixels  = 2440,
  display_height_pixels = 250,
  display_id            = 1,
  show_remote_box       = false,
  show_automation_rect  = false,
  values = {
    "/custom_properties/Amount",             -- [0]  kV_Amount
    "/custom_properties/Tone",               -- [1]  kV_Tone
    "/custom_properties/Character",          -- [2]  kV_Character
    "/custom_properties/Model",              -- [3]  kV_Model
    "/custom_properties/LFO_On",             -- [4]  kV_LfoOn
    "/custom_properties/LFO_Rate",           -- [5]  kV_LfoRate
    "/custom_properties/LFO_Sync",           -- [6]  kV_LfoSync
    "/custom_properties/LFO_Shape",          -- [7]  kV_LfoShape
    "/custom_properties/LFO_Depth_Amount",   -- [8]  kV_DepthAmt
    "/custom_properties/LFO_Depth_Tone",     -- [9]  kV_DepthTone
    "/custom_properties/LFO_Depth_Character",-- [10] kV_DepthChar
    "/custom_properties/LFO_Phase_Amount",   -- [11] kV_PhaseAmt
    "/custom_properties/LFO_Phase_Tone",     -- [12] kV_PhaseTone
    "/custom_properties/LFO_Phase_Character",-- [13] kV_PhaseChar
    "/custom_properties/UI_LfoView",         -- [14] kV_LfoView  (gui_owner)
    "/custom_properties/UI_ModeList",        -- [15] kV_ModeList (gui_owner)
    "/custom_properties/LFO_Phase_Out",      -- [16] kV_PhaseOut (rt_owner)
    "/custom_properties/Mod_Amount_Out",     -- [17] kV_ModAmtOut
    "/custom_properties/Mod_Tone_Out",       -- [18] kV_ModToneOut
    "/custom_properties/Mod_Character_Out",  -- [19] kV_ModCharOut
    "/custom_properties/UI_AnimClock",       -- [20] kV_AnimClock (rt_owner)
    "/custom_properties/LFO_Rate_Sync",      -- [21] kV_LfoRateSync
    "/custom_properties/In_Level",           -- [22] kV_InLevel  (the two bars beside the knobs)
    "/custom_properties/Out_Level",          -- [23] kV_OutLevel
    "/custom_properties/UI_EditMask",        -- [24] kV_EditMask (rt_owner: the value readout)
    "/custom_properties/ui_state",           -- [25] kV_Ui (rtc_owner native object: per-instance display state)
    "/custom_properties/UI_CvMask",          -- [26] kV_CvMask (rt_owner: connected CV inputs, mask / 7)
  },
}

front_widgets[#front_widgets + 1] = jbox.patch_name {
  graphics   = { node = "PatchName_Disp" },
  text_style = "Arial medium small font",
  fg_color   = { 234, 8, 8 },                 -- display red #EA0808
  loader_alt_color = {  67,  8,  8  },        -- spin wheel mid way color
  center     = true,
}

-- Shine over the patch name, under the browse buttons.
front_widgets[#front_widgets + 1] = jbox.static_decoration {
  graphics = { node = "Main_Display_Shine" }, blend_mode = "normal",
}

front_widgets[#front_widgets + 1] = jbox.patch_browse_group {
  graphics = { node = "PatchBrowseGroup" },
}

-- CONTROL OVERLAYS (Erik, 2026-09-27; src/cpp/ui/CtlOverlay.cpp). Transparent
-- C++ displays over the knobs, the IN / OUT bars and the LFO rate, declared
-- LAST so they sit above the main display and its glass. Each has ONE
-- PropertyBox over its rectangle -- the PCFX AMT-fader pattern that gives
-- alt-click automation in Recon -- and handles the drag itself; the main
-- display still draws everything. Knobs and bars exist twice (LFO panel shown
-- / hidden), switched by visibility functions in gui_functions.lua. values BY
-- POSITION (CtlOverlay.cpp kV_*).
local OV_SIZE = {
  Ov_Knob_Amount_On = { 154, 230 },
  Ov_Knob_Tone_On = { 154, 230 },
  Ov_Knob_Style_On = { 154, 230 },
  Ov_Bar_In_On = { 92, 230 },
  Ov_Bar_Out_On = { 92, 230 },
  Ov_Knob_Amount_Off = { 155, 230 },
  Ov_Knob_Tone_Off = { 155, 230 },
  Ov_Knob_Style_Off = { 155, 230 },
  Ov_Bar_In_Off = { 91, 230 },
  Ov_Bar_Out_Off = { 91, 230 },
  Ov_Rate = { 156, 55 },
}
local function ovVis(func)
  return { values = { "/custom_properties/LFO_On", "/custom_properties/UI_ModeList" }, func = func }
end
local function ovDisplay(node, id, values, func)
  return jbox.custom_display {
    graphics              = { node = node },
    background            = "transparent",   -- without this it paints black (PCFX)
    display_width_pixels  = OV_SIZE[node][1],
    display_height_pixels = OV_SIZE[node][2],
    display_id            = id,
    values                = values,
    show_remote_box       = false,
    show_automation_rect  = false,
    visibility            = ovVis(func),
  }
end
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Knob_Amount_On", 2,
    { "/custom_properties/Amount", "/custom_properties/UI_ModeList",
      "/custom_properties/UI_Editing" }, "ekss_ov_lfo_on")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Knob_Tone_On", 3,
    { "/custom_properties/Tone", "/custom_properties/UI_ModeList",
      "/custom_properties/UI_Editing" }, "ekss_ov_lfo_on")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Knob_Style_On", 4,
    { "/custom_properties/Character", "/custom_properties/UI_ModeList",
      "/custom_properties/UI_Editing" }, "ekss_ov_lfo_on")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Bar_In_On", 8,
    { "/custom_properties/In_Level", "/custom_properties/UI_ModeList",
      "/custom_properties/Lvl_AncY", "/custom_properties/Lvl_AncV", "/custom_properties/Lvl_AncS",
      "/custom_properties/Lvl_Grab", "/custom_properties/UI_Editing" }, "ekss_ov_lfo_on")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Bar_Out_On", 9,
    { "/custom_properties/Out_Level", "/custom_properties/UI_ModeList",
      "/custom_properties/Lvl_AncY", "/custom_properties/Lvl_AncV", "/custom_properties/Lvl_AncS",
      "/custom_properties/Lvl_Grab", "/custom_properties/UI_Editing" }, "ekss_ov_lfo_on")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Knob_Amount_Off", 5,
    { "/custom_properties/Amount", "/custom_properties/UI_ModeList",
      "/custom_properties/UI_Editing" }, "ekss_ov_lfo_off")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Knob_Tone_Off", 6,
    { "/custom_properties/Tone", "/custom_properties/UI_ModeList",
      "/custom_properties/UI_Editing" }, "ekss_ov_lfo_off")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Knob_Style_Off", 7,
    { "/custom_properties/Character", "/custom_properties/UI_ModeList",
      "/custom_properties/UI_Editing" }, "ekss_ov_lfo_off")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Bar_In_Off", 10,
    { "/custom_properties/In_Level", "/custom_properties/UI_ModeList",
      "/custom_properties/Lvl_AncY", "/custom_properties/Lvl_AncV", "/custom_properties/Lvl_AncS",
      "/custom_properties/Lvl_Grab", "/custom_properties/UI_Editing" }, "ekss_ov_lfo_off")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Bar_Out_Off", 11,
    { "/custom_properties/Out_Level", "/custom_properties/UI_ModeList",
      "/custom_properties/Lvl_AncY", "/custom_properties/Lvl_AncV", "/custom_properties/Lvl_AncS",
      "/custom_properties/Lvl_Grab", "/custom_properties/UI_Editing" }, "ekss_ov_lfo_off")
front_widgets[#front_widgets + 1] = ovDisplay("Ov_Rate", 12,
    { "/custom_properties/LFO_Rate", "/custom_properties/LFO_Rate_Sync", "/custom_properties/LFO_Sync",
      "/custom_properties/UI_ModeList" }, "ekss_ov_rate")

front = jbox.panel{
  graphics = { node = "Panel_front_bg" },
  widgets  = front_widgets,
}

--------------------------------------------------------------------------
-- back
--------------------------------------------------------------------------
back_widgets = {}

back_widgets[#back_widgets + 1] = jbox.device_name {
  graphics = { node = "TapeBack" },
}

back_widgets[#back_widgets + 1] = jbox.audio_input_socket {
  graphics = { node = "Input_Left"  }, socket = "/audio_inputs/MainInL",
}
back_widgets[#back_widgets + 1] = jbox.audio_input_socket {
  graphics = { node = "Input_Right" }, socket = "/audio_inputs/MainInR",
}
back_widgets[#back_widgets + 1] = jbox.audio_output_socket {
  graphics = { node = "Output_Left"  }, socket = "/audio_outputs/MainOutL",
}
back_widgets[#back_widgets + 1] = jbox.audio_output_socket {
  graphics = { node = "Output_Right" }, socket = "/audio_outputs/MainOutR",
}

-- Amount CV (left): host trim knob + jack.
back_widgets[#back_widgets + 1] = jbox.cv_trim_knob {
  graphics = { node = "Amount_CV_Trim" }, socket = "/cv_inputs/Amount_CV",
}
back_widgets[#back_widgets + 1] = jbox.cv_input_socket {
  graphics = { node = "Amount_CV_Jack" }, socket = "/cv_inputs/Amount_CV",
}
-- Tone CV (middle): host trim knob + jack.
back_widgets[#back_widgets + 1] = jbox.cv_trim_knob {
  graphics = { node = "Tone_CV_Trim" }, socket = "/cv_inputs/Tone_CV",
}
back_widgets[#back_widgets + 1] = jbox.cv_input_socket {
  graphics = { node = "Tone_CV_Jack" }, socket = "/cv_inputs/Tone_CV",
}
-- Character CV (right): host trim knob + jack.
back_widgets[#back_widgets + 1] = jbox.cv_trim_knob {
  graphics = { node = "Character_CV_Trim" }, socket = "/cv_inputs/Character_CV",
}
back_widgets[#back_widgets + 1] = jbox.cv_input_socket {
  graphics = { node = "Character_CV_Jack" }, socket = "/cv_inputs/Character_CV",
}

-- LFO CV out: jack only (trim knobs are for CV inputs).
back_widgets[#back_widgets + 1] = jbox.cv_output_socket {
  graphics = { node = "LFO_CV_Out_Jack" }, socket = "/cv_outputs/LFO_CV_Out",
}

-- Routing icons (static signal-flow graphics).
back_widgets[#back_widgets + 1] = jbox.static_decoration {
  graphics = { node = "Routing_Icon_02" },
}
back_widgets[#back_widgets + 1] = jbox.static_decoration {
  graphics = { node = "Routing_Icon_03" },
}

back = jbox.panel{
  graphics = { node = "Panel_back_bg" },
  widgets  = back_widgets,
}

--------------------------------------------------------------------------
-- folded_front
--------------------------------------------------------------------------
folded_front_widgets = {}
folded_front_widgets[#folded_front_widgets + 1] = jbox.sequence_fader {
  graphics    = { node = "OffOnBypass" },
  value       = "/custom_properties/builtin_onoffbypass",
  orientation = "vertical",
  handle_size = 65,
  inset1      = 0,
  inset2      = 10,
}
folded_front_widgets[#folded_front_widgets + 1] = jbox.device_name {
  graphics = { node = "TapeFoldedFront" },
}
folded_front_widgets[#folded_front_widgets + 1] = jbox.patch_name {
  graphics   = { node = "PatchName_Folded" },
  text_style = "Arial medium small font",   -- as the front panel
  fg_color   = { 234, 8, 8 },               -- display red #EA0808
  loader_alt_color = { 67, 8, 8 },        -- spin wheel mid way color
  center     = true,
}
-- Shine over the patch name, under the browse buttons.
folded_front_widgets[#folded_front_widgets + 1] = jbox.static_decoration {
  graphics = { node = "Folded_Shine" }, blend_mode = "normal",
}
folded_front_widgets[#folded_front_widgets + 1] = jbox.patch_browse_group {
  graphics = { node = "PatchBrowseGroup" },
}

folded_front = jbox.panel{
  graphics = { node = "Panel_folded_front_bg" },
  widgets  = folded_front_widgets,
}

--------------------------------------------------------------------------
-- folded_back
--------------------------------------------------------------------------
folded_back_widgets = {}
folded_back_widgets[#folded_back_widgets + 1] = jbox.device_name {
  graphics = { node = "TapeFoldedBack" },
}

folded_back = jbox.panel{
  graphics     = { node = "Panel_folded_back_bg" },
  cable_origin = { node = "CableOrigin" },
  widgets      = folded_back_widgets,
}
