# Firmware analysis 05 — Audio input, Effects, and Memory (controller features)

Goal: document what the **OEM controller's** audio input, effects section, and memory
section actually DO — functionally — so the ESP32 bridge can emulate them, and confirm
how much (if any) of that logic lives in the **fixture** firmware we have.

Sources & tags: **[fixture-fw]** disassembly of `df282_extract/Df31f2.82` (Rev 2.82, the
strobe-head receiver) · **[manual]** `assets/manuals/dataflash-original-user-manual.pdf`
(Lightwave Research, Rev 1 / Aug '89, by Anthony S. Monday) · **[patent]** US 5,078,039
(Tulk & Belliveau, public domain) · **[web]** external research · **[inferred]** derived.

---

## TL;DR

- **All audio / effects / memory logic lives in the CONTROLLER, not the fixture.** [fixture-fw]
  The fixture firmware is a pure receiver: it takes a per-frame intensity value over the
  9-bit serial link and fires the xenon tube, with local thermal/zero-cross protection.
  Its embedded copyright string literally reads *"Data Flash strobe head. Author - Steve
  Tulk."* There is **no** audio, beat, random, modulate, memory, or program-sequencing
  code in it — and there cannot be (the 8031 has no ADC; INT0/INT1 are disabled).
- To emulate these features we depend on **[manual]** + **[patent]** for behavior and on a
  **live controller capture** for the exact knob curves, timing constants, and audio
  thresholds — none of which are documented numerically.
- The bridge already exposes a parameter for every one of these effects
  (`bridge/src/patterns.h`), so emulation is mostly a matter of wiring an audio source and
  matching the original's *feel* (ranges/curves) to a real-controller capture.

---

## Part 1 — What the FIXTURE firmware proves (receiver-only)  [fixture-fw]

Scanned the full 16 KB image for audio / beat / random / ADC / analog-comparator / effect
/ memory logic. **None present.** Evidence:

### 1.1 Interrupt vector table (0x0000–0x0023)
| Vector | Source | Firmware does |
|---|---|---|
| 0x0003 | External INT0 | `CLR EX0; RETI` — **disabled** (no external trigger input used) |
| 0x000B | Timer0 | `LJMP 0x0440` — **active**: flash-fire timer |
| 0x0013 | External INT1 | `CLR EX1; RETI` — **disabled** |
| 0x001B | Timer1 | `LJMP 0x0481` — **active**: housekeeping/cooldown timer |
| 0x0023 | Serial | `CLR ES; RETI` — **disabled**; serial is **polled** (`JNB RI` loops) |

Both external interrupts are deliberately shut off. There is no input-capture, no
comparator path, no beat/zero-cross *input* interrupt — the only inputs are the polled
serial byte and the DIP switches.

### 1.2 The only "real" input is the DIP address bus
`MOV A,Pn` (port-read) appears **4 times total, all on P1** — the 8-position DIP address
switch, read in the START handler and at init/diag. No P0/P2/P3 byte reads, no external
ADC reads. So the fixture senses nothing analog; it just receives data and reads its
address. [fixture-fw]

### 1.3 The two active timers are the flashtube firing engine, not effects
- **Timer0 ISR (0x0440):** asserts `P3.3 (#ARM)`, drives `P1=0x00`, pulses **`P3.4` = FIRE**
  (`SETB/CLR P3.4` with a `JB P3.4` wait), clears `TCON.4`, decrements the triple
  countdown regs `0x4C/4D/4E` and `0x39/3A/3B`, then `LJMP 0x051D` (intensity output).
  This is the **zero-cross-synced per-flash trigger** already documented in `03`/`04`.
- **Timer1 ISR (0x0481):** increments housekeeping counters `0x36/37/38`, calls `0x08F5`,
  re-runs `0x051D`; on the `0xFF` terminal it blanks `P3.7 (#LED)` and clears a large block
  of intensity/cooldown state. This is the **cooldown/heartbeat-paced** housekeeping path.

Neither timer reads an external signal; both are internal time bases for firing and thermal
protection. They are the timers already accounted for in `03`/`04`, not "unexplained."

### 1.4 The `0x12` SPECIAL path (handler 0x0BC6 → 0x051D) is a forced-fire DIAGNOSTIC
Re-traced it fully:
- `0x0BC6`: a strict 4-byte handshake — reads four serial bytes, **each gated on the 9th
  bit = control** (`JNB RB8`), and requires exactly `0x95, 0x88, 0x25, 0xA8` (via `SUBB`+
  `JNZ`). Any mismatch or a data-bit byte aborts (`RET`). [fixture-fw]
- On full match: `LJMP 0x051D`. That target is the **intensity-output / fire routine**:
  `CLR EA`, then it reads the `P3.6` mode-select pin to choose a gamma table
  (`0x013B` curve A vs `0x014E` curve B), masks a nibble, calls the cooldown clamp
  (`0x07D1`), writes the gamma'd value to **`P1`**, and pulses **`P3.4` = FIRE** in a
  `SETB P3.4 … JB P3.4` loop — i.e. it makes the tube flash. It then reloads the
  `0x4C/4D/4E = 0x10` countdowns and returns. [fixture-fw]
- **Conclusion:** `0x12 + 95 88 25 A8` is a **password-gated forced-flash / self-test /
  factory-diagnostic** command, *not* an audio/effects/memory feature. It is consistent
  with the manual's "Illuminator Self-Diagnostics" section (a ~15 s self-test of varying
  flash rates/intensities) [manual], though that test is normally triggered by *absence* of
  data, not by this command. Either way it is fixture-local and receiver-side. [inferred]

### 1.5 Bottom line for Part 1
The fixture is exactly what the spec says: receive intensity → gamma → zero-cross-synced
fire → thermal cooldown. **Audio, Random, Multiply, Modulate, Memory, program sequencing,
and beat sync are entirely controller-side and absent from this image.** Confirmed, not
force-fit.

---

## Part 2 — The CONTROLLER's features (from the manual)  [manual]

The original controller is a 3U rack unit (Lightwave Research, part #60600004). Front panel
groups: **Advance**, **Effect**, **Program**, **Memory**, plus global Standby/Flash/Power.
Rear panel has the feature I/O. Functional behavior below.

### 2.1 Audio input  [manual]
**Hardware:** rear-panel **AUDIO INPUT** = two **RCA phono jacks, line-level stereo** (#30).
A front-panel **AUDIO knob** (#7) sets sensitivity, with an **LED above it** that lights to
indicate the presence and level of program audio (use it to gain-stage to the music's peaks).

Audio drives **three** distinct behaviors (selected elsewhere on the panel), all sharing the
one AUDIO sensitivity knob:

1. **Audio 1 (Advance section, #5):** "advances a Program's steps with the rhythm of the
   music." A sensed audio pulse **accelerates** step advance. The **Rate/Auto knob** sets
   *how many steps* advance per detected pulse. [manual]
2. **Audio 2 (Advance section, #6):** same sensing, but instead **halts** step advance on a
   pulse (rhythmic freeze rather than rhythmic push). Audio + Auto trim work as in Audio 1. [manual]
3. **Modulate (Effect section, #11):** audio amplitude **modulates head intensity** — see 2.2.

Only one Advance method is active at a time (Auto / Audio 1 / Audio 2 — exactly one LED lit,
Auto is the default). [manual] When an analog/DIN signal is detected the **Preview display
shows "AC"** (analog control). [manual]

**Patent corroboration [patent]:** describes an "audio filter control" mode where "intensity
control is no longer provided by the preprogrammed memory, but is instead provided by a
built-in random generator responsive to the filtered frequencies" — i.e. a filtered-audio →
intensity path, the patent's account of Modulate/audio-reactivity.

### 2.2 Effects section — three combinable modifiers  [manual]
"All, one, or none" of these may be active and they modify a Program's steps as it runs:

- **Modulate (#11):** illuminator intensity varies with the **amplitude** of the audio
  signal. A sensed audio level is translated to an intensity **no greater than the Program's
  max** (and further capped by the Intensity knob). Sensitivity = the AUDIO knob. So:
  louder music → brighter flashes, clamped to the program/master ceiling. [manual]
- **Random (#12 / Effect):** when Send is pressed (or an analog input fires) with Random
  enabled, the previewed Program **starts at a random step** instead of its normal
  beginning, then continues sequentially. Re-triggering re-randomizes the entry point, so
  the same Program "looks" different each time. [manual] (Note: Random in the original is a
  **random START offset**, not a per-step shuffle. The bridge currently models it as shuffle
  — see Part 5.) [manual]/[inferred]
- **Multiply (#13) + Factor knob (#14):** "echoes"/repeats **each step** N times before
  advancing, where N = Factor knob (e.g. Factor 4 → step pattern `1,1,1,1, 2,2,2,2,
  3,3,3,3 …`). A burst/ratchet effect. [manual]

**Other transport/global controls that act like effects [manual]:**
- **Auto (#8) + Rate knob (#9):** free-running step advance at a set speed (the non-audio
  Advance mode).
- **Flash key (#3):** momentary — all illuminators flash at **max intensity regardless of
  status**, once or repeatedly while held.
- **Standby (#2):** disables all output (blackout) regardless of state.
- **Intensity knob (#20):** master maximum brightness; **Intensity Limit switch** (rear #26)
  is an installer-set overall power/brightness cap.
- **Send-and-hold special:** holding Send returns the Program to its start step and
  **"freezes" it flashing at 10 fps**; release resumes. Works during memory playback too;
  **not storable**. [manual]

### 2.3 Memory section — what it stores & recalls  [manual]
**Four user memories** (keys M1–M4, #22), each a **sequence of up to 99 "Steps"**, where
each **Step = one Program "Send"** captured **with all its front-panel settings at the time
of the Send** (Advance mode, the three Effects, and Intensity). So a Memory is a *show* — an
ordered, looping playlist of Programs-with-modifiers, not a scene of raw head levels. [manual]

- **What a Memory stores:** an ordered list (1–99) of `{Program #, Advance mode, Effect
  flags, Intensity}` tuples. It **auto-loops** at the end regardless of length. [manual]
- **What it does NOT store:** raw per-head intensity scenes. The *Programs* are the
  pre-installed patterns (up to 99 of them, plus "Special Function" programs `F1…`); Memory
  just chains and modifies them. [manual] An intensity stored low becomes the **playback
  ceiling** (you can turn down but not up past the stored level). [manual]
- **Playback:** press a Memory key; Standby/Intensity/Flash/**Progress knob** (#24) adjust
  live. **Progress knob repeats each step N times** during playback (the playback-time
  analogue of Multiply). Send-and-hold freezes on a step; Standby halts at the current step.
  **Analog/DIN inputs have priority during playback** and can jump to / hold an individual
  step (held as long as the input is active, then sequencing resumes). [manual]
- **Storage (record):** Enable key → Memory key (Running display shows `E1…E4`); pick a
  Program with up/down, set Advance/Effect/Intensity, **Send** to lay down a step (steps
  must be entered **sequentially**). **Enabling a memory for record erases its prior
  contents on the first Send.** Up to 99 steps; full → both displays go dark. [manual]
- **Advanced storage / editing via analog (DIN):** an analog control device can directly
  address a particular **Step number** for non-destructive edits (correct one step without
  re-recording the whole memory). Editing is the "most useful" analog function. [manual]
- **Memory Lockout (security):** hold **Standby while turning the keyswitch on**, then toggle
  M1–M4 to lock/unlock each memory (LED lit = unlocked/programmable, off = locked). A locked
  memory played and edited shows **"L.C."** — changes are temporary, not recordable. [manual]

**Patent corroboration [patent]:** "one of four memories … selected by switches"; each memory
is "a group of pages wherein each page provides a scene and contains stored information
concerning lamp identification addresses and intensities," advanced/reversed by "two sequence
control switches," with an "advance switch" for manual page advance and a "send switch" to
transmit. (The patent's "page = scene of addresses+intensities" framing is lower-level than
the shipped manual's "step = Program Send"; the manual reflects the production firmware.)

### 2.4 Other relevant controller I/O  [manual]
- **Remote Analog Control (#28):** two locking **8-pin DIN** connectors, **12 channels of
  0 V→+10–16 V**, for a touch panel / rock desk / lighting board to call Programs and Memory
  Steps live. A keypad-style encoding (tens/doubler/reverse keys) maps 12 channels → values
  up to 99. Standby state changes momentary-vs-latched behavior. [manual]
- **Remote Enable (#29):** 3.5 mm jack; +5–16 V forces Standby (remote blackout). [manual]
- **Serial Port (#27):** DB-style "communications port" for external control systems
  (undocumented protocol). [manual]
- **Data Link Out (#25):** the digitally-encoded serial feed to the fixtures (the link this
  project decodes).
- **Fixture flash limits [manual]:** max 17 fps, min 0.5 fps, 16 intensity levels, ~2.5 min
  cooldown cycles, self-resetting thermal breaker — these bound how aggressive any emulated
  fast/Multiply/beat mode can be.

---

## Part 3 — Patent 5,078,039 summary (operating modes / audio / effects / memory)  [patent]

Public-domain, same author as the fixture firmware. It is the semantic spec; key points
relevant here (short quotes only):

- **Modes:** "preprogrammed memory operation," a "stand-by switch" to enable/disable output,
  and a "modulate switch" that "activates an alternative control mode."
- **Audio:** an "audio filter control" path; in modulate mode "intensity control … is
  instead provided by a built-in random generator responsive to the filtered frequencies"
  (filtered-audio-driven intensity/randomization).
- **Effects / intensity:** "one of 16 possible intensity levels"; a "built-in random
  generator"; manual override via "a manual intensity control knob."
- **Memory:** "one of four memories … selected by switches," each = "a group of pages …
  each page provides a scene … lamp identification addresses and intensities."
- **Sequencing:** "two sequence control switches" (top advances, bottom reverses), an
  "advance switch" for manual page advance, and a "send switch" to transmit control data.

This agrees with the manual at the behavioral level and with the firmware at the wire level
(16 levels, 4 memories, addressed fixtures, send/advance transport).

---

## Part 4 — Web research  [web]

The manual + patent are thorough, so web research was confirmatory only. Public community
knowledge (forums, archived product pages) consistently describes the original Dataflash as:
a 1989 Lightwave Research xenon-strobe system, up-to-256 addressable 1000 W illuminators on a
proprietary serial link, controller with 99 programs, 4 memories, audio advance + audio
modulation, and DIN analog remote — matching the manual. No published byte-level audio/effect
timing constants exist; this project's captures remain the only empirical source for the wire
behavior. **No new functional detail beyond the manual/patent was found.** [web]

---

## Part 5 — Emulation plan for the ESP32 bridge

The bridge already mirrors the controller's model: a **stage sequencer = a Program**, with
the controller's controls promoted to parameters (`bridge/src/patterns.h` `PatternState`).
Mapping each OEM feature to the bridge:

| OEM feature [manual] | Behavior | Bridge param / status | To CAPTURE empirically |
|---|---|---|---|
| **Auto + Rate** | free-run step advance at a rate | `speed` (+ `advanceBeat=false`) — **done** | Rate knob min/max in steps/s; is the curve linear? |
| **Audio 1** (advance) | audio pulse *accelerates* advance; Rate = steps/pulse | `advanceBeat=true`, `bpm`/tap — **partial** (needs audio in) | onset-detect threshold; "steps per pulse" mapping vs AUDIO knob |
| **Audio 2** (advance) | audio pulse *halts* advance | **not modeled** — add an "audio-halt" advance mode | same audio front-end; halt duration per pulse |
| **Modulate** (effect) | head intensity ∝ audio amplitude, clamped to program/master max | `modulate` + `audioLevel` — **stub** (needs audio in) | amplitude→intensity curve; attack/decay; AUDIO-knob sensitivity law |
| **Random** (effect) | start Program at a **random step**, then sequential | `randomOrder` — **mismatch**: bridge shuffles order; OEM randomizes *entry point*. Add a `randomStart` mode to match exactly | confirm it's start-offset only (manual says so) |
| **Multiply + Factor** | repeat each step N times (burst) | `factor` (1..8) — **done** | Factor knob range (max N); does it interact with Audio advance? |
| **Flash** | momentary all-at-max | `flash` — **done** | n/a (local) |
| **Standby/Blackout** | kill output | `blackout` — **done** | n/a |
| **Intensity / Limit** | master max / installer cap | `intensity` — **done** (Limit = config cap) | n/a |
| **Send-and-hold freeze** | freeze on step, flash @10 fps | **not modeled** — add a transient "freeze" transport | confirm 10 fps freeze rate |
| **Memory (M1–M4)** | 4 playlists of ≤99 `{Program, Advance, Effect, Intensity}` steps, auto-loop | **not modeled** — add a memory layer above the sequencer: list of program-presets-with-modifiers | n/a (behavior fully documented) |
| **Progress knob** | repeat each memory step N times in playback | maps to per-step `factor` during memory playback | Progress knob range |
| **DIN analog remote / step jump** | external 12-ch analog calls programs/steps; priority + hold | maps to OSC/Art-Net inputs (already a bridge concept) | encoding only matters if reusing real DIN hardware |

### Audio front-end (the one missing hardware piece)
The OEM uses a stereo line-level RCA input + a sensitivity knob + a single envelope/onset
detector feeding both **advance** (pulse/beat) and **modulate** (amplitude). For the bridge,
add a line/mic audio input (ADC or I2S mic) → envelope follower + onset detector → drive
`audioLevel` (for Modulate) and a beat/pulse event (for Audio 1/2 advance). This is the only
feature that needs new hardware; everything else is firmware logic the bridge can already
express. [inferred]

### What still must be captured from the live controller
The documents give **behavior** but no **numbers**. To match the original's *feel* we still
need, from a live-controller capture / bench session:
1. **Rate/Auto knob curve** — steps-per-second min↔max and linearity.
2. **Audio onset threshold & "steps per pulse"** vs the AUDIO sensitivity knob (Audio 1/2).
3. **Modulate amplitude→intensity curve** (and attack/decay) and how the AUDIO knob scales it.
4. **Factor (Multiply) and Progress max N**.
5. **Confirm Random = random start-offset only** (manual says so; verify on the wire).
6. **Send-and-hold freeze rate** (manual says ~10 fps) and the self-diagnostic flash sequence
   (~15 s) for completeness.
7. The **per-fixture byte's intensity-vs-flash-mode nibble split** (already open in `04`) —
   needed before Modulate/Intensity emulation can be ground-truthed against real heads.

None of these constants are recoverable from the fixture firmware (it never sees them); they
are controller-internal and must come from a capture or the controller's own firmware image.
