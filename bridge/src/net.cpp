// net.cpp — network bring-up: WiFi STA (join your network) + AP config portal,
// plus Ethernet for boards that have an internal EMAC.
//
// Priority: if a link comes up (ETH or WiFi STA) we use it. If nothing is up
// after a short grace period, we raise the config AP (SSID/pass in config.h) so
// you can set/replace WiFi credentials from the web UI. The AP drops once a real
// link connects, and comes back automatically if the link is lost.
//
// NOTE: ETH.begin() here is the RMII path (Olimex ESP32-PoE-ISO / LAN8720). On
// chips with no internal EMAC (ESP32-S3, -C3) it fails harmlessly and the WiFi
// path is used. (The WaveShare S3-ETH's W5500 is SPI and would need separate init.)
#include "net.h"
#include "config.h"
#include <ETH.h>
#include <WiFi.h>

static volatile bool s_ethUp = false;
static volatile bool s_staUp = false;
static bool s_apUp = false;
static uint32_t s_downSinceMs = 0;

static void startAP() {
  if (s_apUp) return;
  WiFi.softAP(g_cfg.apSsid, g_cfg.apPass);
  s_apUp = true;
  Serial.printf("[net] AP '%s' @ %s (config portal)\n",
                g_cfg.apSsid, WiFi.softAPIP().toString().c_str());
}

static void stopAP() {
  if (!s_apUp) return;
  WiFi.softAPdisconnect(true);
  s_apUp = false;
  Serial.println("[net] AP down (link up)");
}

static void onEvent(arduino_event_id_t e) {
  switch (e) {
    case ARDUINO_EVENT_ETH_START:     ETH.setHostname(g_cfg.hostname); break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      s_ethUp = true;
      Serial.printf("[net] ETH IP %s\n", ETH.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      s_ethUp = false; break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
      s_staUp = true;
      Serial.printf("[net] STA '%s' IP %s\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (s_staUp) Serial.println("[net] STA disconnected");
      s_staUp = false; break;
    default: break;
  }
}

void net_wifiConnect(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) return;
  Serial.printf("[net] STA connecting to '%s'...\n", ssid);
  WiFi.setHostname(g_cfg.hostname);
  WiFi.begin(ssid, pass);
}

void net_begin() {
  s_downSinceMs = millis();
  WiFi.onEvent(onEvent);
  WiFi.mode(WIFI_AP_STA);            // STA joins your net; AP available for config
  WiFi.setSleep(false);             // keep the radio responsive for UDP/web
  ETH.begin();                      // no-op fail on chips without an EMAC
  if (!g_cfg.useDHCP && g_cfg.staticIP) {
    ETH.config(IPAddress(g_cfg.staticIP), IPAddress(g_cfg.gateway),
               IPAddress(g_cfg.netmask));
  }
  if (g_cfg.wifiSsid[0]) net_wifiConnect(g_cfg.wifiSsid, g_cfg.wifiPass);
  else Serial.println("[net] no WiFi SSID saved — AP config portal will start");
}

void net_loop() {
  if (s_ethUp || s_staUp) {         // a real link is up
    stopAP();
    s_downSinceMs = 0;
    return;
  }
  // nothing up: raise the config AP after a short grace period (and keep it up)
  if (s_downSinceMs == 0) s_downSinceMs = millis();
  if (!s_apUp && millis() - s_downSinceMs > 6000) startAP();
}

bool net_ethUp() { return s_ethUp; }
bool net_staUp() { return s_staUp; }

IPAddress net_ip() {
  if (s_ethUp) return ETH.localIP();
  if (s_staUp) return WiFi.localIP();
  return WiFi.softAPIP();
}

const char* net_mode() {
  return s_ethUp ? "eth" : (s_staUp ? "sta" : (s_apUp ? "ap" : "down"));
}
