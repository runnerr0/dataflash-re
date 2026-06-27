// dataflash_tx.h — Dataflash RS-485 transmitter (9-bit, 375 kbaud) via RMT.
#pragma once
#include <Arduino.h>

// Real 8-head broadcast (what the OEM controller actually emits — see
// protocol/dataflash-protocol-spec.md "Live OEM controller capture"):
//   55 40 <one 8-bit intensity per fixture> 00   (all data-plane, 9th=0)
// interleaved with 0x00 heartbeats (9th=1). NO ARM/START/FIRE on the wire.
static const uint8_t DF_FRAME0    = 0x55;   // alternating-bit preamble (data)
static const uint8_t DF_FRAME1    = 0x40;   // frame-start (data)
static const uint8_t DF_HEARTBEAT = 0x00;   // master timebase (the ONLY control byte, 9th=1)
// Legacy 256-head firmware markers — NOT used by the 8-head controller; kept for
// reference (the firmware decodes these but no controller sends them on the wire).
static const uint8_t DF_ARM       = 0x55;
static const uint8_t DF_START     = 0x7F;
static const uint8_t DF_STOP_FIRE = 0xF7;
static const uint8_t DF_CLEAR     = 0xFF;

class DataflashTx {
public:
  void begin(int txPin, int dePin);

  // Send one full refresh as the 8-head broadcast: 55 40 <count data bytes> 00.
  // intensities[i] = 8-bit level (0..255) for fixture i (0-based). count = fixtures.
  void sendRefresh(const uint8_t* intensities, uint16_t count);

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
  // bit buffer: broadcast = 2 (sync) + count + 1 (term) bytes * 11 bits.
  // sized for count up to 256 (259 bytes * 11 = 2849 bits).
  uint8_t _bits[3072];
  uint16_t _nbits = 0;
};

extern DataflashTx g_tx;
