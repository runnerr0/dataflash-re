# Pattern catalog — grounded in the original Dataflash

The Dataflash is a **xenon strobe**, not an LED array. A "pattern" is about
**flash timing, which heads fire, and per-flash intensity (16 levels)** — not
smooth dimming or color. The pattern set below is tiered: faithful-to-device
first, then idiomatic, then experimental.

## The device's native model (from the original controller manual)
The original Dataflash controller ran **Programs**, each a sequence of **Stages**
(scenes; a stage = a set of heads at intensities, stored in memories). On top of
the sequencer sit a few controls — these become our *parameters*, not separate
patterns:

| Original control | What it did | Our parameter |
|---|---|---|
| Auto + Rate knob | step stages at a set speed | `advance=auto`, `speed` |
| Audio 1/2 + Audio knob | beat accelerates/halts the advance | `advance=beat`, `bpm`/`tap` |
| Modulate | head intensity tracks audio amplitude | `modulate` + `audioLevel` |
| Random | run stages from a random start / order | `random` |
| Multiply + Factor knob | repeat each stage N times (burst/ratchet) | `factor` |
| Flash | momentary all-at-max bump | `flash` |
| Standby | blackout output | `blackout` |
| Intensity / Intensity Limit | max brightness / cap | `intensity` |

So our **grid builder == a Program** (stages × heads), and the engine is a
stage sequencer with those modifiers applied across all modes.

## Tier A — Native (device-faithful)  [start here]
| # | `/df/pattern` | Pattern | Notes |
|---|---|---|---|
| 1 | `all` | All fire | every head at `intensity` |
| 2 | `single` | Single head | bench/addressing validation (`/df/single`) |
| 3 | `seq` | **Program playback** | plays the stored grid = original Stages |

The native experience *is* the sequencer: build a Program in the grid, play it
with `seq`, and shape it live with `speed`/`bpm`/`advance`, `factor` (Multiply),
`random`, `modulate`, plus `flash` and `blackout`.

## Tier B — Framework (idiomatic strobe behaviors)
Sequencer-generated, so the same modifiers (factor/random/advance/modulate) apply.
| # | `/df/pattern` | Pattern | Notes |
|---|---|---|---|
| 4 | `chase` | Running flash | width via `density` |
| 5 | `alternate` | Odd/even banks | flip-flop |
| 6 | `build` | Cumulative build | add a head per stage, then clear |
| 7 | `strobe` | All flash | on/off at `speed`/`bpm` |
| 8 | `sparkle` | Random twinkle | per-stage random subset, `density` |

## Tier C — Alternate / experimental
| # | `/df/pattern` | Pattern | Notes |
|---|---|---|---|
| 9  | `wave` | Sweeping flash window | brief graded flash, not a smooth LED sine |
| 10 | `pingpong` | Bouncing chase | |
| 11 | `comet` | Moving flash + short tail | uses low levels; depends on head low-end |

## Why some "LED" patterns were recast
A xenon head can't hold a smooth fade and has a hard cooldown/duty limit, so the
original scaffold's sine `wave` and decaying `sparkle` were re-cast into the flash
idiom (brief graded flashes / per-stage re-roll). `comet` keeps a tiny tail as an
experiment — validate it actually reads on real heads at low levels.

## Caveats to confirm on hardware
- **Duty/cooldown**: rapid modes (high `speed`, big `factor`, fast `bpm`) may hit the
  fixture's thermal cooldown — it self-protects, but flashes will be dropped. Find the
  practical ceiling on real heads.
- **Low-level rendering**: `comet`/`wave` graded levels assume the head shows
  distinct low intensities; the 16 steps may not be perceptually even.
- **Audio modifiers** (`modulate`, beat `advance`) are stubbed (`/df/audio` sets a
  test level) until an audio-in or beat source is wired.
