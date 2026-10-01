// Saturator -- the touch-screen display (SDK 5 C++ custom display, display_id 1).
// See SaturatorDisplay.cpp for the layout, the gesture map and the notes.
#pragma once
#include "Jukebox.h"

namespace satui {

// Per-instance display state (panel-only native object "UiState", made in
// realtime_controller.lua, bound as the main display's last value). The 45
// build allows no mutable statics, and statics were shared by every instance.
struct UiState {
  bool        hoverValid = false;          // pointer inside, for popup hover / drag highlight
  TJBox_Point hover      = { 0.0f, 0.0f };
  int         tipHoverKey   = 0;           // drawn tooltip: key under the pointer
  TJBox_Int64 tipHoverSince = 0;           // stamped by the next tick
  bool        tipLingerPend = false;
  int         tipKey        = 0;           // key whose label is DRAWN
  TJBox_Int64 tipUntil      = 0;           // linger deadline, 0 = none
  bool        dragSelect    = false;       // press-drag-release selection in a popup
  static constexpr int kMaxBoxes = 16;     // PropertyBoxes: must outlive DisplaySetup
  TJBox_PropertyBox box[kMaxBoxes] = {};
  TJBox_PropertyInfo info = { nullptr, 0 };
};

// display_id in GUI2D/hdgui_2D.lua. Never 0 (kJBox_DisplayID_None).
static constexpr TJBox_DisplayID kMainDisplayID = 1;

inline bool isMainDisplay(TJBox_DisplayID iID) { return iID == kMainDisplayID; }

void MainDisplay_DisplaySetup(const TJBox_DisplayArgs* iArgs);
void MainDisplay_Draw(const TJBox_DisplayArgs* iArgs);
void MainDisplay_Gesture(TJBox_GestureArgs* iArgs);
void MainDisplay_Notify(TJBox_NotifyArgs* iArgs);

} // namespace satui
