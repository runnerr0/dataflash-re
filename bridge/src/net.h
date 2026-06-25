// net.h
#pragma once
#include <Arduino.h>
#include <IPAddress.h>
void net_begin();
void net_loop();
bool net_ethUp();
bool net_staUp();
void net_wifiConnect(const char* ssid, const char* pass);  // (re)join a STA network
IPAddress net_ip();
const char* net_mode();   // "eth" | "sta" | "ap" | "down"
