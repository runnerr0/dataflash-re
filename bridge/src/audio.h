// audio.h — audio input subsystem (line-in + mic, I2S, software-selectable).
//
// Two I2S sources on the ESP32-S3's two I2S controllers, picked at runtime:
//   MIC     -> I2S_NUM_1  (INMP441 / SPH0645 MEMS, 24-bit in 32-bit slots, no MCLK)
//   LINE-IN -> I2S_NUM_0  (PCM/I2S ADC, e.g. PCM1808 — may need MCLK; see audio.cpp)
//
// A DSP task (core 0) reads the selected source, computes a smoothed amplitude
// envelope and onset/beat events, and drives the pattern engine — mirroring the
// original Dataflash controller's audio behaviors:
//   MODULATE : envelope amplitude -> g_pat.audioLevel (intensity)         [OEM Modulate]
//   BEAT1    : each onset advances one stage  (g_pat.beatTicks++)         [OEM Audio 1]
//   BEAT2    : onset halts/holds advance                                  [OEM Audio 2] (TODO)
//
// Whole module compiles to no-ops unless DF_AUDIO is defined (see platformio env
// esp32-s3-audio), so existing builds are untouched.
#pragma once
#include <Arduino.h>

enum AudioSource : uint8_t { AUD_MIC = 0, AUD_LINEIN = 1 };
enum AudioMode   : uint8_t { AUD_OFF = 0, AUD_MODULATE = 1, AUD_BEAT1 = 2, AUD_BEAT2 = 3 };

struct AudioState {
  volatile bool        enabled = false;
  volatile AudioSource source  = AUD_MIC;
  volatile AudioMode   mode    = AUD_OFF;
  volatile float       gain    = 6.0f;    // pre-normalize input gain
  volatile float       gate    = 0.02f;   // noise floor (0..1); below this = silence
  // live readouts (for web UI / OSC feedback)
  volatile float       level   = 0.0f;    // smoothed envelope 0..1 (-> g_pat.audioLevel)
  volatile float       raw     = 0.0f;    // instantaneous block RMS 0..1 (meter)
  volatile uint32_t    beats   = 0;       // onset counter (monotonic)
  volatile bool        present = false;   // signal above gate
};
extern AudioState g_aud;

void audio_begin();                 // init I2S for the configured source + start DSP task
void audio_select(AudioSource s);   // switch active input (re-inits I2S)
