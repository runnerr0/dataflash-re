// main.cpp — Dataflash bridge + sniffer, UNIFIED firmware (role chosen at boot).
// - BRIDGE role: Art-Net/sACN/OSC -> Dataflash RS-485 (the real 8-head broadcast).
// - SNIFFER role: 9-bit RX listen-only protocol analyzer.
// One MAX485 harness does both (half-duplex): GPIO17 = DI (TX) AND RO-via-divider
// (RX, receiver tri-stated during TX); GPIO16 = DE+RE tied = DIR (HIGH=TX, LOW=RX).
// Role = g_cfg.role (NVS, default DF_ROLE_DEFAULT), OR hold BOOT (GPIO0) at power-on
// to force SNIFFER for that boot — no reflash, no rewiring.
#include <Arduino.h>
#include "config.h"
#include "net.h"
#include "inputs.h"
#include "patterns.h"
#include "osc.h"
#include "dataflash_tx.h"
#include "dataflash_rx.h"
#include "webui.h"
#include "ui.h"
#include "audio.h"
#include "soc/gpio_struct.h"

Config g_cfg;
static bool s_sniffer = false;

static uint8_t  intensities[256];   // 8-bit per fixture, persists across frames
static uint32_t lastRefreshMs = 0, lastHbMs = 0;
static uint16_t chasePos = 0;

static void buildIntensities() {
  uint16_t n = g_cfg.fixtureCount;

  if (g_pat.pattern != PAT_LIVE) {            // OSC/web-selected on-device pattern
    patterns_render(intensities, n, millis()); // engine outputs 4-bit (0..15)
    for (uint16_t i = 0; i < n; i++) intensities[i] *= 17;  // -> 8-bit (0..255) for the wire
    return;
  }

  // PAT_LIVE: network passthrough (full 8-bit), with web UI quick-test modes.
  uint8_t tl = g_cfg.testLevel > 15 ? 255 : g_cfg.testLevel * 17;   // 4-bit test level -> 8-bit
  for (uint16_t i = 0; i < n; i++) {
    uint8_t v = 0;
    switch (g_cfg.testMode) {
      case 0: { uint16_t ch = (g_cfg.startChannel - 1) + i;
                v = g_dmx.merged(ch, g_cfg.htpMerge); break; }   // full 8-bit DMX
      case 1: v = tl; break;
      case 2: v = (i == chasePos) ? tl : 0; break;
      case 3: v = (i == g_cfg.testIndex) ? tl : 0; break;
    }
    intensities[i] = v;
  }
}

// ---------------- SNIFFER role: 9-bit RX, listen-only -----------------------
static void snifferSetup() {
  Serial.println("\n[dataflash] role=SNIFFER — 9-bit RX @ 375000");
  Serial.printf("[dataflash] wire: MAX485 RO->GPIO%d (via divider), DIR(GPIO%d)=LOW=listen\n",
                DF_RX_PIN, DF_DE_PIN);
  Serial.println("[dataflash] output: <hex><C|d>  (C=control 9th=1, d=data 9th=0); '[burst]'=>0.4ms gap\n");
  pinMode(DF_DE_PIN, OUTPUT); digitalWrite(DF_DE_PIN, LOW);   // DIR low = receiver on, driver off
  pinMode(DF_RX_PIN, INPUT);
  Serial.printf("[diag] sampling GPIO%d for 1s before starting the decoder...\n", DF_RX_PIN);
  for (int i = 0; i < 5; i++) {
    int last = (GPIO.in >> DF_RX_PIN) & 1, lvl = last, edges = 0;
    uint32_t t = micros();
    while (micros() - t < 200000) { int v = (GPIO.in >> DF_RX_PIN) & 1; if (v != last) { edges++; last = v; } }
    Serial.printf("[diag] gpio%d level=%d edges/200ms=%d\n", DF_RX_PIN, lvl, edges);
  }
  Serial.println("[diag] starting decoder...");
  df_rx_begin(DF_RX_PIN);
}

static void snifferLoop() {
  static uint32_t lastUs = 0, lastStat = 0, nwords = 0, nbursts = 0; static int col = 0;
  DfWord w;
  while (df_rx_pop(&w)) {
    uint32_t now = micros();
    if (now - lastUs > 400) { Serial.print("\n[burst] "); col = 0; nbursts++; }
    lastUs = now; nwords++;
    Serial.printf("%02X%c ", w.value, w.ninth ? 'C' : 'd');
    if (++col % 16 == 0) Serial.print("\n        ");
  }
  if (millis() - lastStat > 1000) {
    lastStat = millis();
    int last = (GPIO.in >> DF_RX_PIN) & 1, lvl = last, edges = 0;
    uint32_t t = micros();
    while (micros() - t < 5000) { int v = (GPIO.in >> DF_RX_PIN) & 1; if (v != last) { edges++; last = v; } }
    Serial.printf("\n[stat] gpio%d level=%d edges/5ms=%d  words=%lu bursts=%lu\n",
                  DF_RX_PIN, lvl, edges, (unsigned long)nwords, (unsigned long)nbursts);
  }
}

// ---------------- BRIDGE role: network/OSC -> RS-485 ------------------------
static void bridgeSetup() {
  Serial.println("\n[dataflash] role=BRIDGE");
  Serial.printf("[dataflash-bridge] RS-485 DI=GPIO%d  DIR(DE+RE)=GPIO%d\n", DF_TX_PIN, DF_DE_PIN);
  g_tx.begin(DF_TX_PIN, DF_DE_PIN);
  net_begin();
  inputs_begin();      // Art-Net + sACN
  osc_begin();         // TouchOSC control
  webui_begin();
  ui_begin();
  audio_begin();       // audio-reactive input (no-op unless DF_AUDIO)
  Serial.println("[dataflash-bridge] ready");
}

static void bridgeLoop() {
  net_loop();
  inputs_loop();
  osc_loop();
  ui_loop();

  if (!g_cfg.outputEnable) { delay(1); return; }

  uint32_t now = millis();
  uint32_t refreshMs = 1000UL / max((uint8_t)1, g_cfg.refreshHz);
  uint32_t hbMs      = 1000UL / max((uint8_t)1, g_cfg.heartbeatHz);

  if (now - lastRefreshMs >= refreshMs) {
    lastRefreshMs = now;
    buildIntensities();
    g_tx.sendRefresh(intensities, g_cfg.fixtureCount);
    if (g_pat.pattern == PAT_LIVE && g_cfg.testMode == 2 && g_cfg.fixtureCount)
      chasePos = (chasePos + 1) % g_cfg.fixtureCount;   // web chase
  } else if (now - lastHbMs >= hbMs) {
    lastHbMs = now;
    g_tx.sendHeartbeat();   // feed fixtures' cooldown/timebase between refreshes
  }
}

// ---------------- entry: pick role at boot ----------------------------------
void setup() {
  Serial.begin(115200); delay(50);
  Serial.println("\n[dataflash] boot");
  g_cfg.load();
  // Role: persisted g_cfg.role, OR hold BOOT (GPIO0 low) at power-on to force sniffer.
  pinMode(0, INPUT_PULLUP); delay(5);
  bool bootHeld = (digitalRead(0) == LOW);
  s_sniffer = (g_cfg.role != 0) || bootHeld;
  if (bootHeld) Serial.println("[dataflash] BOOT held -> SNIFFER for this boot");
  if (s_sniffer) snifferSetup(); else bridgeSetup();
}

void loop() {
  if (s_sniffer) snifferLoop(); else bridgeLoop();
}
