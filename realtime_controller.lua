
format_version = "1.0"

rtc_bindings = {
  { source = "/environment/system_sample_rate", dest = "/global_rtc/init_instance" },
}

global_rtc = {
  init_instance = function(source_property_path, new_value)
    local sample_rate = jbox.load_property("/environment/system_sample_rate")
    local new_no = jbox.make_native_object_rw("Instance", { sample_rate })
    jbox.store_property("/custom_properties/instance", new_no)
    -- the display's per-instance state (ui/*Display.h UiState)
    local ui = jbox.make_native_object_rw("UiState", {})
    jbox.store_property("/custom_properties/ui_state", ui)
  end,
}

rt_input_setup = {
  notify = {
    "/custom_properties/*",
    "/environment/system_sample_rate",
    "/environment/on_off_bypass",
    "/cv_inputs/Amount_CV/*",
    "/cv_inputs/Tone_CV/*",
    "/cv_inputs/Character_CV/*",
    -- LFO sync: host tempo, transport state and song position.
    "/transport/playing",
    "/transport/tempo",
    "/transport/play_pos",
  },
}

sample_rate_setup = {
  native = {
    22050,
    44100,
    48000,
    88200,
    96000,
    192000,
  },
}
