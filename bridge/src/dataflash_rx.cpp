// dataflash_rx.cpp — 9-bit RX sniffer (see dataflash_rx.h).
//
// A dedicated task pinned to core 1 busy-polls the RO GPIO for each UART start
// bit (falling edge), then samples 8 data + 9th + stop at cycle-accurate bit
// centers. 375000 baud -> ~640 CPU cycles/bit @ 240 MHz. Interrupts are shielded
// only for the ~29 us of a single frame, so core 0 (USB / printing) is untouched.
//
// Edge detection is by polling (latency ~tens of ns), so the start instant is
// captured accurately; sampling at bit *centers* gives ~half-bit of margin.
#include "dataflash_rx.h"
#include "soc/gpio_struct.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static int s_pin = -1;
static QueueHandle_t s_q = nullptr;

static inline int rd() { return (GPIO.in >> s_pin) & 0x1; }   // fast single-pin read (GPIO0..31)

// YIELD_SPINS: how long to busy-poll before concluding "idle" and yielding the
// CPU (so loop()/IDLE on this core can run). With a live signal the line changes
// long before this, so we never yield mid-traffic; only an idle line yields.
#define YIELD_SPINS 30000

static void rxTask(void*) {
  const uint32_t CPB = (uint32_t)((uint64_t)getCpuFrequencyMhz() * 1000000ULL / 375000ULL); // cycles/bit
  uint32_t fcount = 0;
  for (;;) {
    uint32_t spins = 0;
    while (rd() == 0) { if (++spins >= YIELD_SPINS) { spins = 0; vTaskDelay(1); } }  // wait for idle high
    spins = 0;
    while (rd() == 1) { if (++spins >= YIELD_SPINS) { spins = 0; vTaskDelay(1); } }  // wait for falling edge = start
    uint32_t t0 = ESP.getCycleCount();
    noInterrupts();                     // shield this one frame (~29 us) on this core only
    uint16_t frame = 0;
    for (int b = 0; b < 10; b++) {      // d0..d7 (LSB first), then 9th, then stop
      // bit center = (b + 1.5)*CPB; sample the 9th bit (b==8) +0.25 bit later so a
      // slightly slow d7 edge can't bleed into it (fixes the lone flaky case, 0x80).
      float frac = (b == 8) ? 1.75f : 1.5f;
      uint32_t target = t0 + (uint32_t)(((float)b + frac) * (float)CPB);
      while ((int32_t)(ESP.getCycleCount() - target) < 0) {}
      if (rd()) frame |= (1u << b);
    }
    interrupts();
    DfWord w = { (uint8_t)(frame & 0xFF), (bool)((frame >> 8) & 1u), (bool)((frame >> 9) & 1u) };
    xQueueSend(s_q, &w, 0);             // loop() on core 1 drains continuously -> lossless
    if (++fcount >= 2000) { fcount = 0; vTaskDelay(1); }  // rare yield only to feed core-0 idle/WDT
  }
}

void df_rx_begin(int rxPin) {
  s_pin = rxPin;
  pinMode(rxPin, INPUT);
  s_q = xQueueCreate(512, sizeof(DfWord));
  // Pin to CORE 0 so the Arduino loop() (core 1) drains + prints fully in parallel
  // = lossless. The task still yields on an idle line and every ~2000 frames, so
  // core-0 idle/watchdog/system tasks keep running (no WDT disable needed).
  xTaskCreatePinnedToCore(rxTask, "df_rx", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 0);
}

bool df_rx_pop(DfWord* out) {
  return s_q && xQueueReceive(s_q, out, 0) == pdTRUE;
}
