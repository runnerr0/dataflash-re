// config.h — persistent configuration (NVS via Preferences), header-only.
#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ---- Pin map (VERIFY against Olimex ESP32-PoE-ISO free pins; UEXT recommended) ----
// Ethernet RMII uses many GPIOs; these are chosen to avoid them. Double-check
// against your board revision before soldering.
#ifndef DF_TX_PIN
#define DF_TX_PIN   4    // -> MAX485 DI   (RMT output, the differential data)
#endif
#ifndef DF_DE_PIN
#define DF_DE_PIN   5    // -> MAX485 DE (+RE). Transmit-only: can also tie high in HW.
#endif
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 13
#endif
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 16
#endif
#ifndef ENC_A_PIN
#define ENC_A_PIN   32
#endif
#ifndef ENC_B_PIN
#define ENC_B_PIN   33
#endif
#ifndef ENC_SW_PIN
#define ENC_SW_PIN  34   // input-only OK for a button
#endif

struct Config {
  // network
  bool     useDHCP      = true;
  uint32_t staticIP     = 0;        // packed; 0 => DHCP
  uint32_t gateway      = 0;
  uint32_t netmask      = 0;
  char     hostname[24] = "dataflash-bridge";
  // wifi station (client): joins this network if wifiSsid is set; else AP only
  char     wifiSsid[33] = "";
  char     wifiPass[64] = "";
  // wifi fallback AP (config portal when no STA/ethernet link)
  char     apSsid[24]   = "dataflash-bridge";
  char     apPass[24]   = "dataflash";   // >=8 chars
  // protocol mapping
  uint16_t universe     = 0;        // Art-Net/sACN universe to listen on
  uint16_t startChannel = 1;        // DMX channel (1-based) for fixture address 0
  uint16_t fixtureCount = 32;       // number of Dataflash fixtures (1..256)
  bool     nibbleSwap   = false;    // flip even/odd->high/low nibble if capture disagrees
  bool     htpMerge     = true;     // HTP-merge Art-Net + sACN; else last-arrived wins
  // output
  uint8_t  refreshHz    = 44;       // full-packet refresh rate
  uint8_t  heartbeatHz  = 120;      // heartbeat (0x00) cadence between refreshes
  bool     outputEnable = true;     // master enable for RS-485 output
  // test
  uint8_t  testMode     = 0;        // 0=live, 1=all-on, 2=chase, 3=single
  uint8_t  testLevel    = 8;        // 0..15 (4-bit)
  uint16_t testIndex    = 0;        // fixture for single/chase

  void load() {
    Preferences p; p.begin("dfbridge", true);
    useDHCP      = p.getBool ("dhcp",  useDHCP);
    universe     = p.getUShort("uni",  universe);
    startChannel = p.getUShort("start",startChannel);
    fixtureCount = p.getUShort("nfix", fixtureCount);
    nibbleSwap   = p.getBool ("nswap", nibbleSwap);
    htpMerge     = p.getBool ("htp",   htpMerge);
    refreshHz    = p.getUChar("rhz",   refreshHz);
    heartbeatHz  = p.getUChar("hhz",   heartbeatHz);
    outputEnable = p.getBool ("oen",   outputEnable);
    p.getString("host", hostname, sizeof(hostname));
    p.getString("wssid", wifiSsid, sizeof(wifiSsid));
    p.getString("wpass", wifiPass, sizeof(wifiPass));
    p.end();
  }
  void save() {
    Preferences p; p.begin("dfbridge", false);
    p.putBool ("dhcp",  useDHCP);
    p.putUShort("uni",  universe);
    p.putUShort("start",startChannel);
    p.putUShort("nfix", fixtureCount);
    p.putBool ("nswap", nibbleSwap);
    p.putBool ("htp",   htpMerge);
    p.putUChar("rhz",   refreshHz);
    p.putUChar("hhz",   heartbeatHz);
    p.putBool ("oen",   outputEnable);
    p.putString("host", hostname);
    p.putString("wssid", wifiSsid);
    p.putString("wpass", wifiPass);
    p.end();
  }
};

extern Config g_cfg;
