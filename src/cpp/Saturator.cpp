#include "Saturator.h"
#include <logging.h>

Device::Device() noexcept : Device(48000) {}

Device::Device(int iSampleRate) noexcept : fSampleRate(iSampleRate) {}

void Device::ensureInit() noexcept
{
  if(fInitialized)
    return;

  fInLRef  = JBox_GetMotherboardObjectRef("/audio_inputs/MainInL");
  fInRRef  = JBox_GetMotherboardObjectRef("/audio_inputs/MainInR");
  fOutLRef = JBox_GetMotherboardObjectRef("/audio_outputs/MainOutL");
  fOutRRef = JBox_GetMotherboardObjectRef("/audio_outputs/MainOutR");

  TJBox_ObjectRef cp = JBox_GetMotherboardObjectRef("/custom_properties");
  fOnOffPropRef = JBox_MakePropertyRef(cp, "builtin_onoffbypass");

  fCVAmountValueRef = JBox_MakePropertyRef(
      JBox_GetMotherboardObjectRef("/cv_inputs/Amount_CV"), "value");
  fCVToneValueRef = JBox_MakePropertyRef(
      JBox_GetMotherboardObjectRef("/cv_inputs/Tone_CV"), "value");
  fCVCharValueRef = JBox_MakePropertyRef(
      JBox_GetMotherboardObjectRef("/cv_inputs/Character_CV"), "value");
  fInLConnRef = JBox_MakePropertyRef(fInLRef, "connected");
  fInRConnRef = JBox_MakePropertyRef(fInRRef, "connected");

  // Host transport (LFO sync). Read per batch.
  {
    TJBox_ObjectRef transport = JBox_GetMotherboardObjectRef("/transport");
    fTempoRef            = JBox_MakePropertyRef(transport, "tempo");
    fTransportPlayingRef = JBox_MakePropertyRef(transport, "playing");
    fPlayPosRef          = JBox_MakePropertyRef(transport, "play_pos");
  }

  // LFO + animation outputs for the display (rt_owner).
  fLfoPhaseOutRef = JBox_MakePropertyRef(cp, "LFO_Phase_Out");
  fModAmtOutRef   = JBox_MakePropertyRef(cp, "Mod_Amount_Out");
  fCvConnRef[0] = JBox_MakePropertyRef(JBox_GetMotherboardObjectRef("/cv_inputs/Amount_CV"), "connected");
  fCvConnRef[1] = JBox_MakePropertyRef(JBox_GetMotherboardObjectRef("/cv_inputs/Tone_CV"), "connected");
  fCvConnRef[2] = JBox_MakePropertyRef(JBox_GetMotherboardObjectRef("/cv_inputs/Character_CV"), "connected");
  fCvMaskRef    = JBox_MakePropertyRef(cp, "UI_CvMask");
  fModToneOutRef  = JBox_MakePropertyRef(cp, "Mod_Tone_Out");
  fModCharOutRef  = JBox_MakePropertyRef(cp, "Mod_Character_Out");
  fAnimClockRef   = JBox_MakePropertyRef(cp, "UI_AnimClock");
  fEditMaskRef    = JBox_MakePropertyRef(cp, "UI_EditMask");
  fLfoCvOutRef    = JBox_MakePropertyRef(JBox_GetMotherboardObjectRef("/cv_outputs/LFO_CV_Out"), "value");

  fMeterLRef     = JBox_MakePropertyRef(cp, "Input_MeterL");
  fMeterRRef     = JBox_MakePropertyRef(cp, "Input_MeterR");
  fMeterLPeakRef = JBox_MakePropertyRef(cp, "Input_MeterL_Peak");
  fMeterRPeakRef = JBox_MakePropertyRef(cp, "Input_MeterR_Peak");
  {
    const float batchTime = static_cast<float>(kBatchFrames) / static_cast<float>(fSampleRate);
    fPpmFallPerBatch  = (24.0f / 2.8f) * batchTime;         // PPM: 24 dB in 2.8 s
    fPeakFallPerBatch = 24.0f * batchTime;                  // peak line: 24 dB/s after the hold
    fPeakHoldBatches  = static_cast<int>(5.0f / batchTime); // hold 5 s
    if(fPeakHoldBatches < 1) fPeakHoldBatches = 1;
    // Everything that used to be counted in 48 kHz batches, from real time.
    fMeterWriteInterval = std::max(1, static_cast<int>(0.0213f / batchTime + 0.5f));
    fLfoPublishInterval = std::max(1, static_cast<int>(0.0107f / batchTime + 0.5f));
    fSlew = 1.0f - std::exp(-batchTime / kSlewSec);
    // Brown noise: the 48 kHz leak 0.98 (corner ~155 Hz) at any rate, with the
    // gain renormalised so its level matches the 48 kHz one.
    fBrownLeak = std::pow(0.98f, 48000.0f / static_cast<float>(fSampleRate));
    fBrownIn   = 1.0f - fBrownLeak;
    fBrownGain = 10.0f * std::sqrt((0.02f / 1.98f) / (fBrownIn / (1.0f + fBrownLeak)));
  }

  // Seed caches from live values so the first batch is correct.
  {
    auto num = [&](TJBox_Tag t) { return JBox_GetNumber(JBox_LoadMOMPropertyByTag(cp, t)); };
    fCached_Amount      = static_cast<float>(num(kTag_Amount));
    fCached_Tone        = static_cast<float>(num(kTag_Tone));
    fCached_Character   = static_cast<float>(num(kTag_Character));
    fCached_Model       = static_cast<int>(num(kTag_Model) + 0.5);
    fCached_LfoOn       = static_cast<int>(num(kTag_LfoOn) + 0.5);
    fCached_LfoRate     = static_cast<float>(num(kTag_LfoRate));
    fCached_LfoRateSync = static_cast<int>(num(kTag_LfoRateSync) + 0.5);
    fCached_LfoSync     = static_cast<int>(num(kTag_LfoSync) + 0.5);
    fCached_LfoShape    = static_cast<int>(num(kTag_LfoShape) + 0.5);
    fCached_LfoDepth[0] = static_cast<float>(num(kTag_LfoDepthAmt));
    fCached_LfoDepth[1] = static_cast<float>(num(kTag_LfoDepthTone));
    fCached_LfoDepth[2] = static_cast<float>(num(kTag_LfoDepthChar));
    fCached_LfoPhase[0] = static_cast<float>(num(kTag_LfoPhaseAmt));
    fCached_LfoPhase[1] = static_cast<float>(num(kTag_LfoPhaseTone));
    fCached_LfoPhase[2] = static_cast<float>(num(kTag_LfoPhaseChar));
    fCached_InLevel     = static_cast<float>(num(kTag_InLevel));
    fCached_OutLevel    = static_cast<float>(num(kTag_OutLevel));
    fInGain             = trimGain(fCached_InLevel);
    fOutGain            = trimGain(fCached_OutLevel);
  }
  fCached_OnOff = static_cast<int>(JBox_GetNumber(JBox_LoadMOMProperty(fOnOffPropRef)));
  fModAmt = fCached_Amount; fModTone = fCached_Tone; fModChar = fCached_Character;
  fAnimAmtSm = fCached_Amount;

  // One-pole LP coefficient for the tilt split (~700 Hz).
  const float twoPi = 2.0f * 3.14159265358979f;
  // (TONE tilt split coefficient is computed per-batch from the per-mode pivot fM_tilt.)

  // Pre-drive conditioning filters feeding the shaper.
  fPreHpCoef = std::exp(-twoPi * 15.0f    / static_cast<float>(fSampleRate)); // 1-pole HP @15 Hz
  fPreLpCoef = 1.0f - std::exp(-twoPi * 18500.0f / static_cast<float>(fSampleRate)); // 1-pole LP @18.5 kHz

  // Post-shaper DC blocker (~10 Hz) to remove asymmetry's DC term.
  fDcCoef = std::exp(-twoPi * 10.0f / static_cast<float>(fSampleRate));

  // Pre/de-emphasis split (~900 Hz) for engaging the top end in the shaper.
  fEmphCoef = 1.0f - std::exp(-twoPi * 900.0f / static_cast<float>(fSampleRate));

  // High-band octave-up exciter split (~1.2 kHz).
  fAirCoef = 1.0f - std::exp(-twoPi * 1200.0f / static_cast<float>(fSampleRate));

  // Air shelf split (~7 kHz).
  fShelfCoef = 1.0f - std::exp(-twoPi * 7000.0f / static_cast<float>(fSampleRate));

  // Transparent auto-makeup level envelope (~20 ms): smooth so it controls loudness
  // without distorting the waveform or pumping.
  fEnvCoef = 1.0f - std::exp(-1.0f / (0.020f * static_cast<float>(fSampleRate)));

  // Seed slewed values to the loaded model + drive (no startup ramp).
  {
    int sel = fCached_Model;
    if(sel < 0) sel = 0; else if(sel > kNumModels - 1) sel = kNumModels - 1;
    const VoiceModel &M0 = kModels[sel];
    fM_air = M0.air; fM_fuzz = M0.fuzz; fM_freq = M0.freq;
    fM_neutralFold = M0.neutralFold; fM_noiseOff = M0.noiseOff; fM_hp = M0.hpHz; fM_lp = M0.lpHz;
    fM_bellFreq = M0.bellFreq; fM_bellQ = M0.bellQ; fM_bellGain = M0.bellGainDb;
    fM_bell2Freq = M0.bell2Freq; fM_bell2Q = M0.bell2Q; fM_bell2Gain = M0.bell2GainDb;
    fM_outTrim = M0.outTrimDb; fM_outSlope = M0.outTrimSlope; fM_tilt = M0.tiltHz;
    fM_charRange = M0.charRange; fM_asym = M0.asym; fM_foldScale = M0.foldScale;
    fCharLoHz = kCharEq[sel].loHz; fCharHiHz = kCharEq[sel].hiHz;
    fSlewDrive = fCached_Amount;
    fSlewChar  = fCached_Character;
    fSlewTone  = fCached_Tone;
    fM_hardClip = M0.hardClip;
    fSlewInit = true;
  }

  fInitialized = true;
}

void Device::readInputBuffer(TJBox_ObjectRef iInputRef, TJBox_PropertyRef iConnRef, float *oBuffer) noexcept
{
  // An unconnected input leaves stale data in the DSP buffer: read it as silence.
  if(!JBox_GetBoolean(JBox_LoadMOMProperty(iConnRef)))
  {
    for(int i = 0; i < kBatchFrames; ++i) oBuffer[i] = 0.0f;
    return;
  }
  TJBox_Value cv = JBox_LoadMOMPropertyByTag(iInputRef, kJBox_AudioInputBuffer);
  JBox_GetDSPBufferData(cv, 0, kBatchFrames, oBuffer);
}

void Device::writeOutputBuffer(TJBox_ObjectRef iOutputRef, const float *iBuffer) noexcept
{
  TJBox_Value cv = JBox_LoadMOMPropertyByTag(iOutputRef, kJBox_AudioOutputBuffer);
  JBox_SetDSPBufferData(cv, 0, kBatchFrames, iBuffer);
}

// =============================================================================
// Input meter: SpiritLevel PPM ballistics (see Saturator.h), stored decimated
// and only on change, so a silent / steady input costs no host writes.
// =============================================================================
void Device::updateMeter(float iPeakL, float iPeakR) noexcept
{
  const float dbL = dbFromPeak(iPeakL), dbR = dbFromPeak(iPeakR);
  fMeterL = (dbL > fMeterL) ? dbL : std::max(fMeterL - fPpmFallPerBatch, -72.0f);
  fMeterR = (dbR > fMeterR) ? dbR : std::max(fMeterR - fPpmFallPerBatch, -72.0f);
  if(dbL >= fMeterLPeak)                   { fMeterLPeak = dbL; fMeterHoldL = 0; }
  else if(++fMeterHoldL >= fPeakHoldBatches) fMeterLPeak = std::max(fMeterLPeak - fPeakFallPerBatch, -72.0f);
  if(dbR >= fMeterRPeak)                   { fMeterRPeak = dbR; fMeterHoldR = 0; }
  else if(++fMeterHoldR >= fPeakHoldBatches) fMeterRPeak = std::max(fMeterRPeak - fPeakFallPerBatch, -72.0f);
  if(fMeterHoldL > fPeakHoldBatches) fMeterHoldL = fPeakHoldBatches;   // no int overflow on long silence
  if(fMeterHoldR > fPeakHoldBatches) fMeterHoldR = fPeakHoldBatches;
  if(++fMeterBatchCounter >= fMeterWriteInterval)
  {
    fMeterBatchCounter = 0;
    const TJBox_PropertyRef refs[4] = { fMeterLRef, fMeterRRef, fMeterLPeakRef, fMeterRPeakRef };
    const float vals[4] = { meter01FromDb(fMeterL), meter01FromDb(fMeterR),
                            meter01FromDb(fMeterLPeak), meter01FromDb(fMeterRPeak) };
    for(int k = 0; k < 4; ++k)
      if(vals[k] != fMeterPub[k]) { fMeterPub[k] = vals[k]; JBox_StoreMOMProperty(refs[k], JBox_MakeNumber(vals[k])); }
  }
}

// =============================================================================
// Icon animation clock (display only; runs in every on/off state). Integrates
// real time x (0.6 + 0.6 * slewed Amount), LFO-modulated Amount included.
// Stored NORMALIZED (clock / 3600): a jbox.number must stay in 0..1.
// =============================================================================
void Device::updateAnimClock() noexcept
{
  const double dt  = static_cast<double>(kBatchFrames) / static_cast<double>(fSampleRate);
  const float  amt = (fCached_LfoOn != 0) ? fModAmt : fCached_Amount;
  const float  a   = std::isfinite(amt) ? std::min(1.0f, std::max(0.0f, amt)) : 0.0f;
  const float  k   = static_cast<float>(1.0 - std::exp(-dt / 0.15));   // ~150 ms slew
  fAnimAmtSm += k * (a - fAnimAmtSm);
  fAnimClock += dt * (0.6 + 0.6 * static_cast<double>(fAnimAmtSm));
  if(fAnimClock >= 3600.0) fAnimClock -= 3600.0;
  const int interval = std::max(1, static_cast<int>(0.04 / dt));        // ~every 40 ms
  if(++fAnimPublishCounter >= interval)
  {
    fAnimPublishCounter = 0;
    JBox_StoreMOMProperty(fAnimClockRef, JBox_MakeNumber(fAnimClock / 3600.0));
  }
}

// =============================================================================
// LFO -> Amount / Tone / Character (the Degrader's LFO, unchanged).
//   Sync ON, transport playing : phase locked to the song position.
//   Sync ON, stopped           : free-runs at the synced rate from the tempo.
//   Sync OFF                   : free-runs, 0.05 .. 60 Hz.
// The phase advances per batch; process() evaluates it per 16-sample
// sub-block (lfoOffsets) and adds it AFTER the knob slew, so fast rates keep
// their depth (the ~27 ms knob slew was a ~6 Hz low-pass on the LFO: -11 dB
// at 20 Hz, -20 dB at 60 Hz; Erik 2026-09-22).
// =============================================================================
void Device::updateLfo() noexcept
{
  fModAmt = fCached_Amount; fModTone = fCached_Tone; fModChar = fCached_Character;
  fLfoRaw = 0.0f;
  fLfoPhasePrev = fLfoPhase;
  if(fCached_LfoOn == 0)
    return;

  const double sr = static_cast<double>(fSampleRate);
  if(fCached_LfoSync != 0)
  {
    int di = fCached_LfoRateSync;
    if(di < 0) di = 0; else if(di > sat::kNumDivs - 1) di = sat::kNumDivs - 1;
    const double beats = sat::kDivBeats[di];
    const double ppq   = fTransportPlaying ? JBox_GetNumber(JBox_LoadMOMProperty(fPlayPosRef)) : -1.0;
    if(fTransportPlaying && std::isfinite(ppq) && ppq >= 0.0)
      fLfoPhase = (ppq / 15360.0) / beats;                       // locked to the song
    else
      fLfoPhase += (fTempoBPM / 60.0) / beats * kBatchFrames / sr;
  }
  else
    fLfoPhase += sat::freeHz(fCached_LfoRate) * kBatchFrames / sr;
  if(!std::isfinite(fLfoPhase) || fLfoPhase > 1.0e6) fLfoPhase = 0.0;

  int shape = fCached_LfoShape;
  if(shape < 0) shape = 0; else if(shape > sat::kNumLfoShapes - 1) shape = sat::kNumLfoShapes - 1;
  fLfoRaw  = static_cast<float>(sat::lfoValue(shape, fLfoPhase));   // for the CV out
  fModAmt  = static_cast<float>(sat::modulated(fCached_Amount,    fCached_LfoDepth[0],
                                sat::lfoValue(shape, fLfoPhase + fCached_LfoPhase[0])));
  fModTone = static_cast<float>(sat::modulated(fCached_Tone,      fCached_LfoDepth[1],
                                sat::lfoValue(shape, fLfoPhase + fCached_LfoPhase[1])));
  fModChar = static_cast<float>(sat::modulated(fCached_Character, fCached_LfoDepth[2],
                                sat::lfoValue(shape, fLfoPhase + fCached_LfoPhase[2])));
}

// LFO offsets at fraction t through the batch: the phase is interpolated from
// the batch start to its end. A backwards or large jump (song-position relock
// on a loop, the NaN guard) holds the new phase instead of sweeping across it.
void Device::lfoOffsets(float t, float &oAmt, float &oTone, float &oChar) const noexcept
{
  double ph = fLfoPhase;
  const double d = fLfoPhase - fLfoPhasePrev;
  if(d >= 0.0 && d < 0.5) ph = fLfoPhasePrev + d * static_cast<double>(t);
  int shape = fCached_LfoShape;
  if(shape < 0) shape = 0; else if(shape > sat::kNumLfoShapes - 1) shape = sat::kNumLfoShapes - 1;
  const double a = sat::modulated(fCached_Amount,    fCached_LfoDepth[0], sat::lfoValue(shape, ph + fCached_LfoPhase[0]));
  const double o = sat::modulated(fCached_Tone,      fCached_LfoDepth[1], sat::lfoValue(shape, ph + fCached_LfoPhase[1]));
  const double c = sat::modulated(fCached_Character, fCached_LfoDepth[2], sat::lfoValue(shape, ph + fCached_LfoPhase[2]));
  oAmt  = std::pow(static_cast<float>(a), kDriveTaper) - std::pow(fCached_Amount, kDriveTaper);
  oTone = static_cast<float>(o) - fCached_Tone;
  oChar = static_cast<float>(c) - fCached_Character;
}

// Decimated (fLfoPublishInterval batches, ~10 ms) and only on change, so an idle LFO
// costs nothing and a running one ~100 small writes a second.
void Device::publishLfo() noexcept
{
  if(++fLfoPublishCounter < fLfoPublishInterval)
    return;
  fLfoPublishCounter = 0;
  auto pub = [](TJBox_PropertyRef ref, float v, float &last) {
    if(std::fabs(v - last) > 1.0e-4f) { last = v; JBox_StoreMOMProperty(ref, JBox_MakeNumber(v)); }
  };
  pub(fLfoPhaseOutRef, static_cast<float>(fLfoPhase - std::floor(fLfoPhase)), fPubPhase);
  // Knob + CV + LFO, as the engine uses them (the CV scaling matches the DSP:
  // Amount full, Tone half, Style half). Unconnected jacks read as 0.
  int mask = 0;
  float cv[3] = { 0.0f, 0.0f, 0.0f };
  const TJBox_PropertyRef cvRef[3] = { fCVAmountValueRef, fCVToneValueRef, fCVCharValueRef };
  for(int k = 0; k < 3; ++k)
    if(JBox_GetBoolean(JBox_LoadMOMProperty(fCvConnRef[k])))
    {
      mask |= 1 << k;
      const float x = static_cast<float>(JBox_GetNumber(JBox_LoadMOMProperty(cvRef[k])));
      cv[k] = std::isfinite(x) ? x : 0.0f;
    }
  auto clamp01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
  pub(fModAmtOutRef,  clamp01(fModAmt  + cv[0]),         fPubAmt);
  pub(fModToneOutRef, clamp01(fModTone + 0.5f * cv[1]),  fPubTone);
  pub(fModCharOutRef, clamp01(fModChar + 0.5f * cv[2]), fPubChar);
  if(mask != fCvMaskPub)
  {
    fCvMaskPub = mask;
    JBox_StoreMOMProperty(fCvMaskRef, JBox_MakeNumber(static_cast<double>(mask) / 7.0));
  }
}

// LFO CV out: the raw LFO (-1..+1), every batch in every on/off
// state (a CV source keeps running through bypass), stored only on change.
void Device::writeLfoCv() noexcept
{
  const float v = fLfoRaw;
  if(v != fLfoCvPub) { fLfoCvPub = v; JBox_StoreMOMProperty(fLfoCvOutRef, JBox_MakeNumber(v)); }
}

// Edit readout (Erik, 2026-09-27). Any change to in / amount / tone / style / out
// -- from the panel, automation, Remote or a patch -- holds that control's bit
// in UI_EditMask for kEditHoldSec (0.5 s); a panel drag holds it until the mouse
// is released (UI_Editing, from the overlays). The display covers the label row and
// shows the value(s). Runs before every early return (OFF / BYPASS too).
// The first batch's diffs are the initial load and are skipped.
void Device::updateEditMask(const TJBox_PropertyDiff iDiffs[], TJBox_UInt32 iCount) noexcept
{
  const int hold = static_cast<int>(kEditHoldSec * static_cast<float>(fSampleRate) / kBatchFrames) + 1;
  if(fEditPrimed)
    for(TJBox_UInt32 i = 0; i < iCount; ++i)
    {
      int b = -1;
      switch(iDiffs[i].fPropertyTag)
      {
        case kTag_InLevel:   b = 0; break;
        case kTag_Amount:    b = 1; break;
        case kTag_Tone:      b = 2; break;
        case kTag_Character: b = 3; break;
        case kTag_OutLevel:  b = 4; break;
        default: break;
      }
      if(b >= 0) fEditHold[b] = hold;
    }
  // A panel drag (the overlays' UI_Editing, applied AFTER the value diffs so a
  // release in the same batch wins): held for as long as the mouse is down --
  // even when it stops moving -- and dropped the moment it is released.
  for(TJBox_UInt32 i = 0; i < iCount; ++i)
    if(iDiffs[i].fPropertyTag == kTag_UiEditing)
    {
      const int e = static_cast<int>(JBox_GetNumber(iDiffs[i].fCurrentValue) + 0.5) - 1;
      if(e < 0 && fEditingCtl >= 0) fEditHold[fEditingCtl] = 0;          // released: gone now
      fEditingCtl = (e >= 0 && e < 5) ? e : -1;
    }
  if(fEditingCtl >= 0) fEditHold[fEditingCtl] = hold;
  fEditPrimed = true;
  int mask = 0;
  for(int b = 0; b < 5; ++b)
    if(fEditHold[b] > 0) { --fEditHold[b]; if(fEditHold[b] > 0) mask |= 1 << b; }
  if(mask != fEditMaskPub)
  {
    fEditMaskPub = mask;
    JBox_StoreMOMProperty(fEditMaskRef, JBox_MakeNumber(static_cast<double>(mask) / 31.0));
  }
}

void Device::renderBatch(const TJBox_PropertyDiff iPropertyDiffs[],
                         TJBox_UInt32 iDiffCount) noexcept
{
  ensureInit();

  // --- diff loop: refresh cached knob values ---------------------------
  for(TJBox_UInt32 i = 0; i < iDiffCount; ++i)
  {
    const TJBox_PropertyDiff &d = iPropertyDiffs[i];
    switch(d.fPropertyTag)
    {
      case kTag_Amount:       fCached_Amount      = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_Tone:         fCached_Tone        = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_Character:    fCached_Character   = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_Model:        fCached_Model       = static_cast<int>(JBox_GetNumber(d.fCurrentValue) + 0.5); break;
      case kTag_LfoOn:        fCached_LfoOn       = static_cast<int>(JBox_GetNumber(d.fCurrentValue) + 0.5); break;
      case kTag_LfoRate:      fCached_LfoRate     = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_LfoRateSync:  fCached_LfoRateSync = static_cast<int>(JBox_GetNumber(d.fCurrentValue) + 0.5); break;
      case kTag_LfoSync:      fCached_LfoSync     = static_cast<int>(JBox_GetNumber(d.fCurrentValue) + 0.5); break;
      case kTag_LfoShape:     fCached_LfoShape    = static_cast<int>(JBox_GetNumber(d.fCurrentValue) + 0.5); break;
      case kTag_LfoDepthAmt:  fCached_LfoDepth[0] = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_LfoDepthTone: fCached_LfoDepth[1] = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_LfoDepthChar: fCached_LfoDepth[2] = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_LfoPhaseAmt:  fCached_LfoPhase[0] = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_LfoPhaseTone: fCached_LfoPhase[1] = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_LfoPhaseChar: fCached_LfoPhase[2] = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_InLevel:      fCached_InLevel     = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      case kTag_OutLevel:     fCached_OutLevel    = static_cast<float>(JBox_GetNumber(d.fCurrentValue));     break;
      default: break;
    }
  }
  updateEditMask(iPropertyDiffs, iDiffCount);   // the display's value readout

  fCached_OnOff = static_cast<int>(JBox_GetNumber(JBox_LoadMOMProperty(fOnOffPropRef)));

  float inL[kBatchFrames], inR[kBatchFrames];
  readInputBuffer(fInLRef, fInLConnRef, inL);
  readInputBuffer(fInRRef, fInRConnRef, inR);

  // --- input level meter: peak per channel, exp decay, decimated write.
  // Runs in every state (on/off/bypass) so the meter always shows what is
  // coming IN, regardless of processing.
  // Sanitize the input FIRST: a NaN/Inf sample from an upstream device would
  // otherwise poison foldTri (Inf-Inf = NaN) and every filter state permanently,
  // and end up stored in our output buffer (an SDK violation that kills the
  // rest of the chain). Non-finite samples become silence; the batch goes on.
  float peakL = 0.0f, peakR = 0.0f;
  for(int n = 0; n < kBatchFrames; ++n)
  {
    if(!std::isfinite(inL[n])) inL[n] = 0.0f;
    if(!std::isfinite(inR[n])) inR[n] = 0.0f;
    const float al = std::fabs(inL[n]); if(al > peakL) peakL = al;
    const float ar = std::fabs(inR[n]); if(ar > peakR) peakR = ar;
  }
  updateMeter(peakL, peakR);

  // Mono -> stereo spread (Erik, 2026-09-22): with only LEFT IN connected, the
  // left signal feeds the right channel too, so the processing and the output
  // are stereo (every stereo stage -- LFO phase, per-channel noise -- then gets
  // two channels to work on). After the meter, so the meter still shows what
  // is actually plugged in. Right-only stays as it is.
  if(JBox_GetBoolean(JBox_LoadMOMProperty(fInLConnRef)) && !JBox_GetBoolean(JBox_LoadMOMProperty(fInRConnRef)))
    for(int n = 0; n < kBatchFrames; ++n) inR[n] = inL[n];

  // Host transport snapshot (LFO sync). Inverted guard on purpose: NaN fails
  // every comparison, so it falls back to 120 BPM.
  fTempoBPM = JBox_GetNumber(JBox_LoadMOMProperty(fTempoRef));
  if(!(fTempoBPM >= 1.0 && fTempoBPM <= 999.0)) fTempoBPM = 120.0;
  fTransportPlaying = JBox_GetBoolean(JBox_LoadMOMProperty(fTransportPlayingRef));

  // LFO runs in every on/off state, so the display's dot and the icon speed
  // keep moving while the device is bypassed.
  updateLfo();
  publishLfo();
  writeLfoCv();
  updateAnimClock();

  if(fCached_OnOff == 0) // OFF -> silence
  {
    // Write NOTHING: an unwritten output buffer is silence, and leaving it
    // unwritten lets the host's silence optimisation idle the chain (writing
    // zeros would defeat it). Same as the Degrader.
    fSilentRun = 0;
    return;
  }
  if(fCached_OnOff == 2) // BYPASS -> passthrough
  {
    fSilentRun = 0;
    if(peakL > 0.0f || peakR > 0.0f)   // silent input: leave the outputs unwritten, as above
    {
      writeOutputBuffer(fOutLRef, inL);
      writeOutputBuffer(fOutRRef, inR);
    }
    return;
  }

  // --- IN bar: gain into the saturator (slewed; unity at the default) ---
  {
    const float tgt = trimGain(fCached_InLevel);
    const float a   = 1.0f - std::exp(-1.0f / (kLvlTrimMs * 0.001f * static_cast<float>(fSampleRate)));
    if(fInGain != tgt || tgt != 1.0f)
      for(int n = 0; n < kBatchFrames; ++n)
      {
        fInGain += a * (tgt - fInGain);
        inL[n] *= fInGain; inR[n] *= fInGain;
      }
  }

  // ON -> process. Knob + CV targets are slewed (~27 ms, anti-zipper); the LFO
  // is added AFTER the slew, per 16-sample sub-block (lfoOffsets), so it keeps
  // its full depth at fast rates.
  // Amount (+ Amount_CV, bipolar full swing), clamped to 0..1. kDriveTaper=1.0
  // means the dry/wet tracks the knob linearly (knob position = wet amount) for
  // precise blending; raise the taper >1 for more resolution at the dry end.
  float cvAmount = static_cast<float>(JBox_GetNumber(JBox_LoadMOMProperty(fCVAmountValueRef)));
  // A non-finite CV sample would ride into fSlewDrive and stay NaN FOREVER
  // (NaN += k*(x-NaN) is NaN even after the CV cleans up) — and the 0..1
  // clamps below don't catch NaN because both comparisons are false.
  if(!std::isfinite(cvAmount)) cvAmount = 0.0f;
  float driveTgt = std::pow(fCached_Amount, kDriveTaper) + cvAmount;   // knob + CV (LFO added after the slew)
  if(driveTgt < 0.0f) driveTgt = 0.0f;
  if(driveTgt > 1.0f) driveTgt = 1.0f;

  // --- Slew Drive + the selected MODEL's voicing centres (~27 ms one-pole) so
  //     switching models / sweeping Drive morphs smoothly instead of clicking. -
  int sel = fCached_Model;
  if(sel < 0) sel = 0; else if(sel > kNumModels - 1) sel = kNumModels - 1;
  const VoiceModel &MT = kModels[sel];
  fSlewDrive  += fSlew * (driveTgt   - fSlewDrive);
  fM_air      += fSlew * (MT.air      - fM_air);
  fM_fuzz     += fSlew * (MT.fuzz     - fM_fuzz);
  fM_freq     += fSlew * (MT.freq     - fM_freq);
  fM_neutralFold += fSlew * (MT.neutralFold - fM_neutralFold);
  fM_noiseOff += fSlew * (MT.noiseOff - fM_noiseOff);
  fM_hp       += fSlew * (MT.hpHz     - fM_hp);
  fM_lp       += fSlew * (MT.lpHz     - fM_lp);
  fM_bellFreq += fSlew * (MT.bellFreq   - fM_bellFreq);
  fM_bellQ    += fSlew * (MT.bellQ      - fM_bellQ);
  fM_bellGain += fSlew * (MT.bellGainDb - fM_bellGain);
  fM_bell2Freq += fSlew * (MT.bell2Freq   - fM_bell2Freq);
  fM_bell2Q    += fSlew * (MT.bell2Q      - fM_bell2Q);
  fM_bell2Gain += fSlew * (MT.bell2GainDb - fM_bell2Gain);
  fM_outTrim  += fSlew * (MT.outTrimDb    - fM_outTrim);
  fM_outSlope += fSlew * (MT.outTrimSlope - fM_outSlope);
  fM_tilt     += fSlew * (MT.tiltHz       - fM_tilt);
  fM_charRange += fSlew * (MT.charRange     - fM_charRange);
  fM_asym     += fSlew * (MT.asym         - fM_asym);
  fM_foldScale+= fSlew * (MT.foldScale    - fM_foldScale);
  fCharLoHz   += fSlew * (kCharEq[sel].loHz - fCharLoHz);
  fCharHiHz   += fSlew * (kCharEq[sel].hiHz - fCharHiHz);
  // Tone + Tone_CV: the CV adds to the bipolar Tone (-1..+1) as on the
  // Degrader, i.e. half of it in knob units.
  float cvTone = static_cast<float>(JBox_GetNumber(JBox_LoadMOMProperty(fCVToneValueRef)));
  if(!std::isfinite(cvTone)) cvTone = 0.0f;            // same NaN-in-slew guard
  float toneTgt = fCached_Tone + 0.5f * cvTone;
  if(toneTgt < 0.0f) toneTgt = 0.0f; else if(toneTgt > 1.0f) toneTgt = 1.0f;
  fSlewTone   += fSlew * (toneTgt - fSlewTone);        // slew TONE (anti-zipper)
  fM_hardClip += fSlew * (MT.hardClip  - fM_hardClip); // crossfade clip type on mode change

  // --- Idle: silent input and our tail has died -> don't render at all ---
  // (Counted in fSilentRun after the render below.) The slews above keep
  // tracking, so Drive/Mode/Character are current when audio returns.
  const bool inSilent = (peakL <= kJBox_SilentThreshold && peakR <= kJBox_SilentThreshold);
  const int  idleBatches = static_cast<int>(kIdleSec * static_cast<float>(fSampleRate)
                                            / static_cast<float>(kBatchFrames));
  if(!inSilent) { fSilentRun = 0; fIdleFlushed = false; }
  else if(fSilentRun >= idleBatches)
  {
    if(!fIdleFlushed) { flushDspState(); fIdleFlushed = true; }   // once: rest state, no denormals
    return;                                                        // outputs unwritten = silent
  }


  // CHARACTER macro: bipolar, 0.5 = neutral. Character_CV adds to it. Slewed.
  float cvChar  = static_cast<float>(JBox_GetNumber(JBox_LoadMOMProperty(fCVCharValueRef)));
  if(!std::isfinite(cvChar)) cvChar = 0.0f;   // same NaN-in-slew guard as Amount
  // Half in knob units, like Tone here and Tone + Style on the Degrader: a
  // full-scale bipolar CV (-1..+1) sweeps the bipolar knob from its centre to
  // either end. It used to add the CV whole, twice the Degrader's reach, for
  // no reason (Erik, 2026-10-01: make the two coherent).
  float charTgt = fCached_Character + 0.5f * cvChar;
  if(charTgt < 0.0f) charTgt = 0.0f; else if(charTgt > 1.0f) charTgt = 1.0f;
  fSlewChar += fSlew * (charTgt - fSlewChar);

  // LFO sub-blocks: with the LFO on, the control -> coefficient maths runs per
  // 16 samples with the LFO evaluated at each sub-block's end and added to the
  // slewed knob values; every derived value then glides per sample. LFO off: one block
  // of 64 and zero offsets -- the exact arithmetic of the old single pass.
  const bool lfoRun = (fCached_LfoOn != 0);
  const int  nSub   = lfoRun ? kLfoSubBlocks : 1;
  const int  subLen = kBatchFrames / nSub;
  float outL[kBatchFrames], outR[kBatchFrames];
  for(int sb = 0; sb < nSub; ++sb)
  {
    float offAmt = 0.0f, offTone = 0.0f, offChar = 0.0f;
    // Sampled at the sub-block END: the per-sample ramp below then lands exactly
    // on the LFO curve every subLen samples (linear interpolation, no lag).
    if(lfoRun) lfoOffsets(static_cast<float>(sb + 1) / static_cast<float>(nSub), offAmt, offTone, offChar);
    float driveM = fSlewDrive + offAmt;
    if(driveM < 0.0f) driveM = 0.0f; else if(driveM > 1.0f) driveM = 1.0f;
    float toneM = fSlewTone + offTone;
    if(toneM < 0.0f) toneM = 0.0f; else if(toneM > 1.0f) toneM = 1.0f;
    float charM = fSlewChar + offChar;
    if(charM < 0.0f) charM = 0.0f; else if(charM > 1.0f) charM = 1.0f;
    const float drive  = driveM;
    const float charBi = (charM - 0.5f) * 2.0f;       // -1..+1

    const float tone = toneM * 2.0f - 1.0f;            // -1..+1 (slewed TONE knob + LFO)

    // Coefficient maths (cos / sin / pow / exp) only when one of its inputs
    // moved; a static setting reuses the cached targets (CPU, 2026-09-27).
    const float coefKey[kNumCoefKeys] = {
      drive, charBi, tone,
      fM_bellFreq, fM_bellQ, fM_bellGain, fM_bell2Freq, fM_bell2Q, fM_bell2Gain,
      fCharLoHz, fCharHiHz, fM_charRange, fM_fuzz, fM_foldScale, fM_neutralFold,
      fM_outTrim, fM_outSlope, fM_air, fM_noiseOff, fM_freq, fM_tilt,
      fM_hardClip, fM_asym, fM_hp, fM_lp };
    bool coefSame = fCoefValid;
    for(int i = 0; coefSame && i < kNumCoefKeys; ++i) coefSame = (coefKey[i] == fCoefKey[i]);
    if(!coefSame)
    {
      for(int i = 0; i < kNumCoefKeys; ++i) fCoefKey[i] = coefKey[i];
      fCoefValid = true;

      // Pre-drive TWO-BAND EQ = the mode's fixed signature voice (no longer touched
      // by CHARACTER, which is now purely fold/fuzz). Two peaking bells let a mode
      // place bumps/dips freely: bass+air "smile", or a broad ~3oct mid DIP (Q~0.4,
      // -gain) carving the mid + a narrower ~1.5oct restore bump. TONE shifts both
      // centres by +/-kBellTilt.
      const float toneShift = 1.0f + tone * kBellTilt;
      // Bells = TPT state-variable (Simper) peaking filters: the same response
      // as the RBJ peaking biquad they replace, but numerically robust under
      // per-sample coefficient glides (a 150 Hz TDF2 biquad in float turned
      // the ramp rounding into ~-50 dB gain noise, 2026-09-27).
      float bellC[4], bell2C[4], chLoC[4], chHiC[4];
      const float fsF = static_cast<float>(fSampleRate);
      svfBellCoefs(fM_bellFreq * toneShift,  fM_bellQ,  std::pow(10.0f, fM_bellGain  * 0.025f), fsF, bellC);   // 10^(dB/40)
      svfBellCoefs(fM_bell2Freq * toneShift, fM_bell2Q, std::pow(10.0f, fM_bell2Gain * 0.025f), fsF, bell2C);

      // CHARACTER halves: clockwise (+) intensifies FUZZ, anticlockwise (-) the FOLD.
      // Per-mode CHARACTER range limit: clean/warm modes get a reduced fuzz/fold
      // swing at the extremes (fM_charRange < 1), heavy modes get the full range.
      const float charPos = ((charBi > 0.0f) ?  charBi : 0.0f) * fM_charRange;
      const float charNeg = ((charBi < 0.0f) ? -charBi : 0.0f) * fM_charRange;

      // DRIVE = amount: pushes BOTH shapers (clip gain + fold depth). CHARACTER also
      // INTENSIFIES the chosen flavour so it's audible even on modes whose neutral
      // already leans that way: +Char deepens the fuzz ripple, -Char deepens the fold.
      const float airScale = fM_air;
      const float satGain  = (1.0f + kSatGrow * drive) * (1.0f + kCharSat * charPos) * (1.0f + kFuzzMode * fM_fuzz); // +Character AND per-mode fuzz drive the tanh harder = more clipping/fuzz
      // foldScale is the per-mode MAX fold at Char -100 (clean <1, extreme up to 2x
      // = denser "double-triangle" folding). It scales the -Character fold drive.
      // Fold depth: per-mode foldScale sets the ceiling; Drive only modulates WITHIN it
      // (0.6..1.0), so it no longer compounds drive*foldScale into white noise. Bounded.
      float foldDepth = 1.0f + kCharFold * charNeg * fM_foldScale * (0.6f + 0.4f * drive);
      if(foldDepth > kFoldMax) foldDepth = kFoldMax;

      // ADDITIVE fold+fuzz weights (NOT a crossfade): the shaper output is
      // wFuzz*fuzz + wFold*fold, with the two weights INDEPENDENT. At centre they
      // sum to 1 (= the mode's baked balance). -Character ADDS fold weight on top of
      // the held fuzz (so fold can grow past the fuzz and dominate, not stay a
      // share) AND folds harder (foldDepth); +Character ADDS fuzz weight + ripple.
      // Total distortion therefore RISES toward either extreme; neither is removed.
      const float wFold = fM_neutralFold        + kCharFoldW * charNeg * fM_foldScale; // fold weight: grows on - (per-mode max)
      const float wFuzz = (1.0f - fM_neutralFold) + kCharFuzzW * charPos; // fuzz weight: grows on +
      // Fixed high-engage into the shaper (model-independent; helps the air octave).
      const float preHiGain = 1.0f + kPreEmph * drive;
      // CHARACTER EQ = two peaking bells (per-mode freqs) moved in OPPOSITE directions:
      // -Char boosts the LOW bell + cuts the HIGH bell (darker/thicker); +Char cuts the
      // LOW + boosts the HIGH (brighter/edgier). Centre = flat (0 dB both). Pre-shaper,
      // so it shifts WHICH bands get saturated. Auto-makeup keeps the level constant.
      svfBellCoefs(fCharLoHz, kCharEqQ, std::pow(10.0f, (-charBi * kCharEqDb) * 0.025f), fsF, chLoC); // lo bell: +gain on -Char
      svfBellCoefs(fCharHiHz, kCharEqQ, std::pow(10.0f, ( charBi * kCharEqDb) * 0.025f), fsF, chHiC); // hi bell: +gain on +Char
      const float deEmph   = kDeEmph  * drive;             // high-cut after shaper (<1)
      // Static makeup REMOVED -> replaced by the adaptive transparent auto-makeup in the
      // per-sample loop: it MEASURES wet vs dry level and caps the wet loudness at
      // dry*pushCurve, only CUTTING the excess (never boosts -> clean modes stay clean).
      // pushCurve rises with Drive^2 up to kMaxLift (+12 dB) at max, so Drive can push
      // loudness gently but never more than +12 dB regardless of mode/EQ/input level.
      const float pushCurve = 1.0f + (kMaxLift - 1.0f) * drive * drive;
      // Per-mode loudness makeup (drive-dependent): matches mode output levels.
      const float outGain  = std::pow(10.0f, (fM_outTrim + fM_outSlope * drive) * 0.05f);
      const float airAmt   = (airScale * kAir) * drive;    // octave exciter (Air knob: bite/sizzle)
      // NOISE auto-tracks DRIVE the same way (more hiss when clean, less when driven),
      // plus the model's noise shift.
      // Noise dials back MODERATELY as Drive rises (countering the clipper amplifying
      // hiss at high Drive), but keeps a floor so it never vanishes the way the old
      // aggressive 1.4*(1-d)^2.2 curve did. Range ~0.8 (clean) -> 0.4 (max) for a 0-off mode.
      const float oneMinusD = 1.0f - drive;
      float noise01 = 0.4f + 0.4f * std::pow(oneMinusD, 1.5f)
                    + fM_noiseOff;
      if(noise01 < 0.0f) noise01 = 0.0f; else if(noise01 > 1.0f) noise01 = 1.0f;
      const float noiseDepth = noise01 * kNoiseMax;          // analog-hiss inject
      const float presGain  = std::pow(10.0f, kPresenceDb * drive * 0.05f); // presence shelf
      // The exciter (airAmt) carries the strong air; cap the shelf boost at ~3x so
      // extreme Air settings tilt brighter without purely slamming the soft-clip.
      float shelfScale = airScale; if(shelfScale > 3.0f) shelfScale = 3.0f;
      const float shelfGain = std::pow(10.0f, (shelfScale * kAirShelfDb) * drive * 0.05f); // air shelf
      // Air-shelf centre = the model's shelf frequency.
      float shelfHz = fM_freq;
      if(shelfHz < 1000.0f) shelfHz = 1000.0f; else if(shelfHz > 20000.0f) shelfHz = 20000.0f;
      const float shelfCoef = 1.0f - std::exp(-6.28318530718f * shelfHz / static_cast<float>(fSampleRate));
      // TONE = treble tilt around the per-mode pivot (fM_tilt). Up = BRIGHTER (boost
      // highs, trim lows); down = DARKER with real low lift (cut highs, boost lows).
      // Stronger than before, especially the dark side, per audition feedback.
      const float gHi = std::pow(10.0f,  tone * 8.0f * 0.05f);   // highs: +/-8 dB (symmetric = gain-neutral)
      const float gLo = std::pow(10.0f, -tone * 8.0f * 0.05f);   // lows : -/+8 dB
      // Dry/wet blend is DECOUPLED from distortion amount. The mix ramps up FAST and
      // reaches FULL wet by kWetKnee (~35% Drive) via a quarter-sine (fast early,
      // eases into full); above the knee the mix stays 100% wet while satGain/foldDepth
      // keep rising = the upper Drive range adds distortion intensity, not more wet.
      // This stops medium-Drive from blending medium-dry into every mode (which made
      // them sound alike); each mode's wet character is heard cleanly from ~35% up.
      // 0% Drive = fully clean (dry); the wet (incl. its noise) is gated to zero there.
      float wetN = drive / kWetKnee; if(wetN > 1.0f) wetN = 1.0f;
      const float driveWet = std::sin(wetN * 1.5707963f);
      const float a    = 1.0f - std::exp(-6.28318530718f * fM_tilt / static_cast<float>(fSampleRate));
      // Pre-drive input filters track the model (HP: bass-tight..sub; LP: top BW).
      const float preHpCoef = std::exp(-6.28318530718f * fM_hp / static_cast<float>(fSampleRate));
      const float preLpCoef = 1.0f - std::exp(-6.28318530718f * fM_lp / static_cast<float>(fSampleRate));
      const float bias      = fM_asym * 2.0f;
      const float rc[kNumRamps] = {
        satGain, foldDepth, wFuzz, wFold, preHiGain, deEmph, outGain, airAmt,
        presGain, shelfGain, driveWet, pushCurve, noiseDepth, gHi, gLo,
        bellC[0], bellC[1], bellC[2], bellC[3],
        bell2C[0], bell2C[1], bell2C[2], bell2C[3],
        chLoC[0], chLoC[1], chLoC[2], chLoC[3],
        chHiC[0], chHiC[1], chHiC[2], chHiC[3],
        charBi, fM_hardClip, bias, a, shelfCoef, preHpCoef, preLpCoef, tanhApprox(bias) };
      for(int i = 0; i < kNumRamps; ++i) fRampCur[i] = rc[i];
    }
    const float preLoGain = 1.0f;
    const float boost     = 1.0f;

    // PER-SAMPLE RAMPS (Erik, 2026-09-27: "modulation of amount sounds bit
    // crushed / sample-rate reduced"; then "all mods must be smooth and
    // transparent, like an invisible hand moving the knob"). Every value that
    // Amount / Tone / Style / the model slews drive -- the gains, all four EQ
    // biquads, the noise colour, the clip mix, the filter corners -- glides
    // linearly from the previous sub-block's value to this one's, sample by
    // sample, so LFO, CV, automation and knob moves never step (a 16- or
    // 64-sample staircase is heard as aliasing / crushing). Small per-sample
    // coefficient steps between two stable EQs keep the TDF2 biquads stable.
    // A static setting has zero steps and skips the increments.
    if(!fRampInit) { for(int i = 0; i < kNumRamps; ++i) fRampPrev[i] = fRampCur[i]; fRampInit = true; }
    alignas(16) float rampVal[kRampSlots], rampStep[kRampSlots];   // padded to a multiple of 4 (SIMD)
    bool  moving = false;                           // a static setting skips the increments
    const float invLen = 1.0f / static_cast<float>(subLen);
    rampVal[kRampSlots - 1] = rampStep[kRampSlots - 1] = 0.0f;   // pad slot
    for(int i = 0; i < kNumRamps; ++i)
    {
      rampVal[i]  = fRampPrev[i];
      rampStep[i] = (fRampCur[i] - fRampPrev[i]) * invLen;
      if(rampStep[i] != 0.0f) moving = true;
      fRampPrev[i] = fRampCur[i];
    }

    for(int n = sb * subLen; n < (sb + 1) * subLen; ++n)
    {
      if(moving)   // one straight (vectorisable) pass beats an indexed walk over only the movers
      {
        for(int i = 0; i < kRampSlots; ++i) rampVal[i] += rampStep[i];   // reaches fRampCur at the last sample
      }
      const float satGain = rampVal[0],  foldDepth = rampVal[1],  wFuzz = rampVal[2],  wFold = rampVal[3];
      const float preHiGain = rampVal[4], deEmph = rampVal[5],    outGain = rampVal[6], airAmt = rampVal[7];
      const float presGain = rampVal[8], shelfGain = rampVal[9],  driveWet = rampVal[10], pushCurve = rampVal[11];
      const float noiseDepth = rampVal[12], gHi = rampVal[13],    gLo = rampVal[14];
      const float* const b1c = &rampVal[15];   // bell 1       (a1 a2 a3 m1)
      const float* const b2c = &rampVal[19];   // bell 2
      const float* const clc = &rampVal[23];   // Character low bell
      const float* const chc = &rampVal[27];   // Character high bell
      const float nzCol = rampVal[31], hardMix = rampVal[32], bias = rampVal[33], a = rampVal[34];
      const float shelfCoef = rampVal[35], preHpCoef = rampVal[36], preLpCoef = rampVal[37], tanhBias = rampVal[38];
      // ---- L ----
      float xL = inL[n];                               // clean dry reference
      // inject multiplicative analog hiss on the WET path (auto-gates: x=0 -> no noise)
      // Character-colored noise: brown (low) at -Char .. white at 0 .. blue (high) at +Char
      float wnL = whiteNoise();
      fNzBrownL = fBrownLeak * fNzBrownL + fBrownIn * wnL;        // leaky integrator -> brown
      float blueL = wnL - fNzPrevL; fNzPrevL = wnL;       // first difference -> blue
      float nzL = (nzCol >= 0.0f) ? ((1.0f - nzCol) * wnL + nzCol * 0.71f * blueL)
                                   : ((1.0f + nzCol) * wnL + (-nzCol) * fBrownGain * fNzBrownL);
      float drvL = xL * (1.0f + noiseDepth * nzL);
      // pre-drive conditioning (HP @15 Hz -> LP @18.5 kHz); feeds the shaper only,
      // so the dry path (xL) stays untouched for a true dry/wet mix.
      float hpL = drvL - fPreHpXL + preHpCoef * fPreHpYL;
      fPreHpXL = drvL; fPreHpYL = hpL;
      fPreLpStateL += preLpCoef * (hpL - fPreLpStateL);
      // pre-drive 2-band bell (peaking EQ, transposed DF2, in series) -> mode voice
      const float bL = svfBell(fPreLpStateL, fBellZ1L, fBellZ2L, b1c);
      const float cL = svfBell(bL, fBell2Z1L, fBell2Z2L, b2c);
      const float dL = svfBell(cL, fCLoZ1L, fCLoZ2L, clc);   // CHARACTER EQ bells: low then high (in series)
      const float eL = svfBell(dL, fCHiZ1L, fCHiZ2L, chc);
      // pre-emphasis: boost highs INTO the shaper so the top distorts too
      fEmphLpL += fEmphCoef * (eL - fEmphLpL);
      float xeL = fEmphLpL * preLoGain + (eL - fEmphLpL) * preHiGain;
      // shape directly (no normalizer): louder input distorts harder; makeup tames level
      float wL = shapeTb(xeL, satGain, foldDepth, wFuzz, wFold, bias, tanhBias) * boost;
      // de-emphasis: pull the highs back to rebalance / tame harshness (deEmph < 1)
      fDeLpL += fEmphCoef * (wL - fDeLpL);
      wL = wL - deEmph * (wL - fDeLpL);
      // post-shaper DC block (removes the asymmetry DC term)
      float dcL = wL - fDcXL + fDcCoef * fDcYL; fDcXL = wL; fDcYL = dcL; wL = dcL;
      // high-band octave-up exciter (EVEN harmonics -> smooth "air") + presence shelf
      fAirLpL += fAirCoef * (wL - fAirLpL);
      float airHiL = wL - fAirLpL;
      float sqL = airHiL * airHiL;                  // even harmonics (octave up) + DC
      fAir2L += fDcCoef * (sqL - fAir2L);           // track & remove the DC of the square
      wL = fAirLpL + airHiL * presGain + airAmt * (sqL - fAir2L);
      // air shelf: boost ~7 kHz and up for 10 kHz presence
      fShelfLpL += shelfCoef * (wL - fShelfLpL);
      wL = fShelfLpL + (wL - fShelfLpL) * shelfGain;
      wL *= outGain;                                // per-mode loudness makeup
      wL = (1.0f - hardMix) * softClip(wL) + hardMix * hardClip(wL); // crossfaded clip (no click on mode change)
      // transparent auto-makeup: track dry & wet level, cap wet at dry*pushCurve (only cut)
      fEnvInL  += fEnvCoef * (std::fabs(xL) - fEnvInL);
      fEnvWetL += fEnvCoef * (std::fabs(wL) - fEnvWetL);
      float gL = (fEnvWetL > 1.0e-5f) ? (fEnvInL * pushCurve) / fEnvWetL : 1.0f;
      if(gL > 1.0f) gL = 1.0f; else if(gL < 0.02f) gL = 0.02f; // never boost; floor for safety
      wL *= gL;
      float oL = xL + driveWet * (wL - xL);         // Drive = dry/wet (clean at 0%)
      // TONE tilt is POST dry/wet, so it shapes tone at ANY drive (even Drive 0).
      fToneLpL += a * (oL - fToneLpL);              // low band
      float hiL = oL - fToneLpL;                    // high band
      oL = gLo * fToneLpL + gHi * hiL;              // tone tilt
      outL[n] = softClip(oL);                       // transparent output safety

      // ---- R ----
      float xR = inR[n];
      float wnR = whiteNoise();
      fNzBrownR = fBrownLeak * fNzBrownR + fBrownIn * wnR;
      float blueR = wnR - fNzPrevR; fNzPrevR = wnR;
      float nzR = (nzCol >= 0.0f) ? ((1.0f - nzCol) * wnR + nzCol * 0.71f * blueR)
                                   : ((1.0f + nzCol) * wnR + (-nzCol) * fBrownGain * fNzBrownR);
      float drvR = xR * (1.0f + noiseDepth * nzR);
      float hpR = drvR - fPreHpXR + preHpCoef * fPreHpYR;
      fPreHpXR = drvR; fPreHpYR = hpR;
      fPreLpStateR += preLpCoef * (hpR - fPreLpStateR);
      const float bR = svfBell(fPreLpStateR, fBellZ1R, fBellZ2R, b1c);
      const float cR = svfBell(bR, fBell2Z1R, fBell2Z2R, b2c);
      const float dR = svfBell(cR, fCLoZ1R, fCLoZ2R, clc);   // CHARACTER EQ bells: low then high (in series)
      const float eR = svfBell(dR, fCHiZ1R, fCHiZ2R, chc);
      fEmphLpR += fEmphCoef * (eR - fEmphLpR);
      float xeR = fEmphLpR * preLoGain + (eR - fEmphLpR) * preHiGain;
      float wR = shapeTb(xeR, satGain, foldDepth, wFuzz, wFold, bias, tanhBias) * boost;
      fDeLpR += fEmphCoef * (wR - fDeLpR);
      wR = wR - deEmph * (wR - fDeLpR);
      float dcR = wR - fDcXR + fDcCoef * fDcYR; fDcXR = wR; fDcYR = dcR; wR = dcR;
      fAirLpR += fAirCoef * (wR - fAirLpR);
      float airHiR = wR - fAirLpR;
      float sqR = airHiR * airHiR;
      fAir2R += fDcCoef * (sqR - fAir2R);
      wR = fAirLpR + airHiR * presGain + airAmt * (sqR - fAir2R);
      fShelfLpR += shelfCoef * (wR - fShelfLpR);
      wR = fShelfLpR + (wR - fShelfLpR) * shelfGain;
      wR *= outGain;                                // per-mode loudness makeup
      wR = (1.0f - hardMix) * softClip(wR) + hardMix * hardClip(wR); // crossfaded clip (no click on mode change)
      fEnvInR  += fEnvCoef * (std::fabs(xR) - fEnvInR);
      fEnvWetR += fEnvCoef * (std::fabs(wR) - fEnvWetR);
      float gR = (fEnvWetR > 1.0e-5f) ? (fEnvInR * pushCurve) / fEnvWetR : 1.0f;
      if(gR > 1.0f) gR = 1.0f; else if(gR < 0.02f) gR = 0.02f;
      wR *= gR;
      float oR = xR + driveWet * (wR - xR);         // Drive = dry/wet (clean at 0%)
      fToneLpR += a * (oR - fToneLpR);              // TONE tilt POST dry/wet (works at any drive)
      float hiR = oR - fToneLpR;
      oR = gLo * fToneLpR + gHi * hiR;
      outR[n] = softClip(oR);                       // transparent output safety
    }
  }  // sub-blocks
  // --- NaN wall --------------------------------------------------------
  // If anything non-finite slipped into this batch (state was already
  // poisoned before the input sanitizer existed, or a path not covered by
  // it), reset every per-sample state and emit silence for THIS batch
  // instead of (a) storing non-finite samples in the output buffer — an SDK
  // violation that kills the downstream chain — and (b) keeping poisoned
  // one-pole states that would otherwise stay NaN forever. One quiet batch,
  // then normal operation resumes.
  float chk = 0.0f;
  for(int n = 0; n < kBatchFrames; ++n) chk += outL[n] + outR[n];
  chk += fDcYL + fDcYR + fEnvInL + fEnvInR + fEnvWetL + fEnvWetR
       + fToneLpL + fToneLpR + fBellZ1L + fBellZ2R + fBell2Z1L + fBell2Z2R
       + fCLoZ1L + fCLoZ2R + fCHiZ1L + fCHiZ2R
       + fPreHpYL + fPreHpYR + fPreLpStateL + fPreLpStateR
       + fEmphLpL + fEmphLpR + fDeLpL + fDeLpR
       + fAirLpL + fAirLpR + fAir2L + fAir2R
       + fShelfLpL + fShelfLpR + fNzBrownL + fNzBrownR
       + fSlewDrive + fSlewChar + fSlewTone + fM_hardClip
       + fM_bellFreq + fM_bellQ + fM_bellGain + fM_foldScale;
  if(!std::isfinite(chk))
  {
    flushDspState();
    // The slewed values are NOT covered by flushDspState (they are controls,
    // not signal): reseed them from the cached knobs and the target model so
    // the recovery lands on the current sound, not on a stale one.
    fSlewDrive = driveTgt; fSlewChar = charTgt; fSlewTone = toneTgt;
    fM_air = MT.air; fM_fuzz = MT.fuzz; fM_freq = MT.freq;
    fM_neutralFold = MT.neutralFold; fM_noiseOff = MT.noiseOff;
    fM_hp = MT.hpHz; fM_lp = MT.lpHz;
    fM_bellFreq = MT.bellFreq; fM_bellQ = MT.bellQ; fM_bellGain = MT.bellGainDb;
    fM_bell2Freq = MT.bell2Freq; fM_bell2Q = MT.bell2Q; fM_bell2Gain = MT.bell2GainDb;
    fM_outTrim = MT.outTrimDb; fM_outSlope = MT.outTrimSlope; fM_tilt = MT.tiltHz;
    fM_charRange = MT.charRange; fM_asym = MT.asym; fM_foldScale = MT.foldScale;
    fM_hardClip = MT.hardClip;
    fCharLoHz = kCharEq[sel].loHz; fCharHiHz = kCharEq[sel].hiHz;
    fRampInit = false; fCoefValid = false;   // the ramps restart from the reseeded controls
    if(!fPanicTraced) { fPanicTraced = true; JBOX_TRACE("Saturator: non-finite state detected - state reset"); }
    return;                                  // outputs unwritten = one silent batch
  }

  // idle bookkeeping: count batches where input AND output are both silent
  if(inSilent)
  {
    float op = 0.0f;
    for(int n = 0; n < kBatchFrames; ++n)
    {
      const float m = std::max(std::fabs(outL[n]), std::fabs(outR[n]));
      if(m > op) op = m;
    }
    if(op <= kIdleOutThr) { ++fSilentRun; if(op <= kJBox_SilentThreshold) return; }   // nothing audible: leave unwritten
    else fSilentRun = 0;
  }

  // --- OUT bar: the last gain in the chain (slewed; unity at the default) ---
  {
    const float tgt = trimGain(fCached_OutLevel);
    const float a   = 1.0f - std::exp(-1.0f / (kLvlTrimMs * 0.001f * static_cast<float>(fSampleRate)));
    if(fOutGain != tgt || tgt != 1.0f)
      for(int n = 0; n < kBatchFrames; ++n)
      {
        fOutGain += a * (tgt - fOutGain);
        outL[n] *= fOutGain; outR[n] *= fOutGain;
      }
  }

  writeOutputBuffer(fOutLRef, outL);
  writeOutputBuffer(fOutRRef, outR);
}
