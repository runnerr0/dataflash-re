// osc.cpp — minimal OSC-over-UDP receiver for TouchOSC control + feedback.
// Schema: see bridge/CONTROL-TOUCHOSC.md
#include "osc.h"
#include "config.h"
#include "patterns.h"
#include "inputs.h"
#include "dataflash_tx.h"
#include "audio.h"
#include <WiFiUdp.h>

#define OSC_PORT     8000     // bridge listens here (TouchOSC "Send" port)
#define OSC_FB_PORT  9000     // bridge sends feedback here (TouchOSC "Receive" port)

static WiFiUDP udp;
static uint8_t rx[1500];
static IPAddress lastSender;
static bool haveSender = false;

// ---- tiny OSC parse helpers ----
static uint32_t be32(const uint8_t* p){ return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]; }
static float    bef(const uint8_t* p){ uint32_t u=be32(p); float f; memcpy(&f,&u,4); return f; }
static int      align4(int n){ return (n + 3) & ~3; }

struct OscMsg {
  const char* addr;
  char  types[8];
  int   ival[8];
  float fval[8];
  int   n;
};

static bool parseOsc(uint8_t* b, int len, OscMsg& m) {
  if (len < 4) return false;
  m.addr = (const char*)b;
  int o = align4(strlen(m.addr) + 1);
  if (o >= len || b[o] != ',') return false;
  const char* tt = (const char*)(b + o + 1);
  int nt = strlen(tt);
  o = align4(o + 1 + nt + 1);
  m.n = 0;
  for (int i = 0; i < nt && m.n < 8; i++) {
    char t = tt[i];
    m.types[m.n] = t;
    if (t == 'i') { m.ival[m.n] = (int32_t)be32(b+o); m.fval[m.n] = m.ival[m.n]; o += 4; }
    else if (t == 'f') { m.fval[m.n] = bef(b+o); m.ival[m.n] = (int)m.fval[m.n]; o += 4; }
    else if (t == 's') { m.ival[m.n]=0; m.fval[m.n]=0; o = align4(o + strlen((char*)(b+o)) + 1); }
    else if (t == 'T') { m.ival[m.n]=1; m.fval[m.n]=1; }
    else if (t == 'F') { m.ival[m.n]=0; m.fval[m.n]=0; }
    m.n++;
  }
  return true;
}

static bool eq(const char* a, const char* b){ return strcmp(a,b)==0; }
static float clamp01(float v){ return v<0?0:(v>1?1:v); }

// ---- tap tempo ----
static uint32_t lastTap = 0;
static void tap() {
  uint32_t now = millis();
  if (lastTap && now - lastTap < 2000) g_pat.bpm = 60000.0f / (now - lastTap);
  lastTap = now;
}

static void dispatch(OscMsg& m) {
  const char* a = m.addr;
  float f = m.n ? m.fval[0] : 0;
  int   i = m.n ? m.ival[0] : 0;

  // transport
  if      (eq(a,"/df/output"))    g_cfg.outputEnable = (i != 0);
  else if (eq(a,"/df/blackout"))  g_pat.blackout = (i != 0);
  else if (eq(a,"/df/flash"))     g_pat.flash = (i != 0);      // momentary bump (T on press, F on release)
  // pattern + core params
  else if (eq(a,"/df/pattern"))   g_pat.pattern = (DfPattern)constrain(i, 0, PAT_COUNT-1);
  else if (eq(a,"/df/speed"))     g_pat.speed = clamp01(f);
  else if (eq(a,"/df/density"))   g_pat.density = clamp01(f);
  else if (eq(a,"/df/intensity")) g_pat.intensity = clamp01(f);
  else if (eq(a,"/df/bpm"))       g_pat.bpm = f;
  else if (eq(a,"/df/tap"))       { if (i) tap(); }
  else if (eq(a,"/df/single"))    g_pat.single = (uint16_t)i;
  // device "Effect" modifiers
  else if (eq(a,"/df/advance"))   g_pat.advanceBeat = (i != 0);    // 0=Auto(rate), 1=Beat(bpm)
  else if (eq(a,"/df/factor"))    g_pat.factor = constrain(i, 1, 8); // Multiply
  else if (eq(a,"/df/random"))    g_pat.randomOrder = (i != 0);     // Random
  else if (eq(a,"/df/modulate"))  g_pat.modulate = (i != 0);       // Modulate (audio amplitude)
  else if (eq(a,"/df/audio"))     g_pat.audioLevel = clamp01(f);   // manual level override (no audio HW)
  // audio input subsystem (DF_AUDIO builds; g_aud exists either way)
  else if (eq(a,"/df/audio/enable")) g_aud.enabled = (i != 0);
  else if (eq(a,"/df/audio/source")) audio_select((AudioSource)(i != 0 ? AUD_LINEIN : AUD_MIC)); // 0=mic,1=line
  else if (eq(a,"/df/audio/mode"))   g_aud.mode   = (AudioMode)constrain(i, 0, 3); // off/level/pulse/advance
  else if (eq(a,"/df/audio/band"))   g_aud.band   = (AudioBand)constrain(i, 0, 3); // full/bass/mid/treble
  else if (eq(a,"/df/audio/gain"))   g_aud.gain   = 0.5f + clamp01(f) * 15.5f;
  else if (eq(a,"/df/audio/gate"))   g_aud.gate   = clamp01(f);
  else if (eq(a,"/df/audio/pulse"))  g_aud.pulseMs = (uint16_t)constrain(i, 5, 200);
  // pattern builder (Program/Stages)
  else if (eq(a,"/df/grid/steps"))g_pat.gridSteps = constrain(i, 1, PatternState::MAX_STEPS);
  else if (eq(a,"/df/grid/clear")){ if (i) memset(g_pat.grid, 0, sizeof(g_pat.grid)); }
  else if (eq(a,"/df/grid/cell")) {            // (i stage, i head, f level0..1)
    if (m.n >= 3) {
      int s = constrain(m.ival[0], 0, PatternState::MAX_STEPS-1);
      int fx= constrain(m.ival[1], 0, 255);
      g_pat.grid[s][fx] = (uint8_t)lroundf(clamp01(m.fval[2]) * 15.0f);
    }
  }
}

// ---- feedback to the iPad ----
static int putStr(uint8_t* b, const char* s){ int n=strlen(s)+1; memcpy(b,s,n); int p=align4(n); while(n<p) b[n++]=0; return p; }
static void sendF(const char* addr, float v) {
  if (!haveSender) return;
  uint8_t b[64]; int o=0;
  o += putStr(b+o, addr);
  o += putStr(b+o, ",f");
  uint32_t u; memcpy(&u,&v,4); b[o]=u>>24; b[o+1]=u>>16; b[o+2]=u>>8; b[o+3]=u; o+=4;
  udp.beginPacket(lastSender, OSC_FB_PORT); udp.write(b,o); udp.endPacket();
}
static void sendS(const char* addr, const char* s) {
  if (!haveSender) return;
  uint8_t b[96]; int o=0;
  o += putStr(b+o, addr); o += putStr(b+o, ",s"); o += putStr(b+o, s);
  udp.beginPacket(lastSender, OSC_FB_PORT); udp.write(b,o); udp.endPacket();
}

void osc_begin() { udp.begin(OSC_PORT); }

void osc_loop() {
  int n;
  while ((n = udp.parsePacket()) > 0) {
    int r = udp.read(rx, sizeof(rx));
    if (r <= 0) continue;
    lastSender = udp.remoteIP(); haveSender = true;
    OscMsg m;
    if (parseOsc(rx, r, m)) dispatch(m);
  }

  static uint32_t last = 0, lastPkts = 0;
  uint32_t now = millis();
  if (now - last >= 100) {                       // ~10 Hz status feedback
    float fps = (g_tx.packets - lastPkts) * (1000.0f / (now - last));
    lastPkts = g_tx.packets; last = now;
    sendF("/df/status/fps", fps);
    sendF("/df/status/output", g_cfg.outputEnable ? 1.0f : 0.0f);
    sendF("/df/status/pattern", (float)g_pat.pattern);
    sendS("/df/status/source", g_dmx.activeSource());
  }
}
