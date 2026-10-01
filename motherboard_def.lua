format_version = "3.0"

-- =============================================================================
-- Saturator - drive / fold character box (Ekss "Basics" series, sibling to
-- Degrader). Touch-screen device (SDK 5): Amount / Tone / Character, the
-- 16-model selector (CLEAN / WARM / GRITTY / HEAVY) and one LFO are drawn in
-- the C++ display (src/cpp/ui/SaturatorDisplay.cpp). Property tags start at 1000.
-- =============================================================================

custom_properties = jbox.property_set {
  gui_owner = {
    properties = {
      -- Display UI state. gui_owner: the C++ display writes these WITHOUT an
      -- undo step (JukeboxTypes.h, TJBox_GestureArgs::fParams), and they are
      -- not patch data.
      -- Which page the LFO wave area shows: 0 = waveform, 1..3 = the options
      -- of Amount / Tone / Character.
      UI_LfoView = jbox.number {
        default = 0, steps = 4,
        ui_name = jbox.ui_text("UI_LfoView LFO View"),
        ui_type = jbox.ui_selector { jbox.ui_text("LFO"), jbox.ui_text("Amount"), jbox.ui_text("Tone"), jbox.ui_text("Character") },
      },
      -- Which popup the touch screen shows: 0 none, 1..4 the model list of
      -- CLEAN / WARM / GRITTY / HEAVY, 5 the LFO shape list (drawn over the
      -- knobs). One property = only one popup at a time.
      UI_ModeList = jbox.number {
        default = 0, steps = 6,
        ui_name = jbox.ui_text("UI_ModeList Mode List"),
        ui_type = jbox.ui_selector { jbox.ui_text("Off"), jbox.ui_text("UI popup clean"), jbox.ui_text("UI popup warm"),
                                     jbox.ui_text("UI popup gritty"), jbox.ui_text("UI popup heavy"), jbox.ui_text("UI popup shapes") },
      },
      -- IN / OUT bar drag state (the PCFX AMT fader law, ui/CtlOverlay.cpp;
      -- Erik, 2026-09-27). GUI scratch written by the bar overlays' gestures:
      -- the shift anchor (pointer y, value, shift state) and the side a drag
      -- from 0 dB left toward. Per instance -- the 45 build allows no statics.
      Lvl_AncY = jbox.number {
        default = 0.5,
        ui_name = jbox.ui_text("Lvl_AncY Level Anchor Y"),
        ui_type = jbox.ui_linear { min = 0, max = 1 },
      },
      Lvl_AncV = jbox.number {
        default = 0.5,
        ui_name = jbox.ui_text("Lvl_AncV Level Anchor Value"),
        ui_type = jbox.ui_linear { min = 0, max = 1 },
      },
      Lvl_AncS = jbox.number {
        default = 0, steps = 2,
        ui_name = jbox.ui_text("Lvl_AncS Level Anchor Shift"),
        ui_type = jbox.ui_linear { min = 0, max = 1 },
      },
      -- Which control an overlay is dragging RIGHT NOW (0 none, 1 in, 2 amount,
      -- 3 tone, 4 style, 5 out), set on the tap, cleared on the release.
      -- Diff-driven in the DSP (property_tag, as BitSynth's Seq_RunBtn): the
      -- value readout ends the moment the mouse lets go (Erik, 2026-09-27).
      UI_Editing = jbox.number {
        default = 0, steps = 6, persistence = "none",
        property_tag = 1018,
        ui_name = jbox.ui_text("UI_Editing Editing"),
        ui_type = jbox.ui_linear { min = 0, max = 5 },
      },
      Lvl_Grab = jbox.number {
        default = 0, steps = 3,
        ui_name = jbox.ui_text("Lvl_Grab Level Grab"),
        ui_type = jbox.ui_linear { min = 0, max = 2 },
      },
    },
  },

  document_owner = {
    properties = {
      -- Amount: drive into the shaper AND the dry/wet (0 = clean, full wet
      -- from ~35 %, the rest of the travel adds drive). Auto makeup in DSP.
      Amount = jbox.number {
        property_tag = 1000,
        default      = 0.35,
        ui_name      = jbox.ui_text("Amount"),
        ui_type      = jbox.ui_percent({ min = 0, max = 100, decimals = 1 }),
      },

      -- Tone: bipolar tilt around the model's pivot. 0.5 = flat.
      Tone = jbox.number {
        property_tag = 1001,
        default      = 0.5,
        ui_name      = jbox.ui_text("Tone"),
        ui_type      = jbox.ui_percent({ min = -100, max = 100, decimals = 0 }),
      },

      -- In / Out level: the two bars beside the knobs on the display. Plain gain
      -- stages, -18 .. +18 dB, stored 0..1 with 0 dB at the centre (0.5). IN sits
      -- before the saturator (so it also sets how hard it is driven); OUT is the
      -- last thing in the chain. Neither applies in Bypass.
      In_Level = jbox.number {
        property_tag = 1016, default = 0.5,
        ui_name = jbox.ui_text("In_Level In Level"),
        ui_type = jbox.ui_linear({ min = -18, max = 18, units = {{ decimals = 1, unit = { template = jbox.ui_text("Level:format dB") } }} }),
      },
      Out_Level = jbox.number {
        property_tag = 1017, default = 0.5,
        ui_name = jbox.ui_text("Out_Level Out Level"),
        ui_type = jbox.ui_linear({ min = -18, max = 18, units = {{ decimals = 1, unit = { template = jbox.ui_text("Level:format dB") } }} }),
      },

      -- Character: bipolar recipe macro. CCW = fold, CW = fuzz, centre = the
      -- model's own balance.
      Character = jbox.number {
        property_tag = 1002,
        default      = 0.5,
        ui_name      = jbox.ui_text("Character"),
        ui_type      = jbox.ui_percent({ min = -100, max = 100, decimals = 0 }),
      },

      -- Model: the circuit voicing. Order MUST match kModels[] / kCharEq[] in
      -- Saturator.h and the tables in SaturatorShared.h. 4 per category:
      --   0-3   CLEAN  (BC12 PV40 TG23 BV98)
      --   4-7   WARM   (WP76 ZX67 A1R  ES83)
      --   8-11  GRITTY (FD88 LF09 NN18 SB05)
      --   12-15 HEAVY  (FF37 GR1T F34K X7R3)
      -- Default 7 = ES83, the house sound.
      Model = jbox.number {
        property_tag = 1003,
        default      = 7,
        steps        = 16,
        ui_name      = jbox.ui_text("Model"),
        ui_type      = jbox.ui_selector {
          jbox.ui_text("BC12"), jbox.ui_text("PV40"), jbox.ui_text("TG23"), jbox.ui_text("BV98"),
          jbox.ui_text("WP76"), jbox.ui_text("ZX67"), jbox.ui_text("A1R"),  jbox.ui_text("ES83"),
          jbox.ui_text("FD88"), jbox.ui_text("LF09"), jbox.ui_text("NN18"), jbox.ui_text("SB05"),
          jbox.ui_text("FF37"), jbox.ui_text("GR1T"), jbox.ui_text("F34K"), jbox.ui_text("X7R3"),
        },
      },

      -- ---- LFO (touch-screen display, SDK 5) --------------------------------
      -- One LFO, three destinations (Amount / Tone / Character), each with its
      -- own bipolar depth and phase offset. Tables + maths: src/cpp/SaturatorShared.h
      -- (shared by the DSP and the display, so they cannot drift). Same LFO as
      -- the Degrader.
      LFO_On = jbox.number {
        property_tag = 1004,
        default      = 0,
        steps        = 2,
        ui_name      = jbox.ui_text("LFO_On LFO"),
        ui_type      = jbox.ui_selector { jbox.ui_text("Off"), jbox.ui_text("On") },
      },
      -- Two rates, as a value_switch would pair them: LFO_Sync picks which one
      -- the display edits and the DSP uses.
      -- FREE rate (sync off): 0..1 -> 0.05 .. 60 Hz, exponential (1200:1)
      -- (SaturatorShared.h freeHz), shown in Hz. Default 0.4225 = 1 Hz
      -- (= kLfoRateDefault; top raised from 20 Hz on 2026-09-22).
      LFO_Rate = jbox.number {
        property_tag = 1005,
        default      = 0.42252466,
        ui_name      = jbox.ui_text("LFO_Rate LFO Rate"),
        ui_type      = jbox.ui_nonlinear({
          data_to_gui = function(data_value)
            return 0.05 * 1200 ^ data_value
          end,
          gui_to_data = function(gui_value)
            return math.log(gui_value / 0.05) / math.log(1200)
          end,
          units = {{ min_value = 0, decimals = 2, unit = { template = jbox.ui_text("LFO_Rate:format Hz"), base = 1 } }},
        }),
      },
      -- SYNCED rate (sync on): a note division, order = SaturatorShared.h
      -- kDivName / kDivBeats. Default 1/1.
      LFO_Rate_Sync = jbox.number {
        property_tag = 1006,
        default      = 7,
        steps        = 25,
        ui_name      = jbox.ui_text("LFO_Rate_Sync LFO Sync Rate"),
        ui_type      = jbox.ui_selector { jbox.ui_text("LFO div 16/1"), jbox.ui_text("LFO div 8/1"), jbox.ui_text("LFO div 4/1"), jbox.ui_text("LFO div 2/1"), jbox.ui_text("LFO div 7/4"), jbox.ui_text("LFO div 3/2"), jbox.ui_text("LFO div 5/4"), jbox.ui_text("LFO div 1/1"), jbox.ui_text("LFO div 7/8"), jbox.ui_text("LFO div 3/4"), jbox.ui_text("LFO div 5/8"), jbox.ui_text("LFO div 1/2"), jbox.ui_text("LFO div 7/16"), jbox.ui_text("LFO div 3/8"), jbox.ui_text("LFO div 5/16"), jbox.ui_text("LFO div 1/4"), jbox.ui_text("LFO div 3/16"), jbox.ui_text("LFO div 1/4T"), jbox.ui_text("LFO div 1/8"), jbox.ui_text("LFO div 1/8T"), jbox.ui_text("LFO div 1/16"), jbox.ui_text("LFO div 1/16T"), jbox.ui_text("LFO div 1/32"), jbox.ui_text("LFO div 1/32T"), jbox.ui_text("LFO div 1/64") },
      },
      LFO_Sync = jbox.number {
        property_tag = 1007,
        default      = 1,
        steps        = 2,
        ui_name      = jbox.ui_text("LFO_Sync LFO Sync"),
        ui_type      = jbox.ui_selector { jbox.ui_text("Off"), jbox.ui_text("On") },
      },
      -- Shape order MUST match SaturatorShared.h kLfoShapeName / lfoValue().
      LFO_Shape = jbox.number {
        property_tag = 1008,
        default      = 1,
        steps        = 7,
        ui_name      = jbox.ui_text("LFO_Shape LFO Shape"),
        ui_type      = jbox.ui_selector {
          jbox.ui_text("LFO sine"), jbox.ui_text("LFO triangle"), jbox.ui_text("LFO saw up"),
          jbox.ui_text("LFO saw down"), jbox.ui_text("LFO square"), jbox.ui_text("LFO s&h"),
          jbox.ui_text("LFO drift"),
        },
      },
      -- Depths: 0..1 stored, 0.5 = none, shown -100..+100 %. 100 % = the full
      -- knob range peak to peak around the knob value (clamped).
      LFO_Depth_Amount = jbox.number {
        property_tag = 1009, default = 0.5,
        ui_name = jbox.ui_text("LFO_Depth_Amount LFO > Amount"),
        ui_type = jbox.ui_percent({ min = -100, max = 100, decimals = 0 }),
      },
      LFO_Depth_Tone = jbox.number {
        property_tag = 1010, default = 0.5,
        ui_name = jbox.ui_text("LFO_Depth_Tone LFO > Tone"),
        ui_type = jbox.ui_percent({ min = -100, max = 100, decimals = 0 }),
      },
      LFO_Depth_Character = jbox.number {
        property_tag = 1011, default = 0.5,
        ui_name = jbox.ui_text("LFO_Depth_Character LFO > Character"),
        ui_type = jbox.ui_percent({ min = -100, max = 100, decimals = 0 }),
      },
      -- Phase offsets per destination, 0..1 = 0..360 degrees.
      LFO_Phase_Amount = jbox.number {
        property_tag = 1012, default = 0,
        ui_name = jbox.ui_text("LFO_Phase_Amount LFO Phase Amount"),
        ui_type = jbox.ui_linear({ min = 0, max = 360, units = {{ decimals = 0, unit = { template = jbox.ui_text("LFO_Phase:format deg") } }} }),
      },
      LFO_Phase_Tone = jbox.number {
        property_tag = 1013, default = 0,
        ui_name = jbox.ui_text("LFO_Phase_Tone LFO Phase Tone"),
        ui_type = jbox.ui_linear({ min = 0, max = 360, units = {{ decimals = 0, unit = { template = jbox.ui_text("LFO_Phase:format deg") } }} }),
      },
      LFO_Phase_Character = jbox.number {
        property_tag = 1014, default = 0,
        ui_name = jbox.ui_text("LFO_Phase_Character LFO Phase Character"),
        ui_type = jbox.ui_linear({ min = 0, max = 360, units = {{ decimals = 0, unit = { template = jbox.ui_text("LFO_Phase:format deg") } }} }),
      },
      -- (tag 1015 was LFO_CV_Out_Level, a back-panel trim on the LFO CV out;
      --  removed: the CV out is a plain jack. Never reuse 1015.)
    },
  },

  rtc_owner = {
    properties = {
      instance = jbox.native_object {},
      -- The main display's per-instance state (hover, drawn-tooltip session,
      -- drag-select, PropertyBoxes): ui/<Dev>Display.h UiState. panel-only, as
      -- PCFX overlay_ui: the 45 build forbids statics, and statics were shared
      -- by every instance (a tooltip showed in all of them). 2026-09-27.
      ui_state = jbox.native_object { access = "panel-only" },
    },
  },

  rt_owner = {
    properties = {
      -- Input level meters (raw incoming audio), 0..1 = -60 .. +12 dBFS
      -- (SpiritLevel PPM ballistics). Read by the Input_Meter custom_display.
      Input_MeterL      = jbox.number { default = 0, ui_name = jbox.ui_text("Input Meter"),      ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      Input_MeterR      = jbox.number { default = 0, ui_name = jbox.ui_text("Input Meter"),      ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      Input_MeterL_Peak = jbox.number { default = 0, ui_name = jbox.ui_text("Input Meter Peak"), ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      Input_MeterR_Peak = jbox.number { default = 0, ui_name = jbox.ui_text("Input Meter Peak"), ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      -- LFO state for the display (written by the DSP, decimated): the phase
      -- (0..1, waveform cursor) and each destination's live modulated value.
      LFO_Phase_Out     = jbox.number { default = 0,    ui_name = jbox.ui_text("LFO Phase Out"),     ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      Mod_Amount_Out    = jbox.number { default = 0.35, ui_name = jbox.ui_text("Mod Amount Out"),    ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      Mod_Tone_Out      = jbox.number { default = 0.5,  ui_name = jbox.ui_text("Mod Tone Out"),      ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      Mod_Character_Out = jbox.number { default = 0.5,  ui_name = jbox.ui_text("Mod Character Out"), ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      -- Icon animation clock (seconds x an Amount-dependent speed, slewed),
      -- integrated by the DSP so a moving Amount changes the icons' speed
      -- smoothly. Stored as clock / 3600 (numbers must stay in 0..1); wraps.
      UI_AnimClock      = jbox.number { default = 0,    ui_name = jbox.ui_text("UI Anim Clock"),     ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      -- Which of in / amount / tone / style / out changed in the last ~1.5 s
      -- (any source: the panel, automation, Remote, a patch), bits 0..4,
      -- stored as mask / 31. The display shows their values in the label row.
      UI_EditMask       = jbox.number { default = 0,    ui_name = jbox.ui_text("UI Edit Mask"),      ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
      UI_CvMask         = jbox.number { default = 0,    ui_name = jbox.ui_text("UI CV Mask"),        ui_type = jbox.ui_linear { min = 0, max = 1.0 } },
    },
  },
}

-- =============================================================================
-- CV sockets: Amount, Tone, Character (each with a host trim knob).
-- =============================================================================
cv_inputs = {
  Amount_CV    = jbox.cv_input{ ui_name = jbox.ui_text("Amount CV") },
  Tone_CV      = jbox.cv_input{ ui_name = jbox.ui_text("Tone CV") },
  Character_CV = jbox.cv_input{ ui_name = jbox.ui_text("Character CV") },
}

-- LFO CV out: the LFO itself (before the per-destination depth/phase),
-- -1..+1, full scale. 0 while the LFO is off.
cv_outputs = {
  LFO_CV_Out = jbox.cv_output{ ui_name = jbox.ui_text("LFO CV Out") },
}

-- =============================================================================
-- Audio I/O (main stereo pair, auto-routed)
-- =============================================================================
audio_inputs = {
  MainInL = jbox.audio_input{ ui_name = jbox.ui_text("audio input L") },
  MainInR = jbox.audio_input{ ui_name = jbox.ui_text("audio input R") },
}

audio_outputs = {
  MainOutL = jbox.audio_output{ ui_name = jbox.ui_text("audio output L") },
  MainOutR = jbox.audio_output{ ui_name = jbox.ui_text("audio output R") },
}

jbox.add_stereo_audio_routing_pair{
  left  = "/audio_inputs/MainInL",
  right = "/audio_inputs/MainInR",
}
jbox.add_stereo_audio_routing_pair{
  left  = "/audio_outputs/MainOutL",
  right = "/audio_outputs/MainOutR",
}

jbox.add_stereo_effect_routing_hint{
  type         = "spreading",
  left_input   = "/audio_inputs/MainInL",
  right_input  = "/audio_inputs/MainInR",
  left_output  = "/audio_outputs/MainOutL",
  right_output = "/audio_outputs/MainOutR",
}

jbox.add_stereo_audio_routing_target{
  signal_type       = "normal",
  left              = "/audio_inputs/MainInL",
  right             = "/audio_inputs/MainInR",
  auto_route_enable = true,
}
jbox.add_stereo_audio_routing_target{
  signal_type       = "normal",
  left              = "/audio_outputs/MainOutL",
  right             = "/audio_outputs/MainOutR",
  auto_route_enable = true,
}

jbox.set_effect_auto_bypass_routing {
  { "/audio_inputs/MainInL", "/audio_outputs/MainOutL" },
  { "/audio_inputs/MainInR", "/audio_outputs/MainOutR" },
}

-- =============================================================================
-- Remote / MIDI
-- =============================================================================
-- Groups for Reason's parameter menus (Combinator programmer, track "Parameter
-- automation" list). Shown once there are >= 10 automatable properties.
ui_groups = {
  {
    ui_name = jbox.ui_text("group name Main"),
    properties = {
      "/custom_properties/Model",
      "/custom_properties/Amount",
      "/custom_properties/Tone",
      "/custom_properties/Character",
      "/custom_properties/In_Level",
      "/custom_properties/Out_Level",
    },
  },
  {
    ui_name = jbox.ui_text("group name LFO"),
    properties = {
      "/custom_properties/LFO_On",
      "/custom_properties/LFO_Sync",
      "/custom_properties/LFO_Rate",
      "/custom_properties/LFO_Rate_Sync",
      "/custom_properties/LFO_Shape",
      "/custom_properties/LFO_Depth_Amount",
      "/custom_properties/LFO_Depth_Tone",
      "/custom_properties/LFO_Depth_Character",
      "/custom_properties/LFO_Phase_Amount",
      "/custom_properties/LFO_Phase_Tone",
      "/custom_properties/LFO_Phase_Character",
    },
  },
}
-- Remote: every user-facing property (short names <= 8 / <= 4 chars).
remote_implementation_chart = {
  ["/custom_properties/Amount"] = {
    internal_name    = "Amount",
    short_ui_name    = jbox.ui_text("Remote Amount:short8"),
    shortest_ui_name = jbox.ui_text("Remote Amount:short4"),
  },
  ["/custom_properties/In_Level"] = {
    internal_name    = "In Level",
    short_ui_name    = jbox.ui_text("Remote In_Level:short8"),
    shortest_ui_name = jbox.ui_text("Remote In_Level:short4"),
  },
  ["/custom_properties/Out_Level"] = {
    internal_name    = "Out Level",
    short_ui_name    = jbox.ui_text("Remote Out_Level:short8"),
    shortest_ui_name = jbox.ui_text("Remote Out_Level:short4"),
  },
  ["/custom_properties/Tone"] = {
    internal_name    = "Tone",
    short_ui_name    = jbox.ui_text("Remote Tone:short8"),
    shortest_ui_name = jbox.ui_text("Remote Tone:short4"),
  },
  ["/custom_properties/Character"] = {
    internal_name    = "Style",
    short_ui_name    = jbox.ui_text("Remote Character:short8"),
    shortest_ui_name = jbox.ui_text("Remote Character:short4"),
  },
  ["/custom_properties/Model"] = {
    internal_name    = "Model",
    short_ui_name    = jbox.ui_text("Remote Model:short8"),
    shortest_ui_name = jbox.ui_text("Remote Model:short4"),
  },
  ["/custom_properties/LFO_On"] = {
    internal_name    = "LFO On",
    short_ui_name    = jbox.ui_text("Remote LFO_On:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_On:short4"),
  },
  ["/custom_properties/LFO_Rate"] = {
    internal_name    = "LFO Rate",
    short_ui_name    = jbox.ui_text("Remote LFO_Rate:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Rate:short4"),
  },
  ["/custom_properties/LFO_Rate_Sync"] = {
    internal_name    = "LFO Sync Rate",
    short_ui_name    = jbox.ui_text("Remote LFO_Rate_Sync:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Rate_Sync:short4"),
  },
  ["/custom_properties/LFO_Sync"] = {
    internal_name    = "LFO Sync",
    short_ui_name    = jbox.ui_text("Remote LFO_Sync:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Sync:short4"),
  },
  ["/custom_properties/LFO_Shape"] = {
    internal_name    = "LFO Shape",
    short_ui_name    = jbox.ui_text("Remote LFO_Shape:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Shape:short4"),
  },
  ["/custom_properties/LFO_Depth_Amount"] = {
    internal_name    = "LFO Depth Amount",
    short_ui_name    = jbox.ui_text("Remote LFO_Depth_Amount:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Depth_Amount:short4"),
  },
  ["/custom_properties/LFO_Depth_Tone"] = {
    internal_name    = "LFO Depth Tone",
    short_ui_name    = jbox.ui_text("Remote LFO_Depth_Tone:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Depth_Tone:short4"),
  },
  ["/custom_properties/LFO_Depth_Character"] = {
    internal_name    = "LFO Depth Style",
    short_ui_name    = jbox.ui_text("Remote LFO_Depth_Character:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Depth_Character:short4"),
  },
  ["/custom_properties/LFO_Phase_Amount"] = {
    internal_name    = "LFO Phase Amount",
    short_ui_name    = jbox.ui_text("Remote LFO_Phase_Amount:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Phase_Amount:short4"),
  },
  ["/custom_properties/LFO_Phase_Tone"] = {
    internal_name    = "LFO Phase Tone",
    short_ui_name    = jbox.ui_text("Remote LFO_Phase_Tone:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Phase_Tone:short4"),
  },
  ["/custom_properties/LFO_Phase_Character"] = {
    internal_name    = "LFO Phase Style",
    short_ui_name    = jbox.ui_text("Remote LFO_Phase_Character:short8"),
    shortest_ui_name = jbox.ui_text("Remote LFO_Phase_Character:short4"),
  },
}
-- THE MIDI CC CHART IS WHAT MAKES A PROPERTY AUTOMATABLE (Scripting Spec 4.3,
-- "Reason-specific: If you map a MIDI CC number to a property, it will be
-- automatable in Reason"). It was EMPTY until 2026-09-27, so nothing on this
-- device could be automated: alt-click opened no lane anywhere, PropertyBoxes
-- and overlays notwithstanding. CCs from 12 up; none of 12..28 is reserved.
midi_implementation_chart = {
  midi_cc_chart = {
    [12] = "/custom_properties/Amount",
    [13] = "/custom_properties/Tone",
    [14] = "/custom_properties/Character",
    [15] = "/custom_properties/Model",
    [16] = "/custom_properties/In_Level",
    [17] = "/custom_properties/Out_Level",
    [18] = "/custom_properties/LFO_On",
    [19] = "/custom_properties/LFO_Rate",
    [20] = "/custom_properties/LFO_Rate_Sync",
    [21] = "/custom_properties/LFO_Sync",
    [22] = "/custom_properties/LFO_Shape",
    [23] = "/custom_properties/LFO_Depth_Amount",
    [24] = "/custom_properties/LFO_Depth_Tone",
    [25] = "/custom_properties/LFO_Depth_Character",
    [26] = "/custom_properties/LFO_Phase_Amount",
    [27] = "/custom_properties/LFO_Phase_Tone",
    [28] = "/custom_properties/LFO_Phase_Character",
  },
}
