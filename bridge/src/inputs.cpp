// inputs.cpp — self-contained Art-Net (6454) + sACN/E1.31 (5568) parsers.
// Uses WiFiUDP, which binds to the LwIP stack and therefore works over the
// ESP32-PoE Ethernet interface as well as WiFi.
#include "inputs.h"
#include "config.h"
#include <WiFiUdp.h>

DmxState g_dmx;
volatile uint32_t g_artPkts = 0, g_sacnPkts = 0;

static WiFiUDP udpArt;
static WiFiUDP udpSacn;
static uint8_t buf[640];

static const uint16_t ART_PORT  = 6454;
static const uint16_t SACN_PORT = 5568;

void inputs_begin() {
  udpArt.begin(ART_PORT);
  // sACN multicast group for the configured universe: 239.255.<hi>.<lo>
  IPAddress mc(239, 255, (g_cfg.universe >> 8) & 0xFF, g_cfg.universe & 0xFF);
  udpSacn.beginMulticast(mc, SACN_PORT);   // also receives unicast to the port
}

static void parseArtnet(int len) {
  if (len < 18) return;
  if (memcmp(buf, "Art-Net\0", 8) != 0) return;
  uint16_t op = buf[8] | (buf[9] << 8);
  if (op != 0x5000) return;                       // OpDmx
  uint16_t uni = buf[14] | (buf[15] << 8);        // SubUni | Net<<8
  if (uni != g_cfg.universe) return;
  uint16_t dlen = (buf[16] << 8) | buf[17];
  if (dlen > 512) dlen = 512;
  if (18 + dlen > len) dlen = len - 18;
  memcpy(g_dmx.art, &buf[18], dlen);
  g_dmx.artMs = millis(); g_dmx.artSeen = true; g_artPkts++;
}

static void parseSacn(int len) {
  if (len < 126) return;
  // loose validation: ACN PID at offset 4, E1.31 framing vector
  if (memcmp(&buf[4], "ASC-E1.17", 9) != 0) return;
  uint16_t uni = (buf[113] << 8) | buf[114];      // framing-layer universe (BE)
  if (uni != g_cfg.universe) return;
  uint16_t propCount = (buf[123] << 8) | buf[124];
  if (propCount == 0) return;
  uint16_t slots = propCount - 1;                 // minus start-code byte
  if (slots > 512) slots = 512;
  if (126 + slots > len) slots = len - 126;
  // buf[125] is the DMX start code (should be 0x00); data begins at 126
  memcpy(g_dmx.sacn, &buf[126], slots);
  g_dmx.sacnMs = millis(); g_dmx.sacnSeen = true; g_sacnPkts++;
}

void inputs_loop() {
  int n;
  while ((n = udpArt.parsePacket()) > 0) {
    int r = udpArt.read(buf, sizeof(buf));
    if (r > 0) parseArtnet(r);
  }
  while ((n = udpSacn.parsePacket()) > 0) {
    int r = udpSacn.read(buf, sizeof(buf));
    if (r > 0) parseSacn(r);
  }
}
