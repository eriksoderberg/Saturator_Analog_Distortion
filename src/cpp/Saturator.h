#pragma once

#include "Jukebox.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "SaturatorShared.h"   // model + LFO tables shared with the display

// -----------------------------------------------------------------------------
// Saturator
//
// 16 circuit models (4 categories) of one shaper chain: pre-drive 2-band voice
// EQ -> Character EQ -> fuzz (asymmetric tanh clip) + fold (triangle folder)
// -> air/presence -> soft/hard clip -> transparent auto-makeup -> dry/wet by
// Amount -> bipolar Tone tilt. One LFO modulates Amount / Tone / Character.
// Amount_CV / Tone_CV / Character_CV add to their knobs. Stereo.
// Property tags start at 1000.
// -----------------------------------------------------------------------------

enum : TJBox_Tag
{
  kTag_Amount    = 1000,
  kTag_Tone      = 1001,
  kTag_Character = 1002,
  kTag_Model     = 1003,   // 0..15, order = SaturatorShared.h kModelName
  // LFO (touch-screen display). UI_* display state is gui_owner, not read here.
  kTag_LfoOn        = 1004,
  kTag_LfoRate      = 1005,
  kTag_LfoRateSync  = 1006,
  kTag_LfoSync      = 1007,
  kTag_LfoShape     = 1008,
  kTag_LfoDepthAmt  = 1009,
  kTag_LfoDepthTone = 1010,
  kTag_LfoDepthChar = 1011,
  kTag_LfoPhaseAmt  = 1012,
  kTag_LfoPhaseTone = 1013,
  kTag_LfoPhaseChar = 1014,
  // 1015: retired (was LFO_CV_Out_Level). Never reuse.
  kTag_InLevel      = 1016,   // in bar  (display): -18..+18 dB, stored 0..1, 0 dB at 0.5
  kTag_OutLevel     = 1017,   // out bar (display): same
  kTag_UiEditing    = 1018,   // gui_owner UI_Editing: the control an overlay is dragging (0 none, 1..5)
};

// "Circuit models" — preset voicing centres. The panel trimmers offset from
// these. Model 7 (ES83) == the settled house sound (the default). air/fuzz are *Scale (x),
// freq is the air-shelf Hz centre, foldOff/noiseOff shift the drive curves,
// hpHz/lpHz set the pre-drive input filters (bass-tight vs sub, top bandwidth).
// bellFreq/bellQ/bellGainDb = a peaking EQ BEFORE the distortion (the main
// per-model voice): boosting a band makes it distort harder (mid-hump = throaty,
// low bump = thick, presence bump = aggressive); a cut cleans that band up.
// bellGainDb is the model's DEFAULT bell (CHARACTER at centre) — a non-zero
// per-model voice (boost OR cut). CHARACTER adds/subtracts up to 6 dB from it.
// TONE shifts the bell CENTRE frequency up/down (kBellTilt); no separate EQ.
//
// neutralFold = the mode's CHARACTER-centre fold/fuzz weighting (0 = all fuzz,
// 1 = all fold). At centre the shaper output is (1-neutralFold)*fuzz +
// neutralFold*fold; CHARACTER then ADDS weight to one side (see renderBatch).
// fuzz sets the per-mode fuzz harshness (ripple depth). DRIVE = amount for both.
struct VoiceModel {
  float air; float fuzz; float freq; float neutralFold; float noiseOff; float hpHz; float lpHz;
  float bellFreq; float bellQ; float bellGainDb; float outTrimDb; float outTrimSlope;
  float tiltHz; float hardClip;
  // --- per-mode distortion FINGERPRINT (the thing that makes each a distinct
  // "circuit", surviving even at the extremes) ---
  float charRange;  // per-mode CHARACTER range scaler (0..1): clean/warm limited, heavy full (repurposed from the removed ripple)
  float asym;       // fuzz asymmetry: 0 = symmetric/odd (transistor), ~0.4 = even/octave (tube)
  float foldScale;  // max-fold multiplier at Char -100: clean <1 (e.g. 0.3), extreme up to 2.0 (dense)
  // SECOND pre-drive EQ band (peaking). With bell1 this gives each mode two
  // freely-placed bumps/dips: bass+air "smile", or a broad ~3oct mid DIP
  // (Q~0.4, -gain) carving the mid + a narrower ~1.5oct restore bump (Q~0.9).
  float bell2Freq; float bell2Q; float bell2GainDb;
};
// trim(dB) = outTrimDb + outTrimSlope*drive (re-fitted least-squares vs median
// mode level for the new fuzz/fold shaper). tiltHz = per-mode TONE pivot.
// hardClip = 1 for Gritty/Heavy (raw hard clip), 0 for Clean/Warm (soft clip).
static constexpr int kNumModels = sat::kNumModels;   // 16, order = SaturatorShared.h
static constexpr VoiceModel kModels[kNumModels] = {
  //  air fuzz freq nFold noise hp lp | bell1 f/Q/g | trim slope tilt hard | chRng asym foldScale | bell2 f/Q/g
  { 7.0f, 0.0f, 14000.0f, 0.0f, -0.4f, 20.0f, 20000.0f, 7000.0f, 0.7f, 4.0f, -0.3f, 0.7f, 900.0f, 0.0f, 0.45f, 0.05f, 1.5f, 150.0f, 0.7f, 2.0f },  //  0 BC12 CLEAN  bright smile (air+bass)
  { 0.0f, 0.0f, 10000.0f, 0.0f, -0.5f, 50.0f, 18500.0f, 900.0f, 0.5f, -2.0f, -1.0f, 3.1f, 700.0f, 0.0f, 0.4f, 0.0f, 1.6f, 5000.0f, 0.7f, 0.0f },  //  1 PV40 CLEAN  pure: gentle scoop
  { 3.0f, 0.0f, 9000.0f, 0.0f, -0.3f, 22.0f, 18500.0f, 1000.0f, 0.4f, -4.0f, -0.3f, 0.2f, 800.0f, 0.0f, 0.4f, 0.05f, 1.6f, 8000.0f, 0.8f, 3.0f },  //  2 TG23 CLEAN  hammock: mid scoop + air
  { 1.5f, 0.0f, 14000.0f, 0.0f, -0.3f, 15.0f, 13000.0f, 600.0f, 0.4f, -6.0f, -0.8f, 2.3f, 550.0f, 0.0f, 0.45f, 0.08f, 1.5f, 100.0f, 0.7f, 4.0f },  //  3 BV98 CLEAN  dark: mid scoop + bass
  { 8.0f, 2.0f, 6000.0f, 0.0f, -0.35f, 15.0f, 18500.0f, 6000.0f, 0.7f, 4.0f, -0.4f, 0.1f, 750.0f, 0.0f, 0.55f, 0.12f, 1.4f, 110.0f, 0.7f, 2.0f },  //  4 WP76 WARM   airy + bass
  { 7.0f, 2.5f, 13000.0f, 0.0f, -0.35f, 100.0f, 20000.0f, 4000.0f, 0.9f, 5.0f, -0.1f, 0.3f, 850.0f, 0.0f, 0.55f, 0.1f, 1.5f, 12000.0f, 0.7f, 2.0f },  //  5 ZX67 WARM   modern presence + air
  { 9.0f, 2.5f, 10500.0f, 0.0f, -0.45f, 60.0f, 18500.0f, 10000.0f, 0.6f, 5.0f, -0.1f, 0.1f, 800.0f, 0.0f, 0.5f, 0.1f, 1.4f, 13000.0f, 0.8f, 3.0f },  //  6 A1R  WARM   double air
  { 6.0f, 3.0f, 10500.0f, 0.0f, -0.3f, 15.0f, 18500.0f, 9000.0f, 0.7f, 3.0f, 0.0f, 0.0f, 700.0f, 0.0f, 0.5f, 0.15f, 1.4f, 150.0f, 0.7f, 2.0f },  //  7 ES83 WARM   gentle smile (bass+air)
  { 5.0f, 0.0f, 11000.0f, 0.0f, -0.25f, 15.0f, 12000.0f, 2000.0f, 0.9f, 4.0f, 0.3f, -0.4f, 700.0f, 1.0f, 0.7f, 0.15f, 1.7f, 120.0f, 0.7f, 2.0f },  //  8 FD88 GRITTY fold presence + bass
  { 4.0f, 3.0f, 7000.0f, 0.0f, -0.2f, 50.0f, 11000.0f, 1400.0f, 1.0f, 5.0f, 2.0f, -0.2f, 600.0f, 1.0f, 0.7f, 0.2f, 1.3f, 350.0f, 0.5f, -3.0f },  //  9 LF09 GRITTY lo-fi nasal + thin low
  { 5.0f, 3.5f, 8500.0f, 0.0f, -0.2f, 75.0f, 16000.0f, 900.0f, 0.4f, -4.0f, 2.0f, -0.1f, 600.0f, 1.0f, 0.7f, 0.25f, 1.4f, 1600.0f, 0.9f, 4.0f },  // 10 NN18 GRITTY vintage mid-carve: dip + hump
  { 3.0f, 4.0f, 9000.0f, 0.0f, -0.3f, 5.0f, 18500.0f, 150.0f, 1.0f, 6.0f, 0.2f, -0.3f, 450.0f, 1.0f, 0.7f, 0.15f, 1.5f, 7000.0f, 0.7f, 2.0f },  // 11 SB05 GRITTY sub bump + air
  { 11.0f, 4.5f, 11500.0f, 0.0f, -0.25f, 15.0f, 20000.0f, 1000.0f, 0.4f, -4.0f, 3.0f, -0.5f, 650.0f, 1.0f, 0.85f, 0.25f, 1.0f, 1800.0f, 0.9f, 5.0f },  // 12 FF37 HEAVY  mid-carve: broad dip + bite restore
  { 0.0f, 6.0f, 10000.0f, 0.0f, -0.25f, 30.0f, 14000.0f, 1000.0f, 1.1f, 6.0f, 2.0f, 0.0f, 600.0f, 1.0f, 1.0f, 0.3f, 1.7f, 9000.0f, 0.7f, 5.0f },  // 13 GR1T HEAVY  grind cocked-mid + air
  { 4.0f, 6.0f, 10000.0f, 0.0f, -0.3f, 25.0f, 15000.0f, 200.0f, 0.8f, 6.0f, 3.0f, -0.5f, 500.0f, 1.0f, 0.9f, 0.35f, 1.8f, 1200.0f, 0.4f, -3.0f },  // 14 F34K HEAVY  thick bass + scooped mid
  { 6.0f, 8.0f, 10000.0f, 0.0f, -0.1f, 25.0f, 18500.0f, 4000.0f, 0.9f, 5.0f, 3.0f, 0.0f, 600.0f, 1.0f, 1.0f, 0.4f, 2.0f, 150.0f, 0.8f, 4.0f },  // 15 X7R3 HEAVY  bright bite + bass
};

// CHARACTER EQ per-mode bell centres: {lowHz, highHz}, placed near each mode's own
// voice-bell bands (bell1/bell2) so Character pushes the mode's character, with a
// slight skirt overlap at Q=kCharEqQ. -Char boosts lowHz + cuts highHz; +Char reverse.
// Order MUST match kModels[] above. (Mode voice bell1/bell2 freqs in comments.)
static constexpr struct { float loHz; float hiHz; } kCharEq[kNumModels] = {
  {300.0f, 7000.0f},   //  0 BC12 (voice 150/7000)
  {500.0f, 5000.0f},   //  1 PV40 (voice 900/5000)
  {600.0f, 7000.0f},   //  2 TG23 (voice 1000/8000)
  {150.0f, 1200.0f},   //  3 BV98 (voice 100/600 dark)
  {250.0f, 6000.0f},   //  4 WP76 (voice 110/6000)
  {1200.0f, 8000.0f},  //  5 ZX67 (voice 4000/12000)
  {800.0f, 9000.0f},   //  6 A1R  (voice 10k/13k airy)
  {250.0f, 6000.0f},   //  7 ES83 (voice 150/9000)
  {200.0f, 2500.0f},   //  8 FD88 (voice 120/2000)
  {350.0f, 1600.0f},   //  9 LF09 (voice 350/1400)
  {700.0f, 1800.0f},   // 10 NN18 (voice 900/1600)
  {200.0f, 6000.0f},   // 11 SB05 (voice 150/7000)
  {700.0f, 2200.0f},   // 12 FF37 (voice 1000/1800)
  {700.0f, 7000.0f},   // 13 GR1T (voice 1000/9000)
  {200.0f, 1500.0f},   // 14 F34K (voice 200/1200)
  {300.0f, 4000.0f},   // 15 X7R3 (voice 150/4000)
};

class Device
{
public:
  Device() noexcept;
  explicit Device(int iSampleRate) noexcept;

  void renderBatch(const TJBox_PropertyDiff iPropertyDiffs[],
                   TJBox_UInt32 iDiffCount) noexcept;

private:
  void ensureInit() noexcept;

  // Zero every per-sample filter state. Called when entering idle-bypass so no
  // denormals linger and the chain resumes cleanly from rest (input was already
  // silent, so the decayed tail is inaudible).
  void flushDspState() noexcept
  {
    fPreHpXL = fPreHpYL = fPreLpStateL = 0.0f;
    fPreHpXR = fPreHpYR = fPreLpStateR = 0.0f;
    fBellZ1L = fBellZ2L = fBellZ1R = fBellZ2R = 0.0f;
    fBell2Z1L = fBell2Z2L = fBell2Z1R = fBell2Z2R = 0.0f;
    fEmphLpL = fDeLpL = fEmphLpR = fDeLpR = 0.0f;
    fDcXL = fDcYL = fDcXR = fDcYR = 0.0f;
    fAirLpL = fAir2L = fAirLpR = fAir2R = 0.0f;
    fShelfLpL = fShelfLpR = 0.0f;
    fToneLpL = fToneLpR = 0.0f;
    fEnvInL = fEnvWetL = fEnvInR = fEnvWetR = 0.0f;
    fNzBrownL = fNzPrevL = fNzBrownR = fNzPrevR = 0.0f;
    fCLoZ1L = fCLoZ2L = fCLoZ1R = fCLoZ2R = 0.0f;
    fCHiZ1L = fCHiZ2L = fCHiZ1R = fCHiZ2R = 0.0f;
  }
  void readInputBuffer(TJBox_ObjectRef iInputRef, TJBox_PropertyRef iConnRef, float *oBuffer) noexcept;
  void writeOutputBuffer(TJBox_ObjectRef iOutputRef, const float *iBuffer) noexcept;
  void updateMeter(float iPeakL, float iPeakR) noexcept;   // input meter, PPM ballistics
  void updateAnimClock() noexcept;   // icon animation clock for the display
  void updateLfo() noexcept;         // advances the LFO one batch, sets fModAmt/fModTone/fModChar
  void publishLfo() noexcept;        // decimated rt_owner writes for the display
  // LFO part of Amount / Tone / Character at fraction t (0..1) through the
  // current batch, as offsets from the knob values (see kLfoSubBlocks).
  void lfoOffsets(float t, float &oAmt, float &oTone, float &oChar) const noexcept;
  void writeLfoCv() noexcept;        // LFO CV out (raw LFO, -1..+1)

  static constexpr TJBox_AudioFramePos kBatchFrames = 64;

  // --- Voicing constants (explore values; tune by ear, then bake in) --------
  // Traditional saturator (no input normalizer): bell -> drive -> tanh/fold.
  // Louder input distorts harder; the dry/wet Drive gives clean-at-0.
  // DRIVE = amount (pushes both shapers). CHARACTER = the fuzz<->fold RECIPE
  // (blend), independent of amount. The two shaper flavours: FUZZ = asymmetric
  // hard clip (bright, harsh, raspy, gated); FOLD = triangle wavefold (round,
  // FM-ish). Each MODE colours its own two ends (bell/air/filters/neutral blend),
  // so modes stay distinct; only the base shapes are shared.
  static constexpr float kSatGrow    = 7.2f;  // fuzz drive growth w/ Drive (-10%: Drive was a bit too strong at max)
  static constexpr float kFoldGrow   = 5.4f;  // fold depth growth w/ Drive (-10% with kSatGrow)
  static constexpr float kPreEmph    = 2.0f;  // high-boost INTO shaper (engages top)
  static constexpr float kBellTilt   = 0.25f; // TONE shifts the bell centre freq by +/-25%
  static constexpr float kDeEmph     = 0.0f;  // de-emphasis REMOVED: keep the pre-emphasized top in the output (sizzle)
  static constexpr float kSlewSec    = 0.026f; // knob / CV / Mode slew time constant (~26 ms; was 0.05 per batch at 48 kHz)
  static constexpr float kMaxLift    = 4.0f;  // transparent auto-makeup CEILING: wet loudness may exceed dry by at most ~+12 dB (10^(12/20)=3.98) in any mode
  static constexpr float kCharEqDb   = 6.0f;  // CHARACTER EQ: +/-6 dB on the per-mode low & high bells at the knob extremes (opposite directions)
  static constexpr float kCharEqQ    = 1.4f;  // CHARACTER EQ bell Q (~0.9 oct = fairly narrow, slight skirt overlap)
  static constexpr float kAir        = 7.0f;  // octave-up exciter amount (reverted)
  static constexpr float kPresenceDb = 3.0f;  // gentle high-shelf emphasis (~1.2 kHz+)
  static constexpr float kAirShelfDb = 12.0f; // strong AIR shelf (~7 kHz+) -> 10 kHz presence (reverted; intentional top lift)
  static constexpr float kNoiseMax   = 0.006f; // analog-hiss inject depth (pulled back further: overall too strong)
  static constexpr float kCharBellDb = 6.0f;  // CHARACTER also swings the pre-drive bell +/- this many dB
  static constexpr float kCharSat    = 3.0f;  // +Character multiplies tanh saturation gain (x4 at +100): strong buzzy clipping = the fuzz
  static constexpr float kFuzzMode   = 0.08f; // per-mode `fuzz` value adds clip drive: satGain *= (1 + kFuzzMode*fuzz). fuzz=0 clean, fuzz=8 ~ x1.6 harder
  static constexpr float kWetKnee    = 0.35f; // Drive fraction at which dry/wet reaches FULL wet (mix decoupled from distortion amount)
  static constexpr float kCharFold   = 3.5f;  // -Character fold depth (NO drive multiplier now; bounded so it can't hit white noise)
  static constexpr float kFoldMax    = 10.0f; // hard ceiling on fold depth (safety vs CV/edge cases)
                                              // threshold so it folds many times (audibly gnarly), not just grazes it
  static constexpr float kCharFoldW  = 0.9f;  // -Character fold WEIGHT (lowered so heavy modes' fold doesn't clip into noise)
  static constexpr float kCharFuzzW  = 0.6f;  // +Character ADDS this much fuzz weight (additive)
  static constexpr float kDriveTaper = 1.0f;  // knob^taper into Drive (1.0 = linear: knob = dry/wet amount)

  // Fast sine approximation (Bhaskara-style parabola + one refinement, ~0.1%
  // error — ample for the fuzz ripple, which is an arbitrary wiggle anyway).
  // Range-reduces to [-pi,pi] first so it stays valid for large drive*signal
  // arguments. ~3-4x cheaper than std::sin (no libm call) on the per-sample path.
  static inline float fastSin(float x) noexcept
  {
    const float twoPi = 6.28318530718f, invTwoPi = 0.159154943f;
    x -= twoPi * std::floor(x * invTwoPi + 0.5f);     // wrap to [-pi, pi]
    const float B = 1.27323954f, C = -0.405284735f;   // 4/pi, -4/pi^2
    float y = B * x + C * x * std::fabs(x);
    y = 0.225f * (y * std::fabs(y) - y) + y;           // refine
    return y;
  }

  // Pade(3,3) tanh approximation (cheap, no transcendentals, exact at |x|=3).
  static inline float tanhApprox(float x) noexcept
  {
    if(x >  3.0f) return  1.0f;
    if(x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
  }

  // Transparent output soft-clip: passes |x| <= 0.8 untouched, then smoothly
  // limits to +/-1. Catches exciter/bloom peaks without coloring the bulk signal.
  static inline float softClip(float x) noexcept
  {
    const float t = 0.8f;
    const float ax = std::fabs(x);
    if(ax <= t) return x;
    const float over = (ax - t) / (1.0f - t);
    return (x < 0.0f ? -1.0f : 1.0f) * (t + (1.0f - t) * tanhApprox(over));
  }

  // Hard clip to +/-1: raw, abrupt limiting for the Gritty/Heavy modes.
  static inline float hardClip(float x) noexcept
  {
    if(x >  1.0f) return  1.0f;
    if(x < -1.0f) return -1.0f;
    return x;
  }

  // Fast white noise in ~[-1, 1] (xorshift32). For analog-hiss injection.
  inline float whiteNoise() noexcept
  {
    fRng ^= fRng << 13; fRng ^= fRng >> 17; fRng ^= fRng << 5;
    return static_cast<float>(static_cast<int32_t>(fRng)) * (1.0f / 2147483648.0f);
  }

  // Triangle wavefolder: reflects the input between +/-1 like a bouncing ball,
  // period 4. Sharp creases -> dense high harmonics ("gnarly/crunchy"), unlike
  // the smooth sine fold. Maps any real input into [-1, 1].
  static inline float foldTri(float x) noexcept
  {
    float t = x - 1.0f;
    t = t - 4.0f * std::floor(t * 0.25f);   // positive modulo -> [0, 4)
    return std::fabs(t - 2.0f) - 1.0f;
  }

  // Saturator shaper. The FUZZ is pure asymmetric CLIPPING: a tanh body driven
  // hard by satGain (Drive + Character) = buzzy ODD harmonics, plus a per-mode
  // `asym` bias for EVEN/octave harmonics (tube-ish) vs symmetric/odd
  // (transistor). NO sin(signal) ripple: sin of the signal is phase modulation =
  // FM sidebands, which always read as "FM synthesis", never as fuzz, at ANY
  // depth/frequency. Per-mode fuzz character now comes from asym + clip drive +
  // the 2-band EQ. FOLD (triangle wavefolder) is a separate -Character flavour.
  static inline float shape(float x, float satGain, float foldDepth,
                            float wFuzz, float wFold, float asym) noexcept
  {
    const float xs   = x * satGain;
    const float bias = asym * 2.0f;   // FIXED modest offset (NOT *satGain): scaling with
                                      // satGain made +Character half-wave rectify -> quieter
    const float fuzz = tanhApprox(xs + bias) - tanhApprox(bias);   // asymmetric clip, no FM
    const float fold = foldTri(x * foldDepth);
    return wFuzz * fuzz + wFold * fold;   // ADDITIVE: summed, not crossfaded
  }
  // Peaking EQ as a TPT state-variable filter (A. Simper): H(s) =
  // (s^2 + s k A^2 + 1) / (s^2 + s k + 1), k = 1/(Q A) -- the RBJ peaking
  // biquad's response, but its states stay well-conditioned at low
  // frequencies and under per-sample coefficient glides. o = {a1, a2, a3, m1}.
  static inline void svfBellCoefs(float hz, float q, float A, float fs, float o[4]) noexcept
  {
    if(hz < 20.0f) hz = 20.0f; else if(hz > 0.45f * fs) hz = 0.45f * fs;
    if(q < 0.05f) q = 0.05f;
    const float g = std::tan(3.14159265358979f * hz / fs);
    const float k = 1.0f / (q * A);
    o[0] = 1.0f / (1.0f + g * (g + k));
    o[1] = g * o[0];
    o[2] = g * o[1];
    o[3] = k * (A * A - 1.0f);
  }
  // z1 / z2 = the two integrator states (ic1eq, ic2eq).
  static inline float svfBell(float v0, float &z1, float &z2, const float c[4]) noexcept
  {
    const float v3 = v0 - z2;
    const float v1 = c[0] * z1 + c[1] * v3;
    const float v2 = z2 + c[1] * z1 + c[2] * v3;
    z1 = 2.0f * v1 - z1;
    z2 = 2.0f * v2 - z2;
    return v0 + c[3] * v1;
  }
  // Same, with tanhApprox(asym * 2) precomputed by the caller (per sub-block).
  static inline float shapeTb(float x, float satGain, float foldDepth,
                              float wFuzz, float wFold, float bias, float tanhBias) noexcept
  {
    const float fuzz = tanhApprox(x * satGain + bias) - tanhBias;
    const float fold = foldTri(x * foldDepth);
    return wFuzz * fuzz + wFold * fold;
  }

  // Meter scale, as SpiritLevel's bars (and the Degrader): -60 .. +12 dBFS
  // -> 0..1. display.lua's colour zones use the same scale.
  static inline float dbFromPeak(float peak) noexcept
  {
    const float a = peak > 1.0e-12f ? peak : 1.0e-12f;
    return 20.0f * std::log10(a);
  }
  static inline float meter01FromDb(float db) noexcept
  {
    if(db < -60.0f) db = -60.0f;
    if(db >  12.0f) db =  12.0f;
    return (db + 60.0f) / 72.0f;
  }

  int  fSampleRate{48000};
  bool fInitialized{false};

  TJBox_ObjectRef fInLRef{0};
  TJBox_ObjectRef fInRRef{0};
  TJBox_ObjectRef fOutLRef{0};
  TJBox_ObjectRef fOutRRef{0};

  TJBox_PropertyRef fOnOffPropRef{};        // /custom_properties/builtin_onoffbypass
  TJBox_PropertyRef fCVAmountValueRef{};    // /cv_inputs/Amount_CV/value
  TJBox_PropertyRef fCVToneValueRef{};      // /cv_inputs/Tone_CV/value
  TJBox_PropertyRef fCVCharValueRef{};      // /cv_inputs/Character_CV/value
  TJBox_PropertyRef fInLConnRef{}, fInRConnRef{};   // /audio_inputs/MainIn*/connected

  // Host transport (LFO sync).
  TJBox_PropertyRef fTempoRef{};            // /transport/tempo
  TJBox_PropertyRef fTransportPlayingRef{};  // /transport/playing
  TJBox_PropertyRef fPlayPosRef{};          // /transport/play_pos (synced phase lock)
  double fTempoBPM{120.0};
  bool   fTransportPlaying{false};

  // Input meter rt_owner property refs + state (read by the Input_Meter display).
  // SpiritLevel's PPM ballistics (all in dB), as the Degrader: 0 ms rise, 24 dB
  // per 2.8 s fall; peak hold 5 s, then 24 dB/s. Rates set from the sample rate
  // in ensureInit, so the meter moves the same at every sample rate.
  TJBox_PropertyRef fMeterLRef{};
  TJBox_PropertyRef fMeterRRef{};
  TJBox_PropertyRef fMeterLPeakRef{};
  TJBox_PropertyRef fMeterRPeakRef{};
  float fMeterL{-72.0f}, fMeterR{-72.0f}, fMeterLPeak{-72.0f}, fMeterRPeak{-72.0f};
  int   fMeterHoldL{0}, fMeterHoldR{0};
  float fPpmFallPerBatch{0.0f}, fPeakFallPerBatch{0.0f};
  int   fPeakHoldBatches{1};
  int   fMeterBatchCounter{0};
  float fMeterPub[4]{-1.0f, -1.0f, -1.0f, -1.0f};   // last stored meter values (store on change only)
  int fMeterWriteInterval{16};                      // batches between meter stores (~21 ms, from the sample rate)

  // --- LFO (one LFO -> Amount / Tone / Character), the Degrader's ----------
  // Maths + tables in SaturatorShared.h (shared with the display).
  int    fCached_LfoOn{0};
  float  fCached_LfoRate{sat::kLfoRateDefault};      // FREE rate 0..1 (default = 1 Hz)
  int    fCached_LfoRateSync{7};                    // SYNCED rate: division index (7 = 1/1)
  int    fCached_LfoSync{1};
  int    fCached_LfoShape{sat::kLfoTri};
  float  fCached_LfoDepth[3] = {0.5f, 0.5f, 0.5f};  // 0.5 = none (Amount, Tone, Character)
  float  fCached_LfoPhase[3] = {0.0f, 0.0f, 0.0f};  // 0..1 = 0..360 deg offset
  double fLfoPhase{0.0};                            // cycles (unbounded; S&H seeds on the integer part)
  double fLfoPhasePrev{0.0};                        // fLfoPhase at the start of this batch (lfoOffsets)
  // LFO on: the batch runs in this many sub-blocks (16 samples each), the
  // controls re-evaluated per sub-block -- ~47 steps per cycle at 60 Hz instead
  // of 12. LFO off: one block, exactly as before (bit-identical output).
  static constexpr int kLfoSubBlocks = 4;
  float  fModAmt{0.35f}, fModTone{0.5f}, fModChar{0.5f};   // this batch's modulated knob values
  float  fLfoRaw{0.0f};                             // this batch's LFO value, -1..+1 (0 while off)
  TJBox_PropertyRef fLfoCvOutRef{};                 // /cv_outputs/LFO_CV_Out/value
  float  fLfoCvPub{2.0f};                           // last stored CV out value (store on change only)

  // IN / OUT level bars (the two bars beside the knobs on the display). Stored
  // 0..1 over -18..+18 dB; the gains are slewed per sample (kLvlTrimMs) so a
  // drag doesn't zipper. IN runs before the saturator, so it also decides how
  // hard it is driven; OUT is the last thing in the chain. Neither applies in
  // OFF or BYPASS, which stay a clean pass.
  static constexpr float kLvlTrimDb = 18.0f;
  static constexpr float kLvlTrimMs = 20.0f;
  float fCached_InLevel{0.5f}, fCached_OutLevel{0.5f};
  float fInGain{1.0f}, fOutGain{1.0f};                // slewed, 1.0 = 0 dB
  static float trimGain(float v01) noexcept
  {
    if(!(v01 >= 0.0f)) v01 = 0.0f; else if(v01 > 1.0f) v01 = 1.0f;
    if(v01 == 0.5f) return 1.0f;                      // exactly unity at the default
    return std::pow(10.0f, ((2.0f * v01 - 1.0f) * kLvlTrimDb) * 0.05f);
  }
  TJBox_PropertyRef fLfoPhaseOutRef{};              // rt_owner -> display
  TJBox_PropertyRef fModAmtOutRef{}, fModToneOutRef{}, fModCharOutRef{};
  // CV on the knobs (2026-10-01): which CV inputs are connected (rt_owner
  // UI_CvMask = mask / 7, bit 0 amount, 1 tone, 2 style) for the display's
  // "cv" mark, and Mod_*_Out now carry knob + CV + LFO, so the knob dot shows
  // whatever is moving the value.
  TJBox_PropertyRef fCvConnRef[3]{}, fCvMaskRef{};
  int fCvMaskPub{-1};
  float  fPubPhase{-1.0f}, fPubAmt{-1.0f}, fPubTone{-1.0f}, fPubChar{-1.0f};
  int    fLfoPublishCounter{0};
  int fLfoPublishInterval{8};                       // batches between LFO stores (~10 ms, from the sample rate)
  // Icon animation clock for the display (rt_owner UI_AnimClock): integrates
  // real time x (0.6 + 0.6 * slewed Amount), so the icons never stop, run at
  // 2x speed at full Amount, and a moving Amount changes their SPEED smoothly.
  TJBox_PropertyRef fAnimClockRef{};
  // Edit readout (rt_owner UI_EditMask, 2026-09-27): which of in / amount /
  // tone / style / out changed recently. A diff sets that control's hold to
  // kEditHoldSec; the display shows the values of the controls still held.
  static constexpr float kEditHoldSec = 0.5f;          // after an automation / Remote / patch change
  TJBox_PropertyRef fEditMaskRef{};
  int  fEditHold[5] = { 0, 0, 0, 0, 0 };   // batches left, per control
  int  fEditMaskPub{-1};
  bool fEditPrimed{false};                 // the first batch's diffs are the load, not an edit
  int  fEditingCtl{-1};                    // control being dragged on the panel (UI_Editing - 1), -1 none
  void updateEditMask(const TJBox_PropertyDiff iDiffs[], TJBox_UInt32 iCount) noexcept;
  double fAnimClock{0.0};
  float  fAnimAmtSm{0.35f};
  int    fAnimPublishCounter{0};

  // --- Idle (CPU + the host's silence optimisation), as the Degrader: once
  // the input has been silent (kJBox_SilentThreshold) AND our own output has
  // stayed below kIdleOutThr for kIdleSec, stop rendering and leave the outputs
  // UNWRITTEN (= silence; writing zeros would defeat the host's idle). The
  // filter states are flushed once on entry (no denormals, clean restart).
  static constexpr float kIdleSec    = 0.6f;
  static constexpr float kIdleOutThr = 1.0e-6f;   // -120 dBFS
  int   fSilentRun{0};                            // consecutive ON batches with silent input AND output
  bool  fIdleFlushed{false};

  int   fCached_OnOff {1};        // builtin_onoffbypass (0/1/2)
  float fCached_Amount {0.35f};
  float fCached_Tone  {0.5f};
  float fCached_Character {0.5f};   // bipolar macro (0.5 = neutral)
  int   fCached_Model {sat::kDefaultModel};   // circuit model selector (0..kNumModels-1)

  // Tone tilt: one-pole LP state per channel (split point ~700 Hz).
  float fToneLpL{0.0f};
  float fToneLpR{0.0f};

  // Pre-drive conditioning (signal stabilisation feeding the shaper only):
  // 1-pole HP @15 Hz -> 1-pole LP @18.5 kHz. Coeffs from sample rate.
  float fPreHpCoef{0.0f};
  float fPreLpCoef{0.0f};
  float fPreHpXL{0.0f}, fPreHpYL{0.0f}, fPreLpStateL{0.0f};
  float fPreHpXR{0.0f}, fPreHpYR{0.0f}, fPreLpStateR{0.0f};

  // Post-shaper DC blocker (asymmetry generates a DC term): 1-pole HP ~10 Hz.
  float fDcCoef{0.0f};
  float fDcXL{0.0f}, fDcYL{0.0f};
  float fDcXR{0.0f}, fDcYR{0.0f};

  // Pre/de-emphasis tilt around the shaper (engages top end). One-pole @~900 Hz.
  float fEmphCoef{0.0f};
  float fEmphLpL{0.0f}, fEmphLpR{0.0f};   // pre-emphasis low-band state
  float fDeLpL{0.0f},   fDeLpR{0.0f};     // de-emphasis low-band state

  uint32_t fRng{0x2545F491u};     // white-noise PRNG state (analog hiss)

  // One-shot flag so the NaN wall's JBOX_TRACE fires once per instance,
  // not once per poisoned batch.
  bool fPanicTraced{false};

  // Slewed control values (smooth Mode/Drive changes, avoid zipper/crackle).
  float fSlewDrive {0.35f};          // slewed Amount (+CV, +LFO): drive + dry/wet
  // Per-sample ramps of the drive / tone / style gains inside each sub-block
  // (renderBatch, 2026-09-27): the previous sub-block's values.
  // 0..14 gains, 15..30 the four EQ bells (a1 a2 a3 m1 each), 31 charBi,
  // 32 hard-clip mix, 33 bias (asym*2), 34 tone-tilt coef, 35 shelf coef,
  // 36/37 pre HP/LP coefs, 38 tanh(bias). fRampCur caches the last computed targets together
  // with fCoefKey (their inputs), so a static setting skips all the maths.
  static constexpr int kNumRamps = 39;
  static constexpr int kRampSlots = 40;   // kNumRamps padded to a multiple of 4
  static_assert(kRampSlots >= kNumRamps && kRampSlots % 4 == 0, "ramp padding");
  static constexpr int kNumCoefKeys = 25;
  float fRampPrev[kNumRamps] = {};
  float fRampCur[kNumRamps] = {};
  float fCoefKey[kNumCoefKeys] = {};
  bool  fRampInit{false};
  bool  fCoefValid{false};
  float fSlew{0.05f};                 // per-batch one-pole coefficient for kSlewSec
  float fBrownLeak{0.98f}, fBrownIn{0.02f}, fBrownGain{10.0f};   // brown-noise integrator, SR-independent
  float fSlewChar  {0.5f};
  float fSlewTone  {0.5f};            // slewed TONE knob (stops zipper/crackle)
  float fM_hardClip{0.0f};            // slewed clip type (0 soft .. 1 hard); crossfades on mode change
  // Transparent auto-makeup: dry & wet level envelopes (per channel) + smoothing coef.
  float fEnvInL{0.0f}, fEnvWetL{0.0f}, fEnvInR{0.0f}, fEnvWetR{0.0f};
  float fEnvCoef{0.0f};               // envelope one-pole coef (~20 ms), set in ensureInit
  // Character-colored noise state: brown (leaky integ) + last sample (for blue/diff).
  float fNzBrownL{0.0f}, fNzPrevL{0.0f}, fNzBrownR{0.0f}, fNzPrevR{0.0f};
  // Character EQ: slewed per-mode low/high bell centres + biquad state (per channel).
  float fCharLoHz{200.0f}, fCharHiHz{4000.0f};
  float fCLoZ1L{0.0f}, fCLoZ2L{0.0f}, fCLoZ1R{0.0f}, fCLoZ2R{0.0f};
  float fCHiZ1L{0.0f}, fCHiZ2L{0.0f}, fCHiZ1R{0.0f}, fCHiZ2R{0.0f};
  float fM_air{6.0f}, fM_fuzz{3.0f}, fM_freq{10500.0f};
  float fM_neutralFold{0.4f}, fM_noiseOff{0.0f}, fM_hp{15.0f}, fM_lp{18500.0f};
  float fM_bellFreq{2000.0f}, fM_bellQ{0.8f}, fM_bellGain{2.0f};
  float fM_bell2Freq{150.0f}, fM_bell2Q{0.7f}, fM_bell2Gain{0.0f}; // 2nd EQ band
  float fM_outTrim{0.0f}, fM_outSlope{0.0f};   // per-mode loudness makeup: dB = outTrim + outSlope*drive
  float fM_tilt{700.0f};                        // per-mode TONE tilt pivot (Hz)
  float fM_charRange{0.7f}, fM_asym{0.1f}, fM_foldScale{1.0f};  // per-mode character-range scaler + asym + max-fold
  bool  fSlewInit{false};

  // Pre-drive peaking-bell biquad state (transposed DF2), per channel. Two bands.
  float fBellZ1L{0.0f}, fBellZ2L{0.0f}, fBellZ1R{0.0f}, fBellZ2R{0.0f};
  float fBell2Z1L{0.0f}, fBell2Z2L{0.0f}, fBell2Z1R{0.0f}, fBell2Z2R{0.0f};

  // High-band octave-up exciter ("air"): one-pole split @~1.2 kHz + DC track of square.
  float fAirCoef{0.0f};
  float fAirLpL{0.0f}, fAirLpR{0.0f};
  float fAir2L{0.0f},  fAir2R{0.0f};   // DC estimate of the squared (even-harmonic) band

  // Air shelf (~7 kHz high-shelf boost) emphasising the 10 kHz overtone region.
  float fShelfCoef{0.0f};
  float fShelfLpL{0.0f}, fShelfLpR{0.0f};
};
