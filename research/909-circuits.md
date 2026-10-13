# TR-909 analog voices: circuit specification for DSP modelling

Scope: Bass Drum (BD), Snare Drum (SD), Low/Mid/High Tom (LT/MT/HT), Rim Shot (RS), Hand Clap (HC), and the shared noise generator they use. Hi-hats and cymbals are 6-bit ROM samples and are not covered here.

This is a clean-room write-up. It contains facts (component values, time constants, frequencies, measured numbers, and the topology of each stage) collected from the sources below and restated in my own words. No code or long text has been copied from any source. Some sources are AGPL or GPL (mw909, schwung-9W9). From those I took only facts such as numbers, part values and block structure. Short quotes from Roland's service notes are marked as quotes.

Date compiled: 2026-10-12.

---

## Sources and how far to trust them

| Key | Source | What it contributes | Notes |
|---|---|---|---|
| **[SN]** | Roland *TR-909 Service Notes*, 1st ed., June 1984. https://archive.org/details/synth_Roland_TR-909_service_notes (OCR text: https://archive.org/download/synth_Roland_TR-909_service_notes/Roland_TR-909_service_notes_djvu.txt) | Primary source: circuit description (p.5–6), change information (p.7), scope drawings (p.9), voicing-board schematic (p.11) | I read the OCR text myself (trigger, analog-voice description, change list, p.9 captions). **I did not read the p.11 schematic image directly.** All part values below come via [MW01]'s reading of the 600 dpi scan. |
| **[MW01]** | mattWoolly, mw909 `research/01-voices-analog.md`. https://github.com/mattWoolly/mw909/blob/main/research/01-voices-analog.md | Richest source: part-by-part schematic reading, derivations, and measurements of one real unit | AGPL-3.0, used for facts only. Measurements are from the AudioRealism 2025 TR-909 sample pack (one unit, 96 kHz, LEVEL 50 %). The file includes its own adversarial fact-check log; I used the corrected values. |
| **[MW05]** | mw909 `research/05-emulation-techniques.md`. https://github.com/mattWoolly/mw909/blob/main/research/05-emulation-techniques.md | Measurement tables (BD pitch fit, decay times, harmonics, SD/tom/RS/HC data) | AGPL-3.0, facts only |
| **[MW03]** | mw909 `research/03-system-sequencer.md`. https://github.com/mattWoolly/mw909/blob/main/research/03-system-sequencer.md | Accent DAC and trigger system | AGPL-3.0, facts only |
| **[N909]** | R@F, network-909.de circuit pages: http://www.network-909.de/bassdrum.htm , snaredrum.htm, toms.htm, rimshot.htm, handclap.htm | Block-level circuit descriptions, largely following [SN] | Fetched over plain http (https fails on TLS). |
| **[RW]** | Robin Whittle, *TR-909 sound mods* (PDF, 10 Oct 2018): https://www.firstpr.com.au/rwi/tr-909/TR-909-Sound-Mods.pdf ; page: https://www.firstpr.com.au/rwi/tr-909/ | Bench observations on the BD, SD snappy, HC and VCA behaviour | Written by a technician who modified real units |
| **[CF]** | Colin Fraser, *909 Sound Mods* (2004): http://www.colinfraser.com/tr909/909mods/909mods.htm | BD envelope, tune and offset circuit roles | Fetched over http |
| **[SOS-BD]** | Gordon Reid, *Synth Secrets: Practical Bass Drum Synthesis*, SOS: https://www.soundonsound.com/techniques/practical-bass-drum-synthesis | BD block diagram, functional level | Simplified (see conflicts) |
| **[SOS-SD]** | Gordon Reid, *Synth Secrets: Practical Snare Drum Synthesis*, SOS: https://www.soundonsound.com/techniques/practical-snare-drum-synthesis | SD block diagram, functional level | — |
| **[9W9]** | synthfr, *schwung-9W9* (GPL-3.0), comments in `src/dsp/er99_circuit.h`, `er99_tom909.h`, `er99_perc909.h`: https://github.com/synthfr/schwung-9W9 | A few independent measurements of real 909 recordings | GPL, facts only. Its measurement method is not documented, so treat these as secondary. |
| **[KVR]** | KVR forum, "Emulating the TR-909(808) clap in detail": https://www.kvraudio.com/forum/viewtopic.php?p=8606994 | Clap folklore (filter at "1140 Hz, Q 1.95") | The poster says himself that he is unsure. The figure is contradicted by the part values (see HC). |

Kurt Werner's papers on 808 voices model the 808 bass drum, cymbal and hats. I found nothing from him on the 909's analog voices. The 808 bridged-T analysis does carry over to the 909 rim shot.

**Tags used below.** *derived* means computed from part values, by [MW01] unless I say otherwise. *measured* means taken from recordings of a real unit. *conflict* means the sources disagree; both values are given.

---

## 0. Shared machinery (applies to every analog voice)

### 0.1 Trigger and accent entry
- The CPU latches a TRIG for each voice. It appears as a **5 V positive pulse lasting 2 ms** [SN p.5 text]. Accent comes from a held 6-bit DAC level (0–5 V) per voice, rewritten at each trigger. RS and HC **share one accent DAC** [SN p.4/5; MW03].
- Every analog voice has the same front end [MW01 §1.1]:
  1. an NPN trigger switch;
  2. a PNP switch that turns the 2 ms trigger into an **unaccented "+15 V pulse"**. This pulse resets the VCOs, forces the comparators, and charges the **pitch-envelope capacitors**;
  3. a PNP **accent switch**. While the trigger (or a hold timer) is active it passes the accent voltage into the **amplitude-envelope capacitors**.
- Each voice buffers its accent with an op-amp of gain 1 + 10k/4.7k = **3.13**, so the nominal range is 0–15.6 V. An envelope cap ends up at about V_acc − 1.2 V (switch saturation plus one diode) [MW01 §1.1; MW03 §3.2]. Above about DAC code 52–56 the buffer probably clips at the op-amp rail. That would explain the owner's manual remark that accent "may not be noticed" at high TOTAL ACCENT [MW03] (derived, not verified).
- **Consequence for modelling:** accent never acts as a plain output gain. It sets envelope start voltages, the BD click size and the HC burst generator. Pitch envelopes are **not** accented (BD, SD and toms all take theirs from the unaccented pulse) [MW01 §0 item 9; RW p.4 for SD].
  - *Conflict:* the [SN p.4] block diagram, as summarised in [MW03 §3.3], suggests accent reaches the BD pitch envelope (ENV3). The schematic reading and measurement (BD Δf flat over accent 0–100 %) say it does not [MW01 §3.2].

### 0.2 The triangle VCO (BD, SD, toms)
[SN p.6] describes the snare VCO as the template and says BD and toms "operate basically in the same manner". Its parts:
- an op-amp **integrator** (47 kΩ into the timing capacitor);
- an op-amp **hysteresis comparator** (Schmitt trigger) that flips the integration direction at ±V_th;
- a **reset transistor** that shorts the integration cap during the trigger, while the trigger pulse also forces the comparator state through a diode. Every hit therefore starts at the **same phase**: triangle at 0 V, mid-slope, in a fixed direction [SN p.6; MW01 §1.2]. Measured hit-to-hit start correlation is 0.91–1.00 (BD) and 0.999 (SD) [MW05 §2.1].
- **Frequency is linear in the control voltage**: f = V_cv / (4·V_th·R·C) [MW01 §1.2].
  - BD: the comparator threshold is V_sat·47k/147k ≈ ±4.3 V nominal. A ±1 op-amp (IC12b) switched by transistor Q10 gives a symmetric triangle [MW01 §3.2].
  - SD and toms: the comparator threshold is ±V_sat/2 ≈ ±6.75 V. The square wave from the comparator drives three paralleled **4069UB CMOS inverters whose supply rails (VDD/VSS) are the control voltage**. The integrator current is therefore VDD/47k one way and |VSS|/47k the other. An 18 V zener sits across the rails [MW01 §1.2]. [SN p.6] calls these inverters "inverting buffers configured as voltage-dependent resistor".
  - With asymmetric rails: f = 1 / (2·V_th·R·C·(1/VDD + 1/|VSS|)) [MW01 §1.2].
- [MW05 §2.1] gives evidence for the linear law: the BD pitch fits f_b + Δf·e^(−t/τ) with 0.1 Hz RMS error, which an exponential converter would not produce.

### 0.3 Triangle → "sine" shaper
- Each VCO output feeds a **10 kΩ resistor into an anti-parallel pair of 1SS133 silicon diodes to ground**, which soft-clips at about ±0.5–0.6 V. A 1.5–2.2 kΩ + 47 Ω divider then attenuates the signal into the VCA transistor's emitter [MW01 §1.3; RW p.3; N909 snaredrum, toms].
- [N909] describes this as giving "sine" or "near sine". In practice it is a rounded triangle with residual odd harmonics (BD figures in §1).

### 0.4 The single-transistor VCA
- One "selected" NPN per path. The signal enters the **emitter** at about ±12–18 mV across 47 Ω. The **envelope capacitor discharges into the base through 220–470 kΩ**, and the collector feeds the voice's summing virtual ground [MW01 §1.3].
- Gain is roughly proportional to base current, i.e. **∝ max(0, V_env − V_BE)/R_b, with a threshold of about 0.4–0.6 V** [RW p.6–7; MW01 §1.3]. Whittle notes the threshold drifts with temperature [RW p.6].
- Two behaviours follow:
  - If the cap's **only** discharge path is the base resistor, the base current, and therefore the gain, decays as a pure exponential, τ = R_b·C [RW p.7]. This applies to SD drum ENV2/ENV3, tom ENV1/ENV3, RS, and the HC reverb.
  - If the cap also discharges through a resistor to ground or to a negative voltage (BD ENV1, SD snappy ENV4), V_env crosses the threshold in finite time. The level then falls ever faster in dB and stops abruptly, a **"cliff"** ending [MW01 §1.3; MW05 §2.2; RW p.6].
- The transistor runs saturated, which probably adds **level-independent even harmonics** (BD H2 ≈ −28 dB). This attribution is unverified [MW01 §1.3].

### 0.5 Output and LEVEL
- Each voice sums its VCA currents into an inverting op-amp whose **feedback resistor is the LEVEL pot**, so output is linear in pot rotation: 50 % = −6 dB, 0 % = silence [MW01 §1.4].
  - LEVEL pot values: BD 100 kΩ B; SD, toms and HC 50 kΩ B; RS 100 kΩ B in parallel with 220 pF.
- From S/N 415300 a 1 kΩ / 0.01 µF RC (≈ 15.9 kHz) sits at each multi-out [MW01 §1.4, §8].
- Every analog voice peaks harder negative than positive (+/− peak ratio 0.53–0.79) [MW05 §10.1].
- Relative Vpp at LEVEL 50 % (re BD): SD +3.6 dB, LT −0.6, MT +0.3, HT +0.4, RS +0.3, HC +9.0 dB [MW05 §10.1].
- No absolute level table exists for the 909's analog voices. The only numeric calibration is the HC trimmer (§6.6) [MW01 §1.5].

### 0.6 Noise generator (shared by SD, BD attack, toms, HC)
- [SN p.6]: a "quasi-random noise generator" made of two shift registers (IC32, IC33) clocked through XOR gates (IC31), with a power-up kick into IC32 pin 1 via D48.
- [MW01 §2] reading of the schematic:
  - two **4006 18-stage** registers chained into 36 stages;
  - XOR feedback from **stages 13 and 31**, i.e. polynomial x^31 + x^13 + 1, which is primitive, so the period is 2^31 − 1;
  - RC clock (R184 10 kΩ, C47 100 pF), about 0.3–0.46 MHz;
  - output 0/+15 V CMOS bits.
  - [SN]'s "32 stages" counts the feedback length; [RW p.5]'s "two 18 stage shift registers" counts the physical chain. Both are consistent with this reading.
- The noise is white across the audio band.
- **One stream feeds all consumers**, so SD and HC noise are identical sample-for-sample. That is the cause of the well-known SD+HC "phasing" [MW01 §2.2]. RS uses no noise.

---

## 1. Bass Drum (BD)

### 1.1 Block diagram
```
TRIG ─► +15 V pulse (unaccented) ─┬─► VCO reset (Q11 shorts C14) + comparator force
                                  ├─► C9 0.33µF charge (pitch env "ENV3")
                                  └─► C7 0.033µF charge (HOLD timer)
ACCENT (×3.13) ─gated─► C8 0.33µF  (amp env "ENV1", clamped ≈ 2.6 V)
               └gated─► click pulse generator (Q8/Q9)

C9 ──► IC12a (offset + scaled C9) ──► ±1 switch (IC12b/Q10) ──► integrator IC13a (47k, C14 0.033µF)
                                                                   ⇅ comparator IC13b (R49 100k / R50 47k)
triangle ─► 10k ─► D10/D11 diode pair ─► 2.2k/47Ω ─► VCA Q12 (base ◄ 470k ◄ C8) ─┐
click pulse ─► diff (C2 3.3nF) ─► Sallen-Key LP 3.7 kHz Q≈3 ─► 22k ─┐             │
NOISE ─► ÷3.6 ─► HP 26 Hz ─► LP ≈ 513 Hz ─► 4.7k ──────────────────┴► node (330Ω) ─► ATTACK VR3 500Ω ─► VCA Q6 (ENV2) ─┤
                                                                                                               IC11a Σ, LEVEL ─► out
```
Sources: [MW01 §3.1], [N909 bassdrum], [SOS-BD].
- [N909] describes two parts: a "clicking attack" path (pulse + low-passed noise through VCA Q6 on ENV-2) and the "bottom end" (VCO → diode clipper → VCA Q12 on ENV-1).
- [SOS-BD] gives the same structure functionally: VCO with a pitch EG → waveshaper → VCA/EG1, plus noise → LPF mixed with a pulse → VCA/EG2.
- *Conflict:* [SOS-BD] calls the BD oscillator a "sawtooth". The service notes and schematic make it a **triangle** from an integrator/comparator [SN p.6; MW01 §3.2; RW p.3]. Use the triangle.

### 1.2 Oscillator and pitch envelope
- **Control voltage**: V_cv = R28·(15 V/R27 + V_C9/R26) = 150k·(15/1.5M + V_C9/330k) = **1.50 V + 0.4545·V_C9** [MW01 §3.2].
  - R27 (1.5 MΩ from the rail) sets the fixed minimum frequency [CF].
- **Base frequency f_b**: derived 56.0 Hz at the nominal 4.32 V threshold. Measured **49.2–49.7 Hz** on two units, which agree within 0.3 % [MW05 §2.2; MW01 §3.2].
  - *Conflict* (−12 %): a triangle peak of about 4.9 V would reconcile both the pitch and the harmonics. **Use 49.5 Hz.**
  - f_b barely moves with TUNE (49.2 → 49.6 Hz across the pot) [MW05].
  - [9W9] also reports "~49 Hz in every" TUNE position.
- **Pitch envelope** (C9):
  - Charge: from the +15 V pulse through D7 + R21 2.2 kΩ (τ ≈ 0.73 ms), to about 14.4 V within the 2 ms trigger.
  - Discharge: through R57 22 kΩ + VR2 TUNE (100 kΩ, audio taper), in parallel with R26 330 kΩ into the summing node. τ = C9·((R57 + VR2)‖R26) = **6.8 ms (TUNE min) to 29.4 ms (TUNE max)** [MW01 §3.2].
  - Measured τ by TUNE: 7.6 / 9.7 / 11.8 / 14.5 / 29.1 ms at 0/25/50/75/100 % [MW05 §2.2]. The jump between 75 % and 100 % reflects the A taper; the VR2 values implied by the measured τ are 2.8 / 10 / 18 / 29 / 98 kΩ.
  - Sweep law (measured, 0.1 Hz RMS fit): **f(t) = f_b + Δf·e^(−t/τ)**, Δf ≈ 229–247 Hz, so **f(0) ≈ 279–296 Hz** [MW05 §2.2]. Derived Δf = 216 Hz.
  - Δf is **accent-independent**: 225–237 Hz with no trend over TOTAL ACCENT 0–100 % [MW01 §3.2].
  - The SN p.9 drawing shows the first cycle at about 2–3 ms, with periods growing to about 8 ms by the fifth cycle [MW05 §2.2] (qualitative).
- **What TUNE does**: it changes only the **decay time of the pitch sweep**, not the base pitch. [CF] describes it as roughly 30–120 ms of audible sweep (≈ 4τ). [RW p.3] states plainly that TUNE does not set the oscillator's overall frequency.
  - *Conflict:* [9W9] claims the sweep height grows with τ (about 4.1 Hz per ms of τ, τ ≈ 7–33 ms). [MW05]'s fits show Δf nearly constant, falling slightly from 247 to 229 Hz as τ grows. The circuit supports [MW05]: C9 charges fully through 2.2 kΩ whatever TUNE is set to. Prefer [MW05].
- **Early units** (S/N < 381500) have C9 = 0.22 µF, giving τ = 4.5–19.6 ms. Roland changed it "for expanding the TUNE range" [SN p.7 change info].

### 1.3 Waveshaper and harmonics
- Triangle (≈ ±4.3–4.9 V) → R52 10 kΩ → D10/D11 anti-parallel → R53 2.2 kΩ + R43 47 Ω → Q12 emitter. The diode node peaks at about 0.5 V [MW01 §3.5; RW p.3].
- Measured harmonics of the settled tone, relative to H1: **H2 −28.1, H3 −25.7, H4 −44.6, H5 −41.0, H6 −46.5, H7 −40.9 dB** [MW05 §2.2]. For comparison a pure triangle gives H3 −19.1, H5 −27.9, H7 −33.8 dB with no even harmonics.
- These ratios hold as the level falls by 20 dB, so the **shaper sits before the VCA**.
- A diode-pair model driven by a 4.9 V-peak triangle reproduces H3/H5/H7 within 1.3 dB. H3 is very sensitive to drive level and nearly nulls around 4.3 V [MW01 §3.5].
- Even harmonics and the negative-peak dominance (+/− ratio 0.45–0.64) are not produced by a matched pair. They are attributed to the VCA transistor or diode mismatch (unverified) [MW01 §3.5; MW05].
- [CF]/[RW] "Drive" mod: replacing R52 with a pot gives about 0.9× to 10× drive, pushing the wave toward a square. This confirms that the diodes set the character.

### 1.4 Amplitude envelope (ENV1 + hold) and DECAY
- **Charge**:
  - The gated accent charges C8 0.33 µF through R35 1.8 kΩ / D9. A clamp (D8 to a 15·10k/57k = 2.63 V divider) limits C8 to **≈ 2.6 V**, so **BD accent saturates**: output is flat above about 80 % TOTAL ACCENT [MW01 §3.4].
  - [CF]: C8 is charged through D9 at the trigger, discharges through VR5 and R58, and its voltage drives VCA Q12.
- **Hold**:
  - Timer C7 0.033 µF / R18 470 kΩ / Q3, charged from the unaccented pulse, keeps the accent switch feeding C8 for a fixed time.
  - Derived ≈ 47–50 ms; measured **61–63 ms at every accent level and every DECAY setting** [MW01 §3.4].
  - [N909] remarks that ENV-1 "looks like a compressed" drum envelope and credits it with the punch.
  - [9W9] reports the level still at 0.96 at 40 ms.
- **Decay**:
  - After the hold, C8 discharges through R_d = R58 47 kΩ + DECAY VR5 (1 MΩ, audio taper) to ground, in parallel with R54 470 kΩ into Q12's base.
  - τ = C8·(R_d‖R54) = **14 ms (DECAY min) to 107 ms (max)**. Because C8 also bleeds to ground, it heads below the ≈ 0.6 V threshold and the sound **cuts off** (thresholded tail) [MW01 §3.4].
  - Measured: the dB slope accelerates, e.g. at DECAY 100 the level falls 1.6 dB per 20 ms early on and 3.7 dB per 20 ms near −45 dB. Hold followed by a thresholded exponential fits far better than a pure exponential [MW05 §2.2]. [9W9] fits τ ≈ 120 ms after the hold.
- **Decay times, t(−20 dB) from onset** (*conflict between measurement passes on the same recordings*):
  - [MW05]: 75 / 115 / 135 / 175 / 235 ms at DECAY 0/25/50/75/100.
  - [MW01] fact-check re-measurement: **≈ 91–101 / 141–145 / 171–176 / 203–212 / 298–299 ms**.
  - Model with nominal parts: 79 / 248 ms (min/max).
  - The re-measurement is the more careful one (0 dB taken as the hold plateau). The nominal model runs about 20 % short; calibrate to about **96 ms / 298 ms**.

### 1.5 ATTACK (click)
- [CF] describes ATTACK as the level of an initial noise burst and click. Two sources are summed into one node [MW01 §3.6; N909]:
  1. **Pulse.** An emitter follower (Q8) carries accent plus the +15 V pulse. It is differentiated by C2 3.3 nF and band-shaped by a **unity Sallen-Key low-pass at f0 ≈ 3.72 kHz, Q ≈ 3.1** (R7 100k, R16 39k, C3 4.7 nF, C4 100 pF), then coupled through C11 0.47 µF / R44 22 kΩ. The result is a ringing click whose size follows accent.
     - [N909] calls this a low-pass (Q8 …) plus band-pass (Q9 …) pulse shaper. It names a "C22" where [MW01] reads C5 0.0068 µF (designator conflict, no effect on function).
  2. **Noise.** Shared noise divided by about 0.28, high-passed at 26 Hz, then **low-passed at ≈ 513 Hz**, through 4.7 kΩ into the node (330 Ω to ground).
- **ATTACK pot** VR3 is a 500 Ω rheostat in series to the Q6 emitter (47 Ω). Derived range **7.5–7.7 dB**; measured **+7 dB** on the first-2 ms peak, +8 dB in >1 kHz energy over the first 5 ms, and +2.7 dB overall Vpp [MW01 §3.6; MW05 §2.2]. Which pot end is clockwise is not verified.
- **ENV2** (Q6 base): C12 0.033 µF, R41 22 kΩ, R42 470 kΩ. τ is about 0.7 ms. Its source is not fully traced (probably the gated accent pulse) [MW01 §3.6; MW05].
- **Measured click** (ATTACK 100 minus ATTACK 0) [MW01 §3.6]:
  - peaks at 1.5–1.6 ms; −6 dB at 1.9 ms; −20 dB by 2.5–3.8 ms;
  - roughly flat in spectrum from 500 Hz to 8 kHz;
  - about −7 dB relative to the whole-hit peak.
  - So the click lasts about as long as the 2 ms trigger.

### 1.6 Accent
- Accent enters the click amplitude, the ENV1 start (clamped), and probably ENV2. It does not reach the pitch or the hold.
- Measured TOTAL ACCENT span **+6.2 dB**, saturating at ≥ 80 % [MW05 §10.3]. Waveform asymmetry changes slightly with accent (+/− 0.45 → 0.49) [MW05].

### 1.7 Character summary
- A fast, deep, linear-in-Hz pitch drop: about 290 Hz → 49.5 Hz with τ 7–29 ms.
- A diode-rounded triangle, so audible H3 at −26 dB.
- A **flat ≈ 62 ms hold** ("compressed" punch), then an RC decay that **cuts off** at the VCA threshold.
- A ≈ 2 ms bright, resonant click (≈ 3.7 kHz ringing) plus dark noise (≈ 500 Hz).

---

## 2. Snare Drum (SD)

### 2.1 Block diagram
```
TRIG ─► +15 V pulse ─┬─► reset VCO1 (Q44/C69), VCO2 (Q45/C71), force comparators
                     ├─► mute VCA bases during trigger (Q46, Q49)
                     └─► C66 0.01µF  = ENV1 (pitch bend) ─► tune node A
TUNE VR6 10k ─► node A ─► follower → VDD, inverter → VSS of 4069UB (IC36)
VCO1 (C69 0.01µF) ─► diodes D65/66 ─► VCA Q50 (ENV3: C83 0.1µF, 242k → τ 24 ms) ─┐
VCO2 (C71 0.0068µF) ► diodes D69/70 ─► VCA Q51 (ENV2: C82 0.047µF, 440k → τ 21 ms)┤
NOISE ─► ÷4 ─► HP 48 Hz ─► SK LP 8.5 kHz Q 1.94 (IC40a) ─┬─► HP 1.52 kHz ─► VCA Q48 (ENV4, TONE) ─┐
                                                       └─► SK HP 4.05 kHz Q 1.31 (IC39a) ─► VCA Q47 (ENV5, ≈2 ms) ─┤
                                                                       IC39b Σ, gain = SNAPPY VR9 ─► 47k ─┤
                                                                                                IC40b Σ, LEVEL ─► out
```
Sources: [SN p.6 text], [MW01 §4.1], [N909 snaredrum], [SOS-SD].
- [SN p.6]: "Drum" = VCO-1 + VCO-2 (C69 ≠ C71, VCO-1 lower). "Snappy" = noise low-passed, then split into a high-passed path (ENV5) and a main path (ENV4), recombined at IC39b.
- [SOS-SD] redraws Roland's block diagram the same way: two oscillators, each with its own VCA and contour generator, plus noise → LPF → two branches (HPF + VCA/EG5, and VCA/EG4), then mixed.

### 2.2 Oscillators and TUNE
- TUNE VR6 (10 kΩ, linear) sets node A to **1.25–2.50 V**, an exact 2:1 range. At rest VDD = +V_A and VSS = −V_A [MW01 §4.2].
- **Settled frequencies** (measured, spectral peaks 50–250 ms) [MW01 §4.2 fact-check]:

  | TUNE | 0 % | 25 % | 50 % | 75 % | 100 % |
  |---|---|---|---|---|---|
  | VCO1 f1 | 117.4 Hz | 144.7 | 173.5 | 200.9 | 230.7 |
  | VCO2 f2 | 176.3 | 217.1 | 260.2 | 301.3 | 346.1 |
  | f2/f1 | 1.502 | 1.500 | 1.500 | 1.500 | 1.500 |

  - Derived f1 = 98.5 / 147.8 / 197 Hz, about 15 % low (*conflict*, cause unknown).
  - The nominal cap ratio is 1.471, but the measured settled ratio is **exactly 3:2** at every setting. That suggests mode-locking through the shared inverter rails (mechanism unverified). During the first ≈ 40 ms the ratio is about 1.55–1.58 [MW01].
  - [MW05]'s 20–80 ms peaks (114/171/230 and 180/262/346 Hz) are consistent.
- Waveform: diode-rounded triangles, as in §0.3. [RW p.4] describes the VCO1 triangle as rounded to "a rough approximation of a sine wave" by D65/D66.

### 2.3 Pitch bend (ENV1)
- [SN p.6]: the trigger pulse reaches the VDD/VSS supply of IC36 through the CV generator, so "the charging rate of C69 also continuously changes for about 20ms", giving a pitch bend. The figure shows the rails going from about ±2 V to ±5 V within 2 ms, then relaxing over about 20 ms [MW01 §4.3].
- Circuit [MW01 §4.3]:
  - C66 0.01 µF charges through D56 + R247 47 kΩ to about 13.3 V. The excess on node A is about **+2.0 V regardless of TUNE**.
  - Relaxation τ = C66·(R248 470k + R249 100k) = **5.7 ms**.
- Measured: τ = 4.8–5.5 ms. The bend is a **constant +216 Hz on VCO1 at onset at every TUNE**, i.e. f1(t) = f1s + 216 Hz·e^(−t/≈5 ms). As a ratio that is k = 1.91 / 1.28 / 0.95 × f1s at TUNE 0/50/100 [MW01 §4.3]. VCO2 follows with the same multiplicative factor (1 + k·e^(−t/τ)).
- Accent-independent [RW p.4]: the bend pulse is the same for every note regardless of accent.
- Whittle's low-TUNE mod (TUNE extended down to zero) leaves only this bend, giving a short low "bump" [RW p.4]. This shows the bend comes entirely from the trigger.

### 2.4 Drum VCAs
- **ENV3** (VCO1 → Q50): C83 0.1 µF discharging only into the base via R291 + R290 = 242 kΩ, **τ = 24.2 ms**. C70 4.7 nF smooths the attack (≈ 1 ms).
- **ENV2** (VCO2 → Q51): C82 0.047 µF via 440 kΩ, **τ = 20.7 ms**, smoothed by C79 0.01 µF.
- Measured: VCO1 τ 24–26 ms, VCO2 τ 25–31 ms. **VCO2 sits −9 dB below VCO1** (derived −8.4 dB) [MW01 §4.4]. [9W9] measured a shell τ of about 28 ms.
- Both drum VCAs are clamped off during the trigger (Q46/Q49), so the tone starts as the trigger ends, with both VCOs phase-aligned. [SN p.6] describes this as muting unwanted noises in the VCO-1 path.
- *Designator conflict:* [RW p.4] names the VCO1/VCO2 envelope caps C70/C79 and the pitch cap "C56". [MW01] reads C83/C82 as the main envelope caps (C70/C79 as smoothing caps) and C66 as the pitch cap. Function and time constants are unaffected.

### 2.5 Snappy (noise)
- **Input** [MW01 §4.5]: noise through R286 10k / R285 3.3k, a quarter of the 15 V swing [RW p.5], then C85 (HP 47.7 Hz), then a **unity Sallen-Key low-pass at 8.49 kHz, Q 1.94**. With the real source impedance the gain peak lands at about 7.4 kHz, +5.1 dB.
- **Path (a), main snappy, ENV4 / TONE**:
  - Filter: C80 0.022 µF into 4.7 kΩ + 47 Ω, a **1st-order HPF at 1.52 kHz**, into VCA Q48. Whittle confirms that C80 forms this high-pass [RW p.8].
  - ENV4 charge: C67 0.47 µF, charged by the accent to about V_acc − 1.2 V.
  - Hold: C68 0.0068 µF / R256 1 MΩ keeps the accent switch on for **≈ 24 ms** (flat top) [RW p.6; MW01].
  - Decay: through R254 100 kΩ + **TONE VR7 (500 kΩ, linear)** **toward a negative voltage (−0.1·V_acc)**, and through R297 220 kΩ into the base. τ = 32 ms (TONE min) to 76 ms (max). The VCA **cuts off hard** when the cap reaches about 0.4–0.6 V [MW01 §4.5; RW p.6].
  - Whittle likens the result to a steam valve opening and shutting [RW p.7]. He also suspects the VCA is near saturation above about 8 V, so the top of the decay is flattened (single source) [RW p.6].
  - **Measured cut-off times** (first −40 dB, 3–12 kHz band): **99 / 179 / 225 / 255 / 295 ms at TONE 0/25/50/75/100**. Before the 30–40 dB cliff the level falls only 6–22 dB [MW01 §4.5].
  - [MW05] gives a different metric: t(−20 dB) = 78 / 110 / 120 / 140 / 143 ms.
- **Path (b), "articulate" HF crack, ENV5**:
  - Filter: a **unity Sallen-Key HPF at 4.05 kHz, Q 1.31** (IC39a), into VCA Q47.
  - ENV5: the gated accent through D64 into C73 470 pF, then 220 kΩ into the base. τ ≈ 0.1 ms after the gate, so in effect a **≈ 2 ms accent-scaled noise burst** [MW01 §4.5; SN p.6].
  - *Conflict:* [RW p.4] calls ENV5 the same for every note regardless of accent, and names its VCA "Q41". [SN p.6] says the accent is gated through Q41 into Q47 as ENV5. Trust [SN]: Q41 is the accent switch, Q47 the VCA.
  - Whittle judges path (b) quiet and very brief, probably inaudible under path (a) [RW p.4].
- **SNAPPY knob** VR9 (10 kΩ, linear) is the feedback resistor of the IC39b summing amp, so snappy level ∝ rotation, silent at 0 [MW01 §4.5]. It then joins the drum through C78 0.47 µF + R294 47 kΩ (HP 7.2 Hz).
- **Measured spectrum**, octave bands re max (TONE 50, 50–120 ms): 250 Hz −6.3 (drum), 500 −10.1, 1k −5.4, 2k −3.0, **4k 0.0**, 8k −5.8, 16k −21.6 dB. This matches path (a)'s filter within 3 dB from 500 Hz to 8 kHz [MW01 §4.5].
- **Measured SNAPPY sweep** (TUNE/TONE 50) [MW05 §2.3]:
  - t(−20 dB): 72 / 90 / 120 ms at SNAPPY 0/50/100;
  - early spectral centroid: 1.1 / 5.1 / 6.5 kHz at the same settings.

### 2.6 Accent
- Accent sets the start of ENV2, ENV3, ENV4 and ENV5, and shifts ENV4's end voltage (−0.1·V_acc). Higher accent therefore also **lengthens the snappy** [RW p.4; MW01 §4.6].
- Measured TOTAL ACCENT span +4.8 dB [MW05 §10.3]. A single forum measurement says the envelope without accent is half its accented size [MW03, citing an Electro-Music thread] (unverified).

### 2.7 Character summary
- Two phase-reset, diode-rounded triangles locked about 3:2 apart, decaying fast (τ ≈ 21–25 ms).
- A fast +216 Hz downward "thwack" bend (τ ≈ 5 ms).
- Snappy noise with resonant peaks around 7–8 kHz above a 1.5 kHz high-pass. It sits on a **24 ms flat hold**, then an accelerating fall to an abrupt cut-off set by TONE (≈ 100–300 ms).
- The snappy noise is the same bitstream as the clap's.

---

## 3. Toms (LT, MT, HT)

### 3.1 Block diagram (same topology for all three)
```
TRIG ─► +15 V pulse ─┬─► reset VCO1/2/3 + force comparators (synced start)
                     ├─► 2 pitch caps → push VSS only (fast 22 ms, slow 220 ms)
                     └─► overdrive timer → VCO1 diode drive ×10 for ≈ 18–32 ms
TUNE 10k ─► VDD (follower) ; VSS = −(VDD + pitch-env terms) (inverter) ; 18 V zener
VCO1 (mid)  ─► 10k‖(1k while overdriven) ─► diodes ─► VCA (ENV1 + ENV2 share) ─┐
VCO2 (low, 0.667×) ─► 10k ─► diodes ─► VCA (ENV2: soft rise, DECAY)           ─┤
VCO3 (high, ≈1.8×) ─► 10k ─► diodes ─┬► VCA (ENV3, ≈ 22–26 ms)                ─┤ Σ, LEVEL ─► out
TOM NOISE (shared) ─► 22k ───────────┘                                          │
```
Sources: [MW01 §5], [N909 toms], [9W9 tom].
- [N909] describes three VCOs, VCO-3 highest and VCO-2 lowest, with a pitch envelope on all VCOs (it calls it "ENV-4"). VCO-1's clipper is trigger-controlled, giving a gradual change "from square … to sine". VCO-2's envelope has the adjustable decay. Tom noise is mixed into the VCO-3 path for the attack.

### 3.2 Part values per tom [MW01 §5.1]

| | LT | MT | HT |
|---|---|---|---|
| VCO1 cap | C18 0.022 µF | C32 0.018 µF | C97 0.015 µF |
| VCO2 cap | C19 0.033 µF | C33 0.027 µF | C102 0.022 µF |
| VCO3 cap | C20 0.012 µF | C34 0.01 µF | C103 0.0082 µF |
| ENV1 (VCO1) | 0.22 µF·470k = 103 ms | 0.15 µF·470k = 70.5 ms | 0.15 µF·470k = 70.5 ms |
| ENV2 cap / DECAY | C23 0.68 µF; VR11 500k + 56k | 0.47 µF; VR14 | 0.47 µF; VR17 |
| ENV3 (VCO3+noise) | 0.056 µF·470k = 26 ms | 26 ms | 0.047 µF·470k = 22 ms |
| Overdrive timer | 0.01 µF·1M (≈ 32 ms) | 0.0068 µF (≈ 22 ms) | 0.0056 µF (≈ 18 ms) |

All integrators use 47 kΩ. Cap ratios VCO1 : VCO2 : VCO3 give frequency ratios **1 : 0.667 : 1.83 (LT), 1 : 0.667 : 1.80 (MT), 1 : 0.682 : 1.83 (HT)**. [9W9] reads the same LT caps and expresses them as VCO2 : VCO1 : VCO3 = 1 : 1.5 : 2.75.

### 3.3 TUNE and frequencies
- TUNE (10 kΩ, linear, with 68k/10k) gives VDD = **1.705–3.409 V, exactly 2:1** [MW01 §5.2]. Measured ranges 1.91–1.97 [MW05].
- **Settled VCO1 frequency** (asymptotic fit, 120–600 ms) [MW01 §5.2]:
  - LT **57.0 / 85.8 / 117.2 Hz** at TUNE 0/50/100;
  - MT **71.9 / 106.6 / 141.3 Hz**;
  - HT **82.8 / 125.4 / 166.7 Hz**.
  - These are 4–8 % below derived (LT 61/92/122, MT 75/112/149, HT 90/134/179 Hz).
  - Values read at 150–250 ms are about 5–8 % higher (LT 62/90/122 etc. [MW05]) because the slow pitch term is still decaying.
- Measured ratios: VCO2/VCO1 0.673–0.679; VCO3/VCO1 1.77–1.94 (HT VCO3 about 6 % above nominal). The weak "3×" partial is VCO1's own third harmonic [MW01 §5.2].

### 3.4 Pitch envelope (VSS-only, two time constants)
- The +15 V pulse charges two caps to about 14.2 V. They discharge only into the VSS inverter's virtual ground [MW01 §5.3]:
  - fast: 0.047 µF·470k = **22 ms**, adding **+3.0 V** to |VSS|;
  - slow: 0.1 µF·2.2M = **220 ms**, adding **+0.65 V**;
  - both independent of TUNE.
- Only **VSS** moves, so f = f_s·2/(1 + VDD/|VSS|). This compressive law makes the 22 ms term look like a ≈ 40–55 ms decay in Hz. The triangle is also **skewed** early (falling faster than rising), which adds even harmonics.
- Measured:
  - VCO1 starts **25–38 % sharp** and relaxes with an apparent τ of 42–55 ms [MW01 §5.3]. The excess falls with TUNE (≈ 0.36 / 0.29 / 0.25 at TUNE 0/50/100).
  - The slow term is visible: f keeps falling about 7 % between 150 and 600 ms (LT TUNE 0: 62.2 → 57.8 Hz).
  - Early-to-settled peaks [MW05 §2.4]: LT 115 → 90 Hz, MT 146 → 112 Hz, HT 166 → 132 Hz (TUNE 50).
  - H2 from the skew: −7 dB at 0–30 ms, about −20 to −27 dB at 150–250 ms.
- Accent does not affect pitch.

### 3.5 VCO1 attack overdrive
- While a timer transistor conducts (≈ 32 / 22 / 18 ms for LT/MT/HT), a 1 kΩ path shunts VCO1's 10 kΩ feed resistor, so its diodes see **≈ 10× drive**. VCO1 therefore starts **near-square** and relaxes toward the rounded triangle [MW01 §5.4; N909 toms].
- Measured VCO1 H3/H5, 0–30 ms vs 150–250 ms: LT −11.6/−18.7 → −17.2/−31.0 dB; MT and HT similar [MW01 §5.4]. The SN p.9 LT drawing shows flat-topped early cycles.

### 3.6 Amplitude envelopes and DECAY
- **ENV1** (VCO1): exponential, τ 103 ms (LT) or 70.5 ms (MT/HT). It also receives part of ENV2 through 220 kΩ.
- **ENV2** (VCO2): a two-capacitor network.
  - C_A (0.68 µF LT, 0.47 µF MT/HT) is charged by the accent and then fills C_ENV2 through 100 kΩ. VCO2 therefore **swells in**, with a rise τ ≈ 34 ms (LT) or 23.5 ms (MT/HT).
  - C_A discharges through DECAY + 56 kΩ: **τ 38–378 ms (LT), 26–261 ms (MT/HT)** across the pot.
  - C_ENV2 also drains into two 220 kΩ base resistors.
  - Net behaviour is derived only; fit it to recordings [MW01 §5.5].
- **ENV3** (VCO3 + tom noise): τ ≈ 26 ms (LT/MT), 22 ms (HT). An attack-only "bark".
- **Measured** amplitude τ at DECAY 100 [MW01 §5.5]:
  - VCO1: LT 166–180 ms, MT 114–118, HT 101–119;
  - VCO2: LT 272–302 ms, MT 158–167, HT 163–173;
  - VCO3: 13–58 ms.
  - Levels: VCO2 −7 to −9 dB re VCO1; VCO3 0.35–0.73 of VCO1.
- **DECAY knob effect** (measured t(−20 dB), TUNE 50, DECAY 0 → 100): LT 165 → 213 ms, MT 103 → 138, HT 108 → 160 ms. The effect saturates above about 40–60 % [MW01 §5.5]. DECAY mostly sets the length of the low VCO2 "bloom".
  - [N909]: with longer decay settings you end up hearing just one oscillator after a few milliseconds.
  - [9W9]: after about 60 ms the ring is nearly a single sine.

### 3.7 Tom noise (shared by all three toms)
- Noise → R190 22k / C161 4.7 nF → Sallen-Key HP (2 × 4.7 nF, 10k/220k). The source impedance damps the resonance, so the loaded network is a **broad band-pass peaking near 900 Hz, only +1.6 dB, −3 dB band ≈ 625–1600 Hz**. (The ideal-source figures would be HP 722 Hz, Q 2.35.)
- It then reaches IC27a two ways:
  - a **sub-millisecond VCA'd "tick"** (Q33; C54 charged from any tom's trigger, τ ≈ 0.22 ms, or 0.47 ms on later units);
  - a steady component through R194 that each tom gates with its own ENV3 (injected through 22 kΩ into its VCO3 emitter node) [MW01 §5.5].
- [SN p.7]: from S/N 426700, C54 changed from 0.0022 to 0.0047 µF and R194 from 47k to 100k. Roland's stated reason is that the change emphasises the attack of the toms. The p.9 LT drawing marks "noise" at the onset.

### 3.8 Accent
- Accent sets ENV1, ENV2 (via C_A) and ENV3. There is no clamp.
- Measured span **+7.3 (LT), +7.7 (MT), +7.4 dB (HT)** [MW05 §10.3].

### 3.9 Character summary
- Three phase-synced, diode-rounded triangles at about 0.67 : 1 : 1.8.
- A fast downward pitch dive (+25–38 %, τ_app ≈ 45 ms) with skew-induced even harmonics, plus a slow 220 ms droop.
- A near-square VCO1 for the first 20–30 ms.
- A bark from VCO3 plus a band-limited noise tick, then a long low VCO2 bloom set by DECAY.

---

## 4. Rim Shot (RS)

### 4.1 Block diagram
```
TRIG ─► C111 0.018µF (edge) ─┬─► accent switch → C119 0.047µF ─► 1M ─► VCA Q65 base  (τ 47 ms)
                            └─► D89, ÷2 = kick ≈ 2.2 V, τ 0.8 ms ─► (+) inputs of F1, F2, F3
F1 bridged-T (0.01µF ×2, 2.2k, 470k)  ─► 12k ─┐
F2 bridged-T (0.027µF ×2, 2.2k, 330k) ─► 12k ─┼─► Σ node with D91/D92 anti-parallel (hard clip ±0.6 V)
F3 bridged-T (0.0047µF ×2, 2.2k, 470k)─► 3.3k*┘     ─► 12k/680Ω ─► VCA Q65 ─► IC50a, LEVEL 100k ‖ 220 pF
     ─► Sallen-Key HP 229 Hz, Q 1.58, then ×2 ─► out          (*12k before S/N 415300)
```
Sources: [MW01 §6], [N909 rimshot], [SN p.7, p.9].
- [N909] describes three bridged-T band-pass resonators with **f1 ≈ 500 Hz, f2 ≈ 220 Hz, f3 ≈ 1000 Hz**, each kicked by a pulse from D89, mixed and clipped by D91/D92, then a VCA (Q64/C119 envelope) and a final high-pass. It also gives the bridged-T Q formula.
- The schematic annotates F1 "T≈2 ms", F2 "T≈4.5 ms" and F3 "T≈1 ms". These are periods, with 30/15/30 Vpp ring amplitudes, so the filters clip at their rails initially [MW01 §6.1].

### 4.2 Resonators
- Bridged-T with excitation on the (+) input (808-style). f = 1/(2π√(R1R2C1C2)); Q = √(R2/R1)/(√(C1/C2) + √(C2/C1)); τ = Q/(πf) [MW01 §6.1; N909].

| Filter | derived f | Q | τ (derived) | measured f | measured τ |
|---|---|---|---|---|---|
| F2 | 218.8 Hz | 6.1 | 8.9 ms | 222 Hz | 9.6 ms |
| F1 | 494.9 Hz | 7.3 | 4.7 ms | 481 Hz | 4.6 ms |
| F3 | 1053 Hz | 7.3 | 2.2 ms | 1019 Hz | 2.8–3.0 ms |

Derived and measured values are from [MW01 §6.2]; the measured spectrum is in [MW05 §2.5].
- *Conflict:* [9W9] reports Q ≈ 10–11 and "ring times" of 15.5 ms at 210 Hz and 7.5 ms at 480 Hz, with the upper resonance at about half the lower one's level at 2 ms. It does not define "ring time"; it may be a −x dB time rather than τ. Prefer the part-derived Q.
- The excitation is the **edge-differentiated trigger** (≈ 2.2 V, τ ≈ 0.8 ms). It is **not accent-scaled** [MW01 §6.2].

### 4.3 Clipper, VCA, output
- The resonator outputs (7–15 V peak) drive milliamps into D91/D92, so the sum is **hard-limited to ≈ ±0.6 V** and comes out square-ish. The weights (12k/12k/3.3k) matter mainly as the rings decay into the clipper's linear region [MW01 §6.3].
  - The SN p.9 drawings of both R417 versions show square-topped cycles under a decaying envelope.
  - Measured intermodulation lines at **723, 1288, 1975, 2446 Hz** (−20 to −35 dB), the same at every accent [MW01 §6.2].
  - Band peaks occur at 1.7 ms (F3), 5.5 ms (F1) and 9.2 ms (F2).
- From S/N 415300, **R417 changed from 12 kΩ to 3.3 kΩ**, weighting the 1 kHz resonator 3.6× more. Roland's stated reason is a more realistic sound [SN p.7].
- VCA Q65: C119 charged to about V_acc − 0.6 V, discharging through 1 MΩ into the base (pure exponential, **τ 47 ms**), which is long compared with the rings [MW01 §6.3].
- Output: LEVEL VR19 in parallel with 220 pF forms a level-dependent low-pass (7.2 kHz at full, 14.5 kHz at half). Then a **Sallen-Key HPF at 229 Hz, Q 1.58** (+4.3 dB peak, right at F2) and ×2 gain [MW01 §6.4]. [N909] says this final high-pass removes unwanted low frequencies.
- **Measured envelope**: t(−10/−20/−30 dB) = 9.6 / 17 / 21.7 ms (accent 0) and 9.7 / 21 / 26 ms (accent 100) [MW05 §2.5].
- **Accent** changes level only (partials and τ unchanged), span **+3.9 dB** [MW05 §10.3]. RS has only a LEVEL knob.

### 4.4 Character summary
- Three short, high-Q rings (≈ 220 / 480–495 / 1020–1050 Hz) slammed into a hard diode clipper. The clipper causes the square-ish, intermodulated, "woody" crack.
- A resonant 229 Hz high-pass emphasises the 220 Hz body.

---

## 5. Hand Clap (HC)

### 5.1 Block diagram
```
NOISE ─► 47k ─► MFB band-pass (IC26b): f0 963 Hz, Q 2.13, gain −1.6
   ├─► C56/39k+1k (HP 181 Hz, ÷40) ─► OTA BA662A (IC30) ◄─ control current from Q38 exp. converter ◄─ burst generator
   │        └─► 82k / C58 1nF into summing node (1st-order HP 1.94 kHz) ──────────────────────────────┐
   └─► 1k + C57 0.47µF (LP ≈ 440 Hz loaded) ─► VCA Q37 (base ◄ 470k ◄ C61 0.1µF ◄ 470k ◄ C59 0.1µF) ─┤ IC28a Σ, LEVEL 50k ─► out
Burst generator: accent (×3.13) ─gated (Q35/Q36)─► AN6912 quad comparator (IC29) + RC timers
                 (C62 0.027µF·1M = 27 ms gate; C65 0.47µF slammed via 330Ω by a transistor) ─► Q38 (dual PNP) ─► OTA
                 TM trimmer 10k ─► R494 ─► other base of Q38 (sets burst depth/density)
```
Sources: [MW01 §7.1], [N909 handclap], [RW p.8–10].
- [N909]: the circuit is nearly the same as the TR-808's. Noise is band-passed by IC26b, then split. One part goes through VCA IC30 on a special envelope made of **four attack-decay envelopes (op-amps a–d), each starting as the previous one ends**. The other goes through VCA Q37 on a simple envelope that gives a reverb-like background.
- [RW p.9]: the OTA is driven by "a series of short pulses, like a few cycles of a sawtooth wave", a sharp rise followed by a slope down. Trimmer TM sets their overall level and how far they run together, through one base of dual-PNP Q38, which works roughly as a log-to-linear (exponential) converter.
  - Whittle's added Density pot shows TM's range: at one end almost no pulses, at the other the pulses merge into one long pulse.
  - [RW] confirms the band-pass input node impedance (47k ‖ 10k ≈ 8.2k) and that a low-impedance source there can make the filter self-oscillate.

### 5.2 Band-pass
- MFB (Deliyannis) band-pass with R1 47k, R2 10k (to ground), R3 150k (feedback), C1 = C2 = 4.7 nF: **f0 = 963 Hz, Q = 2.13, centre gain −1.6** [MW01 §7.2].
- Measured spectral peak 1031 Hz in the bursts and 938 Hz in the tail; a later re-measure gave 965 / 911 Hz [MW01 §7.2]. [MW05] found peaks at 0.92–1.05 kHz.
- *Conflict:* [KVR]'s "≈ 1140 Hz, Q ≈ 1.95" is not supported by the part values, and its own poster was unsure. Use 963 Hz / Q 2.13.
- The 808 clap's band-pass, for contrast, is about 1.27 kHz, Q ≈ 1.5 [MW01 §7.6].

### 5.3 Burst ("hands") path
- The OTA input is high-passed at 181 Hz and attenuated ÷40. Its output current goes through a **1.94 kHz first-order high-pass** (82 kΩ / 1 nF into the virtual ground), so the bursts are brighter than the tail [MW01 §7.3].
- **Measured burst onsets: 0, 9.8, 21.5, 31.4 ms** (±0.4 ms over 19 hits, deterministic). Spacings are 9.8 / 11.7 / 9.9 ms, and the 4th burst merges into the tail [MW05 §2.6; MW01 §7.3]. A fact-check pass gave 0 / 10.3 / 21.9 / 31.8 ms.
- **Burst envelope**: each burst jumps to within 1–3 dB of the peak and then falls almost linearly in dB, **≈ 25 dB in 8–9 ms (τ ≈ 2.6–3.3 ms)**, reaching about −25 to −29 dB before the next one. This fits an exponential converter driven by an RC-recovering sawtooth [MW01 §7.3].
- *Conflict:*
  - [9W9] describes the bursts as three "pre-echoes" **rising** in level (0.62 / 0.70 / 0.86, ~12 ms apart) before a full-level main clap that starts the room tail.
  - [N909] and [KVR] describe four roughly equal envelopes, about 11 ms apart in [KVR]'s description.
  - [MW01]'s 19-hit measurement (all bursts within 1–3 dB of peak) is the most detailed. Prefer equal bursts, but check by ear.
- **Burst timing circuit**: a 27 ms gate timer (C62/R229) matches the ≈ 30 ms burst window. The RC that sets the ≈ 10 ms burst period is **not identified**; candidates are C65 with R221 18k (8.5 ms) or with R220 82k (38.5 ms). The AN6912 sequencing has not been traced [MW01 §7.3, §10]. Compare the 808: burst onsets 0 / 10.8 / 22.1 / 29.7 ms [MW01 §7.6].
- Measured burst spectrum (octave bands 125 Hz … 48 kHz re max): −22 / −12 / **0 / 0** / −6 / −12 / −19 / −26 / −35 dB, centroid ≈ 2.45 kHz [MW01 §7.3].

### 5.4 Reverb (tail) path
- Low-pass at **≈ 440 Hz** (R204 1k / C57 0.47 µF, loaded), so the tail is darker than the bursts. It is attenuated about ×0.014 into VCA Q37 [MW01 §7.4].
- Envelope: D52 charges C59 0.1 µF, which feeds C61 0.1 µF through 470 kΩ, which feeds the base through 470 kΩ. This two-pole ladder **swells in** (derived eigen-τ ≈ 18 ms rise, 123 ms decay; peak ≈ 25–30 ms) [MW01 §7.4]. What charges D52, and for how long, is not traced.
- Measured tail [MW05 §2.6]:
  - −16 dB at 60 ms, −26 at 100, −34 at 140, −44 at 200, −47 at 300 ms;
  - slope −96 dB/s (**τ ≈ 90 ms**);
  - t(−20 dB) 77–85 ms, t(−30 dB) 143–151 ms.
- Tail spectrum: octave bands −16 / −9 / **0** / −4 / −14 / −21 dB … (peak at the 500 Hz–1 kHz octave), centroid ≈ 1.4 kHz [MW01 §7.3].
- [RW p.9]: shorting C61 disables the reverb part.

### 5.5 Accent and level
- Accent enters only the burst generator. Measured span **+2.7 dB** (noisy) [MW05 §10.3]. HC shares its accent DAC with RS [MW03].
- LEVEL VR20 (50 kΩ, linear) is the summing feedback for both paths.

### 5.6 Calibration
- TM is the **only trimmer in the analog voices**. Its setting is "2 Vp-p at MULTI OUT, LEVEL centre", read beside TM on the schematic [MW01 §7.3].
- It is a strong source of unit-to-unit variance in burst depth and density.

### 5.7 Character summary
- 909-specific details compared with the 808:
  - band-pass ≈ 960 Hz (lower than the 808's 1.27 kHz);
  - 1.94 kHz tilt on the bursts;
  - darker 440 Hz tail;
  - LFSR noise shared with the SD.
- Four deterministic sawtooth-shaped bursts roughly 10–12 ms apart, through an exponential VCA.
- A swelling, ≈ 90 ms reverb tail.

---

## 6. Front-panel controls: quick reference

| Voice | Knob | Part | What it changes | Range |
|---|---|---|---|---|
| BD | TUNE | VR2 100k A | pitch-sweep τ only | 6.8–29.4 ms (4.5–19.6 ms on S/N < 381500); f(0) ≈ 280–296 Hz, f_b ≈ 49.5 Hz fixed |
| BD | ATTACK | VR3 500 Ω B | click (pulse + noise) level into Q6 | ≈ 7.5 dB span |
| BD | DECAY | VR5 1M A | ENV1 discharge after the 62 ms hold | τ 14–107 ms; t(−20 dB) ≈ 96–298 ms measured |
| BD/SD/toms/RS/HC | LEVEL | feedback pot | linear output gain | 0 to max |
| SD | TUNE | VR6 10k B | both VCO rails (base pitch) | f1 117–231 Hz, f2 = 1.5·f1 (2:1 span) |
| SD | TONE | VR7 500k B | snappy ENV4 decay (to a negative voltage, hard cut) | τ 32–76 ms; cut-off ≈ 99–295 ms |
| SD | SNAPPY | VR9 10k B | snappy level (summing feedback) | linear, 0 = none |
| Toms | TUNE | 10k B | VDD (base pitch of all 3 VCOs) | 2:1; LT 57–117, MT 72–141, HT 83–167 Hz (VCO1, settled) |
| Toms | DECAY | 500k B | ENV2 (VCO2 bloom; part of VCO1) | C_A τ 38–378 ms (LT), 26–261 ms (MT/HT); overall t(−20 dB) only +30–50 % |

Pot values and tapers are from [MW01 §0 item 10] (SN p.1/p.2 parts list). Function descriptions are from [CF], [RW] and [N909].

---

## 7. Unknowns and open conflicts

1. **BD base pitch**: 56 Hz derived vs 49.5 Hz measured. Possibly a larger triangle amplitude than assumed; calibrate to 49.5 Hz [MW01].
2. **BD decay length**: two measurement passes on the same files disagree (235 vs 298 ms at DECAY 100). The hold is 62 ms measured vs about 48 ms derived [MW01].
3. **BD sweep height vs TUNE**: [MW05] measures it nearly constant (229–247 Hz); [9W9] reports it growing with τ.
4. **SD absolute pitch** is about 15 % above derived, and the **3:2 VCO lock** has no explained mechanism [MW01].
5. **SD ENV5 accent dependence**: [SN] says accented, [RW] says not. ENV5's audibility is minor either way.
6. **Tom ENV2 network** dynamics are derived only, and the DECAY law is fitted, not derived.
7. **HC burst generator**: the period RC, the comparator sequencing, and whether bursts are equal or rising (see the sources in §5.3). The reverb charge path is also untraced.
8. **RS Q**: part-derived 6–7 vs [9W9]'s "Q ~10–11".
9. **Accent DAC codes** (normal / per-voice accent / TOTAL ACCENT mapping) are undocumented [MW03].
10. **Even-harmonic source** (VCA transistor vs diode mismatch) is unverified.
11. **Absolute levels**: no volt table exists. The p.9 drawings (BD ≈ 1.2 / 0.6 Vpp with/without accent, SD ≈ 2.1 / 1.0, LT ≈ 1.9 / 1.2, all knobs centre) carry about ±30 % error [MW01 §1.5].
12. All measurements come from **one or two units**. Unit variance (5 % parts, VCA thresholds, the HC trimmer) is real.

Production variants worth exposing as an "early/late unit" switch: BD C9 0.22 → 0.33 µF (S/N 381500); RS R417 12k → 3.3k (S/N 415300); tom-noise C54/R194 change (S/N 426700); multi-out RC from S/N 415300 [SN p.7].

---

## 8. Implementation notes (minimal faithful DSP structure per voice)

General rules:
- **Envelopes**: model every envelope as an RC state with exact exponential coefficients (v ← target + (v − target)·e^(−1/(τ·fs))). Model diode charge paths as a separate fast τ. Make hold timers an RC crossing a threshold, not a sample counter [MW05 §2.7].
- **VCA**: g = softplus_k(V_env − V_th) with V_th ≈ 0.4–0.6 V and a ≈ 20 mV knee. It is linear above threshold; this matters for the BD and snappy cut-offs.
- **VCO**: a linear-FM phase accumulator (f = f_s + k·V_env), triangle output, reset to mid-slope at a fixed direction at the end of the 2 ms trigger. Band-limit the corners with polyBLAMP and the reset step with polyBLEP. 1× oversampling suffices for these frequencies [MW05 §2.1].
- **Diode shaper**: solve (x − u)/R_s = u/R_load + 2·I_s·sinh(u/(n·V_T)), or tabulate it. tanh(k·x) is an acceptable approximation; fit k to the BD harmonics.
- **Noise**: one shared white PRNG feeding SD, BD attack, tom noise and HC. There is no need to run the 2^31 LFSR at audio rate, but keep it **one stream** so that SD+HC correlate.
- **Accent**: a per-voice held voltage (RS/HC share one). It drives envelope start voltages and is never a post-gain.
- **Filters**: TPT/SVF or biquads at the stated f0/Q.

### BD
1. On trigger: P = 1 (unaccented). Reset the triangle. Hold-timer state = 1.
2. Pitch: V9 jumps to its peak (fast 0.73 ms charge), then decays with τ_T = 6.8–29.4 ms (A taper; early units 4.5–19.6 ms). f = 49.5 + 230·(V9/V9_0) Hz.
3. Triangle (peak ≈ 4.9 V equivalent) → diode-pair shaper → small even-order term fitted to H2 ≈ −28 dB.
4. ENV1: start = min(V_acc − 1.2, 2.63 V). Hold 62 ms. Then dV/dt = −V/(R_d·C8) − max(0, V − 0.6)/(470k·C8), with C8 = 0.33 µF and R_d = 47k + DECAY (1 MΩ A). Gain ∝ max(0, V − 0.6), softened. Calibrate so t(−20 dB) ≈ 96 / 298 ms at DECAY 0 / 100.
5. Click: a 2 ms trigger-derived pulse (amplitude ∝ accent) → differentiator (≈ 3.3 nF/100k) → SK LP 3.72 kHz, Q 3.1. Plus noise → HP 26 Hz → LP 513 Hz. Sum, multiply by ATTACK gain (−25 dB … −17.5 dB), then by ENV2 (τ ≈ 0.7 ms after the trigger). Target: click peaks at about 1.5 ms and is −20 dB by about 3 ms.
6. Sum the two paths and apply LEVEL (linear).

### SD
1. On trigger: mute both drum VCAs for 2 ms. Reset VCO1 and VCO2 in phase.
2. Rails: VDD = V_tune + 2.0 V·e^(−t/5.5 ms), where V_tune = 1.25 V at TUNE 0 to 2.5 V at TUNE 100 (linear). Calibrate so f1 = 117.4 → 230.7 Hz settled, with an onset excess of +216 Hz. f2 = 1.5·f1 settled (allow about 1.55–1.58 in the first 40 ms, or simply track the same rail factor).
3. Shapers: diode pair on each triangle. VCA1 τ 24 ms; VCA2 τ 21 ms at −9 dB. Envelope start ∝ (V_acc − 1.2). Pure exponentials with about 1 ms attack smoothing.
4. Noise conditioning: ÷4 → HP 48 Hz → SK LP 8.5 kHz, Q 1.94 (peak ≈ 7.4 kHz with source loading).
   - Path a: HP1 1.52 kHz → VCA on ENV4. ENV4: start ≈ V_acc − 1.2 (fit ≈ 10 V at the reference accent), flat 24 ms, then decay toward V_end ≈ −0.1·V_acc (fit −0.5 V) with τ = 0.47 µF·((100k + 500k·TONE)‖220k). The VCA uses a 0.6 V threshold and a soft top saturation, so it cuts off at 99–295 ms over TONE.
   - Path b: SK HP 4.05 kHz, Q 1.31 → VCA on a 2 ms accent-scaled gate (τ 0.1 ms release).
   - Sum a + b, multiply by SNAPPY (linear), HP 7 Hz, add to the drum, apply LEVEL.

### Toms (one engine, three part sets)
1. On trigger: reset VCO1/2/3 in phase. Fast pitch cap = 3.02 V (τ 22 ms); slow pitch cap = 0.645 V (τ 220 ms). Overdrive timer starts.
2. Rails: VDD = V_tune (1.705–3.409 V); |VSS| = VDD + fast + slow. Each VCO: f = f_s,i·2/(1 + VDD/|VSS|). Run the integrator at VDD-proportional slope one way and |VSS|-proportional the other (this gives the skew and early even harmonics). Scale f_s to the measured values (LT 57/86/117, MT 72/107/141, HT 83/125/167 Hz for VCO1). VCO2 = 0.667–0.68×, VCO3 = 1.77–1.94×.
3. Shapers:
   - VCO1: a diode pair whose input gain ramps from about 10× down to 1× as the overdrive timer (C·1 MΩ, 32/22/18 ms) discharges below 0.6 V. It starts near-square.
   - VCO2/VCO3: normal drive.
4. VCAs:
   - VCO1 on ENV1 (τ 103 / 70 / 70 ms) plus an ENV2 share.
   - VCO2 on ENV2: two-capacitor network, rise τ 34 / 23.5 ms; C_A decay through DECAY + 56k. Fit it to measured VCO2 τ ≈ 300 / 165 / 170 ms at DECAY 100 and the overall t(−20 dB) table.
   - VCO3 on ENV3 (τ 26 / 26 / 22 ms).
   - All starts ∝ accent.
5. Tom noise (shared): noise through the loaded network (broad band-pass, peak ≈ 900 Hz, 625–1600 Hz). Add a 0.47 ms tick on any tom trigger, plus a steady component gated by each tom's ENV3 into the VCO3 path.
6. Sum, apply LEVEL.

### RS
1. On trigger: a kick pulse of about 2.2 V, τ 0.8 ms (not accented), into three resonators (biquad band-pass or TPT SVF), using the excitation-on-(+) bridged-T response: F1 495 Hz Q 7.3; F2 219 Hz Q 6.1; F3 1053 Hz Q 7.3. Alternatively calibrate to the measured 481 / 222 / 1019 Hz and τ 4.6 / 9.6 / 2.9 ms. Let early peaks clip at the op-amp rails (±13.5 V).
2. Weighted sum (1 : 1 : 3.6 late units; 1 : 1 : 1 early) → hard diode clip at ±0.6 V (sinh/exp diode law or a hard clip with a small knee).
3. VCA: start ∝ (V_acc − 0.6), pure exponential τ 47 ms.
4. LEVEL with a 220 pF·R_level low-pass (7.2 kHz at full), then SK HP 229 Hz, Q 1.58, ×2.

### HC
1. Noise (the same stream as SD) → MFB band-pass 963 Hz, Q 2.13.
2. Burst path:
   - HP 181 Hz → VCA with gain = exp(control). The control is a sawtooth-like train: 4 rises at 0 / 9.8 / 21.5 / 31.4 ms, each followed by about 25 dB of fall in 8–9 ms. The 4th merges into the tail.
   - Then 1st-order HP 1.94 kHz.
   - A "TM" parameter sets the exponential offset (burst depth and density); calibrate so the burst train is about 2 Vpp at LEVEL centre equivalent. Accent scales the burst control (span ≈ +2.7 dB).
3. Reverb path: LP 440 Hz → VCA on a two-pole swell (rise ≈ 18 ms, decay fitted to ≈ 90 ms, i.e. −96 dB/s; derived 123 ms).
4. Sum, apply LEVEL.
