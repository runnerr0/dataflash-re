// dataflash_tx.cpp — emit arbitrary 9-bit UART frames at 375 kbaud using RMT.
//
// WHY RMT: the Dataflash link is 9-bit serial where the 9th bit flags
// control(1) vs data(0). The classic ESP32 UART supports only computed
// even/odd parity (no stick/mark-space), so it cannot hold a constant 9th
// bit. RMT lets us emit each bit at an exact duration -> full control.
//
// FRAME (UART convention, LSB first, idle = HIGH):
//   start(0) d0 d1 d2 d3 d4 d5 d6 d7 ninth stop(1)   = 11 bit-times
// MAX485 DI takes this TTL stream; A/B become the differential pair.
//
// TIMING: RMT clk_div=1 -> 12.5 ns/tick. bit = 1/375000 = 2.6667 us = 213.33
// ticks. We use 213 -> 375587 baud (+0.16%), well within UART tolerance.
//
// Legacy RMT driver (driver/rmt.h, IDF 4.4 / Arduino-ESP32 2.x). Port to the
// rmt_tx.h encoder API if you move to Arduino-ESP32 3.x / IDF 5.x.
//
// TODO(hw): this uses blocking rmt_write_items (a refresh ~4 ms). Fine for a
// target/validator; move to non-blocking double-buffer if CPU headroom matters.

#include "dataflash_tx.h"
#include "driver/rmt.h"

#define DF_RMT_CHANNEL   RMT_CHANNEL_0
#define DF_BIT_TICKS     213          // ~2.6667us @ clk_div=1 (80MHz/1)
#define DF_CLK_DIV       1

// RMT memory blocks: classic ESP32 has 8 blocks (64 words ea); ESP32-C3 has only
// 4 (48 words ea). Blocking rmt_write_items refills via ISR, so a couple blocks
// stream any length on either chip — don't hog all of the C3's RMT RAM.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define DF_RMT_MEM_BLOCKS  2
#else
#define DF_RMT_MEM_BLOCKS  4
#endif

DataflashTx g_tx;

void DataflashTx::begin(int txPin, int dePin) {
  _txPin = txPin; _dePin = dePin;
  if (_dePin >= 0) { pinMode(_dePin, OUTPUT); digitalWrite(_dePin, HIGH); } // TX-only: keep driver enabled

  rmt_config_t c = {};
  c.rmt_mode = RMT_MODE_TX;
  c.channel = DF_RMT_CHANNEL;
  c.gpio_num = (gpio_num_t)_txPin;
  c.clk_div = DF_CLK_DIV;
  c.mem_block_num = DF_RMT_MEM_BLOCKS; // more RMT RAM so larger bursts stream cleanly (chip-aware)
  c.tx_config.loop_en = false;
  c.tx_config.carrier_en = false;
  c.tx_config.idle_output_en = true;
  c.tx_config.idle_level = RMT_IDLE_LEVEL_HIGH;   // UART idle = high
  rmt_config(&c);
  rmt_driver_install(c.channel, 0, 0);
}

void DataflashTx::addByte(uint8_t v, bool ninth) {
  if (_nbits + 11 > (int)sizeof(_bits)) return;   // guard
  _bits[_nbits++] = 0;                              // start bit
  for (int i = 0; i < 8; i++) _bits[_nbits++] = (v >> i) & 1;  // LSB first
  _bits[_nbits++] = ninth ? 1 : 0;                 // 9th bit (control/data flag)
  _bits[_nbits++] = 1;                              // stop bit
}

void DataflashTx::flush() {
  if (_nbits == 0) return;
  // pack two bits per rmt_item32_t
  static rmt_item32_t items[ (sizeof(((DataflashTx*)0)->_bits)/2) + 2 ];
  int n = 0;
  for (int i = 0; i < _nbits; i += 2) {
    rmt_item32_t it = {};
    it.level0 = _bits[i];       it.duration0 = DF_BIT_TICKS;
    if (i + 1 < _nbits) { it.level1 = _bits[i+1]; it.duration1 = DF_BIT_TICKS; }
    else                { it.level1 = 1;          it.duration1 = DF_BIT_TICKS; } // pad with idle-high
    items[n++] = it;
  }
  rmt_write_items(DF_RMT_CHANNEL, items, n, true /*blocking*/);
  lastSendMicros = micros();
  reset();
}

void DataflashTx::sendRefresh(const uint8_t* intensities, uint16_t count) {
  if (count == 0) return;
  if (count > 256) count = 256;
  reset();

  // 8-head broadcast: 55 40 + one 8-bit intensity per fixture + trailing 00.
  // All data-plane (9th bit = 0). No ARM/START/FIRE — this is what the OEM
  // controller actually emits (protocol spec "Live OEM controller capture").
  addByte(DF_FRAME0, false);            // 0x55 preamble
  addByte(DF_FRAME1, false);            // 0x40 frame-start
  for (uint16_t i = 0; i < count; i++)
    addByte(intensities[i], false);     // one fixture per byte, full 8-bit
  addByte(0x00, false);                 // trailing frame terminator (seen on the wire)

  flush();
  packets++;
}

void DataflashTx::sendHeartbeat() {
  reset();
  addByte(DF_HEARTBEAT, true);
  flush();
  heartbeats++;
}
