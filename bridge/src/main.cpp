// main.cpp — Dataflash bridge: Art-Net / sACN / OSC -> Dataflash RS-485.
// - As a TARGET: receives a DMX universe (Art-Net/sACN) and re-emits it (PAT_LIVE).
// - As a CONTROLLER: TouchOSC over OSC selects on-device patterns + a custom grid,
//   which free-run on the ESP32 so the rig keeps going if the iPad disconnects.
#include <Arduino.h>
#include "config.h"
#include "net.h"
#include "inputs.h"
#include "patterns.h"
#include "osc.h"
#include "dataflash_tx.h"
#include "webui.h"
#include "ui.h"

Config g_cfg;

static uint8_t  intensities[256];   // 4-bit per fixture, persists across frames
static uint32_t lastRefreshMs = 0, lastHbMs = 0;
static uint16_t chasePos = 0;

static void buildIntensities() {
  uint16_t n = g_cfg.fixtureCount;

  if (g_pat.pattern != PAT_LIVE) {            // OSC/web-selected on-device pattern
    patterns_render(intensities, n, millis());
    return;
  }

  // PAT_LIVE: network passthrough, with the web UI's quick test modes available
  for (uint16_t i = 0; i < n; i++) {
    uint8_t v = 0;
    switch (g_cfg.testMode) {
      case 0: { uint16_t ch = (g_cfg.startChannel - 1) + i;
                v = g_dmx.merged(ch, g_cfg.htpMerge) >> 4; break; }
      case 1: v = g_cfg.testLevel; break;
      case 2: v = (i == chasePos) ? g_cfg.testLevel : 0; break;
      case 3: v = (i == g_cfg.testIndex) ? g_cfg.testLevel : 0; break;
    }
    intensities[i] = v & 0x0F;
  }
}

void setup() {
  Serial.begin(115200); delay(50);
  Serial.println("\n[dataflash-bridge] boot");
  g_cfg.load();

  Serial.printf("[dataflash-bridge] RS-485 TX=GPIO%d  DE=GPIO%d\n", DF_TX_PIN, DF_DE_PIN);
  g_tx.begin(DF_TX_PIN, DF_DE_PIN);
  net_begin();
  inputs_begin();      // Art-Net + sACN
  osc_begin();         // TouchOSC control
  webui_begin();
  ui_begin();
  Serial.println("[dataflash-bridge] ready");
}

void loop() {
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
    g_tx.sendRefresh(intensities, g_cfg.fixtureCount, g_cfg.nibbleSwap);
    if (g_pat.pattern == PAT_LIVE && g_cfg.testMode == 2 && g_cfg.fixtureCount)
      chasePos = (chasePos + 1) % g_cfg.fixtureCount;   // web chase
  } else if (now - lastHbMs >= hbMs) {
    lastHbMs = now;
    g_tx.sendHeartbeat();   // feed fixtures' cooldown/timebase between refreshes
  }
}
