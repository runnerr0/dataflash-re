// inputs.h — Art-Net + sACN(E1.31) receivers and a small merge buffer.
#pragma once
#include <Arduino.h>

struct DmxState {
  uint8_t  art[512]  = {0};   uint32_t artMs  = 0;   bool artSeen  = false;
  uint8_t  sacn[512] = {0};   uint32_t sacnMs = 0;   bool sacnSeen = false;
  static const uint32_t SOURCE_TIMEOUT_MS = 2500;

  // merged 8-bit value for 0-based DMX channel (HTP of fresh sources)
  uint8_t merged(uint16_t ch, bool htp) const {
    uint32_t now = millis();
    bool aFresh = artSeen  && (now - artMs)  < SOURCE_TIMEOUT_MS;
    bool sFresh = sacnSeen && (now - sacnMs) < SOURCE_TIMEOUT_MS;
    uint8_t a = (aFresh && ch < 512) ? art[ch]  : 0;
    uint8_t s = (sFresh && ch < 512) ? sacn[ch] : 0;
    if (!htp) return sFresh ? s : a;          // last-source-wins (sacn preferred if both)
    return a > s ? a : s;                      // HTP
  }
  const char* activeSource() const {
    uint32_t now = millis();
    bool a = artSeen  && (now-artMs)  < SOURCE_TIMEOUT_MS;
    bool s = sacnSeen && (now-sacnMs) < SOURCE_TIMEOUT_MS;
    if (a && s) return "artnet+sacn";
    if (a) return "artnet";
    if (s) return "sacn";
    return "none";
  }
};

extern DmxState g_dmx;
extern volatile uint32_t g_artPkts, g_sacnPkts;

void inputs_begin();
void inputs_loop();
