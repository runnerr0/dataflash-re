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
#ifndef DF_RX_PIN
#define DF_RX_PIN   16   // sniff mode only: MAX485 RO input (set RE=DE=GND to listen-only)
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
  uint16_t fixtureCount = 8;        // 8-head controller = 8 fixtures (1 byte each)
  bool     nibbleSwap   = false;    // legacy (256-head 2-per-byte model); unused by the 8-head broadcast
  bool     htpMerge     = true;     // HTP-merge Art-Net + sACN; else last-arrived wins
  // output
  uint8_t  refreshHz    = 44;       // full-packet refresh rate
  uint8_t  heartbeatHz  = 120;      // heartbeat (0x00) cadence between refreshes
  bool     outputEnable = true;     // master enable for RS-485 output
  // test
  uint8_t  testMode     = 0;        // 0=live, 1=all-on, 2=chase, 3=single
  uint8_t  testLevel    = 8;        // 0..15 (4-bit)
  uint16_t testIndex    = 0;        // fixture for single/chase
  // audio input (DF_AUDIO builds)
  bool     audioEnable  = false;    // master enable for audio reactivity
  uint8_t  audioSource  = 0;        // 0=mic (I2S1), 1=line-in (I2S0)
  uint8_t  audioMode    = 1;        // 0=off, 1=level, 2=pulse, 3=advance
  uint8_t  audioBand    = 0;        // 0=full, 1=bass, 2=mid, 3=treble
  uint8_t  audioGain    = 96;       // input gain x16 (96 => 6.0); see audio.cpp

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
    audioEnable  = p.getBool ("aen",   audioEnable);
    audioSource  = p.getUChar("asrc",  audioSource);
    audioMode    = p.getUChar("amode", audioMode);
    audioBand    = p.getUChar("aband", audioBand);
    audioGain    = p.getUChar("again", audioGain);
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
    p.putBool ("aen",   audioEnable);
    p.putUChar("asrc",  audioSource);
    p.putUChar("amode", audioMode);
    p.putUChar("aband", audioBand);
    p.putUChar("again", audioGain);
    p.putString("host", hostname);
    p.putString("wssid", wifiSsid);
    p.putString("wpass", wifiPass);
    p.end();
  }
};

extern Config g_cfg;
