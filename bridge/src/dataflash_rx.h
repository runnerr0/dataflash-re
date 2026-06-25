// dataflash_rx.h — 9-bit RX sniffer for the original Dataflash 375k link.
//
// No 8-bit USB-serial adapter can recover the 9th (control/data) bit. Here the
// MAX485 receiver output (RO) feeds an ESP32 GPIO and we bit-bang the RX in a
// dedicated, interrupt-shielded core-1 task, recovering the FULL 11-bit frame:
// start + 8 data (LSB first) + 9th bit + stop. Each decoded word reports the
// 9th bit, so we can finally tell control (ARM/START/FIRE, 9th=1) from data.
#pragma once
#include <Arduino.h>

struct DfWord {
  uint8_t value;   // the 8 data bits
  bool    ninth;   // 9th bit: 1 = control marker, 0 = data
  bool    stopOk;  // stop bit was high (frame looks valid)
};

void df_rx_begin(int rxPin);   // start the sniffer task listening on rxPin (MAX485 RO)
bool df_rx_pop(DfWord* out);    // drain one decoded word; false if none waiting
