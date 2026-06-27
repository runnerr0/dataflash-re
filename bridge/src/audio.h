// audio.h — audio input subsystem (line-in + mic, I2S, software-selectable).
//
// Two I2S sources on the ESP32-S3's two I2S controllers, picked at runtime:
//   MIC     -> I2S_NUM_1  (INMP441 / SPH0645 MEMS, 24-bit in 32-bit slots, no MCLK)
//   LINE-IN -> I2S_NUM_0  (PCM/I2S ADC, e.g. PCM1808 — may need MCLK; see audio.cpp)
//
// A DSP task (core 0) reads the selected source, isolates a frequency BAND with a
// biquad band-pass, and drives the strobe engine from that band's envelope + onsets
// — "straightforward but effective" audio→strobe mapping:
//   LEVEL   : band amplitude -> g_pat.audioLevel (intensity / Modulate)
//   PULSE   : band onset -> momentary all-heads flash (kick = strobe pop)
//   ADVANCE : band onset -> advance one stage (beat-synced patterns; g_pat.beatTicks++)
// The BAND selector (Full / Bass / Mid / Treble) is the "just the bass" / "just the
// highs" control. Onsets on the chosen band also feed g_aud.beats (beat counting).
//
// Whole module compiles to no-ops unless DF_AUDIO is defined (env esp32-s3-audio).
#pragma once
#include <Arduino.h>

enum AudioSource : uint8_t { AUD_MIC = 0, AUD_LINEIN = 1 };
enum AudioMode   : uint8_t { AUD_OFF = 0, AUD_LEVEL = 1, AUD_PULSE = 2, AUD_ADVANCE = 3 };
enum AudioBand   : uint8_t { BAND_FULL = 0, BAND_BASS = 1, BAND_MID = 2, BAND_TREBLE = 3 };

struct AudioState {
  volatile bool        enabled = false;
  volatile AudioSource source  = AUD_MIC;
  volatile AudioMode   mode    = AUD_LEVEL;
  volatile AudioBand   band    = BAND_FULL;
  volatile float       gain    = 6.0f;    // pre-normalize input gain
  volatile float       gate    = 0.02f;   // noise floor (0..1); below this = silence
  volatile uint16_t    pulseMs = 40;      // PULSE flash duration per onset
  // live readouts (for web UI / OSC feedback)
  volatile float       level   = 0.0f;    // smoothed band envelope 0..1 (-> g_pat.audioLevel)
  volatile float       raw     = 0.0f;    // instantaneous band RMS 0..1 (meter)
  volatile uint32_t    beats   = 0;       // onset counter on the selected band (monotonic)
  volatile bool        present = false;   // signal above gate
};
extern AudioState g_aud;

void audio_begin();                 // init I2S for the configured source + start DSP task
void audio_select(AudioSource s);   // switch active input (re-inits I2S)
