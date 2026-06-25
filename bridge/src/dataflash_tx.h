// dataflash_tx.h — Dataflash RS-485 transmitter (9-bit, 375 kbaud) via RMT.
#pragma once
#include <Arduino.h>

// Control-byte markers (all sent with 9th bit = 1). From protocol/dataflash-protocol-spec.md
static const uint8_t DF_ARM       = 0x55;
static const uint8_t DF_START     = 0x7F;
static const uint8_t DF_HEARTBEAT = 0x00;
static const uint8_t DF_STOP_FIRE = 0xF7;
static const uint8_t DF_CLEAR     = 0xFF;

class DataflashTx {
public:
  void begin(int txPin, int dePin);

  // Send one full refresh: ARM, START, <dataBytes>, STOP/FIRE.
  // intensities[i] = 4-bit level (0..15) for fixture address i (0-based).
  // count = number of fixtures (1..256). nibbleSwap flips even/odd -> hi/lo.
  void sendRefresh(const uint8_t* intensities, uint16_t count, bool nibbleSwap);

  // Send a single heartbeat (0x00, 9th=1) to feed the fixtures' cooldown timers.
  void sendHeartbeat();

  // stats
  volatile uint32_t packets = 0;
  volatile uint32_t heartbeats = 0;
  uint32_t lastSendMicros = 0;

private:
  void addByte(uint8_t value, bool ninthBit);   // append 11 bits to _bits
  void flush();                                  // bits -> RMT items -> wire
  void reset() { _nbits = 0; }

  int _txPin = -1, _dePin = -1;
  // bit buffer: max packet ~131 bytes * 11 = 1441 bits
  uint8_t _bits[1600];
  uint16_t _nbits = 0;
};

extern DataflashTx g_tx;
