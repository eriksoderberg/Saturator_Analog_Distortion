#include "Jukebox.h"
#include <cstring>
#include <logging.h>
#include "Saturator.h"
#include "ui/SaturatorDisplay.h"
#include "ui/CtlOverlay.h"

void *JBox_Export_CreateNativeObject(const char iOperation[],
                                     const TJBox_Value iParams[],
                                     TJBox_UInt32 iCount)
{
  RE_LOGGING_INIT_FOR_RE("Saturator");

  if(std::strcmp(iOperation, "Instance") == 0)
  {
    DLOG_F(INFO, "CreateNativeObject / Instance");
    if(iCount >= 1)
    {
      TJBox_Float64 sampleRate = JBox_GetNumber(iParams[0]);
      return new Device(static_cast<int>(sampleRate));
    }
    return new Device();
  }

  // The main display's per-instance state (ui/SaturatorDisplay.h), panel-only.
  if(std::strcmp(iOperation, "UiState") == 0)
    return new satui::UiState();

#if LOCAL_NATIVE_BUILD
  ABORT_F("Unknown operation [%s] passed to JBox_Export_CreateNativeObject", iOperation);
#else
  return nullptr;
#endif
}

void JBox_Export_RenderRealtime(void *iPrivateState,
                                const TJBox_PropertyDiff iPropertyDiffs[],
                                TJBox_UInt32 iDiffCount)
{
  if(!iPrivateState)
    return;
  auto device = reinterpret_cast<Device *>(iPrivateState);
  device->renderBatch(iPropertyDiffs, iDiffCount);
}

// ----------------------------------------------------------------------------
// SDK 5 C++ display callbacks.
//
// CMakeLists.txt exports these four symbols (re-cmake v1.8.5 does not), so they
// must always be defined. Dispatch on the widget's display_id (hdgui_2D.lua):
//   2..12 = the control overlays (ui/CtlOverlay.cpp): knobs, IN/OUT bars, LFO rate.
//   1 = Main_Display, the touch screen (ui/SaturatorDisplay.cpp).
// The Input_Meter is still a Lua display and never reaches these.
// ----------------------------------------------------------------------------
void JBox_Export_DisplaySetup(const TJBox_DisplayArgs *iArgs)
{
  if(satui::isMainDisplay(iArgs->fDisplayInfo.fID)) satui::MainDisplay_DisplaySetup(iArgs);
  else if(satui::isCtlOverlay(iArgs->fDisplayInfo.fID)) satui::CtlOverlay_DisplaySetup(iArgs);
}
void JBox_Export_Draw(const TJBox_DisplayArgs *iArgs)
{
  if(satui::isMainDisplay(iArgs->fDisplayInfo.fID)) satui::MainDisplay_Draw(iArgs);
  else if(satui::isCtlOverlay(iArgs->fDisplayInfo.fID)) satui::CtlOverlay_Draw(iArgs);
}
void JBox_Export_Gesture(TJBox_GestureArgs *iArgs)
{
  if(satui::isMainDisplay(iArgs->fDisplayInfo.fID)) satui::MainDisplay_Gesture(iArgs);
  else if(satui::isCtlOverlay(iArgs->fDisplayInfo.fID)) satui::CtlOverlay_Gesture(iArgs);
}
void JBox_Export_Notify(TJBox_NotifyArgs *iArgs)
{
  if(satui::isMainDisplay(iArgs->fDisplayArgs.fDisplayInfo.fID)) satui::MainDisplay_Notify(iArgs);
}
