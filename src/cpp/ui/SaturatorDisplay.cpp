// =============================================================================
// Saturator -- the touch-screen display (SDK 5 C++ custom display, display_id 1).
//
// PORTED FROM THE DEGRADER (ui/DegraderDisplay.cpp, 2026-09-22): same layout,
// gestures, PropertyBoxes, LFO section and knobs, in the Saturator's four reds.
// What differs: the categories (CLEAN / WARM / GRITTY / HEAVY), the default
// model (ES83), and the pictograms -- one schematic symbol per circuit.
// Keep the two files in step when either one's shared logic changes.
//
// COORDINATES. The display covers the panel's screen window, 2440 x 250 panel
// px at (390, 50) (the black window in Panel_Front.png is 380..2840 x
// 40..310; the shine sits 5 px outside the display). The coordinate system is
// set to exactly that size (hdgui_2D.lua display_width_pixels / height), so
// 1 unit = 1 panel px: display x = panel x - 390, display y = panel y - 50.
//
// Left of the knobs, the layout uses the full display (Erik, 2026-09-21): the
// categories start at the display's left edge, the 73 px that used to be black
// there went half to a wider mode box (name + icon) and half to moving the
// mode box, LFO list and LFO panel 37 px left. Knobs and the patch area stay.
//
// LAYOUT (left to right):
//   categories   CLEAN / WARM / GRITTY / HEAVY, one per row
//   mode box     model name, animated pictogram, one-line description. Tap a
//                category (or the mode box) and the mode box turns into that
//                category's model list: click a model, or press on the category,
//                drag onto a model and release (PCFX style). Hover highlights.
//   LFO list     "LFO" + on/off square, then amount / tone / character. Tap a
//                row to show its page in the wave area; drag a destination row
//                up/down to set that destination's depth directly.
//   LFO panel    rate (drag) + sync square across the top, always visible.
//                The wave area shows the waveform (tap: shape popup over the
//                knobs), or the selected destination's page: depth and phase
//                value fields (drag up/down, double-click resets) + step arrows.
//   knobs        Amount / Tone / Character, drawn; vertical drag, Shift = fine,
//                double-click = default. A dot rides outside the ring at the
//                live LFO-modulated value, over an arc showing the LFO's reach.
//   patch area   labels + the name well. The patch_name and patch_browse_group
//                are STOCK widgets placed over this display (SDK 5 overlap).
//
// TAPS. Only a plain left click with no modifiers is consumed -- plus
// Cmd/Ctrl-click on a value, which resets it (resetToDefault). Alt and
// context clicks go to the host's PropertyBoxes (automation lane, Remote,
// the tooltip) -- consuming one overrides the box (SDK BigDisplay; PCFX
// PatternBox.cpp, where this was learned the hard way).
//
// GESTURES. Which control a drag belongs to is re-derived on every update from
// where the press STARTED (fStartState + fParamsStart). The pointer position
// for popup hover / drag highlighting, the drawn-tooltip session and the
// drag-select flag live in this instance's UiState (a panel-only native object,
// values[kV_Ui]) -- no statics (2026-09-27).
//
// TEXT is drawn with kJBox_Transform_Host (font sizes as in stock widgets),
// with its rect in display coordinates -- exactly as the SDK's BigDisplay
// example does it. Fonts: see kFontLabel & co (one step down from the first
// build, after measuring it against the mockup).
//
// NO TJBox_Transform anywhere else: a non-null transform drops the display
// scaling (see fillRotRect). Rotations are done by hand.
//
// UNVERIFIED until tested in Recon:
//   * that fCoordToValueFactor is in display units (the SDK knob uses it so)
// =============================================================================
#include "SaturatorDisplay.h"
#include "../SaturatorShared.h"

#include <algorithm>
#include <cmath>
#include "TinyFmt.h"   // u45 has no <cstdio>: tiny snprintf stand-in

namespace satui {
namespace {

// ---- bound values: order MUST match `values` in GUI2D/hdgui_2D.lua ----------
enum : TJBox_UInt32 {
  kV_Amount = 0, kV_Tone, kV_Character, kV_Model,
  kV_LfoOn, kV_LfoRate, kV_LfoSync, kV_LfoShape,
  kV_DepthAmt, kV_DepthTone, kV_DepthChar,
  kV_PhaseAmt, kV_PhaseTone, kV_PhaseChar,
  kV_LfoView, kV_ModeList,
  kV_PhaseOut, kV_ModAmtOut, kV_ModToneOut, kV_ModCharOut,
  kV_AnimClock,                                // rt_owner UI_AnimClock (DSP-integrated icon clock)
  kV_LfoRateSync,
  kV_InLevel, kV_OutLevel,                     // the in / out bars beside the knobs                              // synced rate: division index 0..13
  kV_EditMask,
  kV_Ui,                                       // rtc_owner ui_state: this instance's UiState (native object, panel-only)
  kV_CvMask,                                   // rt_owner UI_CvMask: connected CV inputs (bit 0 amount, 1 tone, 2 style) / 7
  kV_Count
};

// This INSTANCE's display state (hover, drawn-tooltip session, drag-select,
// PropertyBoxes): a panel-only native object bound as values[kV_Ui] (Erik,
// 2026-09-27: "tooltips need to be per instance" -- they were file statics,
// shared by every Saturator, and the 45 build rejects statics anyway). Before
// the RTC has made it (the panel can draw first) the caller's scratch is used.
UiState& uiState(const TJBox_Value* p, TJBox_UInt32 count, UiState& scratch)
{
  if(p == nullptr || count <= kV_Ui || JBox_GetType(p[kV_Ui]) != kJBox_NativeObject) return scratch;
  UiState* st = static_cast<UiState*>(JBox_GetNativeObjectRW(p[kV_Ui]));
  return st != nullptr ? *st : scratch;
}

// Undo-step names = texts.lua keys (required for every document_owner write).
const char* const kG_Knob[3]  = { "Amount", "Tone", "Character" };
const char* const kG_Depth[3] = { "LFO_Depth_Amount LFO > Amount", "LFO_Depth_Tone LFO > Tone",
                                  "LFO_Depth_Character LFO > Character" };
const char* const kG_Phase[3] = { "LFO_Phase_Amount LFO Phase Amount", "LFO_Phase_Tone LFO Phase Tone",
                                  "LFO_Phase_Character LFO Phase Character" };
const char* const kG_In       = "In_Level In Level";
const char* const kG_Out      = "Out_Level Out Level";
const char* const kG_Mode     = "Model";
const char* const kG_LfoOn    = "LFO_On LFO";
const char* const kG_Rate     = "LFO_Rate LFO Rate";
const char* const kG_RateSync = "LFO_Rate_Sync LFO Sync Rate";
const char* const kG_Sync     = "LFO_Sync LFO Sync";
const char* const kG_Shape    = "LFO_Shape LFO Shape";

const float kKnobDefault[3] = { 0.35f, 0.5f, 0.5f };  // = motherboard defaults
// Two rate properties (like a value_switch pair): LFO_Sync picks which one is
// shown, edited, reset and given the PropertyBox (so the tooltip shows a note
// value or Hz).
const float kRateDefault     = sat::kLfoRateDefault;   // FREE rate: 1 Hz
const int   kRateSyncDefault = 7;                      // SYNCED rate: 1/1 (index 7 of the 25 divisions)

// ---- palette (mockup) --------------------------------------------------------
constexpr TJBox_Color rgba(unsigned r, unsigned g, unsigned b, unsigned a)
{ return (TJBox_Color(a) << 24) | (TJBox_Color(r) << 16) | (TJBox_Color(g) << 8) | TJBox_Color(b); }

// Saturator: four reds on black (Erik, 2026-09-22), in the Degrader's roles.
constexpr TJBox_Color kRed     = rgba(234,   8,   8, 255);   // #EA0808 -- all lit ink
constexpr TJBox_Color kRedDim  = rgba(122,  15,  15, 255);   // #7A0F0F dim ink: idle labels, name + description, square frames
constexpr TJBox_Color kCellOn  = rgba( 67,   8,   8, 255);   // #430808 selected cell, LFO top bar, wave area
constexpr TJBox_Color kCellOff = rgba( 36,   4,   4, 255);   // #240404 idle cell, mode box, LFO panel
constexpr TJBox_Color kCell30  = rgba(240,  28,  28,  77);   // rgba(240,28,28,0.3): model-list highlight
constexpr TJBox_Color kCell15  = rgba(240,  28,  28,  38);   // rgba(240,28,28,0.15): mode box inner well (mockup)
constexpr TJBox_Color kPopBg   = kCellOff;                   // popups (model list, LFO shapes): #240404
constexpr TJBox_Color kPopSel  = kRedDim;                    // the current entry in a popup: #7A0F0F
constexpr TJBox_Color kKnobIn  = rgba(  0,   0,   0, 255);   // knob centre + notch, value fields: black
constexpr TJBox_Color kWell    = rgba( 67,   8,   8, 255);   // #430808 = the inner well as it renders (#240404 + 15 % red): "holes" in the icons

// ---- layout (see COORDINATES above) ------------------------------------------
// Categories
constexpr float kDisplayW = 2440, kDisplayH = 250;           // hdgui_2D.lua Main_Display size (drawTooltip clamps to it)
// Row grid copied from the SpiritLevel mode buttons (Erik, 2026-09-23): a 4 px
// (20 unit) left margin, and the four rows fill the FULL display height --
// 4 x 55 + 3 x 10 = 250 -- instead of sitting 2 px in from the top and bottom.
constexpr float kCatL = 20, kCatR = 230, kRowH = 55;
constexpr float kRowT[4] = { 0, 65, 130, 195 };
// Mode box + its inner well, name, icon, description
// (366 wide: the old 330 + half of the freed 73 px)
// Full display height, like the category and LFO rows (Erik, 2026-09-26);
// the well keeps its 15 px inset top and bottom, so the icon stays centred.
constexpr float kBoxL = 242, kBoxT = 0, kBoxR = 608, kBoxB = 250;
constexpr float kInL  = 272, kInT  = 15, kInR  = 578, kInB  = 235;
constexpr float kIconL = 348, kIconT = 97, kIconR = 503, kIconB = 167;   // 155 wide, centred in the well
// Open model list: fills the mode box only, so the LFO section stays visible.
constexpr float kListL = kBoxL, kListT = kBoxT, kListR = kBoxR, kListB = kBoxB;
constexpr float kListNameL = kListL + 20, kListDescL = kListL + 140;
// Rows share the list height evenly, whatever the category's model count
// (Erik: no gap at the bottom of a 4-model list).
inline float listRowH(int cat) { return (kListB - kListT - 10) / static_cast<float>(sat::kCatCount[cat]); }
// UI_ModeList doubles as "which popup is open" (one popup at a time):
// 0 none, 1..4 the model list of category 0..3 (CLEAN..HEAVY), 5 LFO shapes.
enum : int { kPopNone = 0, kPopCat0 = 1, kPopShape = 5 };
inline int popCat(int pop) { return (pop >= kPopCat0 && pop < kPopCat0 + 4) ? pop - kPopCat0 : -1; }
// LFO shape popup: drawn over the knob area, which it replaces while open.
// 4 x 2 grid of cells (7 shapes), each a mini waveform + its name.
// Centred where the knobs sit with the LFO on ((1223 + 1919) / 2 = 1571).
// 2026-09-29 (Erik): widened to 10 units from the LFO panel and the patch area
// (was 36 / 35) and full height on the row grid (0..250, was 10..240).
constexpr float kShpL = 1233, kShpT = 0, kShpR = 1909, kShpB = 250;
// Popup labels: the saws are just "up" / "down" -- the icon shows the saw, and
// "saw down" ran out of its cell. Host-facing names stay in kLfoShapeName.
static const char* const kShapePopName[7] = { "sine", "triangle", "up", "down", "square", "s&h", "drift" };
constexpr int   kShpCols = 4, kShpRows = 2;
constexpr float kShpGap = 10;
// LFO list
constexpr float kLfoL = 623, kLfoR = 838, kLfoTxtL = 643;
// LFO on/off square at the LEFT edge of the LFO row, 1 host px (5 units) in,
// vertically centred; the "LFO" label is centred in the whole row (Erik).
constexpr float kOnSqL = kLfoL + 5, kOnSqT = 7.5f;            // 40 x 40, inner 20 x 20 (centred in row 0)
// LFO panel
// Full height on the LFO list's row grid (Erik, 2026-09-27: "LFO options box
// wrong height"): top bar 0..55 = the LFO row, the lower part 65..250 = the
// amount..style rows, with the same 10 px black gap. Was 11..60 / ..240.
constexpr float kPanL = 853, kPanT = 0, kPanR = 1223, kPanB = 250, kBarB = 55;
constexpr float kPanLowT = 65;                                // lower panel top (= kRowT[1])
// Top bar, left to right (Erik, 2026-09-29): [sync square] "sync"  <value> [arrows].
// Square 5 units in from the left edge, like the LFO on/off square (kOnSqL); arrows 14 in from the right.
constexpr float kSyncSqL = kPanL + 5, kSyncSqT = 7.5f;             // centred in the bar, level with the LFO on/off square
constexpr float kSyncTxtL = kSyncSqL + 48, kSyncTxtR = kSyncTxtL + 90;   // "sync", left-aligned after the square
constexpr float kWaveL = 888, kWaveT = 75, kWaveR = 1188, kWaveB = 240;
// Destination page (Erik's mockup): the selected destination row runs straight
// into the page, both #430808, so the tab and its page read as one piece.
// "depth" / "phase" labels, a black value field each (drag it up/down,
// double-click resets) and up/down step arrows beside it.
constexpr float kPageL = 838, kPageT = 65, kPageR = 1223, kPageB = 250;
constexpr float kDestLabelL = 855;
// kField* is the row (hit area, arrows); the black box is kFieldInset smaller
// on every side (Erik: "3 px smaller").
constexpr float kFieldL = 976, kFieldR = 1153, kFieldH = 49, kFieldInset = 3;
constexpr float kFieldT[2] = { 101, 165 };                    // depth, phase
constexpr float kArrL = 1166, kArrR = 1201, kArrH = 20;      // up arrow on top, down arrow at the bottom
// The up/down pair spans exactly the black value box (row inset kFieldInset top
// and bottom), not the whole row (Erik: arrows 2 px too tall).
// Rate step arrows, in the top bar at the right end of the rate area.
constexpr float kRateArrR = kPanR - 14, kRateArrL = kRateArrR - 35;   // step arrows at the bar's right end
constexpr float kRateL = kSyncTxtR + 5, kRateR = kRateArrL - 8;       // rate value (right-aligned) + drag area
// Knobs: 130 px red disc, 90 px black centre, 14.8 x 49.69 notch from the top
// Knob x positions are dynamic (Erik, 2026-09-21): the three knobs, 215 apart,
// are centred between the right edge of the LFO section and the left edge of
// the patch area. With the LFO off its panel is hidden, so that edge is the
// LFO list's right edge and the knobs slide left.
// 2026-09-27 (Erik): 185 -> 173 and the bars 133 -> 125 from the outer knobs.
// The whole group (in, knobs, out) now leaves >= 10 units (HD px) between the
// "in" / "out" labels + 0 dB ticks and the LFO panel / patch area: with the LFO
// panel shown the "out" label ended ~20 units INSIDE the patch area before.
constexpr float kKnobSpacing = 173;
// LFO panel HIDDEN: the row has 1081 units instead of 696, so it spreads out
// (Erik, 2026-09-27; spread further the same day): knobs 260 apart, bars 180
// out -- ~60 units left between the in / out labels and the LFO list / patch area.
constexpr float kKnobSpacingWide = 260, kLvlBarDXWide = 180;
constexpr float kPatchL = 2309 - 390;                        // patch name widget's left edge (panel 2309)
constexpr float kLfoEdgeOn = kPanR, kLfoEdgeOff = kLfoR;     // LFO panel shown / hidden
inline float knobCX(int k, bool lfoOn)
{
  const float mid = 0.5f * ((lfoOn ? kLfoEdgeOn : kLfoEdgeOff) + kPatchL);
  return mid + (k - 1) * (lfoOn ? kKnobSpacing : kKnobSpacingWide);
}
// IN / OUT level bars (Erik's mockup, 2026-09-23): a 21 x 185 trough either side
// of the knob row, filled from the bottom, dragged up and down. They ride WITH
// the knobs: kLvlBarDX out from the first / last knob centre, so the whole group
// re-centres when the LFO panel is hidden.
constexpr float kLvlBarW = 21, kLvlBarT = 60, kLvlBarB = 245;
constexpr float kLvlBarDX = 125;                              // knob centre -> bar centre
// The bar's hit area AND its PropertyBox (Erik, 2026-09-26): the trough is only
// ~4 host px wide, so a box that size lost the host tooltip as soon as the
// pointer drifted, showed a hairline automation rectangle and missed most
// alt-clicks (automation lane). Now kLvlHitPad out on each side (3 host px)
// and from the top of the "in"/"out" label to the display bottom, like a
// PCFX fader node. Still well clear of the knob boxes (68 units away).
// Pad 15 -> 35 (2026-09-27, Erik): the automation rectangle around a bar was
// much narrower than a knob's. 35 keeps 2.5 units clear of the knob overlay
// with the LFO panel shown (kLvlBarDX 125 - kKnobR 77 - 10.5 - 35).
constexpr float kLvlHitPad = 35, kLvlHitT = 20, kLvlHitB = 250;
inline float lvlBarCX(int which, bool lfoOn)                  // 0 = in, 1 = out
{ const float dx = lfoOn ? kLvlBarDX : kLvlBarDXWide;
  return (which == 0) ? knobCX(0, lfoOn) - dx : knobCX(2, lfoOn) + dx; }
inline float lvlHitL(int which, bool lfoOn) { return lvlBarCX(which, lfoOn) - 0.5f * kLvlBarW - kLvlHitPad; }
inline float lvlHitR(int which, bool lfoOn) { return lvlBarCX(which, lfoOn) + 0.5f * kLvlBarW + kLvlHitPad; }

// Knobs 10 px lower and labels 10 px higher than the first mockup (Erik,
// 2026-09-21): room for the LFO reach arc above the ring.
constexpr float kKnobCY = 156, kKnobR = 65, kKnobRIn = 45;
constexpr float kKnobLabelT = 1, kKnobLabelB = 61;
constexpr float kNotchW = 14.8f, kNotchH = 49.69f;
constexpr float kModR = 73, kModThick = 5, kModDotR = 6;      // LFO reach arc + live dot

constexpr float kSweepDeg = 270.0f;                           // knob travel, 7 o'clock -> 5 o'clock

// ---- small helpers ------------------------------------------------------------
float num(const TJBox_Value* p, TJBox_UInt32 i, float def)
{
  if(p == nullptr) return def;
  const double v = JBox_GetNumber(p[i]);
  return std::isfinite(v) ? static_cast<float>(v) : def;
}
int inum(const TJBox_Value* p, TJBox_UInt32 i, int def)
{
  const float v = num(p, i, static_cast<float>(def));
  return static_cast<int>(std::floor(v + 0.5f));
}
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
bool inside(TJBox_Point p, float l, float t, float r, float b)
{ return p.fX >= l && p.fX < r && p.fY >= t && p.fY < b; }

const TJBox_PathStyle kFill = { 0.0f, kJBox_Cap_Butt, kJBox_Join_Bevel };

void fillRect(float l, float t, float r, float b, TJBox_Color c)
{
  const TJBox_Rect rc = { l, t, r, b };
  JBox_DrawRect(&rc, c, &kFill, nullptr);
}

void text(float l, float t, float r, float b, const char* s, TJBox_Font f, TJBox_HAlign h, TJBox_Color c)
{
  TJBox_Text tx;
  tx.fRect  = { l, t, r, b };
  tx.fHAlign = h;
  tx.fText  = s;
  tx.fFont  = f;
  JBox_DrawText(&tx, c, kJBox_Transform_Host);
}

// ---- fonts -------------------------------------------------------------------
// Host fonts render at a fixed size (kJBox_Transform_Host), NOT scaled with the
// display. Measured in Reason 14: ArialMedium / ArialMediumBold came out ~30 %
// taller than the mockup, ArialLargeBold ~50 %. So one step down everywhere.
// The mockup's bold labels (categories, "LFO", model names in the list) use
// ArialMediumSmall too: there is no smaller host bold, and a faux bold (drawn
// twice, offset) looked bad in Reason (Erik, 2026-09-21).
constexpr TJBox_Font kFontLabel = kJBox_Font_ArialMediumSmall;   // labels, rows, rate, description
constexpr TJBox_Font kFontSmall = kJBox_Font_ArialSmall;         // secondary readouts (depth %, shape name)
constexpr TJBox_Font kFontCat   = kJBox_Font_ArialSmall;         // category buttons -- same font as the
                                                                 // SpiritLevel mode buttons (Erik, 2026-09-23)
constexpr TJBox_Font kFontName  = kJBox_Font_ArialMediumLargeBold; // model name in the mode box

// The "bold" labels. Plain ArialMediumSmall; kept as one function so a
// different font for them is a one-line change.
// Erik (2026-09-21): the category, LFO-row, depth/phase and rate/sync labels
// sat 1 px too low in Reason. 1 host px = 5 display units (the panel is 5x the
// 754 px rack width), so those rects move up by kLabelNudgeY.
constexpr float kLabelNudgeY = 5.0f;

void textBold(float l, float t, float r, float b, const char* s, TJBox_HAlign h, TJBox_Color c)
{
  text(l, t, r, b, s, kFontLabel, h, c);
}

void polyline(const TJBox_Point* pts, TJBox_UInt32 n, float thick, TJBox_Color c, bool closed = false)
{
  TJBox_PathRun run;
  run.fType = kJBox_PathRun_Line;
  run.fPtCount = n;
  run.fPts = pts;
  run.fClose = closed;
  const TJBox_PathStyle st = { thick, kJBox_Cap_Round, kJBox_Join_Round };
  JBox_DrawPath(nullptr, &run, 1, &st, c, nullptr);
}

// A filled circle = a zero-length stroke with round caps (as JukeboxCPP.h's
// JBox_FillCircle does).
void fillCircle(float cx, float cy, float r, TJBox_Color c)
{
  const TJBox_Point pts[2] = { { cx, cy }, { cx, cy } };
  polyline(pts, 2, r * 2.0f, c);
}

// Fill the rectangle (l,t,r,b), rotated `deg` clockwise about (cx,cy).
// Rotated by hand, NOT with a TJBox_Transform: any non-null transform drops the
// display's coordinate scaling (Jukebox.h: "null iTransform to scale according
// to the size of the custom display coordinate system"), so a transformed
// shape lands in host pixels -- 1/5 size, somewhere near the top left. That
// was the Degrader's "snow flakes" over the CT60 name (the reel spokes) and the missing
// knob notches.
void fillRotRect(float l, float t, float r, float b, float cx, float cy, float deg, TJBox_Color c)
{
  const float a = deg * 0.017453292519943f, cs = std::cos(a), sn = std::sin(a);
  const float xs[4] = { l, r, r, l }, ys[4] = { t, t, b, b };
  TJBox_Point pts[4];
  for(int i = 0; i < 4; ++i)
  {
    const float dx = xs[i] - cx, dy = ys[i] - cy;
    pts[i] = { cx + dx * cs - dy * sn, cy + dx * sn + dy * cs };   // y down: positive = clockwise
  }
  TJBox_PathRun run;
  run.fType = kJBox_PathRun_Line;
  run.fPtCount = 4;
  run.fPts = pts;
  run.fClose = true;
  JBox_DrawPath(nullptr, &run, 1, &kFill, c, nullptr);
}

// knob value 0..1 -> angle in degrees, 0 = straight up
float knobDeg(float v) { return -0.5f * kSweepDeg + kSweepDeg * clamp01(v); }

// point on a circle at `deg` (0 = up, clockwise)
TJBox_Point polar(float cx, float cy, float r, float deg)
{
  const float a = deg * 0.017453292519943f;
  return { cx + r * std::sin(a), cy - r * std::cos(a) };
}

// ---- the hit map ---------------------------------------------------------------
enum class Hit { None, Category, ModeBox, LfoOn, LfoRow, Rate, RateStep, Sync, WaveShape, ShapeCell, Field, Step, Knob, LvlBar };
// LvlBar: index 0 = in, 1 = out.
// RateStep: index 0 = up (faster), 1 = down (slower).
// Field: index = which (0 depth, 1 phase). Step: index = which * 2 + (0 up, 1 down).
struct Target { Hit what; int index; };

int popupOf(const TJBox_Value* v) { return inum(v, kV_ModeList, kPopNone); }

// Pointer position for popup highlighting: set by Notify OnMove (hover) and by
// Gesture OnUpdate (press-drag onto an entry). See GESTURES at the top.
bool hovered(const UiState& S, float l, float t, float r, float b) { return S.hoverValid && inside(S.hover, l, t, r, b); }

// ---- the drawn bottom-left tooltip (Erik, 2026-09-22) -------------------------
// LFO_On, the three destination rows and the wave/shape view want their
// tooltip at the bottom-left. A PropertyBox's stock tooltip has no position
// control, and DisableTooltip disables the device in Reason 14.1 (ES20), so
// these three get NO PropertyBox (MainDisplay_DisplaySetup) and the display
// draws a replica of the host tooltip itself (drawTip) -- the ES20 / BitSynth
// technique, see "Tooltips: what a custom display can and cannot do" in the
// reason-re-development skill.
//
// Key under p: 0 none, 1 LFO on/off square, 2 wave/shape view, 3..5 the
// amount / tone / character rows, 6..9 the categories. Rects = the boxes they used to have.
int listRowAt(TJBox_Point p, int cat);
// listCat >= 0: that category's model list is open in the mode box. Its rows
// get keys 10 + row (Erik, 2026-09-27: the host's Model tooltip over the open
// list was static; now the label follows the row under the pointer).
int tipKeyAt(TJBox_Point p, bool lfoOn, int view, int listCat)
{
  if(listCat >= 0)
  {
    const int row = listRowAt(p, listCat);
    if(row >= 0) return 10 + row;
  }
  if(inside(p, kOnSqL, kOnSqT, kOnSqL + 40, kOnSqT + 40)) return 1;
  if(lfoOn && view == 0 && inside(p, kWaveL, kWaveT, kWaveR, kWaveB)) return 2;
  for(int i = 0; i < 3; ++i)
    if(inside(p, kLfoL, kRowT[i + 1], kLfoR, kRowT[i + 1] + kRowH)) return 3 + i;
  for(int i = 0; i < 4; ++i)                                   // categories (2026-09-27)
    if(inside(p, kCatL, kRowT[i], kCatR, kRowT[i] + kRowH)) return 6 + i;
  return 0;
}

// Session timing as the host does it (BitSynth SeqDisplay.cpp, ES20
// PatchBayDisplay.cpp, same numbers): the first label after a 750 ms dwell;
// while one is up another key shows AT ONCE; leaving lingers 100 ms (landing on
// a key inside that window is still "at once"); a tap ends the session.
// Per instance: UiState (the native object).
constexpr TJBox_Int64 kTipDelayUS  = 750000;
constexpr TJBox_Int64 kTipLingerUS = 100000;

// Pointer onto a new key (or off all of them). True if the drawn label changed.
bool tipHoverChanged(UiState& S, int key)
{
  if(key == S.tipHoverKey) return false;
  S.tipHoverKey   = key;
  S.tipHoverSince = 0;
  S.tipLingerPend = false;
  if(key != 0)
  {
    if(S.tipKey != 0) { S.tipKey = key; S.tipUntil = 0; return true; }   // session open: switch now
    return false;                                                     // closed: the tick opens it
  }
  if(S.tipKey != 0 && S.tipUntil == 0) S.tipLingerPend = true;           // left: linger, then close
  return false;
}

// Periodical tick. True if the drawn label changed.
bool tipTick(UiState& S, TJBox_Int64 now)
{
  if(S.tipHoverKey != 0 && S.tipHoverSince == 0) S.tipHoverSince = (now != 0) ? now : 1;
  if(S.tipLingerPend) { S.tipLingerPend = false; S.tipUntil = now + kTipLingerUS; }
  if(S.tipHoverKey != 0 && S.tipKey == 0 && now - S.tipHoverSince >= kTipDelayUS)
  { S.tipKey = S.tipHoverKey; S.tipUntil = 0; return true; }             // dwell expired: open
  if(S.tipUntil != 0 && now >= S.tipUntil) { S.tipKey = 0; S.tipUntil = 0; return true; }   // linger expired
  return false;
}

// A click ends the session (the host's rule); the next label earns its dwell again.
void tipEndSession(UiState& S)
{
  S.tipKey = S.tipHoverKey = 0;
  S.tipHoverSince = S.tipUntil = 0;
  S.tipLingerPend = false;
}

// Row of the open model list under p, or -1.
int listRowAt(TJBox_Point p, int cat)
{
  if(!inside(p, kListL, kListT, kListR, kListB)) return -1;
  const float dy = p.fY - kListT - 5;
  const int row = (dy < 0.0f) ? -1 : static_cast<int>(dy / listRowH(cat));
  return (row >= 0 && row < sat::kCatCount[cat]) ? row : -1;
}
// Shape cell of the shape popup under p, or -1.
int shapeCellAt(TJBox_Point p)
{
  if(!inside(p, kShpL, kShpT, kShpR, kShpB)) return -1;
  const float cw = (kShpR - kShpL) / kShpCols, ch = (kShpB - kShpT) / kShpRows;
  const int i = static_cast<int>((p.fY - kShpT) / ch) * kShpCols + static_cast<int>((p.fX - kShpL) / cw);
  return (i >= 0 && i < sat::kNumLfoShapes) ? i : -1;
}

Target hitTest(TJBox_Point p, const TJBox_Value* v)
{
  const int pop = popupOf(v);
  for(int i = 0; i < 4; ++i)
    if(inside(p, kCatL, kRowT[i], kCatR, kRowT[i] + kRowH)) return { Hit::Category, i };
  if(inside(p, kBoxL, kBoxT, kBoxR, kBoxB)) return { Hit::ModeBox, 0 };
  const bool lfoOn = inum(v, kV_LfoOn, 0) != 0;
  const bool shapePop = lfoOn && pop == kPopShape;           // the popup belongs to the (visible) LFO panel
  if(shapePop && inside(p, kShpL, kShpT, kShpR, kShpB)) return { Hit::ShapeCell, shapeCellAt(p) };
  {
    if(inside(p, kLfoL, kOnSqT - 5, kOnSqL + 45, kOnSqT + 45)) return { Hit::LfoOn, 0 };
    for(int i = 0; i < 4; ++i)
      if(inside(p, kLfoL, kRowT[i], kLfoR, kRowT[i] + kRowH)) return { Hit::LfoRow, i };
    if(!lfoOn) goto knobs;                                     // LFO panel hidden
    if(inside(p, kPanL, kPanT, kSyncTxtR, kBarB)) return { Hit::Sync, 0 };       // square + label
    if(inside(p, kRateArrL - 8, kPanT, kRateArrR + 8, 0.5f * (kPanT + kBarB))) return { Hit::RateStep, 0 };
    if(inside(p, kRateArrL - 8, 0.5f * (kPanT + kBarB), kRateArrR + 8, kBarB)) return { Hit::RateStep, 1 };
    if(inside(p, kRateL, kPanT, kRateR, kBarB)) return { Hit::Rate, 0 };
    const int view = inum(v, kV_LfoView, 0);
    if(view == 0)
    {
      if(inside(p, kPanL, kBarB, kPanR, kPanB)) return { Hit::WaveShape, 0 };
    }
    else
    {
      for(int w = 0; w < 2; ++w)
      {
        if(inside(p, kFieldL, kFieldT[w], kFieldR, kFieldT[w] + kFieldH)) return { Hit::Field, w };
        if(inside(p, kArrL - 8, kFieldT[w] - 4, kArrR + 8, kFieldT[w] + 0.5f * kFieldH))
          return { Hit::Step, w * 2 };
        if(inside(p, kArrL - 8, kFieldT[w] + 0.5f * kFieldH, kArrR + 8, kFieldT[w] + kFieldH + 4))
          return { Hit::Step, w * 2 + 1 };
      }
    }
  }
knobs:
  if(!shapePop)
    for(int k = 0; k < 3; ++k)
    {
      const float dx = p.fX - knobCX(k, lfoOn), dy = p.fY - kKnobCY;
      if(dx * dx + dy * dy <= (kKnobR + 12) * (kKnobR + 12)) return { Hit::Knob, k };
    }
  for(int b = 0; b < 2; ++b)                                   // in / out bars
  {
    if(!shapePop && inside(p, lvlHitL(b, lfoOn), kLvlHitT, lvlHitR(b, lfoOn), kLvlHitB))
      return { Hit::LvlBar, b };
  }
  return { Hit::None, 0 };
}

// ---- pictograms (one schematic symbol per model) -----------------------------
// The circuit each model is voiced after (SaturatorShared.h kModelDesc, the word
// under the icon), confirmed by Erik 2026-09-22. All drawn round the icon centre
// (kIcX, kIcY) within about 250 x 88 px of the well (the mockup's BC12 symbol is
// 80 px across). Symbols in lit red; the moving parts:
//   - "current": dark dots marching along the leads (kWell holes in the red line)
//   - electrons: red dots flying cathode -> plate inside the valves
//   - glow: a pulsing red halo behind a valve's heater / plate
//   - waves: scrolling mini scopes showing what the circuit does to a sine
// Motion runs on the DSP's animation clock `clk` (faster with Amount, never
// stops); random jitter re-rolls on real time `t`. Amount also deepens the
// fold / clip on the waveform icons.
//   CLEAN   BC12 transistor (the mockup)   PV40 op-amp + clean gain
//           TG23 transformer               BV98 iron-core inductor
//   WARM    WP76 twin triode (glow)        ZX67 JFET
//           A1R  pentode (electrons)       ES83 triode (electrons + glow)
//   GRITTY  FD88 wavefolder scope          LF09 IC chip (blinking pins)
//           NN18 diode pair clipper        SB05 rectifier bridge
//   HEAVY   FF37 fuzz pair (two BJTs)      GR1T germanium PNP (jittery current)
//           F34K silicon hard-clip scope   X7R3 power tube (red-hot plate)
constexpr float kIcX = 0.5f * (kIconL + kIconR), kIcY = 0.5f * (kIconT + kIconB);   // 425.5, 132
constexpr float kSymTh = 5.0f;                                  // lead weight (mockup: 5 px)

TJBox_Color withAlpha(TJBox_Color c, float a01)
{
  const unsigned a = static_cast<unsigned>(std::floor(255.0f * clamp01(a01) + 0.5f));
  return (c & 0x00FFFFFFu) | (TJBox_Color(a) << 24);
}

void line(float x1, float y1, float x2, float y2, float th, TJBox_Color c)
{
  const TJBox_Point p[2] = { { x1, y1 }, { x2, y2 } };
  polyline(p, 2, th, c);
}

void ring(float cx, float cy, float r, float th, TJBox_Color c)
{
  constexpr int kN = 48;
  TJBox_Point p[kN];
  for(int i = 0; i < kN; ++i) p[i] = polar(cx, cy, r, i * (360.0f / kN));
  polyline(p, kN, th, c, true);
}

void triFill(TJBox_Point a, TJBox_Point b, TJBox_Point c, TJBox_Color col)
{
  const TJBox_Point pts[3] = { a, b, c };
  TJBox_PathRun run;
  run.fType = kJBox_PathRun_Line;
  run.fPtCount = 3;
  run.fPts = pts;
  run.fClose = true;
  JBox_DrawPath(nullptr, &run, 1, &kFill, col, nullptr);
}

// The point a fraction u (0..1) of the way along a polyline, by length.
TJBox_Point along(const TJBox_Point* p, int n, float u)
{
  float total = 0.0f;
  for(int i = 1; i < n; ++i) total += std::hypot(p[i].fX - p[i - 1].fX, p[i].fY - p[i - 1].fY);
  float d = clamp01(u) * total;
  for(int i = 1; i < n; ++i)
  {
    const float seg = std::hypot(p[i].fX - p[i - 1].fX, p[i].fY - p[i - 1].fY);
    if(d <= seg && seg > 0.0f)
    {
      const float f = d / seg;
      return { p[i - 1].fX + f * (p[i].fX - p[i - 1].fX), p[i - 1].fY + f * (p[i].fY - p[i - 1].fY) };
    }
    d -= seg;
  }
  return p[n - 1];
}

// `count` dots marching along a lead path: dark holes in the red line.
void flowDots(const TJBox_Point* p, int n, int count, double clk, double speed, float jitter, double t)
{
  const int64_t frame = static_cast<int64_t>(t * 12.0);
  for(int k = 0; k < count; ++k)
  {
    double u = std::fmod(clk * speed + static_cast<double>(k) / count, 1.0);
    if(u < 0.0) u += 1.0;
    TJBox_Point q = along(p, n, static_cast<float>(u));
    if(jitter > 0.0f)                                         // germanium grind
    {
      q.fX += jitter * static_cast<float>(sat::cycleRandom(frame * 16 + k * 2));
      q.fY += jitter * static_cast<float>(sat::cycleRandom(frame * 16 + k * 2 + 1));
    }
    fillCircle(q.fX, q.fY, 2.3f, kWell);
  }
}

// A mini scope: y = f(x01) (in -1..1) drawn over x0..x1 round cy.
template<typename F>
void scope(float x0, float x1, float cy, float amp, float th, TJBox_Color c, F f)
{
  constexpr int kN = 64;
  TJBox_Point pts[kN];
  for(int i = 0; i < kN; ++i)
  {
    const float x01 = static_cast<float>(i) / (kN - 1);
    pts[i] = { x0 + x01 * (x1 - x0), cy - amp * static_cast<float>(f(static_cast<double>(x01))) };
  }
  polyline(pts, kN, th, c);
}

// Diode from anode a to cathode b: leads, a filled triangle pointing at b, the bar.
void diode(float ax, float ay, float bx, float by, float s, TJBox_Color c)
{
  const float dx = bx - ax, dy = by - ay, len = std::hypot(dx, dy);
  if(len < 1.0f) return;
  const float ux = dx / len, uy = dy / len, nx = -uy, ny = ux, h = 0.5f * s;
  const float mx = 0.5f * (ax + bx), my = 0.5f * (ay + by);
  const TJBox_Point apex = { mx + ux * h, my + uy * h };
  line(ax, ay, mx - ux * h, my - uy * h, kSymTh, c);
  line(apex.fX, apex.fY, bx, by, kSymTh, c);
  triFill({ mx - ux * h + nx * h, my - uy * h + ny * h }, { mx - ux * h - nx * h, my - uy * h - ny * h }, apex, c);
  line(apex.fX + nx * h, apex.fY + ny * h, apex.fX - nx * h, apex.fY - ny * h, kSymTh, c);
}

// Bipolar transistor, the BC12 mockup geometry at scale s (s = 1: 80 px ring).
// pnp: emitter arrow pointing IN, current dots run emitter -> collector.
void bjt(float cx, float cy, float s, bool pnp, bool arrow, double clk, double t, float jitter)
{
  ring(cx, cy, 37.0f * s, 6.0f * s, kRed);
  fillRect(cx - 12.0f * s, cy - 25.0f * s, cx - 2.0f * s, cy + 25.0f * s, kRed);   // base bar
  line(cx - 51.0f * s, cy, cx - 12.0f * s, cy, kSymTh * s, kRed);                  // base lead
  line(cx - 4.0f * s, cy - 7.0f * s, cx + 29.0f * s, cy - 40.0f * s, kSymTh * s, kRed);   // collector
  line(cx - 4.0f * s, cy + 7.0f * s, cx + 29.0f * s, cy + 42.0f * s, kSymTh * s, kRed);   // emitter
  if(arrow)
  {
    // arrow head on the emitter, inside the ring: NPN points out, PNP points in
    const float ex = cx + 9.0f * s, ey = cy + 20.8f * s, hs = 13.0f * s;
    const float ux = 0.70710678f * (pnp ? -1.0f : 1.0f), uy = ux;       // along the emitter
    const float nx = 0.70710678f, ny = -0.70710678f;                      // across it
    const float bx = ex - ux * 0.6f * hs, by = ey - uy * 0.6f * hs;
    triFill({ ex + ux * 0.6f * hs, ey + uy * 0.6f * hs },
            { bx + nx * 0.55f * hs, by + ny * 0.55f * hs }, { bx - nx * 0.55f * hs, by - ny * 0.55f * hs }, kRed);
  }
  const TJBox_Point path[4] = { { cx + 29.0f * s, cy - 40.0f * s }, { cx - 3.0f * s, cy - 8.0f * s },
                                { cx - 3.0f * s, cy + 8.0f * s },   { cx + 29.0f * s, cy + 42.0f * s } };
  if(!pnp) flowDots(path, 4, 3, clk, 0.45, jitter, t);
  else
  {
    const TJBox_Point rev[4] = { path[3], path[2], path[1], path[0] };
    flowDots(rev, 4, 3, clk, 0.45, jitter, t);
  }
}

// Dashed grid line of a valve, y fixed, x0..x1.
void grid(float x0, float x1, float y, TJBox_Color c)
{
  for(float x = x0; x < x1 - 2.0f; x += 10.0f)
    fillRect(x, y - 2.0f, std::min(x + 6.0f, x1), y + 2.0f, c);
}

// Electrons: red dots rising from y0 (cathode) to y1 (plate) in lanes xs[].
void electrons(const float* xs, int lanes, float y0, float y1, double clk, double speed)
{
  for(int k = 0; k < lanes; ++k)
  {
    const double u = std::fmod(clk * speed + k * 0.37 + (k & 1) * 0.5, 1.0);
    fillCircle(xs[k], y0 + static_cast<float>(u) * (y1 - y0), 2.0f, kRed);
  }
}

// Valve pieces: glass envelope, a plate (bar + lead up), an indirectly heated
// cathode (bar, bent end, lead down) and the heater V.
void plate(float cx, float y, float w, float th)
{
  fillRect(cx - 0.5f * w, y - th, cx + 0.5f * w, y, kRed);
  line(cx, y - th, cx, kIcY - 44.0f, kSymTh, kRed);
}
void cathode(float cx, float y, float w)
{
  const TJBox_Point p[3] = { { cx - 0.5f * w, y }, { cx + 0.5f * w, y }, { cx + 0.5f * w, y + 6.0f } };
  polyline(p, 3, 4.0f, kRed);
}
void heater(float cx, float y, TJBox_Color c)
{
  const TJBox_Point p[3] = { { cx - 7.0f, y }, { cx, y + 7.0f }, { cx + 7.0f, y } };
  polyline(p, 3, 3.0f, c);
}

// A horizontal/vertical coil of `humps` half-circles, as a point list.
int coilPts(TJBox_Point* out, float x, float y, float len, int humps, bool vertical, bool flip)
{
  constexpr int kSeg = 8;
  const float w = len / humps, r = 0.5f * w;
  int n = 0;
  for(int k = 0; k < humps; ++k)
    for(int j = (k == 0 ? 0 : 1); j <= kSeg; ++j)
    {
      const float a = 3.14159265f * static_cast<float>(j) / kSeg;          // 0..pi
      const float along = (k + 0.5f) * w - r * std::cos(a);
      const float bulge = r * std::sin(a) * (flip ? -1.0f : 1.0f);
      out[n++] = vertical ? TJBox_Point{ x + bulge, y + along } : TJBox_Point{ x + along, y - bulge };
    }
  return n;
}

double sine01(double x01, double cycles, double ph) { return std::sin(6.283185307179586 * (x01 * cycles - ph)); }

void drawModelIcon(int model, double t, double clk, float amount)
{
  const float cx = kIcX, cy = kIcY;
  const double ph = clk * 0.35;                                // scope scroll (cycles)
  const float amt = clamp01(amount);
  switch(model)
  {
    case 0:   // BC12 transistor -- Erik's mockup symbol, current marching through it
      bjt(cx, cy, 1.0f, false, false, clk, t, 0.0f);
      break;

    case 1:   // PV40 op-amp: a small sine in, the same sine bigger out (pure gain)
    {
      const TJBox_Point tri[3] = { { cx - 38, cy - 36 }, { cx + 36, cy }, { cx - 38, cy + 36 } };
      polyline(tri, 3, kSymTh, kRed, true);
      line(cx - 62, cy - 16, cx - 40, cy - 16, kSymTh, kRed);          // - input
      line(cx - 62, cy + 16, cx - 40, cy + 16, kSymTh, kRed);          // + input
      line(cx + 38, cy, cx + 62, cy, kSymTh, kRed);                    // output
      line(cx - 31, cy - 16, cx - 19, cy - 16, 3.5f, kRed);            // "-"
      line(cx - 31, cy + 16, cx - 19, cy + 16, 3.5f, kRed);            // "+"
      line(cx - 25, cy + 10, cx - 25, cy + 22, 3.5f, kRed);
      scope(cx - 125, cx - 70, cy, 7.0f, 3.5f, kRedDim, [&](double x) { return sine01(x, 1.5, ph); });
      scope(cx + 70, cx + 125, cy, 20.0f, 4.0f, kRed, [&](double x) { return sine01(x, 1.5, ph); });
      break;
    }

    case 2:   // TG23 transformer: two coils on a core, the sine passes through
    {
      TJBox_Point c1[48], c2[48];
      const int n1 = coilPts(c1, cx - 20, cy - 28, 56, 4, true, false);
      const int n2 = coilPts(c2, cx + 20, cy - 28, 56, 4, true, true);
      polyline(c1, static_cast<TJBox_UInt32>(n1), 4.5f, kRed);
      polyline(c2, static_cast<TJBox_UInt32>(n2), 4.5f, kRed);
      line(cx - 5, cy - 32, cx - 5, cy + 32, 3.5f, kRed);              // core
      line(cx + 5, cy - 32, cx + 5, cy + 32, 3.5f, kRed);
      line(cx - 44, cy - 28, cx - 20, cy - 28, 4.0f, kRed);
      line(cx - 44, cy + 28, cx - 20, cy + 28, 4.0f, kRed);
      line(cx + 20, cy - 28, cx + 44, cy - 28, 4.0f, kRed);
      line(cx + 20, cy + 28, cx + 44, cy + 28, 4.0f, kRed);
      scope(cx - 125, cx - 55, cy, 14.0f, 4.0f, kRed, [&](double x) { return sine01(x, 1.25, ph); });
      scope(cx + 55, cx + 125, cy, 14.0f, 4.0f, kRed, [&](double x) {
        return std::tanh(1.6 * sine01(x, 1.25, ph - 0.1)) / std::tanh(1.6); });   // gently rounded
      break;
    }

    case 3:   // BV98 inductor: an iron-core coil, current flowing through
    {
      TJBox_Point p[48];
      int n = 0;
      p[n++] = { cx - 80, cy + 10 };
      n += coilPts(p + n, cx - 44, cy + 10, 88, 4, false, false);
      p[n++] = { cx + 80, cy + 10 };
      polyline(p, static_cast<TJBox_UInt32>(n), kSymTh, kRed);
      line(cx - 44, cy - 20, cx + 44, cy - 20, 3.5f, kRed);            // iron core
      line(cx - 44, cy - 28, cx + 44, cy - 28, 3.5f, kRed);
      flowDots(p, n, 4, clk, 0.3, 0.0f, t);
      break;
    }

    case 4:   // WP76 twin triode: two sections in one bottle, heaters glowing
    {
      const float glow = 0.18f + 0.14f * static_cast<float>(0.5 + 0.5 * std::sin(clk * 2.3))
                       + 0.04f * static_cast<float>(sat::cycleRandom(static_cast<int64_t>(t * 10.0)));
      fillCircle(cx, cy + 16, 30, withAlpha(kRed, glow));
      ring(cx, cy, 38, kSymTh, kRed);
      for(int s = 0; s < 2; ++s)
      {
        const float sx = cx + (s ? 15.0f : -15.0f);
        plate(sx, cy - 16, 18, 5);
        grid(sx - 10, sx + 10, cy, kRed);
        cathode(sx - 1, cy + 14, 16);
      }
      heater(cx, cy + 25, kRed);
      break;
    }

    case 5:   // ZX67 JFET (N-channel): channel bar, gate arrow in, current drain -> source
    {
      ring(cx, cy, 37, 6, kRed);
      fillRect(cx - 2, cy - 25, cx + 4, cy + 25, kRed);                // channel
      const TJBox_Point dr[3] = { { cx + 26, cy - 44 }, { cx + 26, cy - 16 }, { cx + 2, cy - 16 } };
      const TJBox_Point so[3] = { { cx + 2, cy + 16 }, { cx + 26, cy + 16 }, { cx + 26, cy + 44 } };
      polyline(dr, 3, kSymTh, kRed);
      polyline(so, 3, kSymTh, kRed);
      line(cx - 51, cy + 16, cx - 3, cy + 16, kSymTh, kRed);            // gate
      triFill({ cx - 3, cy + 16 }, { cx - 16, cy + 9 }, { cx - 16, cy + 23 }, kRed);   // arrow into the channel
      const TJBox_Point path[6] = { dr[0], dr[1], { cx + 1, cy - 16 }, { cx + 1, cy + 16 }, so[1], so[2] };
      flowDots(path, 6, 3, clk, 0.45, 0.0f, t);
      break;
    }

    case 6:   // A1R pentode: plate, three grids, electrons streaming up
    {
      ring(cx, cy, 38, kSymTh, kRed);
      plate(cx, cy - 22, 34, 5);
      grid(cx - 20, cx + 20, cy - 11, kRed);
      grid(cx - 20, cx + 20, cy - 1, kRedDim);
      grid(cx - 20, cx + 20, cy + 9, kRedDim);
      cathode(cx, cy + 20, 30);
      static const float kLanes[4] = { -12, -4, 4, 12 };
      float xs[4];
      for(int k = 0; k < 4; ++k) xs[k] = cx + kLanes[k];
      electrons(xs, 4, cy + 15, cy - 21, clk, 0.9);
      break;
    }

    case 7:   // ES83 triode: plate, grid, cathode, glowing heater, electrons
    {
      const float glow = 0.15f + 0.12f * static_cast<float>(0.5 + 0.5 * std::sin(clk * 1.7));
      fillCircle(cx, cy + 22, 16, withAlpha(kRed, glow));
      ring(cx, cy, 38, kSymTh, kRed);
      plate(cx, cy - 18, 32, 5);
      grid(cx - 19, cx + 19, cy, kRed);
      line(cx - 50, cy, cx - 21, cy, kSymTh, kRed);                     // grid lead
      cathode(cx, cy + 16, 28);
      heater(cx - 6, cy + 25, kRed);
      static const float kLanes[3] = { -9, 1, 11 };
      float xs[3];
      for(int k = 0; k < 3; ++k) xs[k] = cx + kLanes[k];
      electrons(xs, 3, cy + 12, cy - 17, clk, 0.7);
      break;
    }

    case 8:   // FD88 wavefolder: a sine folding back on itself, deeper with Amount
    {
      const double depth = 1.3 + (0.9 + 0.6 * std::sin(clk * 0.9)) * (0.5 + amt);
      line(cx - 110, cy - 32, cx + 110, cy - 32, 2.0f, kRedDim);         // fold thresholds
      line(cx - 110, cy + 32, cx + 110, cy + 32, 2.0f, kRedDim);
      scope(cx - 110, cx + 110, cy, 32.0f, 4.5f, kRed, [&](double x) {
        double v = depth * sine01(x, 2.0, ph) - 1.0;                   // triangle folder, as Saturator.h foldTri
        v = v - 4.0 * std::floor(v * 0.25);
        return std::fabs(v - 2.0) - 1.0; });
      break;
    }

    case 9:   // LF09 IC chip: a DIP package, data blinking on its pins
    {
      const TJBox_Point body[4] = { { cx - 46, cy - 24 }, { cx + 46, cy - 24 }, { cx + 46, cy + 24 }, { cx - 46, cy + 24 } };
      polyline(body, 4, kSymTh, kRed, true);
      TJBox_Point notch[9];
      for(int j = 0; j < 9; ++j) notch[j] = polar(cx - 46, cy, 8, j * 22.5f);   // half circle into the body
      polyline(notch, 9, 3.5f, kRed);
      fillCircle(cx - 33, cy + 12, 3.5f, kRed);                         // pin-1 dot
      const int64_t frame = static_cast<int64_t>(clk * 5.0);
      for(int i = 0; i < 5; ++i)
      {
        const float px = cx - 38 + i * 19.0f;
        const bool top = sat::cycleRandom(frame * 16 + i) > 0.0;
        const bool bot = sat::cycleRandom(frame * 16 + i + 8) > 0.0;
        fillRect(px, cy - 38, px + 8, cy - 27, top ? kRed : kRedDim);
        fillRect(px, cy + 27, px + 8, cy + 38, bot ? kRed : kRedDim);
      }
      break;
    }

    case 10:  // NN18 diode pair: anti-parallel clipper, sine in, clipped sine out
    {
      line(cx - 58, cy - 26, cx + 58, cy - 26, kSymTh, kRed);          // signal rail
      line(cx - 22, cy + 26, cx + 22, cy + 26, kSymTh, kRed);          // ground rail
      diode(cx - 22, cy - 26, cx - 22, cy + 26, 18, kRed);              // down
      diode(cx + 22, cy + 26, cx + 22, cy - 26, 18, kRed);              // up
      line(cx, cy + 26, cx, cy + 34, 3.5f, kRed);                        // ground
      line(cx - 12, cy + 35, cx + 12, cy + 35, 3.5f, kRed);
      line(cx - 7, cy + 40, cx + 7, cy + 40, 3.0f, kRed);
      line(cx - 3, cy + 45, cx + 3, cy + 45, 2.5f, kRed);
      const double g = 1.5 + 2.5 * amt;
      scope(cx - 125, cx - 66, cy, 18.0f, 4.0f, kRed, [&](double x) { return sine01(x, 1.25, ph); });
      scope(cx + 66, cx + 125, cy, 18.0f, 4.0f, kRed, [&](double x) {
        return std::tanh(g * sine01(x, 1.25, ph)) / std::tanh(g) * 0.55; });
      break;
    }

    case 11:  // SB05 rectifier bridge: four diodes, the sine flipped into bumps
    {
      const float dx = cx - 30;
      const TJBox_Point L = { dx - 38, cy }, T = { dx, cy - 38 }, R = { dx + 38, cy }, B = { dx, cy + 38 };
      diode(L.fX, L.fY, T.fX, T.fY, 13, kRed);
      diode(L.fX, L.fY, B.fX, B.fY, 13, kRed);
      diode(T.fX, T.fY, R.fX, R.fY, 13, kRed);
      diode(B.fX, B.fY, R.fX, R.fY, 13, kRed);
      line(cx + 35, cy + 24, cx + 125, cy + 24, 2.0f, kRedDim);         // zero line
      scope(cx + 35, cx + 125, cy + 24, 44.0f, 4.0f, kRed, [&](double x) { return std::fabs(sine01(x, 1.25, ph)); });
      break;
    }

    case 12:  // FF37 fuzz pair: two transistors in cascade
    {
      bjt(cx - 38, cy, 0.62f, false, false, clk, t, 0.0f);
      bjt(cx + 38, cy, 0.62f, false, false, clk + 0.33, t, 0.0f);
      const TJBox_Point wire[4] = { { cx - 38 + 18.0f, cy + 26.0f }, { cx - 8, cy + 26.0f },
                                    { cx - 8, cy }, { cx + 38 - 31.6f, cy } };
      polyline(wire, 4, 3.5f, kRed);
      break;
    }

    case 13:  // GR1T germanium PNP: arrow in, current running backwards and gritty
      bjt(cx, cy, 1.0f, true, true, clk, t, 1.0f + 1.5f * amt);
      break;

    case 14:  // F34K silicon fuzz: a sine slammed into a near-square
    {
      const double g = 2.0 + (2.5 + 1.5 * std::sin(clk * 0.8)) * (0.4 + amt);
      scope(cx - 110, cx + 110, cy, 30.0f, 2.5f, kRedDim, [&](double x) { return sine01(x, 2.0, ph); });
      scope(cx - 110, cx + 110, cy, 30.0f, 4.5f, kRed, [&](double x) {
        const double v = g * sine01(x, 2.0, ph);
        return v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : v); });
      break;
    }

    default:  // X7R3 power tube: beam plates, red-hot plate, electrons
    {
      const float heat = 0.22f + 0.2f * static_cast<float>(0.5 + 0.5 * std::sin(clk * 3.1))
                       + 0.08f * static_cast<float>(sat::cycleRandom(static_cast<int64_t>(t * 14.0)));
      fillCircle(cx, cy - 22, 24, withAlpha(kRed, heat));               // plate glow
      ring(cx, cy, 40, kSymTh, kRed);
      plate(cx, cy - 20, 40, 8);
      grid(cx - 18, cx + 18, cy - 5, kRed);
      grid(cx - 18, cx + 18, cy + 5, kRedDim);
      line(cx - 27, cy - 8, cx - 18, cy + 12, 3.5f, kRedDim);           // beam-forming plates
      line(cx + 27, cy - 8, cx + 18, cy + 12, 3.5f, kRedDim);
      cathode(cx, cy + 20, 30);
      static const float kLanes[5] = { -12, -6, 0, 6, 12 };
      float xs[5];
      for(int k = 0; k < 5; ++k) xs[k] = cx + kLanes[k];
      electrons(xs, 5, cy + 15, cy - 19, clk, 1.3);
      break;
    }
  }
}

// ---- drawing sections -------------------------------------------------------
void drawCategories(int cat, int listCat)
{
  if(listCat >= 0) cat = listCat;                             // an open list lights its own category
  for(int i = 0; i < 4; ++i)
  {
    fillRect(kCatL, kRowT[i], kCatR, kRowT[i] + kRowH, (i == cat) ? kCellOn : kCellOff);
    // Erik (2026-09-23): with the smaller font the category labels sat 1 px HIGH,
    // so this row drops kLabelNudgeY (1 host px = 5 display units).
    text(kCatL, kRowT[i] - 5, kCatR, kRowT[i] + kRowH + 5, sat::kCatName[i], kFontCat,
             kJBox_HAlign_Center, (i == cat) ? kRed : kRedDim);
  }
}

void drawModeBox(const UiState& S, int model, int listCat, double t, double clk, float amount)
{
  if(listCat >= 0)
  {
    // listCat's models, one per row, in the mode box. The current model (if it
    // is in this category) is #7A0F0F; the row under the pointer gets a tint.
    fillRect(kListL, kListT, kListR, kListB, kPopBg);
    const int n = sat::kCatCount[listCat];
    const float rowH = listRowH(listCat);
    for(int i = 0; i < n; ++i)
    {
      const int m = sat::kCatFirst[listCat] + i;
      const float rt = kListT + 5 + i * rowH;
      if(m == model) fillRect(kListL + 5, rt, kListR - 5, rt + rowH - 2, kPopSel);
      if(hovered(S, kListL, rt, kListR, rt + rowH)) fillRect(kListL + 5, rt, kListR - 5, rt + rowH - 2, kCell30);
      // the selected row's text in the dark popup colour, on its #7A0F0F bar:
      // lit ink on that bar was too low-contrast to read (Erik, Degrader)
      const TJBox_Color ink = (m == model) ? kPopBg : kRed;
      text(kListNameL, rt, kListDescL - 10, rt + rowH - 2, sat::kModelName[m],
           kJBox_Font_SmallLabel, kJBox_HAlign_Left, ink);
      text(kListDescL, rt, kListR - 10, rt + rowH - 2, sat::kModelDesc[m],
           kJBox_Font_ArialSmall, kJBox_HAlign_Left, ink);
    }
    return;
  }
  fillRect(kBoxL, kBoxT, kBoxR, kBoxB, kCellOff);
  fillRect(kInL, kInT, kInR, kInB, kCell15);
  text(kInL, kInT + 6, kInR, kInT + 66, sat::kModelName[model],
       kFontName, kJBox_HAlign_Center, kRedDim);
  // Motion runs on the DSP's animation clock (UI_AnimClock): it integrates a
  // slewed Amount-dependent speed, so turning Amount changes the SPEED smoothly
  // instead of jumping the phase (t * speed jumped wildly), and the icons keep
  // moving at Amount 0. Random re-rolls ("frames") still run on real time t.
  drawModelIcon(model, t, clk, amount);
  text(kInL + 2, kInB - 58, kInR, kInB + 2, sat::kModelDesc[model],
       kFontLabel, kJBox_HAlign_Center, kRedDim);
}

void drawSquare(float l, float t, bool on)
{
  fillRect(l, t, l + 40, t + 40, kRedDim);
  if(on) fillRect(l + 10, t + 10, l + 30, t + 30, kRed);
}

void drawLfoList(int view, bool lfoOn)
{
  static const char* const kRowName[4] = { "LFO", "amount", "tone", "style" };
  for(int i = 0; i < 4; ++i)
  {
    // a selected destination row is a tab: it runs on into its page (drawn by
    // drawLfoPanel from kPageL), so no gap on its right.
    // LFO off: no row is lit, header included -- the panel and its page are
    // hidden (Erik, 2026-09-22). UI_LfoView keeps its value, so switching the
    // LFO back on returns to the same page.
    const bool sel = (i == view) && lfoOn;
    // A selected tab overlaps its page by 3 units: kLfoR (838) is not on the host
    // pixel grid (167.6 px), so two edges meeting there left a hairline seam.
    const float r = (sel && i > 0) ? kLfoR + 3 : kLfoR;
    fillRect(kLfoL, kRowT[i], r, kRowT[i] + kRowH, sel ? kCellOn : kCellOff);
    if(i == 0)
      textBold(kLfoL, kRowT[i] - 5 - kLabelNudgeY, kLfoR, kRowT[i] + kRowH + 5 - kLabelNudgeY, kRowName[i], kJBox_HAlign_Center, lfoOn ? kRed : kRedDim);
    else
      text(kLfoTxtL, kRowT[i] - 5 - kLabelNudgeY, kLfoR, kRowT[i] + kRowH + 5 - kLabelNudgeY, kRowName[i],
           kFontLabel, kJBox_HAlign_Left, sel ? kRed : kRedDim);
  }
  drawSquare(kOnSqL, kOnSqT, lfoOn);
}

// One LFO cycle (S&H / drift: 8, so the randomness shows) as a polyline in the
// box l..r x top..bot. Shared by the wave area and the shape popup.
void drawShapeLine(int shape, float l, float top, float r, float bot, float thick, TJBox_Color ink)
{
  const float mid = 0.5f * (top + bot), amp = 0.5f * (bot - top);
  const bool random = (shape == sat::kLfoSH || shape == sat::kLfoDrift);
  const double cycles = random ? 8.0 : 1.0;
  constexpr int kN = 97;
  TJBox_Point pts[kN];
  for(int i = 0; i < kN; ++i)
  {
    // just short of 1.0 at the end, so square and saw draw their last segment
    const double x = (i == kN - 1) ? 0.99999 : i / double(kN - 1);
    const double y = sat::lfoValue(shape, x * cycles);
    pts[i] = { l + static_cast<float>(x) * (r - l), mid - static_cast<float>(y) * amp };
  }
  polyline(pts, kN, thick, ink);
}

void drawWaveform(int shape, float phase, bool lfoOn)
{
  const TJBox_Color ink = lfoOn ? kRed : kRedDim;
  // The mockup's triangle: a 261 x 120 box at (1265, 138) with a 5 px line, so
  // the line centre runs 2.5 px inside it.
  const float l = kWaveL + 22.5f, r = kWaveL + 278.5f, top = kWaveT + 20.5f, bot = kWaveT + 135.5f;
  const float mid = 0.5f * (top + bot), amp = 0.5f * (bot - top);
  const bool random = (shape == sat::kLfoSH || shape == sat::kLfoDrift);
  drawShapeLine(shape, l, top, r, bot, 5, ink);
  if(lfoOn && !random)
  {
    const float px = l + clamp01(phase) * (r - l);
    const float py = mid - static_cast<float>(sat::lfoValue(shape, phase)) * amp;
    fillCircle(px, py, 9, kRed);
    fillCircle(px, py, 4, kCellOn);
  }
  // No shape name (not in the mockup); the drawn bottom-left tooltip over the
  // wave area names it (drawTip, key 2 -- LFO_Shape has no PropertyBox).
}

void fillTriangle(TJBox_Point a, TJBox_Point b, TJBox_Point c, TJBox_Color col)
{
  const TJBox_Point pts[3] = { a, b, c };
  TJBox_PathRun run;
  run.fType = kJBox_PathRun_Line;
  run.fPtCount = 3;
  run.fPts = pts;
  run.fClose = true;
  JBox_DrawPath(nullptr, &run, 1, &kFill, col, nullptr);
}

// Up/down step arrows filling top..bot: an up triangle at the top, a down
// triangle at the bottom, each kArrH tall.
void drawStepArrows(float l, float r, float top, float bot, TJBox_Color ink)
{
  const float cx = 0.5f * (l + r);
  fillTriangle({ l, top + kArrH }, { r, top + kArrH }, { cx, top }, ink);             // up
  fillTriangle({ l, bot - kArrH }, { r, bot - kArrH }, { cx, bot }, ink);             // down
}

void drawDestPage(const TJBox_Value* v, int d, bool lfoOn)
{
  const TJBox_Color ink = lfoOn ? kRed : kRedDim;
  static const char* const kName[2] = { "depth", "phase" };
  char buf[2][24];
  tfmt::format(buf[0], sizeof buf[0], "%+d %%",
                static_cast<int>(std::floor(sat::depthBipolar(num(v, kV_DepthAmt + d, 0.5f)) * 100.0 + 0.5)));
  tfmt::format(buf[1], sizeof buf[1], "%d deg",
                static_cast<int>(std::floor(num(v, kV_PhaseAmt + d, 0.0f) * 360.0 + 0.5)));
  for(int w = 0; w < 2; ++w)
  {
    const float t = kFieldT[w], b = t + kFieldH;
    text(kDestLabelL, t - 10 - kLabelNudgeY, kFieldL - 10, b + 10 - kLabelNudgeY, kName[w], kFontLabel, kJBox_HAlign_Left, ink);
    fillRect(kFieldL + kFieldInset, t + kFieldInset, kFieldR - kFieldInset, b - kFieldInset, kKnobIn);   // black
    text(kFieldL + 10, t - 5, kFieldR - 12, b + 5, buf[w], kFontSmall, kJBox_HAlign_Right, ink);
    drawStepArrows(kArrL, kArrR, t + kFieldInset, b - kFieldInset, ink);
  }
}

void drawLfoPanel(const TJBox_Value* v, int view, bool lfoOn)
{
  fillRect(kPanL, kPanLowT, kPanR, kPanB, kCellOff);            // lower panel (black gap above it, like the rows)
  fillRect(kPanL, kPanT, kPanR, kBarB, kCellOn);                // top bar (rate + sync)
  if(view == 0) fillRect(kWaveL, kWaveT, kWaveR, kWaveB, kCellOn);   // wave area
  else          fillRect(kPageL, kPageT, kPageR, kPageB, kCellOn);   // destination page, joined to its tab
  const bool sync = inum(v, kV_LfoSync, 1) != 0;
  char buf[24];
  if(sync)
  {
    int di = inum(v, kV_LfoRateSync, kRateSyncDefault);
    if(di < 0) di = 0; else if(di > sat::kNumDivs - 1) di = sat::kNumDivs - 1;
    tfmt::format(buf, sizeof buf, "%s", sat::kDivName[di]);
  }
  else
  {
    const double hz = sat::freeHz(num(v, kV_LfoRate, kRateDefault));
    // At most 3 characters of number (Erik, 2026-09-27: "45.7 Hz" ran into the
    // step arrows): 10..60 Hz whole ("45 Hz"), 1..10 one decimal ("4.5 Hz"),
    // below 1 Hz two decimals without the leading zero (".35 Hz", ".05 Hz").
    // Rounded first, so 9.96 reads "10 Hz", not "10.0 Hz".
    const double h1 = std::floor(hz * 10.0 + 0.5) / 10.0, h2 = std::floor(hz * 100.0 + 0.5) / 100.0;
    if(h1 >= 10.0)     tfmt::format(buf, sizeof buf, "%.0f Hz", std::floor(hz + 0.5));
    else if(h2 >= 1.0) tfmt::format(buf, sizeof buf, "%.1f Hz", h1);
    else
    {
      char tmp[16];
      tfmt::format(tmp, sizeof tmp, "%.2f", h2);                // "0.35"
      tfmt::format(buf, sizeof buf, "%s Hz", tmp + 1);           // ".35"
    }
  }
  // the rate value sat 1 host px too high with the label nudge: no nudge here
  text(kRateL, kPanT - 10, kRateR, kBarB + 2, buf, kFontLabel, kJBox_HAlign_Right, kRed);
  drawStepArrows(kRateArrL, kRateArrR, kPanT + kFieldInset, kBarB - kFieldInset, kRed);
  text(kSyncTxtL, kPanT - 10 - kLabelNudgeY, kSyncTxtR, kBarB + 2 - kLabelNudgeY, "sync", kFontLabel, kJBox_HAlign_Left, kRed);
  drawSquare(kSyncSqL, kSyncSqT, sync);

  int shape = inum(v, kV_LfoShape, sat::kLfoTri);
  if(shape < 0) shape = 0; else if(shape > sat::kNumLfoShapes - 1) shape = sat::kNumLfoShapes - 1;
  if(view == 0) drawWaveform(shape, num(v, kV_PhaseOut, 0.0f), lfoOn);
  else          drawDestPage(v, view - 1, lfoOn);
}

// IN / OUT bars: trough + fill from the bottom, label above like a knob's.
void drawLevelBars(const TJBox_Value* v, bool lfoOn)
{
  static const char* const kBarLabel[2] = { "in", "out" };
  for(int b = 0; b < 2; ++b)
  {
    const float cx = lvlBarCX(b, lfoOn);
    const float l = cx - 0.5f * kLvlBarW, r = cx + 0.5f * kLvlBarW;
    const float val = clamp01(num(v, kV_InLevel + b, 0.5f));
    fillRect(l, kLvlBarT, r, kLvlBarB, kCellOn);
    const float fillT = kLvlBarB - val * (kLvlBarB - kLvlBarT);
    if(fillT < kLvlBarB) fillRect(l, fillT, r, kLvlBarB, kRed);
    // 0 dB mark (Erik, 2026-09-26): a 1 x 1 host px square each side of the trough,
    // 1 host px out from it (1 host px = 5 units), centred on 0 dB = value 0.5.
    const float y0 = kLvlBarB - 0.5f * (kLvlBarB - kLvlBarT);
    fillRect(l - 10.0f, y0 - 2.5f, l - 5.0f, y0 + 2.5f, kRed);
    fillRect(r + 5.0f,  y0 - 2.5f, r + 10.0f, y0 + 2.5f, kRed);
    text(cx - 110, kKnobLabelT, cx + 110, kKnobLabelB, kBarLabel[b], kFontLabel, kJBox_HAlign_Center, kRed);
  }
}

// ---- value readout in the label row (Erik, 2026-09-27) ----------------------
// When in / amount / tone / style / out change -- from the panel, automation,
// Remote or a patch -- the DSP holds that control's bit in UI_EditMask for
// 1.5 s (Device::updateEditMask). While any bit is set, the label row is
// covered with black, from the LFO section's edge to the patch area, so
// nothing spills over the LFO panel (it did, with the value in the "in" label):
//   one control    "input: -12dB" / "style: +10%", centred on the knob row
//   several        each control's value in its own column, no names
//                  ("+12dB  +10%  0  -40%  0dB"); the ones not changing dim.
// Units as the host's (motherboard): dB -18..+18, amount 0..100 %, tone /
// style -100..+100 %.
void fmtValue(const TJBox_Value* v, int i, bool withUnitAtZero, char* out, size_t n)
{
  if(i == 0 || i == 4)                                          // in / out, dB
  {
    const float db = std::floor((2.0f * clamp01(num(v, kV_InLevel + (i == 0 ? 0 : 1), 0.5f)) - 1.0f) * 180.0f + 0.5f) / 10.0f;
    if(std::fabs(db) < 0.05f)                tfmt::format(out, n, "0dB");
    else if(std::fabs(db - std::floor(db + 0.5f)) < 0.01f) tfmt::format(out, n, "%+ddB", static_cast<int>(std::floor(db + 0.5f)));
    else                                     tfmt::format(out, n, "%+.1fdB", db);
  }
  else if(i == 1)                                               // amount 0..100 %
  {
    const float pc = std::floor(clamp01(num(v, kV_Amount, kKnobDefault[0])) * 1000.0f + 0.5f) / 10.0f;
    if(std::fabs(pc - std::floor(pc + 0.5f)) < 0.01f) tfmt::format(out, n, "%d%%", static_cast<int>(std::floor(pc + 0.5f)));
    else                                               tfmt::format(out, n, "%.1f%%", pc);
  }
  else                                                          // tone / style, bipolar %
  {
    const int pct = static_cast<int>(std::floor((clamp01(num(v, kV_Amount + i - 1, 0.5f)) - 0.5f) * 200.0f + 0.5f));
    if(pct == 0) tfmt::format(out, n, withUnitAtZero ? "0%%" : "0");
    else         tfmt::format(out, n, "%+d%%", pct);
  }
}

void drawEditReadout(const TJBox_Value* v, bool lfoOn)
{
  const int mask = static_cast<int>(std::floor(clamp01(num(v, kV_EditMask, 0.0f)) * 31.0f + 0.5f));
  if(mask == 0) return;
  const float L = (lfoOn ? kLfoEdgeOn : kLfoEdgeOff) + 5.0f, R = kPatchL - 5.0f;
  fillRect(L, 0.0f, R, kLvlBarT - 2.0f, kKnobIn);               // black over the five labels
  static const char* const kName[5] = { "input", "amount", "tone", "style", "output" };
  int count = 0, only = 0;
  for(int i = 0; i < 5; ++i) if(mask & (1 << i)) { ++count; only = i; }
  char val[24];
  if(count == 1)
  {
    char line[48];
    fmtValue(v, only, true, val, sizeof val);
    tfmt::format(line, sizeof line, "%s: %s", kName[only], val);
    text(L, kKnobLabelT, R, kKnobLabelB, line, kFontLabel, kJBox_HAlign_Center, kRed);
    return;
  }
  for(int i = 0; i < 5; ++i)
  {
    const float cx = (i == 0) ? lvlBarCX(0, lfoOn) : (i == 4) ? lvlBarCX(1, lfoOn) : knobCX(i - 1, lfoOn);
    float l = cx - 90.0f, r = cx + 90.0f;                       // a column; kept inside L..R
    if(l < L) { r += L - l; l = L; }
    if(r > R) { l -= r - R; r = R; }
    fmtValue(v, i, false, val, sizeof val);
    text(l, kKnobLabelT, r, kKnobLabelB, val, kFontLabel, kJBox_HAlign_Center, (mask & (1 << i)) ? kRed : kRedDim);
  }
}

void drawKnobs(const TJBox_Value* v, bool lfoOn)
{
  static const char* const kLabel[3] = { "amount", "tone", "style" };
  const int cvMask = static_cast<int>(std::floor(clamp01(num(v, kV_CvMask, 0.0f)) * 7.0f + 0.5f));
  for(int k = 0; k < 3; ++k)
  {
    const float cx = knobCX(k, lfoOn), cy = kKnobCY;
    const float val = clamp01(num(v, kV_Amount + k, kKnobDefault[k]));
    text(cx - 110, kKnobLabelT, cx + 110, kKnobLabelB, kLabel[k], kFontLabel, kJBox_HAlign_Center, kRed);

    // LFO reach: an arc just outside the ring, from the lowest to the highest
    // value the LFO can push the knob to, and a dot at the live value.
    const float depth = static_cast<float>(sat::depthBipolar(num(v, kV_DepthAmt + k, 0.5f)));
    // CV in (2026-10-01): the dot is the live value -- knob + CV + LFO -- so it also
    // shows with only a CV moving the knob; the reach arc stays LFO-only.
    const bool cvOn  = (cvMask >> k) & 1;
    const bool lfoMv = lfoOn && std::fabs(depth) > 0.005f;
    if(lfoMv)
    {
      const float lo = clamp01(val - 0.5f * std::fabs(depth)), hi = clamp01(val + 0.5f * std::fabs(depth));
      constexpr int kN = 40;
      TJBox_Point arc[kN];
      for(int i = 0; i < kN; ++i)
        arc[i] = polar(cx, cy, kModR, knobDeg(lo + (hi - lo) * i / float(kN - 1)));
      polyline(arc, kN, kModThick, kRedDim);
    }
    if(lfoMv || cvOn)
    {
      const TJBox_Point dot = polar(cx, cy, kModR, knobDeg(num(v, kV_ModAmtOut + k, val)));
      fillCircle(dot.fX, dot.fY, kModDotR, kRed);
    }

    fillCircle(cx, cy, kKnobR, kRed);
    fillCircle(cx, cy, kKnobRIn, kKnobIn);
    fillRotRect(cx - 0.5f * kNotchW, cy - kKnobR - 1, cx + 0.5f * kNotchW, cy - kKnobR + kNotchH,
                cx, cy, knobDeg(val), kKnobIn);
    if(cvOn)                                          // a cable is in this knob's CV jack
      text(cx - kKnobRIn, cy - kKnobRIn, cx + kKnobRIn, cy + kKnobRIn, "cv", kFontSmall, kJBox_HAlign_Center, kRedDim);
  }
}

// LFO shape popup, over the knob area. Current shape highlighted.
void drawShapePopup(const UiState& S, int shape)
{
  fillRect(kShpL, kShpT, kShpR, kShpB, kPopBg);
  const float cw = (kShpR - kShpL) / kShpCols, ch = (kShpB - kShpT) / kShpRows;
  for(int i = 0; i < sat::kNumLfoShapes; ++i)
  {
    const float l = kShpL + (i % kShpCols) * cw, t = kShpT + (i / kShpCols) * ch;
    const float g = kShpGap * 0.5f;
    if(i == shape) fillRect(l + g, t + g, l + cw - g, t + ch - g, kPopSel);
    if(hovered(S, l, t, l + cw, t + ch)) fillRect(l + g, t + g, l + cw - g, t + ch - g, kCell30);
    drawShapeLine(i, l + 30, t + 18, l + cw - 30, t + ch - 45, 4, kRed);
    text(l, t + ch - 50, l + cw, t + ch - 2, kShapePopName[i], kFontSmall, kJBox_HAlign_Center, kRed);
  }
}

// The patch area ("Computer Controlled", the name well, "Patch Browser") is
// all panel art now (Erik, 2026-09-21). The display draws nothing there.

// Write a document_owner value: the host makes it an undo step named by
// gestureKey (a texts.lua key -- required for document_owner writes).
void setNum(TJBox_GestureArgs* a, TJBox_UInt32 i, double v, const char* gestureKey)
{
  a->fParams[i] = JBox_MakeNumber(v);
  a->fGestureNameKey = gestureKey;
}

// Write a gui_owner value (UI_LfoView / UI_ModeList): no undo step, no name.
void setGui(TJBox_GestureArgs* a, TJBox_UInt32 i, double v)
{
  a->fParams[i] = JBox_MakeNumber(v);
}

// Centre detent for the bipolar knobs (Tone, Character), Erik 2026-09-22.
// The drag runs in a "virtual" range that is kDetent longer than the knob's
// 0..1, with a flat stretch at the centre: dragging onto 0 % holds the knob
// there for kDetent of extra travel, so it snaps to 0 % and takes a slightly
// longer drag to leave it. A knob resting at 0 % starts in the middle of the
// stretch (half the detent either way). kDetent is in knob units: 0.06 = the
// travel that would otherwise move the knob 6 %.
constexpr float kDetent = 0.06f;
float detentToDrag(float v)                                   // knob value -> virtual position
{
  if(std::fabs(v - 0.5f) < 1.0e-4f) return 0.5f + 0.5f * kDetent;
  return (v < 0.5f) ? v : v + kDetent;
}
float detentFromDrag(float u)                                 // virtual position -> knob value
{
  if(u < 0.0f) u = 0.0f; else if(u > 1.0f + kDetent) u = 1.0f + kDetent;
  if(u < 0.5f) return u;
  if(u <= 0.5f + kDetent) return 0.5f;
  return u - kDetent;
}

// Standard Reason drag response (the SDK knob's formula): vertical, Shift =
// fine, follows the user's Mouse Knob Range preference.
float dragFactorY(const TJBox_GestureArgs* a)
{ return a->fCoordToValueFactor.fY * a->fCoordToValueModifiersFactor * a->fCoordToValueKnobRangeFactor; }

// Tooltip text width in EMs: Arial's advance widths (1/1000 em), since nothing
// in the API measures a string. Verbatim from ES20 PatchBayDisplay.cpp.
float labelEmWidth(const char* s)
{
  // ASCII 32..126.
  static const short kAdv[95] = {
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,   //  !"#$%&'()*+,-./
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556,   // 0-9:;<=>?
    1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,  // @A-O
    667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556,   // P-Z[\]^_
    333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,   // `a-o
    556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584         // p-z{|}~
  };
  float w = 0.0f;
  for(const char* p = s; *p != '\0'; ++p)
  {
    const unsigned char c = static_cast<unsigned char>(*p);
    w += (c >= 32 && c < 127) ? kAdv[c - 32] : 556;
  }
  return w * 0.001f;
}

// The host-look tooltip: cream box, black ArialMedium, hard shadow. Every size
// constant is ES20's drawTooltip, measured off the host's own tooltip. ES20's
// display unit is 1 HOST px; this display's is 1 panel px = 1/5 host px, so the
// sizes are scaled x5 (kHost) and the text uses kJBox_Transform_Host (a NULL
// transform shrank the font to 1/5 here). Fixed 2026-09-27 after Reason showed
// a tiny box and tiny text.
//
// PLACEMENT, the only change: BOTTOM-LEFT -- the box's top-right corner just
// outside the control's bottom-left corner (ES20/the host: bottom-right).
// No room on the left: fold to the right. No room below: fold above. Then
// clamp. (cx, cy) = control centre, (halfW, halfH) = its half-extents.
void drawTooltip(float cx, float cy, float halfW, float halfH, const char* label)
{
  // ES20's numbers are in HOST px (its display unit is 1 host px). Here 1 unit =
  // 1 panel px = 1/5 host px, so every size is x kHost (Erik, 2026-09-27: the
  // box and text came out 1/5 size in Reason).
  constexpr float kHost     = 5.0f;
  constexpr float kEmUnits  = 11.4f  * kHost;
  constexpr float kPadX     = 4.0f   * kHost;
  constexpr float kMinW     = 30.0f  * kHost;
  constexpr float kGap      = 2.0f   * kHost;
  constexpr float kTextH    = 17.4f  * kHost;
  constexpr float kInkCap   = 3.55f  * kHost;
  constexpr float kInkBase  = 11.53f * kHost;
  constexpr float kInkDesc  = 2.34f  * kHost;
  constexpr float kPadTop   = 5.0f   * kHost;
  constexpr float kPadBot   = 1.6f   * kHost;
  constexpr float kInkNudge = 1.0f   * kHost;
  constexpr float kShadow   = 1.1f   * kHost;
  const float h = (kInkBase + kInkDesc + kPadBot) - (kInkCap - kPadTop);   // 16.92 host px
  const float w = std::max(kMinW, labelEmWidth(label) * kEmUnits + 2.0f * kPadX);

  float left = cx - halfW - kGap - w;
  float top  = cy + halfH + kGap;
  if(left < kHost)                  left = cx + halfW + kGap;
  if(left + w > kDisplayW - kHost)  left = kDisplayW - kHost - w;
  if(left < kHost)                  left = kHost;
  if(top + h > kDisplayH - kHost)   top  = cy - halfH - kGap - h;
  if(top < kHost)                   top  = kHost;

  fillRect(left + kShadow, top + kShadow, left + w + kShadow, top + h + kShadow, rgba(0, 0, 0, 190));   // shadow
  fillRect(left, top, left + w, top + h, rgba(241, 238, 186, 255));                          // host cream
  const float textTop = top - (kInkCap - kPadTop) - kInkNudge;
  TJBox_Text tx;
  tx.fRect   = { left, textTop, left + w, textTop + kTextH };
  tx.fHAlign = kJBox_HAlign_Center;
  tx.fText   = label;
  tx.fFont   = kJBox_Font_ArialMedium;
  // Host transform (as every other text here): a NULL transform scales the FONT
  // with the display, i.e. to 1/5 size on this 1-unit = 1-panel-px display.
  JBox_DrawText(&tx, rgba(0, 0, 0, 255), kJBox_Transform_Host);
}

// Shape names as the host would print them (the LFO_Shape ui_selector in
// texts.lua); sat::kLfoShapeName is the popup's lowercase set.
const char* const kShapeTipName[sat::kNumLfoShapes] = {
  "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H", "Drift",
};

// The label for S.tipKey, in the host's "name: value" form. Drawn LAST.
void drawTip(const UiState& S, const TJBox_Value* v, bool lfoOn, int view)
{
  char buf[48];
  // A row of the open model list: "Model: WP76", at that row.
  if(S.tipKey >= 10)
  {
    const int listCat = popCat(popupOf(v));
    const int row = S.tipKey - 10;
    if(listCat < 0 || row >= sat::kCatCount[listCat]) return;           // list closed meanwhile
    const float rowH = listRowH(listCat), rt = kListT + 5 + row * rowH;
    tfmt::format(buf, sizeof buf, "Model: %s", sat::kModelName[sat::kCatFirst[listCat] + row]);
    drawTooltip(0.5f * (kListL + kListR), rt + 0.5f * rowH, 0.5f * (kListR - kListL), 0.5f * rowH, buf);
    return;
  }
  // A category: just its name, a hint that a click opens its model list (Erik,
  // 2026-09-27). The categories sit at the display's left edge, so drawTooltip
  // folds the box to their right.
  if(S.tipKey >= 6 && S.tipKey <= 9)
  {
    const int c = S.tipKey - 6;
    drawTooltip(0.5f * (kCatL + kCatR), kRowT[c] + 0.5f * kRowH, 0.5f * (kCatR - kCatL), 0.5f * kRowH, sat::kCatName[c]);
    return;
  }
  if(S.tipKey == 1)
    drawTooltip(kOnSqL + 20, kOnSqT + 20, 20, 20, (inum(v, kV_LfoOn, 0) != 0) ? "LFO: On" : "LFO: Off");
  else if(S.tipKey == 2)
  {
    if(!(lfoOn && view == 0)) return;                            // wave area not on screen
    int shape = inum(v, kV_LfoShape, sat::kLfoTri);
    if(shape < 0) shape = 0; else if(shape > sat::kNumLfoShapes - 1) shape = sat::kNumLfoShapes - 1;
    tfmt::format(buf, sizeof buf, "LFO Shape: %s", kShapeTipName[shape]);
    drawTooltip(0.5f * (kWaveL + kWaveR), 0.5f * (kWaveT + kWaveB), 0.5f * (kWaveR - kWaveL), 0.5f * (kWaveB - kWaveT), buf);
  }
  else if(S.tipKey >= 3 && S.tipKey <= 5)
  {
    static const char* const kDestName[3] = { "LFO > Amount", "LFO > Tone", "LFO > Style" };
    const int d = S.tipKey - 3;
    const int pct = static_cast<int>(std::floor(sat::depthBipolar(num(v, kV_DepthAmt + d, 0.5f)) * 100.0 + 0.5));
    tfmt::format(buf, sizeof buf, "%s: %d%%", kDestName[d], pct);
    drawTooltip(0.5f * (kLfoL + kLfoR), kRowT[d + 1] + 0.5f * kRowH, 0.5f * (kLfoR - kLfoL), 0.5f * kRowH, buf);
  }
}

} // namespace

// =============================================================================
// Callbacks
// =============================================================================
void MainDisplay_DisplaySetup(const TJBox_DisplayArgs* iArgs)
{
  if(!isMainDisplay(iArgs->fDisplayInfo.fID)) return;

  // 50 ms tick for the pictogram animation (only while the panel is visible).
  // Everything else redraws when its bound value changes.
  JBox_SetNotifyRequest(kJBox_NotifyPeriodical, kJBox_Periodical50MS);
  // Pointer enter / move / exit, for the popup hover highlight.
  JBox_SetNotifyRequest(kJBox_NotifyPointing, 1);

  // PropertyBoxes: automation rectangle, alt-click lane, Remote menu and the
  // tooltip, served by the host. They follow what is ON SCREEN: the SDK's
  // BigDisplay example notes that DisplaySetup runs before each Draw (Reason
  // 14; not a documented guarantee), so every state change re-lays them out.
  //   - knobs at their current x (they move when the LFO is off); none while
  //     the shape popup covers them
  //   - LFO panel boxes only while the LFO is on; the waveform has NO box
  //     (Erik: LFO Shape's tooltip slipped through over the depth/phase page)
  //   - a destination page: its own depth and phase rows
  // Sized with headroom (at most 11 used). It was [10] once, and box 11 overran
  // into the info struct: "TJBox_PropertyBox::fFlags: Unknown flag set".
  const bool ok    = iArgs->fDisplayInfo.fParamCount >= kV_Count;
  const TJBox_Value* v = ok ? iArgs->fParams : nullptr;
  UiState scratch;
  UiState& S = uiState(v, iArgs->fDisplayInfo.fParamCount, scratch);
  if(&S == &scratch)                                         // no state yet: no boxes (the host may keep the pointer)
  {
    TJBox_PropertyInfo none;
    none.fBoxes = nullptr;
    none.fCount = 0;
    JBox_SetPropertyInfo(&none);
    return;
  }
  const bool lfoOn = ok && inum(v, kV_LfoOn, 0) != 0;
  int view = ok ? inum(v, kV_LfoView, 0) : 0;
  if(view < 0 || view > 3) view = 0;
  int n = 0;
  auto box = [&](TJBox_UInt32 idx, float l, float t, float r, float b) {
    if(n >= UiState::kMaxBoxes) return;                               // never write past the array
    S.box[n].fPropertyIndex = idx;
    S.box[n].fPropertyRef   = kJBox_InvalidPropertyRef;
    S.box[n].fRect          = { l, t, r, b };
    S.box[n].fFlags         = 0;     // never DisableTooltip: it disables the device in Reason 14.1 (ES20)
    ++n;
  };
  // Knobs, IN / OUT bars and the LFO rate: NO box here since 2026-09-27 -- each
  // has its own overlay display on top (ui/CtlOverlay.cpp) with the box, so
  // alt-click opens the automation lane (it never did from this display).
  // The Model box only while the list is closed: over the open list its host
  // tooltip showed one static label; the rows have drawn tooltips instead.
  if(!ok || popCat(popupOf(v)) < 0) box(kV_Model,  kBoxL, kBoxT, kBoxR, kBoxB);
  // LFO_On and the destination rows: NO box (2026-09-22) -- their tooltip is
  // drawn bottom-left by the display (drawTip), and a box would pop the host's
  // own on top of it. They lose the on-panel automation ring, alt-click lane
  // and Remote menu; a destination's depth keeps its box on its open page.
  if(lfoOn)
  {
    box(kV_LfoSync, kSyncSqL, kSyncSqT, kSyncSqL + 40, kSyncSqT + 40);
    if(view > 0)
    {
      const int d = view - 1;
      for(int w = 0; w < 2; ++w)
        box((w == 0 ? kV_DepthAmt : kV_PhaseAmt) + d,
            kDestLabelL, kFieldT[w] - 4, kArrR + 8, kFieldT[w] + kFieldH + 4);
    }
  }
  S.info.fBoxes = S.box;
  S.info.fCount = static_cast<TJBox_UInt32>(n);
  JBox_SetPropertyInfo(&S.info);
}

void MainDisplay_Draw(const TJBox_DisplayArgs* iArgs)
{
  if(!isMainDisplay(iArgs->fDisplayInfo.fID)) return;
  if(iArgs->fDisplayInfo.fParamCount < kV_Count) return;       // hdgui values list out of sync
  const TJBox_Value* v = iArgs->fParams;
  UiState scratch;
  const UiState& S = uiState(v, iArgs->fDisplayInfo.fParamCount, scratch);

  int model = inum(v, kV_Model, 0);
  if(model < 0) model = 0; else if(model > sat::kNumModels - 1) model = sat::kNumModels - 1;
  int view = inum(v, kV_LfoView, 0);
  if(view < 0) view = 0; else if(view > 3) view = 3;
  const int  pop   = popupOf(v);
  const int  listCat = popCat(pop);
  const bool lfoOn = inum(v, kV_LfoOn, 0) != 0;
  const double t   = static_cast<double>(iArgs->fTimeStamp.fSystemClockUS) * 1.0e-6;
  const float amount = clamp01(num(v, kV_Amount, 0.35f));

  drawCategories(sat::categoryOf(model), listCat);
  // UI_AnimClock is stored normalized (seconds / 3600); read it as a double --
  // float would lose the sub-frame resolution near the top of the hour.
  double clk = JBox_GetNumber(v[kV_AnimClock]) * 3600.0;
  if(!std::isfinite(clk)) clk = 0.0;
  drawModeBox(S, model, listCat, t, clk, amount);
  drawLfoList(view, lfoOn);
  if(lfoOn) drawLfoPanel(v, view, lfoOn);                    // hidden while the LFO is off
  if(lfoOn && pop == kPopShape)                               // the shape popup replaces the knobs while open
  {
    int shape = inum(v, kV_LfoShape, sat::kLfoTri);
    if(shape < 0) shape = 0; else if(shape > sat::kNumLfoShapes - 1) shape = sat::kNumLfoShapes - 1;
    drawShapePopup(S, shape);
  }
  else
  {
    drawKnobs(v, lfoOn);
    drawLevelBars(v, lfoOn);                                  // hidden with the knobs under the shape popup
    drawEditReadout(v, lfoOn);                                // over the label row while values change
  }
  drawTip(S, v, lfoOn, view);                                    // LAST: over everything
}

void MainDisplay_Notify(TJBox_NotifyArgs* iArgs)
{
  if(!isMainDisplay(iArgs->fDisplayArgs.fDisplayInfo.fID)) return;
  if(iArgs->fDisplayArgs.fDisplayInfo.fParamCount < kV_Count) return;
  const TJBox_Value* v = iArgs->fDisplayArgs.fParams;
  UiState scratch;
  UiState& S = uiState(v, iArgs->fDisplayArgs.fDisplayInfo.fParamCount, scratch);
  const int pop = popupOf(v);
  const bool lfoOn = inum(v, kV_LfoOn, 0) != 0;
  int view = inum(v, kV_LfoView, 0);
  if(view < 0 || view > 3) view = 0;
  switch(iArgs->fCall)
  {
    case kJBox_OnPeriodical:
      // Animate the pictogram whenever it is showing (not the model list), at
      // every Amount: the icons never stop (Erik).
      if(popCat(pop) < 0) iArgs->fDrawFlag = true;
      // The drawn tooltip's dwell / linger deadlines fall due while nothing moves.
      if(tipTick(S, iArgs->fDisplayArgs.fTimeStamp.fSystemClockUS)) iArgs->fDrawFlag = true;
      break;
    case kJBox_OnEnter:
    case kJBox_OnMove:
      S.hover = iArgs->fCurrentState.fPoint;
      S.hoverValid = true;
      if(pop != kPopNone) iArgs->fDrawFlag = true;             // redraw only while a popup can highlight
      if(tipHoverChanged(S, tipKeyAt(S.hover, lfoOn, view, popCat(pop)))) iArgs->fDrawFlag = true;
      break;
    case kJBox_OnExit:
      S.hoverValid = false;
      if(pop != kPopNone) iArgs->fDrawFlag = true;
      if(tipHoverChanged(S, 0)) iArgs->fDrawFlag = true;
      break;
  }
}

// Press-drag-release selection (PCFX popups): a press on a category (or the
// mode box / wave area) opens its popup; while the button is held, the entry
// under the pointer is highlighted, and releasing over an entry picks it.
// Releasing anywhere else leaves the popup open for a normal click.
// (Replaces the 100 ms press-and-hold, which never fired in Reason.)

namespace {
// Defaults = motherboard_def.lua. Returns false if nothing resettable was hit.
bool resetToDefault(TJBox_GestureArgs* a, const Target& tg)
{
  const TJBox_Value* v = a->fParams;
  switch(tg.what)
  {
    case Hit::Knob:      setNum(a, kV_Amount + tg.index, kKnobDefault[tg.index], kG_Knob[tg.index]); return true;
    case Hit::LvlBar:    setNum(a, kV_InLevel + tg.index, 0.5, (tg.index == 0) ? kG_In : kG_Out); return true;  // 0 dB
    case Hit::Category:
    case Hit::ModeBox:   setNum(a, kV_Model, sat::kDefaultModel, kG_Mode);   // ES83
                         a->fParams[kV_ModeList] = JBox_MakeNumber(kPopNone); return true;
    case Hit::LfoOn:     setNum(a, kV_LfoOn, 0.0, kG_LfoOn);                 // off
                         if(popupOf(v) == kPopShape) a->fParams[kV_ModeList] = JBox_MakeNumber(kPopNone);
                         return true;
    case Hit::Rate:
    case Hit::RateStep:
      if(inum(v, kV_LfoSync, 1) != 0) setNum(a, kV_LfoRateSync, kRateSyncDefault, kG_RateSync);   // 1/1
      else                            setNum(a, kV_LfoRate, kRateDefault, kG_Rate);               // 1 Hz
      return true;
    case Hit::Sync:      setNum(a, kV_LfoSync, 1.0, kG_Sync);                return true;   // on
    case Hit::WaveShape:
    case Hit::ShapeCell: setNum(a, kV_LfoShape, sat::kLfoTri, kG_Shape);     return true;   // triangle
    case Hit::LfoRow:
      if(tg.index < 1) return false;
      setNum(a, kV_DepthAmt + tg.index - 1, 0.5, kG_Depth[tg.index - 1]);    return true;   // depth 0
    case Hit::Field:
    case Hit::Step:
    {
      const int d = inum(v, kV_LfoView, 1) - 1;
      if(d < 0 || d > 2) return false;
      const int w = (tg.what == Hit::Field) ? tg.index : tg.index / 2;      // 0 depth, 1 phase
      if(w == 0) setNum(a, kV_DepthAmt + d, 0.5, kG_Depth[d]);
      else       setNum(a, kV_PhaseAmt + d, 0.0, kG_Phase[d]);
      return true;
    }
    default:             return false;
  }
}
} // namespace

void MainDisplay_Gesture(TJBox_GestureArgs* iArgs)
{
  if(!isMainDisplay(iArgs->fDisplayInfo.fID)) return;
  if(iArgs->fDisplayInfo.fParamCount < kV_Count) return;
  UiState scratch;
  UiState& S = uiState(iArgs->fParams, iArgs->fDisplayInfo.fParamCount, scratch);

  switch(iArgs->fCall)
  {
    case kJBox_OnTap:
    {
      S.dragSelect = false;
      if(S.tipKey != 0) iArgs->fDrawFlag = true;
      tipEndSession(S);                                         // a click ends the tooltip session (host rule)
      // Plain left click only -- see TAPS at the top.
      const TJBox_ModifierKeys& m = iArgs->fCurrentState.fModifierKeys;
      // Cmd-click (Ctrl-click on Windows) = reset the value under the pointer to
      // its motherboard default (Erik). Only where a value sits; anywhere else
      // the tap falls through to the host untouched.
      if(iArgs->fTap == kJBox_TapRegular && m.fCmdCtrl && !m.fShift && !m.fAlt)
      {
        if(resetToDefault(iArgs, hitTest(iArgs->fCurrentState.fPoint, iArgs->fParams)))
        {
          iArgs->fConsumeFlag = true;
          iArgs->fDrawFlag = true;
        }
        return;
      }
      if(iArgs->fTap != kJBox_TapRegular || m.fShift || m.fCmdCtrl || m.fAlt) return;

      const TJBox_Value* v = iArgs->fParams;
      const Target tg = hitTest(iArgs->fCurrentState.fPoint, v);
      const int model = inum(v, kV_Model, 0);
      const int cat   = sat::categoryOf(model);
      const int  pop  = popupOf(v);
      const int  listCat = popCat(pop);
      S.hover = iArgs->fCurrentState.fPoint;
      S.hoverValid = true;

      switch(tg.what)
      {
        case Hit::Category:
          // Always open the category's list, never pick a model by itself
          // (Erik, 2026-09-21). The open category again closes it.
          if(listCat == tg.index) setGui(iArgs, kV_ModeList, kPopNone);
          else
          {
            setGui(iArgs, kV_ModeList, kPopCat0 + tg.index);
            S.dragSelect = true;
          }
          break;
        case Hit::ModeBox:
          if(listCat >= 0)
          {
            const int row = listRowAt(iArgs->fCurrentState.fPoint, listCat);
            if(row >= 0) setNum(iArgs, kV_Model, sat::kCatFirst[listCat] + row, kG_Mode);
            iArgs->fParams[kV_ModeList] = JBox_MakeNumber(kPopNone);
          }
          else
          {
            setGui(iArgs, kV_ModeList, kPopCat0 + cat);
            S.dragSelect = true;
          }
          break;
        case Hit::LfoOn:
          setNum(iArgs, kV_LfoOn, inum(v, kV_LfoOn, 0) ? 0.0 : 1.0, kG_LfoOn);
          // turning the LFO off hides its panel: close its shape popup too, or it
          // would reappear on the next LFO-on (bug sweep 2026-09-21)
          if(pop == kPopShape) iArgs->fParams[kV_ModeList] = JBox_MakeNumber(kPopNone);
          break;
        case Hit::LfoRow:
          if(tg.index > 0 && iArgs->fTapDouble)
            setNum(iArgs, kV_DepthAmt + tg.index - 1, 0.5, kG_Depth[tg.index - 1]);   // double-click: depth off
          else
          {
            // Any LFO row -- the "LFO" header (waveform page) or a destination
            // (its depth/phase page) -- opens its page AND switches the LFO ON if
            // it was off (Erik; destinations too since 2026-09-22). Only the
            // on/off square switches it off.
            if(inum(v, kV_LfoOn, 0) == 0)
              setNum(iArgs, kV_LfoOn, 1.0, kG_LfoOn);
            setGui(iArgs, kV_LfoView, tg.index);
          }
          break;
        case Hit::Rate:
          if(iArgs->fTapDouble) resetToDefault(iArgs, tg);
          break;
        case Hit::RateStep:
        {
          // synced: one division per click; free: 1 % of the range (~7 % in Hz)
          const int dir = (tg.index == 0) ? 1 : -1;
          if(inum(v, kV_LfoSync, 1) != 0)
          {
            int di = inum(v, kV_LfoRateSync, kRateSyncDefault) + dir;
            if(di < 0) di = 0; else if(di > sat::kNumDivs - 1) di = sat::kNumDivs - 1;
            setNum(iArgs, kV_LfoRateSync, di, kG_RateSync);
          }
          else
          {
            const double rate = num(v, kV_LfoRate, kRateDefault);
            const double nr = std::floor(rate * 100.0 + 0.5) / 100.0 + dir * 0.01;
            setNum(iArgs, kV_LfoRate, clamp01(static_cast<float>(nr)), kG_Rate);
          }
          break;
        }
        case Hit::Sync:
          setNum(iArgs, kV_LfoSync, inum(v, kV_LfoSync, 1) ? 0.0 : 1.0, kG_Sync);
          break;
        case Hit::WaveShape:                                   // open / close the shape popup
          if(pop == kPopShape) setGui(iArgs, kV_ModeList, kPopNone);
          else
          {
            setGui(iArgs, kV_ModeList, kPopShape);
            S.dragSelect = true;
          }
          break;
        case Hit::ShapeCell:
          if(tg.index >= 0) setNum(iArgs, kV_LfoShape, tg.index, kG_Shape);
          iArgs->fParams[kV_ModeList] = JBox_MakeNumber(kPopNone);
          break;
        case Hit::Field:
        {
          const int d = inum(v, kV_LfoView, 1) - 1;
          if(d >= 0 && d < 3 && iArgs->fTapDouble)             // double-click resets
          {
            if(tg.index == 0) setNum(iArgs, kV_DepthAmt + d, 0.5, kG_Depth[d]);
            else              setNum(iArgs, kV_PhaseAmt + d, 0.0, kG_Phase[d]);
          }
          break;
        }
        case Hit::Step:
        {
          // depth: 1 % per click (0.005 of the 0..1 range); phase: 5 deg, wrapping
          const int d = inum(v, kV_LfoView, 1) - 1;
          if(d < 0 || d > 2) break;
          const double dir = (tg.index & 1) ? -1.0 : 1.0;
          if(tg.index < 2)
          {
            const double cur = num(v, kV_DepthAmt + d, 0.5f);
            const double pct = std::floor((cur - 0.5) * 200.0 + 0.5) + dir;             // snap to whole %
            setNum(iArgs, kV_DepthAmt + d, clamp01(static_cast<float>(0.5 + pct / 200.0)), kG_Depth[d]);
          }
          else
          {
            const double cur = num(v, kV_PhaseAmt + d, 0.0f);
            double deg = std::floor(cur * 72.0 + 0.5) * 5.0 + dir * 5.0;                // snap to 5 deg
            if(deg < 0.0) deg += 360.0; else if(deg > 359.0) deg -= 360.0;
            setNum(iArgs, kV_PhaseAmt + d, deg / 360.0, kG_Phase[d]);
          }
          break;
        }
        case Hit::Knob:
          if(iArgs->fTapDouble) setNum(iArgs, kV_Amount + tg.index, kKnobDefault[tg.index], kG_Knob[tg.index]);
          break;
        case Hit::LvlBar:
          if(iArgs->fTapDouble) setNum(iArgs, kV_InLevel + tg.index, 0.5, (tg.index == 0) ? kG_In : kG_Out);
          break;
        case Hit::None:
          if(pop != kPopNone) setGui(iArgs, kV_ModeList, kPopNone);   // tap elsewhere closes a popup
          else return;
          break;
      }
      iArgs->fConsumeFlag = true;
      iArgs->fDrawFlag = true;
      break;
    }

    case kJBox_OnUpdate:
    {
      if(S.dragSelect)                                          // highlight the entry under the pointer
      {
        S.hover = iArgs->fCurrentState.fPoint;
        S.hoverValid = true;
        iArgs->fDrawFlag = true;
        break;
      }
      // A double-click reset: ignore the drag that follows it. The drag would be
      // computed from fParamsStart (the value BEFORE the reset) and undo it.
      if(iArgs->fTapDouble) break;
      // A cmd-click reset is the whole gesture (as on the PCFX faders).
      if(iArgs->fStartState.fModifierKeys.fCmdCtrl) break;
      // Re-derive the grabbed control from where the press started.
      const Target tg = hitTest(iArgs->fStartState.fPoint, iArgs->fParamsStart);
      const float dy = iArgs->fCurrentState.fPoint.fY - iArgs->fStartState.fPoint.fY;
      const TJBox_Value* s = iArgs->fParamsStart;
      switch(tg.what)
      {
        case Hit::Knob:
        {
          // Amount: plain drag. Tone / Character (bipolar): centre detent.
          const float start = num(s, kV_Amount + tg.index, 0.5f);
          const float drag  = dy * dragFactorY(iArgs);
          setNum(iArgs, kV_Amount + tg.index,
                 (tg.index == 0) ? clamp01(start - drag) : detentFromDrag(detentToDrag(start) - drag),
                 kG_Knob[tg.index]);
          break;
        }
        case Hit::LvlBar:                                     // (normally the bar's overlay takes it: CtlOverlay.cpp)
          setNum(iArgs, kV_InLevel + tg.index,
                 clamp01(num(s, kV_InLevel + tg.index, 0.5f) - dy * dragFactorY(iArgs)),
                 (tg.index == 0) ? kG_In : kG_Out);
          break;
        case Hit::Rate:
          if(inum(s, kV_LfoSync, 1) != 0)
          {
            // the drag's 0..1 travel spans the 14 divisions
            const float t01 = static_cast<float>(inum(s, kV_LfoRateSync, kRateSyncDefault)) / (sat::kNumDivs - 1)
                            - dy * dragFactorY(iArgs);
            int di = static_cast<int>(std::floor(clamp01(t01) * (sat::kNumDivs - 1) + 0.5f));
            setNum(iArgs, kV_LfoRateSync, di, kG_RateSync);
          }
          else
            setNum(iArgs, kV_LfoRate, clamp01(num(s, kV_LfoRate, kRateDefault) - dy * dragFactorY(iArgs)), kG_Rate);
          break;
        case Hit::LfoRow:
          if(tg.index > 0 && std::fabs(dy) > 2.0f)                          // drag a row = its depth
            setNum(iArgs, kV_DepthAmt + tg.index - 1,
                   clamp01(num(s, kV_DepthAmt + tg.index - 1, 0.5f) - dy * dragFactorY(iArgs)), kG_Depth[tg.index - 1]);
          break;
        case Hit::Field:
        {
          const int d = inum(s, kV_LfoView, 1) - 1;
          if(d < 0 || d > 2) return;
          if(tg.index == 0)
            setNum(iArgs, kV_DepthAmt + d, clamp01(num(s, kV_DepthAmt + d, 0.5f) - dy * dragFactorY(iArgs)), kG_Depth[d]);
          else
            setNum(iArgs, kV_PhaseAmt + d, clamp01(num(s, kV_PhaseAmt + d, 0.0f) - dy * dragFactorY(iArgs)), kG_Phase[d]);
          break;
        }
        default:
          return;
      }
      iArgs->fDrawFlag = true;
      break;
    }

    case kJBox_OnRelease:
      if(S.dragSelect)
      {
        S.dragSelect = false;
        const TJBox_Point p = iArgs->fCurrentState.fPoint;
        // Only a real drag selects: a click on the mode box opens the list
        // under the pointer, and its release must not pick that row at once.
        const float mx = p.fX - iArgs->fStartState.fPoint.fX, my = p.fY - iArgs->fStartState.fPoint.fY;
        if(mx * mx + my * my < 8.0f * 8.0f) { iArgs->fDrawFlag = true; break; }
        const int pop = popupOf(iArgs->fParams);
        const int listCat = popCat(pop);
        if(listCat >= 0)
        {
          const int row = listRowAt(p, listCat);
          if(row >= 0)
          {
            setNum(iArgs, kV_Model, sat::kCatFirst[listCat] + row, kG_Mode);
            iArgs->fParams[kV_ModeList] = JBox_MakeNumber(kPopNone);
          }
        }
        else if(pop == kPopShape)
        {
          const int i = shapeCellAt(p);
          if(i >= 0)
          {
            setNum(iArgs, kV_LfoShape, i, kG_Shape);
            iArgs->fParams[kV_ModeList] = JBox_MakeNumber(kPopNone);
          }
        }
        iArgs->fDrawFlag = true;
      }
      break;

    case kJBox_OnCancel:
      S.dragSelect = false;
      break;

    default:
      break;
  }
}

} // namespace satui
