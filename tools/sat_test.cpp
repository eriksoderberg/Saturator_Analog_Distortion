// Offline torture test for Saturator (NaN hardening 2026-08-21, SDK 5 + LFO 2026-09-22).
// Minimal self-contained Jukebox mock (only what Saturator.cpp calls), then:
//   1. clean sine at several drive/character settings -> output finite, bounded
//   2. NaN/Inf bursts in the INPUT (the chain-death scenario) -> output stays
//      finite forever, and after the input cleans up the output comes back
//   3. NaN on both CVs -> same recovery contract
//   4. hot input (+18 dBFS square) for minutes -> finite, bounded
//   5. LFO: every shape, free + synced, full depths -> finite, and the
//      modulated values published for the display and the LFO CV out really move
// Build (from the project root, SDK 5 headers + a logging.h stub):
//   g++ -std=c++17 -O2 -I <SDK>/API -I src/cpp -I <stub> tools/sat_test.cpp
#include "Jukebox.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <map>
#include <string>
#include <vector>

// ------------------------------------------------------------- mock -------
namespace mock {
struct Prop { double num{0.0}; };
std::map<std::string, TJBox_ObjectRef> gObjs;
std::map<long long, Prop> gProps;             // key: obj*1000 + small hash
std::vector<std::string> gTraces;
float gInL[64], gInR[64], gOutL[64], gOutR[64];
long long gBadStores = 0;

TJBox_ObjectRef obj(const std::string& p)
{
  auto it = gObjs.find(p);
  if (it != gObjs.end()) return it->second;
  TJBox_ObjectRef r = static_cast<TJBox_ObjectRef>(gObjs.size() + 1);
  gObjs[p] = r; return r;
}
long long pkey(TJBox_ObjectRef o, const char* k)
{
  long long h = 1469598103934665603LL;
  for (const char* c = k; *c; ++c) h = (h ^ *c) * 1099511628211LL;
  return o * 100000 + (h % 99991);
}
TJBox_Value packNum(double v)
{
  TJBox_Value x; std::memset(&x, 0, sizeof(x));
  int t = 1; std::memcpy(x.fSecret, &t, 4);
  std::memcpy(x.fSecret + 8, &v, 8);
  return x;
}
TJBox_Value packBuf(int which)   // 0 inL 1 inR 2 outL 3 outR
{
  TJBox_Value x; std::memset(&x, 0, sizeof(x));
  int t = 3; std::memcpy(x.fSecret, &t, 4);
  double d = which; std::memcpy(x.fSecret + 8, &d, 8);
  return x;
}
} // namespace mock

TJBox_ObjectRef JBox_GetMotherboardObjectRef(const char iMOMPath[])
{ return mock::obj(iMOMPath); }

// TJBox_PropertyRef is an opaque handle in SDK 5: keep our own table.
namespace mock { std::vector<std::pair<TJBox_ObjectRef, std::string>> gRefs; }
TJBox_PropertyRef JBox_MakePropertyRef(TJBox_ObjectRef iObject, const char iKey[])
{
  mock::gRefs.emplace_back(iObject, iKey);
  return static_cast<TJBox_PropertyRef>(mock::gRefs.size());   // 1-based
}

TJBox_Value JBox_LoadMOMProperty(TJBox_PropertyRef iProperty)
{
  const auto& r = mock::gRefs[iProperty - 1];
  return mock::packNum(mock::gProps[mock::pkey(r.first, r.second.c_str())].num);
}

TJBox_Value JBox_LoadMOMPropertyByTag(TJBox_ObjectRef iObject, TJBox_Tag iTag)
{
  // audio buffers: tag kJBox_AudioInputBuffer / kJBox_AudioOutputBuffer on
  // the four audio objects; custom-prop tags: value stored under the tag key.
  const TJBox_ObjectRef inL = mock::obj("/audio_inputs/MainInL");
  const TJBox_ObjectRef inR = mock::obj("/audio_inputs/MainInR");
  const TJBox_ObjectRef outL = mock::obj("/audio_outputs/MainOutL");
  const TJBox_ObjectRef outR = mock::obj("/audio_outputs/MainOutR");
  if (iObject == inL)  return mock::packBuf(0);
  if (iObject == inR)  return mock::packBuf(1);
  if (iObject == outL) return mock::packBuf(2);
  if (iObject == outR) return mock::packBuf(3);
  char k[16]; std::snprintf(k, sizeof(k), "#%d", static_cast<int>(iTag));
  return mock::packNum(mock::gProps[mock::pkey(iObject, k)].num);
}

TJBox_Float64 JBox_GetNumber(TJBox_Value iValue)
{ double d; std::memcpy(&d, iValue.fSecret + 8, 8); return d; }

TJBox_Value JBox_MakeNumber(TJBox_Float64 iNumber) { return mock::packNum(iNumber); }

void JBox_StoreMOMProperty(TJBox_PropertyRef iProperty, TJBox_Value iValue)
{
  const double v = JBox_GetNumber(iValue);
  const auto& r = mock::gRefs[iProperty - 1];
  const bool cv = (r.first == mock::obj("/cv_outputs/LFO_CV_Out"));   // CV: -1..+1
  if (!std::isfinite(v) || v < (cv ? -1.0001 : -0.0001) || v > 1.0001) ++mock::gBadStores;
  mock::gProps[mock::pkey(r.first, r.second.c_str())].num = v;
}

void JBox_GetDSPBufferData(TJBox_Value iValue, TJBox_AudioFramePos iStartFrame,
                           TJBox_AudioFramePos iEndFrame, TJBox_AudioSample oAudio[])
{
  double d; std::memcpy(&d, iValue.fSecret + 8, 8);
  const float* src = (d < 0.5) ? mock::gInL : mock::gInR;
  for (auto i = iStartFrame; i < iEndFrame; ++i) oAudio[i - iStartFrame] = src[i];
}

void JBox_SetDSPBufferData(TJBox_Value iValue, TJBox_AudioFramePos iStartFrame,
                           TJBox_AudioFramePos iEndFrame, const TJBox_AudioSample iAudio[])
{
  double d; std::memcpy(&d, iValue.fSecret + 8, 8);
  float* dst = (d < 2.5) ? mock::gOutL : mock::gOutR;
  for (auto i = iStartFrame; i < iEndFrame; ++i) dst[i] = iAudio[i - iStartFrame];
}

void JBox_Trace(const char iFile[], TJBox_Int32, const char iMessage[])
{ mock::gTraces.push_back(std::string(iFile) + ": " + iMessage); }

TJBox_Bool JBox_GetBoolean(TJBox_Value) { return 1; }

// ------------------------------------------------------------ device ------
#include "Saturator.h"
#include "../src/cpp/Saturator.cpp"

static void setProp(const char* objPath, const char* key, double v)
{ mock::gProps[mock::pkey(mock::obj(objPath), key)].num = v; }
static void setTag(const char* objPath, int tag, double v)
{ char k[16]; std::snprintf(k, sizeof(k), "#%d", tag);
  mock::gProps[mock::pkey(mock::obj(objPath), k)].num = v; }

int main()
{
  int fails = 0;
  auto run = [&](Device& d, int batches, auto fillIn, const char* what,
                 bool expectAudioBack) {
    double sumAbs = 0.0; long long nonFinite = 0; double tailAbs = 0.0;
    for (int b = 0; b < batches; ++b) {
      fillIn(b);
      d.renderBatch(nullptr, 0);
      for (int i = 0; i < 64; ++i) {
        if (!std::isfinite(mock::gOutL[i]) || !std::isfinite(mock::gOutR[i])) ++nonFinite;
        else {
          sumAbs += std::fabs(mock::gOutL[i]);
          if (b > batches - 50) tailAbs += std::fabs(mock::gOutL[i]);
        }
      }
    }
    const bool cameBack = tailAbs > 0.01;
    const bool ok = (nonFinite == 0) && (!expectAudioBack || cameBack);
    std::printf("%-42s non-finite %-6lld tail |out| %-10.3f %s\n",
                what, nonFinite, tailAbs, ok ? "OK" : "FAIL");
    if (!ok) ++fails;
  };

  setProp("/custom_properties", "builtin_onoffbypass", 1.0);
  setTag("/custom_properties", kTag_Amount, 0.6);
  setTag("/custom_properties", kTag_Tone, 0.5);
  setTag("/custom_properties", kTag_Character, 0.5);
  setTag("/custom_properties", kTag_Model, 2.0);

  // 1. clean sine
  {
    Device d(48000);
    long long t = 0;
    run(d, 2000, [&](int) {
      for (int i = 0; i < 64; ++i, ++t) {
        const float s = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * t / 48000.0f);
        mock::gInL[i] = s; mock::gInR[i] = s;
      }
    }, "clean sine, model 2, amount 0.6", true);
  }

  // 2. NaN/Inf burst mid-stream, then clean again
  {
    Device d(48000);
    long long t = 0;
    run(d, 3000, [&](int b) {
      for (int i = 0; i < 64; ++i, ++t) {
        float s = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * t / 48000.0f);
        if (b >= 1000 && b < 1100) s = (i % 3 == 0) ? NAN : ((i % 3 == 1) ? INFINITY : s * 1e30f);
        mock::gInL[i] = s; mock::gInR[i] = s;
      }
    }, "NaN/Inf/1e30 burst then clean", true);
  }

  // 3. NaN on both CVs for a while, then clean
  {
    Device d(48000);
    long long t = 0;
    run(d, 3000, [&](int b) {
      const double cv = (b >= 1000 && b < 1200) ? NAN : 0.1;
      setProp("/cv_inputs/Amount_CV", "value", cv);
      setProp("/cv_inputs/Tone_CV", "value", cv);
      setProp("/cv_inputs/Character_CV", "value", cv);
      for (int i = 0; i < 64; ++i, ++t) {
        const float s = 0.4f * std::sin(2.0f * 3.14159265f * 220.0f * t / 48000.0f);
        mock::gInL[i] = s; mock::gInR[i] = s;
      }
    }, "NaN CV burst then clean", true);
    setProp("/cv_inputs/Amount_CV", "value", 0.0);
    setProp("/cv_inputs/Tone_CV", "value", 0.0);
    setProp("/cv_inputs/Character_CV", "value", 0.0);
  }

  // 4. hot square (+18 dBFS) for ~2 minutes, every model
  for (int m = 0; m < kNumModels; ++m) {
    setTag("/custom_properties", kTag_Model, m);
    Device d(48000);
    long long t = 0;
    char label[64]; std::snprintf(label, sizeof(label), "hot +18dB square, model %d", m);
    run(d, 1500, [&](int) {
      for (int i = 0; i < 64; ++i, ++t) {
        const float s = ((t / 109) % 2) ? 8.0f : -8.0f;
        mock::gInL[i] = s; mock::gInR[i] = s;
      }
    }, label, true);
  }

  // 5. LFO: every shape, free (fast) and synced, full +/- depths, phases set
  setTag("/custom_properties", kTag_Model, sat::kDefaultModel);
  setTag("/custom_properties", kTag_LfoOn, 1.0);
  setTag("/custom_properties", kTag_LfoDepthAmt, 1.0);
  setTag("/custom_properties", kTag_LfoDepthTone, 0.0);
  setTag("/custom_properties", kTag_LfoDepthChar, 1.0);
  setTag("/custom_properties", kTag_LfoPhaseTone, 0.25);
  setTag("/custom_properties", kTag_LfoPhaseChar, 0.5);
  setProp("/transport", "tempo", 128.0);
  for (int sync = 0; sync < 2; ++sync)
    for (int sh = 0; sh < sat::kNumLfoShapes; ++sh) {
      setTag("/custom_properties", kTag_LfoSync, sync);
      setTag("/custom_properties", kTag_LfoRate, 0.9);        // ~29 Hz (0.05..60 Hz curve)
      setTag("/custom_properties", kTag_LfoRateSync, 11);     // 1/16
      setTag("/custom_properties", kTag_LfoShape, sh);
      Device d(48000);
      long long t = 0;
      double lo = 1.0, hi = 0.0, cvLo = 1.0, cvHi = -1.0;
      char label[64]; std::snprintf(label, sizeof(label), "LFO %s, shape %s", sync ? "synced" : "free", sat::kLfoShapeName[sh]);
      run(d, 1500, [&](int b) {
        setProp("/transport", "play_pos", b * 64.0 / 48000.0 * (128.0 / 60.0) * 15360.0);
        for (int i = 0; i < 64; ++i, ++t) {
          const float s = 0.4f * std::sin(2.0f * 3.14159265f * 220.0f * t / 48000.0f);
          mock::gInL[i] = s; mock::gInR[i] = s;
        }
        const double m = mock::gProps[mock::pkey(mock::obj("/custom_properties"), "Mod_Amount_Out")].num;
        if (b > 100) { lo = std::min(lo, m); hi = std::max(hi, m); }
        const double c = mock::gProps[mock::pkey(mock::obj("/cv_outputs/LFO_CV_Out"), "value")].num;
        if (b > 100) { cvLo = std::min(cvLo, c); cvHi = std::max(cvHi, c); }
      }, label, true);
      if (hi - lo < 0.3) { std::printf("   ^ modulated Amount only spans %.3f..%.3f: FAIL\n", lo, hi); ++fails; }
      if (cvHi - cvLo < 0.8 || cvLo > 0.0 || cvHi < 0.0) { std::printf("   ^ LFO CV out only spans %.3f..%.3f: FAIL\n", cvLo, cvHi); ++fails; }
    }
  setTag("/custom_properties", kTag_LfoOn, 0.0);

  std::printf("bad meter stores: %lld, traces: %zu\n", mock::gBadStores, mock::gTraces.size());
  for (auto& s : mock::gTraces) std::printf("  TRACE %s\n", s.c_str());
  if (mock::gBadStores) ++fails;
  std::printf(fails ? "FAILURES: %d\n" : "all OK\n", fails);
  return fails;
}
