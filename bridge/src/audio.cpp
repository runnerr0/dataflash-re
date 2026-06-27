// audio.cpp — I2S audio input + band DSP, drives the strobe engine (see audio.h).
#include "audio.h"
#include "patterns.h"
#include "config.h"
#include <math.h>

AudioState g_aud;

#ifdef DF_AUDIO
#include "driver/i2s.h"          // legacy I2S driver (Arduino-ESP32 2.0.x / IDF 4.4)

// ---- I2S pin map (override in platformio env; VERIFY against your S3-ETH header
//      and the W5500 SPI pins before wiring — these are placeholders) ----
#ifndef DF_AUD_MIC_BCK
#define DF_AUD_MIC_BCK   8
#endif
#ifndef DF_AUD_MIC_WS
#define DF_AUD_MIC_WS    9
#endif
#ifndef DF_AUD_MIC_SD
#define DF_AUD_MIC_SD   10
#endif
#ifndef DF_AUD_LINE_BCK
#define DF_AUD_LINE_BCK 11
#endif
#ifndef DF_AUD_LINE_WS
#define DF_AUD_LINE_WS  12
#endif
#ifndef DF_AUD_LINE_SD
#define DF_AUD_LINE_SD  13
#endif

static const int   SR    = 16000;     // sample rate (plenty for envelope + onset)
static const int   BLK   = 256;       // samples per DSP block (~16 ms @ 16 kHz)
static const float ATTACK = 0.55f;    // envelope rise (fast)
static const float DECAY  = 0.06f;    // envelope fall (slow) -> musical "bounce"

static i2s_port_t s_port = I2S_NUM_1;

// ---- one biquad band-pass; reconfigured when the band selector changes ----
struct Biquad { float b0,b1,b2,a1,a2, x1,x2,y1,y2; };
static Biquad s_bq{};
static AudioBand s_band = BAND_FULL;

static void bandpass(Biquad& q, float f0, float Q, float fs) {
  float w0 = 2.0f * (float)M_PI * f0 / fs;
  float cw = cosf(w0), sw = sinf(w0), alpha = sw / (2.0f * Q);
  float a0 = 1.0f + alpha;
  q.b0 = alpha / a0;  q.b1 = 0.0f;  q.b2 = -alpha / a0;     // 0 dB peak band-pass
  q.a1 = (-2.0f * cw) / a0;  q.a2 = (1.0f - alpha) / a0;
  q.x1 = q.x2 = q.y1 = q.y2 = 0.0f;
}
static inline float biq(Biquad& q, float x) {
  float y = q.b0*x + q.b1*q.x1 + q.b2*q.x2 - q.a1*q.y1 - q.a2*q.y2;
  q.x2 = q.x1; q.x1 = x; q.y2 = q.y1; q.y1 = y;
  return y;
}
static void configBand(AudioBand b) {
  switch (b) {                                              // center, Q
    case BAND_BASS:   bandpass(s_bq,   90.0f, 1.0f, SR); break;   // kick / sub
    case BAND_MID:    bandpass(s_bq,  800.0f, 0.7f, SR); break;   // body / vocals
    case BAND_TREBLE: bandpass(s_bq, 5000.0f, 0.9f, SR); break;   // hats / cymbals
    default: break;                                          // FULL = no filter
  }
  s_band = b;
}

static i2s_port_t portFor(AudioSource s) { return s == AUD_LINEIN ? I2S_NUM_0 : I2S_NUM_1; }

static void installI2S(AudioSource src) {
  i2s_port_t port = portFor(src);
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = SR;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;       // 24-bit MEMS data sits in 32-bit slots
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;        // mono (mic L/R pin -> GND = left)
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = BLK;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = false;
  cfg.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;                 // INMP441 needs no MCLK
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  if (src == AUD_LINEIN) {
    pins.bck_io_num = DF_AUD_LINE_BCK; pins.ws_io_num = DF_AUD_LINE_WS; pins.data_in_num = DF_AUD_LINE_SD;
    // NOTE: a PCM1808-class ADC needs an MCLK (SCKI). If your line-in board requires
    // it, route MCLK (set cfg.use_apll=true, fixed_mclk=256*SR) and pins.mck_io_num.
  } else {
    pins.bck_io_num = DF_AUD_MIC_BCK;  pins.ws_io_num = DF_AUD_MIC_WS;  pins.data_in_num = DF_AUD_MIC_SD;
  }
  i2s_driver_install(port, &cfg, 0, nullptr);
  i2s_set_pin(port, &pins);
  i2s_zero_dma_buffer(port);
  s_port = port;
}

static void dspTask(void*) {
  static int32_t buf[BLK];
  float avg = 0.0f, prev = 0.0f;     // onset: slow energy average + previous block
  uint32_t lastBeatMs = 0, flashUntil = 0;
  configBand((AudioBand)g_aud.band);
  for (;;) {
    size_t nread = 0;
    if (i2s_read(s_port, buf, sizeof(buf), &nread, portMAX_DELAY) != ESP_OK || nread == 0) {
      vTaskDelay(1); continue;
    }
    int n = (int)(nread / sizeof(int32_t));
    if ((AudioBand)g_aud.band != s_band) configBand((AudioBand)g_aud.band);  // band switched

    // block mean (DC) on the raw 24-bit signal
    double mean = 0;
    for (int i = 0; i < n; i++) mean += (double)(buf[i] >> 8);
    mean /= n;

    // band energy: biquad-filtered (BASS/MID/TREBLE) or DC-removed broadband (FULL)
    double e = 0;
    if (s_band == BAND_FULL) {
      for (int i = 0; i < n; i++) { double d = (double)(buf[i] >> 8) - mean; e += d * d; }
    } else {
      for (int i = 0; i < n; i++) { float y = biq(s_bq, (float)((buf[i] >> 8) - mean)); e += (double)y * y; }
    }
    float rms = (float)sqrt(e / n) / 8388608.0f;          // normalize by 2^23
    float r = rms * g_aud.gain;
    if (r > 1.0f) r = 1.0f;
    g_aud.raw = r;

    bool present = r > g_aud.gate;
    g_aud.present = present;

    // attack/decay envelope
    float lv = g_aud.level;
    float target = present ? r : 0.0f;
    lv += (target - lv) * (target > lv ? ATTACK : DECAY);
    if (lv < 0) lv = 0;
    g_aud.level = lv;

    // onset on the selected band: energy spikes above the running average (refractory)
    uint32_t now = millis();
    bool onset = present && r > avg * 1.5f && (r - prev) > 0.02f && (now - lastBeatMs) > 110;
    if (onset) { g_aud.beats++; lastBeatMs = now; }
    avg += (r - avg) * 0.05f;
    prev = r;

    // ---- map to the strobe engine ----
    if (!g_aud.enabled) {
      g_pat.modulate = false; g_pat.useBeatTicks = false;
    } else switch (g_aud.mode) {
      case AUD_LEVEL:                                       // band amplitude -> intensity
        g_pat.audioLevel = g_aud.level; g_pat.modulate = true;
        g_pat.useBeatTicks = false; break;
      case AUD_PULSE:                                       // onset -> momentary all-flash
        if (onset) flashUntil = now + g_aud.pulseMs;
        g_pat.flash = (now < flashUntil);
        g_pat.modulate = false; g_pat.useBeatTicks = false; break;
      case AUD_ADVANCE:                                     // onset -> advance a stage
        if (onset) g_pat.beatTicks++;
        g_pat.useBeatTicks = true; g_pat.modulate = false; break;
      default:                                              // AUD_OFF
        g_pat.modulate = false; g_pat.useBeatTicks = false; break;
    }
  }
}

void audio_begin() {
  g_aud.enabled = g_cfg.audioEnable;
  g_aud.source  = (AudioSource)g_cfg.audioSource;
  g_aud.mode    = (AudioMode)g_cfg.audioMode;
  g_aud.band    = (AudioBand)g_cfg.audioBand;
  g_aud.gain    = g_cfg.audioGain / 16.0f;                // stored as uint8 *16
  installI2S(g_aud.source);
  xTaskCreatePinnedToCore(dspTask, "df_audio", 4096, nullptr, configMAX_PRIORITIES - 3, nullptr, 0);
  Serial.printf("[audio] I2S %s port %d  mode=%u band=%u gain=%.1f\n",
                g_aud.source == AUD_LINEIN ? "LINE-IN" : "MIC", (int)s_port,
                g_aud.mode, g_aud.band, g_aud.gain);
}

void audio_select(AudioSource s) {
  if (s == g_aud.source) return;
  i2s_driver_uninstall(s_port);
  g_aud.source = s;
  installI2S(s);
  Serial.printf("[audio] switched to %s (port %d)\n", s == AUD_LINEIN ? "LINE-IN" : "MIC", (int)s_port);
}

#else  // !DF_AUDIO — stubs so the module is a no-op in non-audio builds
void audio_begin() {}
void audio_select(AudioSource) {}
#endif
