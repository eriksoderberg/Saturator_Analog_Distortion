// Saturator -- control overlays: small transparent C++ displays over the knobs,
// the IN / OUT bars and the LFO rate (Erik, 2026-09-27). See CtlOverlay.cpp.
#pragma once
#include "Jukebox.h"

namespace satui {

// display_id in GUI2D/hdgui_2D.lua (the main display is 1).
//   2..4   amount / tone / style knob, LFO panel shown   (visible: LFO on, no shape popup)
//   5..7   amount / tone / style knob, LFO panel hidden  (visible: LFO off)
//   8, 9   in / out bar, LFO panel shown
//   10, 11 in / out bar, LFO panel hidden
//   12     LFO rate readout                               (visible: LFO on)
static constexpr TJBox_DisplayID kOvFirstID = 2;
static constexpr TJBox_DisplayID kOvLastID  = 12;

inline bool isCtlOverlay(TJBox_DisplayID iID) { return iID >= kOvFirstID && iID <= kOvLastID; }

void CtlOverlay_DisplaySetup(const TJBox_DisplayArgs* iArgs);
void CtlOverlay_Draw(const TJBox_DisplayArgs* iArgs);
void CtlOverlay_Gesture(TJBox_GestureArgs* iArgs);

} // namespace satui
