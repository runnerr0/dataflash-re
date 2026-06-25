// patterns.h — strobe pattern engine, modeled on the original Dataflash.
//
// Design note: the Dataflash is a STROBE, not an LED array. Patterns are about
// FLASH timing + which heads fire + per-flash intensity (16 levels), NOT smooth
// dimming. The original controller's model is a PROGRAM = sequence of STAGES,
// advanced by Auto(rate)/Audio(beat), modified by Multiply(repeat), Random
// (shuffle), Modulate(audio-amplitude intensity). We mirror that: a stage
// sequencer core + per-mode stage generators + those modifiers.
// Tiers below: NATIVE (device-faithful) / FRAMEWORK (idiomatic) / ALTERNATE.
#pragma once
#include <Arduino.h>

enum DfPattern : uint8_t {
  PAT_LIVE = 0,     // network passthrough (Art-Net/sACN) — handled in main
  // --- NATIVE (faithful to the original Dataflash) ---
  PAT_ALL,          // all heads fire at intensity
  PAT_SINGLE,       // one head (addressing / bench validation)
  PAT_SEQ,          // play the stored Program (the grid) = device "Stages"
  // --- FRAMEWORK (idiomatic strobe behaviors, sequencer-generated) ---
  PAT_CHASE,        // running flash
  PAT_ALTERNATE,    // odd/even banks flip
  PAT_BUILD,        // cumulative add a head per stage, then clear
  PAT_STROBE,       // all flash on/off at rate/bpm
  PAT_SPARKLE,      // random heads flash each stage (density)
  // --- ALTERNATE / experimental ---
  PAT_WAVE,         // phase-offset flash sweeping the array
  PAT_PINGPONG,     // bouncing chase
  PAT_COMET,        // moving flash with a short (1-2 level) tail
  PAT_COUNT
};

struct PatternState {
  volatile DfPattern pattern   = PAT_LIVE;
  volatile float     speed     = 0.5f;   // 0..1 -> Auto advance rate
  volatile float     density   = 0.5f;   // 0..1 -> sparkle prob / chase+comet width
  volatile float     intensity = 1.0f;   // 0..1 -> per-flash brightness (master)
  volatile float     bpm       = 120.0f; // beat advance / strobe rate
  volatile uint16_t  single    = 0;      // head for PAT_SINGLE

  // device "Effect" modifiers (apply across the sequencer modes)
  volatile bool      advanceBeat = false; // false=Auto(rate), true=Audio/beat(bpm)
  volatile uint8_t   factor      = 1;      // Multiply: repeat each stage N times (1..8)
  volatile bool      randomOrder = false;  // Random: shuffle stage order
  volatile bool      modulate    = false;  // intensity tracks audioLevel
  volatile float     audioLevel  = 1.0f;   // 0..1 (stub until audio-in wired)

  // transport
  volatile bool      blackout = false;
  volatile bool      flash    = false;     // momentary all-at-max bump

  // stored Program (the grid builder): stages x up to 256 heads, 0..15
  static const uint8_t MAX_STEPS = 32;
  uint8_t  grid[MAX_STEPS][256] = {{0}};
  volatile uint8_t gridSteps = 8;

  const char* name() const {
    static const char* n[PAT_COUNT] = {
      "live","all","single","seq","chase","alternate","build",
      "strobe","sparkle","wave","pingpong","comet"};
    return n[pattern < PAT_COUNT ? pattern : 0];
  }
};

extern PatternState g_pat;

// Fill out[0..count-1] with 4-bit levels (0..15). Not called for PAT_LIVE.
void patterns_render(uint8_t* out, uint16_t count, uint32_t nowMs);
