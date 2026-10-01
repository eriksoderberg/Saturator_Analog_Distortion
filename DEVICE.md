# Saturator — assembly spec

## One-line pitch
Sixteen circuits of drive, from a clean transistor to a red-hot power tube, on a
touch screen with an LFO. Grab it without thinking.

## Price / tier
9 EUR — cheapest tier, never on sale.

## Platform
RE SDK 5.0.0 (Reason 14+). Front panel = one C++ touch screen
(`src/cpp/ui/SaturatorDisplay.cpp`), same layout as the Degrader, red palette
(#000000 / #240404 / #430808 / #7A0F0F / #EA0808).

## Controls (touch screen, 1U)
- **Amount** — drive into the shaper AND dry/wet: clean at 0, full wet from
  ~35 %, the rest adds drive. Auto makeup keeps the level steady.
- **Tone** — bipolar tilt around the model's pivot.
  Tone and Style (property id: Character) have a centre detent: a drag holds at 0 % for a short
  extra travel (display code, kDetent = 6 %).
- **Style** — bipolar recipe: CCW = wavefold, CW = fuzz, centre = the
  model's own balance.
- **Model** — 16 circuits in 4 categories (tap a category, pick a model):

| Category | Models (gentle -> extreme) |
|---|---|
| CLEAN  | BC12 transistor · PV40 op-amp · TG23 transformer · BV98 inductor |
| WARM   | WP76 twin triode · ZX67 FET · A1R pentode · ES83 triode (default) |
| GRITTY | FD88 wavefolder · LF09 IC chip · NN18 diode pair · SB05 rectifier |
| HEAVY  | FF37 fuzz pair · GR1T germanium · F34K silicon fuzz · X7R3 power tube |

- **LFO** (the Degrader's): on/off, rate free (0.05–60 Hz) or synced (25 divisions, 16/1 … 1/64,
  locked to the song while playing), 7 shapes, depth + phase per destination
  (Amount / Tone / Style).

## CV (back)
- in: Amount_CV, Tone_CV, Character_CV (each with a trim knob)
- out: LFO_CV_Out — the raw LFO (-1..+1), jack only (no trim: trims are for CV
  inputs). 0 while the LFO is off; keeps running through bypass. Jack at
  (2780,135), same position on the Degrader.

## DSP notes
- Pade(3,3) tanh fuzz + triangle wavefolder, per-model 2-band voice EQ,
  Character EQ, air/presence, soft/hard clip, transparent auto-makeup.
- Model voicings: `Saturator.h` kModels[] / kCharEq[] (order = SaturatorShared.h).
- SDK 5 transit (2026-09-22) is sound-neutral: with the LFO off the output is
  bit-identical to the SDK 4.6 build for every model (offline comparison).
- NOTE: hard clipping aliases at extreme Amount. Consider 2x oversampling later.

## Definition of done (ship checklist)
- [x] DSP implemented (Amount/Tone/Character/Model + CV)
- [x] LFO (Degrader's) in DSP + display
- [x] SDK 5 touch screen, red palette, 16 model icons
- [x] Panel art (front/back/folded)
- [ ] Builds clean locally + on the online build server
- [ ] Sanity-checked in Reason (bypass, off, CV in, automation, LFO sync)
- [ ] Icons / text sizes checked in Reason (host fonts differ from the mockup)
- [ ] Default patch sounds good on a drum bus / synth
- [ ] 5-10 factory patches
- [ ] Shop copy + screenshot
- [ ] Consider 2x oversampling if top-end aliasing is audible
