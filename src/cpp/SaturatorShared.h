// =============================================================================
// SaturatorShared.h -- tables and maths shared by the DSP (Saturator.cpp) and the
// touch-screen display (ui/SaturatorDisplay.cpp).
//
// Jukebox-free on purpose: pure constants + inline maths, so the display draws
// EXACTLY what the DSP does (same division table, same LFO shapes, same rate
// curve) and the two can never drift apart. Change a table here, both follow.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>

namespace sat {

// ---- Models ------------------------------------------------------------------
// Order MUST match the Model ui_selector in motherboard_def.lua and kModels[] /
// kCharEq[] in Saturator.h (0-3 Clean, 4-7 Warm, 8-11 Gritty, 12-15 Heavy).
// Within a category the models run gentle -> extreme (fold + fuzz "grit").
static constexpr int kNumModels     = 16;
static constexpr int kNumCategories = 4;
static constexpr int kCatFirst[kNumCategories] = { 0, 4, 8, 12 };
static constexpr int kCatCount[kNumCategories] = { 4, 4, 4, 4 };
static constexpr int kDefaultModel  = 7;   // ES83, the house sound (= motherboard default)

static const char* const kCatName[kNumCategories] = { "CLEAN", "WARM", "GRITTY", "HEAVY" };

static const char* const kModelName[kNumModels] = {
  "BC12", "PV40", "TG23", "BV98",
  "WP76", "ZX67", "A1R",  "ES83",
  "FD88", "LF09", "NN18", "SB05",
  "FF37", "GR1T", "F34K", "X7R3",
};

// The circuit each model is voiced after -- one short word under the icon,
// read from the DSP fingerprint (Saturator.h kModels[]): soft vs hard clip,
// asym (0 = symmetric/odd = transistor-like, ~0.4 = even = tube-like), fold
// and fuzz. Keep to ~13 characters (268 px text box at the host font).
static const char* const kModelDesc[kNumModels] = {
  "transistor",   "op-amp",       "transformer",  "inductor",
  "twin triode",  "FET",          "pentode",      "triode",
  "wavefolder",   "IC chip",      "diode pair",   "rectifier",
  "fuzz pair",    "germanium",    "silicon fuzz", "power tube",
};

inline int categoryOf(int model)
{
  if(model < kCatFirst[1]) return 0;
  if(model < kCatFirst[2]) return 1;
  if(model < kCatFirst[3]) return 2;
  return 3;
}

// ---- LFO ---------------------------------------------------------------------
// Shapes. Order MUST match the LFO_Shape ui_selector in motherboard_def.lua.
enum : int { kLfoSine = 0, kLfoTri, kLfoSawUp, kLfoSawDown, kLfoSquare, kLfoSH, kLfoDrift, kNumLfoShapes };
static const char* const kLfoShapeName[kNumLfoShapes] = {
  "sine", "triangle", "saw up", "saw down", "square", "s&h", "drift",
};

// Synced divisions, slow -> fast, in quarter-note beats. LFO_Rate (0..1) picks
// one of these when Sync is on (rounded to the nearest index).
static constexpr int kNumDivs = 25;
// Note-value grid, slow to fast -- the same 25 divisions as the PCFX (Erik,
// 2026-09-23), named in BAR terms: 1/1 = one bar = 4 beats, 1/4 = a quarter
// note. The odd numerators cover the dotted and tuplet values: 3/8 = dotted
// quarter, 3/16 = dotted eighth, 5/x = quintuplet feel, 7/x = septuplet feel.
static constexpr double kDivBeats[kNumDivs] = {
  64.0, 32.0, 16.0, 8.0, 7.0,
  6.0, 5.0, 4.0, 7.0 / 2.0, 3.0,
  5.0 / 2.0, 2.0, 7.0 / 4.0, 3.0 / 2.0, 5.0 / 4.0,
  1.0, 3.0 / 4.0, 2.0 / 3.0, 1.0 / 2.0, 1.0 / 3.0,
  1.0 / 4.0, 1.0 / 6.0, 1.0 / 8.0, 1.0 / 12.0, 1.0 / 16.0,
};
static const char* const kDivName[kNumDivs] = {
  "16/1", "8/1", "4/1", "2/1", "7/4", "3/2",
  "5/4", "1/1", "7/8", "3/4", "5/8", "1/2",
  "7/16", "3/8", "5/16", "1/4", "3/16", "1/4T",
  "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T",
  "1/64",
};

// Free-running rate curve: 0.05 Hz .. 60 Hz, exponential over LFO_Rate 0..1
// (top raised from 20 Hz, Erik 2026-09-22). 1 Hz sits at 0.4225 (kLfoRateDefault).
static constexpr double kLfoMinHz = 0.05;
static constexpr double kLfoMaxHz = 60.0;
// ln(20) / ln(1200): the stored value that gives 1 Hz = the LFO_Rate default in
// motherboard_def.lua. Keep the two in step if the curve changes.
static constexpr float kLfoRateDefault = 0.42252466f;

inline int divIndex(double rate01)
{
  int i = static_cast<int>(std::floor(rate01 * (kNumDivs - 1) + 0.5));
  if(i < 0) i = 0; else if(i > kNumDivs - 1) i = kNumDivs - 1;
  return i;
}

inline double freeHz(double rate01)
{
  if(rate01 < 0.0) rate01 = 0.0; else if(rate01 > 1.0) rate01 = 1.0;
  return kLfoMinHz * std::pow(kLfoMaxHz / kLfoMinHz, rate01);
}

// Deterministic per-cycle random in -1..1 (for S&H / drift). Integer hash, so a
// phase offset simply shifts which cycle a destination is in -- the same
// random sequence, delayed, like a phase-offset copy of a periodic shape.
inline double cycleRandom(int64_t cycle)
{
  uint32_t x = static_cast<uint32_t>(cycle) * 0x9E3779B1u + 0x7F4A7C15u;
  x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
  return static_cast<double>(x) * (2.0 / 4294967295.0) - 1.0;
}

// LFO value in -1..+1 at `phase` cycles (unbounded; only the fraction matters
// for the periodic shapes, the integer part seeds S&H / drift).
inline double lfoValue(int shape, double phase)
{
  const double fl = std::floor(phase);
  const double f  = phase - fl;                       // 0..1
  switch(shape)
  {
    case kLfoSine:    return std::sin(6.283185307179586 * f);
    case kLfoTri:     return 1.0 - 4.0 * std::fabs(f - 0.5);   // -1 at 0, +1 mid-cycle (the /\ on screen)
    case kLfoSawUp:   return 2.0 * f - 1.0;
    case kLfoSawDown: return 1.0 - 2.0 * f;
    case kLfoSquare:  return (f < 0.5) ? 1.0 : -1.0;
    case kLfoSH:      return cycleRandom(static_cast<int64_t>(fl));
    case kLfoDrift:
    {
      const double a = cycleRandom(static_cast<int64_t>(fl));
      const double b = cycleRandom(static_cast<int64_t>(fl) + 1);
      const double s = 0.5 - 0.5 * std::cos(3.141592653589793 * f);   // smooth glide a -> b
      return a + (b - a) * s;
    }
    default:          return 0.0;
  }
}

// Depth properties are stored 0..1 with 0.5 = no modulation (shown -100..+100 %).
// 100 % depth swings the destination +/-0.5 around its knob value, i.e. the
// full knob range peak to peak; the result is clamped to 0..1.
inline double depthBipolar(double depth01) { return (depth01 - 0.5) * 2.0; }

inline double modulated(double base01, double depth01, double lfo)
{
  double v = base01 + depthBipolar(depth01) * 0.5 * lfo;
  if(v < 0.0) v = 0.0; else if(v > 1.0) v = 1.0;
  return v;
}

} // namespace sat
