# Settings

Knobs are 0..127. Start from PITCH 64, plain trigs (note C4), Model-TG Filter open and Attack 0.
PUNCH is the accent: p-lock it on the steps to accent. "Mars" rows reproduce the 909 From Mars hits the voices were
fitted to.

## BD (KICK)

SHAPE 0 (the 909; 1 808, 2 606). COLOR Attack, FINE Drive, SWEEP Sweep depth, CONTOUR Tune (sweep time), DECAY. CONTOUR, COLOR and DECAY follow a
real 909's TUNE, ATTACK and DECAY pots (measured at 5 positions each): 0..127 = the pot's 0..100 %.

| Sound | COLOR | FINE | SWEEP | CONTOUR | DECAY |
|---|---|---|---|---|---|
| Default (pots at half) | 64 | 64 | 64 | 64 | 64 |
| Mars Clean Long A | 44 | 64 | 64 | 0 | 127 |
| Mars Tape Medium F | 127 | 64 | 64 | 127 | 32 |
| Mars Tube Long F | 127 | 64 | 64 | 127 | 112 |
| Hard techno | 100 | 80 | 70 | 20 | 40 |
| Distorted | 70 | 100-127 | 64 | 60 | 70 |
| No click | 0 | 30 | 50 | 80 | 110 |

(The Mars rows are accented hits: PUNCH on.)

- CONTOUR: the 909's TUNE, the pitch sweep's time constant from 7.6 ms (0) to 28 ms (127); most of the change is
  in the top quarter, as on the real pot.
- COLOR: the 909's ATTACK over 16..127 (about 6 dB of click); below 16 the click fades out.
- DECAY: the 909's DECAY. Short hold, then a tail that cuts off (-20 dB at about 80 ms to 210 ms, unaccented).
- FINE: 64 is the 909, lower is rounder, higher drives toward a square.
- SWEEP: depth of the pitch drop (64: about 270 Hz above the 49.5 Hz base). PITCH moves the whole kick.

## SD (SNARE)

SHAPE 0 (the 909; 1 808, 2 606). COLOR Snappy, FINE Tone, SWEEP Pitch drop, CONTOUR Body, DECAY snappy length.

| Sound | COLOR | FINE | SWEEP | CONTOUR | DECAY |
|---|---|---|---|---|---|
| Default | 64 | 64 | 64 | 64 | 64 |
| Mars Clean E_01 | 34 | 64 | 64 | 64 | 28 |
| Mars Clean E_03 | 21 | 64 | 64 | 64 | 29 |
| Mars Clean E_10 | 95 | 64 | 64 | 64 | 102 |
| House | 110 | 80 | 64 | 64 | 90 |
| Fat | 60 | 40 | 80 | 90 | 60 |

- COLOR: the 909's SNAPPY. DECAY: the 909's TONE (snappy cut-off, about 100 to 300 ms).
- PITCH: the 909's TUNE (64: 198 and 315 Hz).
- FINE: noise brightness. SWEEP: pitch bend at the start. CONTOUR: drum decay.

## Hats (METAL)

COLOR Tone, SHAPE 0 closed / 1 open, SWEEP Bits, DECAY (127 = as sampled).

| Sound | COLOR | SHAPE | SWEEP | DECAY |
|---|---|---|---|---|
| Closed | 64 | 0 | 0 | 127, or 90 for tighter |
| Open | 64 | 1 | 0 | 127, or 100 for shorter |
| Darker | 40 | | 0 | |
| Lo-fi | 64 | | 90-110 | |

Choke: put closed and open on one track and p-lock SHAPE 1 on the open steps. A closed hat cuts the open one.
COLOR 64 is flat; lower is darker, higher thinner.

## RS/CP (PERC)

SHAPE 0 rim, 1 clap.

| Sound | COLOR | SHAPE | SWEEP | CONTOUR | DECAY |
|---|---|---|---|---|---|
| Rim | 64 | 0 | 64 | | 64 |
| Sharper rim | 90 | 0 | 90 | | 40 |
| Clap | 64 | 1 | 64 | 64 | 64 |
| Tight clap | 64 | 1 | 0-20 | 50 | 50 |
| Big clap | 64 | 1 | 75 | 70 | 95 |
| Darker / brighter | 30 / 100 | 1 | 64 | 64 | 64 |

- Rim: COLOR drive, SWEEP top ring level, DECAY.
- Clap: SWEEP burst spacing (64: 0, 11, 24, 30 ms; 0 merges them), CONTOUR burst length, DECAY reverb tail,
  COLOR width and brightness. PITCH tunes it.

## Toms (TONE)

COLOR Noise, SHAPE 0 low / 1 mid / 2 high, SWEEP Pitch dive depth, CONTOUR Dive time, DECAY.

| Sound | COLOR | SHAPE | SWEEP | CONTOUR | DECAY |
|---|---|---|---|---|---|
| Default | 64 | 0 / 1 / 2 | 64 | 64 | 64 |
| Mars | 64 | 0 / 1 / 2 | 64 | 64 | 56 / 57 / 65 |
| Short | 64 | | 70 | 40 | 20 |
| Long | 50 | | 60 | 80 | 110 |

- PITCH: 64 gives LT 61, MT 92, HT 120 Hz settled; each hit starts about a third higher.
- DECAY: the 909's DECAY, mostly the low oscillator's bloom.
- Fill: one track, p-lock SHAPE 2, 1, 0 across the steps.

## Cymbal (CHORD)

COLOR Tone, SHAPE 0 crash / 1 ride, SWEEP Bits, DECAY (127 = as sampled). The CHORD slot shows COLOR and SHAPE values
as chord names; the sound is right.

| Sound | COLOR | SHAPE | SWEEP | DECAY |
|---|---|---|---|---|
| Crash | 64 | 0 | 0 | 127 |
| Ride | 64 | 1 | 0 | 127, or 100 for shorter |
| Lo-fi | | | 90-110 | |
