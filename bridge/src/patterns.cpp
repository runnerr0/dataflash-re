// patterns.cpp — stage-sequencer strobe engine (see patterns.h design note).
#include "patterns.h"
#include <math.h>

PatternState g_pat;

// "on" level for this frame = master intensity (and audio modulate), 0..15
static inline uint8_t hiLevel() {
  float m = g_pat.intensity;
  if (g_pat.modulate) m *= g_pat.audioLevel;
  if (m < 0) m = 0; if (m > 1) m = 1;
  return (uint8_t)lroundf(m * 15.0f);
}

// current stage index, applying Auto/Beat advance + Multiply(repeat) + Random(shuffle)
static uint32_t advanceStage(uint32_t nowMs, uint16_t stageCount) {
  if (stageCount == 0) return 0;
  uint8_t f = g_pat.factor < 1 ? 1 : g_pat.factor;
  uint32_t raw;
  if (g_pat.useBeatTicks) {                                   // OEM Audio 1: one onset = one tick
    raw = g_pat.beatTicks;
  } else {
    float sps = g_pat.advanceBeat ? (g_pat.bpm / 60.0f)       // fixed beat (bpm)
                                  : (0.5f + g_pat.speed * 12.0f); // Auto rate
    raw = (uint32_t)((nowMs / 1000.0) * sps);
  }
  uint32_t st = raw / f;                                      // Multiply: hold N ticks
  if (g_pat.randomOrder) st = st * 2654435761u >> 8;          // Random: shuffle order
  return st % stageCount;
}

static inline bool prob(float p) { return (esp_random() & 0xFFFF) < (uint32_t)(p * 65535.0f); }

void patterns_render(uint8_t* out, uint16_t count, uint32_t nowMs) {
  if (!count) return;
  uint8_t hi = hiLevel();

  // transport overrides
  if (g_pat.blackout) { memset(out, 0, count); return; }
  if (g_pat.flash)    { memset(out, 15, count); return; }   // bump = all at max

  switch (g_pat.pattern) {
    case PAT_ALL:
      memset(out, hi, count); break;

    case PAT_SINGLE:
      memset(out, 0, count);
      if (g_pat.single < count) out[g_pat.single] = hi;
      break;

    case PAT_SEQ: {                                  // play stored Program (grid)
      uint8_t steps = g_pat.gridSteps ? g_pat.gridSteps : 1;
      uint32_t s = advanceStage(nowMs, steps);
      for (uint16_t i = 0; i < count; i++) {
        uint8_t lvl = g_pat.grid[s][i];              // 0..15 stored
        out[i] = (uint8_t)((lvl * hi) / 15);         // scale by master
      }
      break;
    }

    case PAT_CHASE: {
      memset(out, 0, count);
      uint32_t head = advanceStage(nowMs, count);
      uint16_t w = 1 + (uint16_t)(g_pat.density * 6.0f);     // 1..7 wide
      for (uint16_t k = 0; k < w; k++)
        out[(head + count - k) % count] = hi;               // hard flashes (no fade tail)
      break;
    }

    case PAT_ALTERNATE: {
      uint32_t s = advanceStage(nowMs, 2);
      for (uint16_t i = 0; i < count; i++) out[i] = ((i & 1) == s) ? hi : 0;
      break;
    }

    case PAT_BUILD: {
      uint32_t s = advanceStage(nowMs, count + 1);            // 0..count (count = all, wraps)
      for (uint16_t i = 0; i < count; i++) out[i] = (i < s) ? hi : 0;
      break;
    }

    case PAT_STROBE: {
      uint32_t s = advanceStage(nowMs, 2);                    // on/off
      memset(out, s == 0 ? hi : 0, count);
      break;
    }

    case PAT_SPARKLE: {
      static uint32_t lastStage = 0xFFFFFFFF;
      uint32_t s = advanceStage(nowMs, 0xFFFF);               // re-roll each advance tick
      if (s != lastStage) {                                   // only re-randomize on advance
        lastStage = s;
        for (uint16_t i = 0; i < count; i++) out[i] = prob(g_pat.density) ? hi : 0;
      }
      break;
    }

    case PAT_WAVE: {                                          // moving flash, soft window
      uint32_t head = advanceStage(nowMs, count);
      uint16_t w = 1 + (uint16_t)(g_pat.density * (count / 2));
      for (uint16_t i = 0; i < count; i++) {
        uint16_t d = (i + count - head) % count; if (d > count - d) d = count - d;
        out[i] = (d <= w) ? (uint8_t)((hi * (w - d)) / w) : 0; // brief graded flash
      }
      break;
    }

    case PAT_PINGPONG: {
      uint32_t s = advanceStage(nowMs, count > 1 ? 2 * count - 2 : 1);
      uint16_t pos = (s < count) ? s : (2 * count - 2 - s);
      memset(out, 0, count); out[pos] = hi;
      break;
    }

    case PAT_COMET: {                                         // head + short tail (uses low levels)
      memset(out, 0, count);
      uint32_t head = advanceStage(nowMs, count);
      uint16_t tail = 1 + (uint16_t)(g_pat.density * 4.0f);
      for (uint16_t k = 0; k <= tail; k++) {
        uint8_t lvl = (uint8_t)((hi * (tail + 1 - k)) / (tail + 1));
        out[(head + count - k) % count] = lvl;
      }
      break;
    }

    default: memset(out, 0, count);
  }
}
