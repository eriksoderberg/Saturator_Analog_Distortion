// =============================================================================
// Saturator -- CONTROL OVERLAYS (Erik, 2026-09-27).
//
// "The in/out and three knobs do not support alt+click for automation, they
// should. So should LFO rate. We made it work for display drawn faders in
// PCFX, please make it work in Degrader and Saturator too."
//
// THE ACTUAL CAUSE (found 2026-09-27, after these overlays were built): the
// motherboard's midi_cc_chart was EMPTY, and in Reason only properties mapped
// to a MIDI CC are automatable (Scripting Spec 4.3). No PropertyBox anywhere
// could open a lane -- a test square outside every other widget failed too.
// motherboard_def.lua now maps CC 12..28. The overlays stay: they give each
// control its own box, the drag-from-the-label and the release signal for the
// value readout (UI_Editing). If a lane still fails to open from them, the next
// suspect is the overlap with the main display (PCFX DESIGN.md "Pattern number
// automation": a box over another widget lost alt-click).
//
// The overlays DRAW NOTHING: the main display still paints the knobs, bars,
// labels and the rate. They only take the pointer: drag, shift = fine,
// double-click and cmd-click = default, and the box (tooltip, automation
// rectangle, alt-click lane, Remote menu).
//
// Knobs and bars move when the LFO panel is shown or hidden, so each has two
// overlays, switched by a visibility function (GUI2D/gui_functions.lua) on
// LFO_On and UI_ModeList: the "LFO on" set also hides while the LFO shape
// popup (UI_ModeList 5) covers the knob row, so the popup's cells stay the
// main display's.
//
// GEOMETRY. Every overlay's coordinate system is its node size (1 unit = 1
// panel px, like the main display). The nodes (GUI2D/device_2D.lua) are
// centred on the main display's knob / bar centres, so here the centre is
// always half the display size. tools/check_overlays.py pins the nodes to the
// main display's layout constants.
//
// NO MUTABLE STATICS: the 45 build rejects them. The bar drag's anchor and
// grab side live in gui_owner properties (Lvl_Anc*, Lvl_Grab), as on PCFX.
// =============================================================================
#include "CtlOverlay.h"
#include "../SaturatorShared.h"

#include <cmath>

namespace satui {
namespace {

enum Kind { kKnob, kBar, kRate, kNone };
struct Ov { Kind kind; int index; };      // knob 0..2, bar 0 in / 1 out

Ov overlayOf(TJBox_DisplayID id)
{
  switch(id)
  {
    case 2: case 3: case 4:  return { kKnob, static_cast<int>(id) - 2 };
    case 5: case 6: case 7:  return { kKnob, static_cast<int>(id) - 5 };
    case 8: case 9:          return { kBar,  static_cast<int>(id) - 8 };
    case 10: case 11:        return { kBar,  static_cast<int>(id) - 10 };
    case 12:                 return { kRate, 0 };
    default:                 return { kNone, 0 };
  }
}

// ---- values BY POSITION (hdgui_2D.lua) --------------------------------------
// knob: [0] the knob, [1] UI_ModeList, [2] UI_Editing
// bar:  [0] the level, [1] UI_ModeList, [2..4] Lvl_AncY / V / S, [5] Lvl_Grab, [6] UI_Editing
// rate: [0] LFO_Rate, [1] LFO_Rate_Sync, [2] LFO_Sync, [3] UI_ModeList
constexpr TJBox_UInt32 kV_Val = 0, kV_Pop = 1;
constexpr TJBox_UInt32 kV_AncY = 2, kV_AncV = 3, kV_AncS = 4, kV_Grab = 5;
constexpr TJBox_UInt32 kV_Rate = 0, kV_RateSync = 1, kV_Sync = 2, kV_RatePop = 3;
// knob [2], bar [6]: UI_Editing (gui_owner) -- 1 in, 2 amount, 3 tone, 4 style, 5 out
// while this overlay's drag is on, 0 after. The DSP ends the value readout on it.
constexpr TJBox_UInt32 kV_KnobEditing = 2, kV_BarEditing = 6;

TJBox_UInt32 needCount(Kind k) { return k == kKnob ? 3u : (k == kBar ? 7u : 4u); }
double num(const TJBox_Value* p, TJBox_UInt32 i, double def);
void setEditing(TJBox_GestureArgs* a, TJBox_UInt32 i, int code)
{
  if(static_cast<int>(num(a->fParams, i, 0.0) + 0.5) != code) a->fParams[i] = JBox_MakeNumber(code);
}

// Undo-step names = texts.lua keys (the main display's kG_*).
const char* const kG_Knob[3] = { "Amount", "Tone", "Character" };
const char* const kG_Lvl[2]  = { "In_Level In Level", "Out_Level Out Level" };
const char* const kG_Rate     = "LFO_Rate LFO Rate";
const char* const kG_RateSync = "LFO_Rate_Sync LFO Sync Rate";

const float kKnobDefault[3] = { 0.35f, 0.5f, 0.5f };   // = motherboard defaults
const float kRateDefault     = sat::kLfoRateDefault;
const int   kRateSyncDefault = 7;                      // 1/1

// Knobs and bars: the node spans the main display's y 20..250 (kLvlHitT..
// kLvlHitB), so the LABEL above each control grabs it too (Erik, 2026-09-27:
// "the words in and out are click+drag for those values, make the knobs behave
// the same way"). Bar trough: y 60..245 there (kLvlBarT..kLvlBarB).
constexpr float kNodeT   = 20.0f;
// Knob: the disc (kKnobR 65 + 12) around the main display's kKnobCY 156, plus
// the label strip above it.
constexpr float kKnobHitR = 77.0f;
constexpr float kKnobCYLocal = 156.0f - kNodeT;                   // 136
constexpr float kKnobLabelB  = kKnobCYLocal - kKnobHitR;          // 59: above this = the label
constexpr float kTroughT = 60.0f - kNodeT, kTroughB = 245.0f - kNodeT;   // 40, 225

// ---- small helpers -------------------------------------------------------------
double num(const TJBox_Value* p, TJBox_UInt32 i, double def)
{
  const double v = JBox_GetNumber(p[i]);
  return std::isfinite(v) ? v : def;
}
double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
void setNum(TJBox_GestureArgs* a, TJBox_UInt32 i, double v, const char* key)
{
  a->fParams[i] = JBox_MakeNumber(v);
  a->fGestureNameKey = key;
}
// Close whatever popup the main display has open (a tap anywhere else does).
void closePopup(TJBox_GestureArgs* a, TJBox_UInt32 i)
{
  if(num(a->fParams, i, 0.0) > 0.5) a->fParams[i] = JBox_MakeNumber(0.0);
}
// The SDK knob's drag: vertical, Shift = fine, the user's Mouse Knob Range.
double dragFactorY(const TJBox_GestureArgs* a)
{ return a->fCoordToValueFactor.fY * a->fCoordToValueModifiersFactor * a->fCoordToValueKnobRangeFactor; }

// ---- knob: centre detent on tone / style (the main display's kDetent) --------
constexpr double kDetent = 0.06;
double detentToDrag(double v)
{
  if(std::fabs(v - 0.5) < 1.0e-4) return 0.5 + 0.5 * kDetent;
  return (v < 0.5) ? v : v + kDetent;
}
double detentFromDrag(double u)
{
  if(u < 0.0) u = 0.0; else if(u > 1.0 + kDetent) u = 1.0 + kDetent;
  if(u < 0.5) return u;
  if(u <= 0.5 + kDetent) return 0.5;
  return u - kDetent;
}

// ---- bar: the PCFX AMT fader law (AmtFaderLaw.h, FaderAnchor.h) ---------------
// 0 dB = 0.5 holds over a magnetic zone as long as the whole bar (w = 0.5);
// outside it the fill follows the pointer 1:1. A drag that STARTS at 0 dB leaves
// at once (kLeave); a drag into or through 0 dB is held.
namespace lvl {
constexpr double kTravel = kTroughB - kTroughT;              // 185 units = 0..1
constexpr double kW = 0.5;
double rawToValue(double r)
{
  const double c = 0.5 + kW, d = r - c;
  if(d > -kW && d < kW) return 0.5;
  return clamp01(0.5 + (d > 0.0 ? d - kW : d + kW));
}
double valueToRaw(double v)
{
  const double c = 0.5 + kW;
  if(v == 0.5) return c;
  return c + (v - 0.5) + (v > 0.5 ? kW : -kW);
}
double fillTopY(double v) { return kTroughB - v * kTravel; }
constexpr double kEdgeGrab = 12.0;                            // +- units = "on the fill's top edge"
bool onEdge(double y, double v) { return std::fabs(y - fillTopY(v)) <= kEdgeGrab; }
constexpr double kJumpSnap = 0.03;
double jumpValue(double y)
{
  const double v = clamp01((kTroughB - y) / kTravel);
  return (v > 0.5 - kJumpSnap && v < 0.5 + kJumpSnap) ? 0.5 : v;
}
// Only a plain click ON the trough, off the fill's edge, jumps.
double baseValue(double y, double v0, bool shift)
{ return (!shift && y >= kTroughT && y <= kTroughB && !onEdge(y, v0)) ? jumpValue(y) : v0; }
constexpr double kLeave = 1.5;
constexpr int kGrabNone = 0, kGrabUp = 1, kGrabDown = 2;
int decideGrab(int grab, double dy)
{
  if(grab != kGrabNone) return grab;
  if(dy <= -kLeave) return kGrabUp;
  if(dy >= kLeave) return kGrabDown;
  return kGrabNone;
}
double dragValue(double base, double dy, double scale, int grab)
{
  const double up = -dy * scale / kTravel;
  if(base != 0.5) return rawToValue(valueToRaw(base) + up);
  if(grab == kGrabNone) return 0.5;
  const double c = 0.5 + kW, lead = kLeave * scale / kTravel;
  return grab == kGrabUp ? rawToValue(c + kW + (up - lead)) : rawToValue(c - kW + (up + lead));
}
// Shift anchor, stored in gui_owner numbers (0..1): y packed over +-1000 units.
constexpr double kFine = 0.1, kYRange = 1000.0;
double packY(double y) { return clamp01((y + kYRange) / (2.0 * kYRange)); }
double unpackY(double u) { return u * 2.0 * kYRange - kYRange; }
struct Anchor { double y; double v; bool shift; };
void writeAnchor(TJBox_GestureArgs* a, const Anchor& n)
{
  a->fParams[kV_AncY] = JBox_MakeNumber(packY(n.y));
  a->fParams[kV_AncV] = JBox_MakeNumber(n.v);
  a->fParams[kV_AncS] = JBox_MakeNumber(n.shift ? 1.0 : 0.0);
}
Anchor readAnchor(const TJBox_GestureArgs* a)
{
  double v = clamp01(num(a->fParams, kV_AncV, 0.5));
  if(v > 0.5 - 1e-9 && v < 0.5 + 1e-9) v = 0.5;
  return { unpackY(num(a->fParams, kV_AncY, 0.5)), v, num(a->fParams, kV_AncS, 0.0) > 0.5 };
}
} // namespace lvl

// ---- per-kind gestures -----------------------------------------------------------
void knobGesture(TJBox_GestureArgs* a, int k)
{
  const TJBox_ModifierKeys& m0 = a->fStartState.fModifierKeys;
  switch(a->fCall)
  {
    case kJBox_OnTap:
    {
      if(a->fTap != kJBox_TapRegular || m0.fAlt) return;          // the PropertyBox's
      const TJBox_Point p = a->fStartState.fPoint;
      const float dx = p.fX - 0.5f * a->fDisplayInfo.fSize.fX, dy = p.fY - kKnobCYLocal;
      if(p.fY >= kKnobLabelB && dx * dx + dy * dy > kKnobHitR * kKnobHitR) return;   // the corners are not the knob
      closePopup(a, kV_Pop);
      if(m0.fCmdCtrl || (a->fTapDouble && !m0.fShift))              // cmd-click / double-click: default
        setNum(a, kV_Val, kKnobDefault[k], kG_Knob[k]);
      setEditing(a, kV_KnobEditing, k + 2);                         // readout on until release
      a->fConsumeFlag = true;                                       // plain / shift: a drag
      break;
    }
    case kJBox_OnRelease:
    case kJBox_OnCancel:
      setEditing(a, kV_KnobEditing, 0);
      break;
    case kJBox_OnUpdate:
    {
      if(a->fTapDouble || m0.fCmdCtrl) return;                     // a reset was the whole gesture
      const double start = clamp01(num(a->fParamsStart, kV_Val, kKnobDefault[k]));
      const double drag  = (a->fCurrentState.fPoint.fY - a->fStartState.fPoint.fY) * dragFactorY(a);
      const double v = (k == 0) ? clamp01(start - drag) : detentFromDrag(detentToDrag(start) - drag);
      if(v != num(a->fParams, kV_Val, v)) setNum(a, kV_Val, v, kG_Knob[k]);
      break;
    }
    default: break;
  }
}

void barGesture(TJBox_GestureArgs* a, int b)
{
  const TJBox_ModifierKeys& m0 = a->fStartState.fModifierKeys;
  switch(a->fCall)
  {
    case kJBox_OnTap:
    {
      if(a->fTap != kJBox_TapRegular || m0.fAlt) return;          // the PropertyBox's
      closePopup(a, kV_Pop);
      if(num(a->fParams, kV_Grab, 0.0) != lvl::kGrabNone)          // a new gesture: side undecided
        a->fParams[kV_Grab] = JBox_MakeNumber(lvl::kGrabNone);
      if(m0.fCmdCtrl || (a->fTapDouble && !m0.fShift))              // cmd-click / double-click: 0 dB
        setNum(a, kV_Val, 0.5, kG_Lvl[b]);
      else
      {
        double v0 = clamp01(num(a->fParamsStart, kV_Val, 0.5));
        if(v0 > 0.5 - 1e-6 && v0 < 0.5 + 1e-6) v0 = 0.5;
        const double y = a->fStartState.fPoint.fY;
        const bool shift = m0.fShift != 0;
        const double base = lvl::baseValue(y, v0, shift);
        if(base != num(a->fParams, kV_Val, base)) setNum(a, kV_Val, base, kG_Lvl[b]);
        lvl::writeAnchor(a, { y, base, shift });                    // the drag starts here
      }
      setEditing(a, kV_BarEditing, b == 0 ? 1 : 5);                  // readout on until release
      a->fConsumeFlag = true;
      break;
    }
    case kJBox_OnRelease:
    case kJBox_OnCancel:
      setEditing(a, kV_BarEditing, 0);
      break;
    case kJBox_OnUpdate:
    {
      if(a->fTapDouble || m0.fCmdCtrl) return;
      const double y = a->fCurrentState.fPoint.fY;
      const lvl::Anchor a0 = lvl::readAnchor(a);
      int grab = static_cast<int>(num(a->fParams, kV_Grab, 0.0) + 0.5);
      auto valueFrom = [&grab, a, y](const lvl::Anchor& an) {
        if(an.v == 0.5)
        {
          const int g = lvl::decideGrab(grab, y - an.y);
          if(g != grab) { grab = g; a->fParams[kV_Grab] = JBox_MakeNumber(static_cast<double>(g)); }
        }
        return lvl::dragValue(an.v, y - an.y, an.shift ? lvl::kFine : 1.0, grab);
      };
      const bool shiftNow = a->fCurrentState.fModifierKeys.fShift != 0;
      lvl::Anchor an = a0;
      if(shiftNow != a0.shift)                                      // re-anchor where the fill is now
      {
        an = { y, valueFrom(a0), shiftNow };
        lvl::writeAnchor(a, an);
      }
      const double v = valueFrom(an);
      if(v != num(a->fParams, kV_Val, v)) setNum(a, kV_Val, v, kG_Lvl[b]);
      break;
    }
    default: break;
  }
}

void rateGesture(TJBox_GestureArgs* a)
{
  const TJBox_ModifierKeys& m0 = a->fStartState.fModifierKeys;
  switch(a->fCall)
  {
    case kJBox_OnTap:
    {
      if(a->fTap != kJBox_TapRegular || m0.fAlt) return;          // the PropertyBox's
      closePopup(a, kV_RatePop);
      if(m0.fCmdCtrl || (a->fTapDouble && !m0.fShift))              // default: 1/1 or 1 Hz
      {
        if(num(a->fParams, kV_Sync, 1.0) > 0.5) setNum(a, kV_RateSync, kRateSyncDefault, kG_RateSync);
        else                                    setNum(a, kV_Rate, kRateDefault, kG_Rate);
      }
      a->fConsumeFlag = true;
      break;
    }
    case kJBox_OnUpdate:
    {
      if(a->fTapDouble || m0.fCmdCtrl) return;
      const TJBox_Value* s = a->fParamsStart;
      const double dy = a->fCurrentState.fPoint.fY - a->fStartState.fPoint.fY;
      if(num(s, kV_Sync, 1.0) > 0.5)
      {
        // the drag's 0..1 travel spans the divisions (the main display's law)
        constexpr int n = sat::kNumDivs;
        const double t01 = num(s, kV_RateSync, kRateSyncDefault) / (n - 1) - dy * dragFactorY(a);
        const int di = static_cast<int>(std::floor(clamp01(t01) * (n - 1) + 0.5));
        if(di != static_cast<int>(num(a->fParams, kV_RateSync, di) + 0.5)) setNum(a, kV_RateSync, di, kG_RateSync);
      }
      else
      {
        const double v = clamp01(num(s, kV_Rate, kRateDefault) - dy * dragFactorY(a));
        if(v != num(a->fParams, kV_Rate, v)) setNum(a, kV_Rate, v, kG_Rate);
      }
      break;
    }
    default: break;
  }
}

} // namespace

// =============================================================================
void CtlOverlay_DisplaySetup(const TJBox_DisplayArgs* iArgs)
{
  const Ov ov = overlayOf(iArgs->fDisplayInfo.fID);
  if(ov.kind == kNone) return;
  const TJBox_Point sz = iArgs->fDisplayInfo.fSize;
  // ONE box over the control -- PCFX AmtFader.cpp, proven for alt-click in Recon.
  // A LOCAL, as there: no statics in the 45 build.
  TJBox_PropertyBox box;
  box.fPropertyIndex = kV_Val;
  if(ov.kind == kRate && iArgs->fDisplayInfo.fParamCount >= needCount(kRate))
    box.fPropertyIndex = (num(iArgs->fParams, kV_Sync, 1.0) > 0.5) ? kV_RateSync : kV_Rate;
  box.fPropertyRef = kJBox_InvalidPropertyRef;
  // The whole node: at least the control's hit area (tooltip, automation
  // rectangle and alt-click all live in the box).
  box.fRect = { 0.0f, 0.0f, sz.fX, sz.fY };
  box.fFlags = 0;          // NO flags: DisableTooltip disables the device in Reason 14.1 (ES20 5f)
  TJBox_PropertyInfo info;
  info.fBoxes = &box;
  info.fCount = 1;
  JBox_SetPropertyInfo(&info);
}

void CtlOverlay_Draw(const TJBox_DisplayArgs* /*iArgs*/)
{
  // Nothing: the main display paints these controls. background = "transparent".
}

void CtlOverlay_Gesture(TJBox_GestureArgs* iArgs)
{
  const Ov ov = overlayOf(iArgs->fDisplayInfo.fID);
  if(ov.kind == kNone || iArgs->fDisplayInfo.fParamCount < needCount(ov.kind)) return;
  switch(ov.kind)
  {
    case kKnob: knobGesture(iArgs, ov.index); break;
    case kBar:  barGesture(iArgs, ov.index);  break;
    case kRate: rateGesture(iArgs);           break;
    default: break;
  }
}

} // namespace satui
