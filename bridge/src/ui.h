// ui.h — front panel (SSD1306 OLED + rotary encoder). STUB for now.
// Future: menu for network config + live output validation, driven by the encoder.
// Hardware hooks (pins in config.h): OLED on I2C (OLED_SDA/SCL), encoder on ENC_A/B/SW.
#pragma once
#include <Arduino.h>

// TODO: implement with U8g2 (display) + interrupt-driven quadrature decode.
//   - screen 1: status (net/ip, source, fps)
//   - screen 2: network config (DHCP/static, universe)
//   - screen 3: output test (mode/level/fixture) -> writes g_cfg test fields
inline void ui_begin() { /* init U8g2 + encoder pins here */ }
inline void ui_loop()  { /* read encoder, render screen here */ }
