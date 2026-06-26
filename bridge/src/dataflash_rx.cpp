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
      uint32_t target = t0 + ((2u * (uint32_t)b + 3u) * CPB) / 2u;  // center of bit b = (b + 1.5)*CPB
      while ((int32_t)(ESP.getCycleCount() - target) < 0) {}
      if (rd()) frame |= (1u << b);
    }
    interrupts();
    DfWord w = { (uint8_t)(frame & 0xFF), (bool)((frame >> 8) & 1u), (bool)((frame >> 9) & 1u) };
    xQueueSend(s_q, &w, 0);             // non-blocking; a dropped word is fine for a sniffer
    if (++fcount >= 128) { fcount = 0; vTaskDelay(1); }  // periodically yield so loop() drains + prints
  }
}

void df_rx_begin(int rxPin) {
  s_pin = rxPin;
  pinMode(rxPin, INPUT);
  s_q = xQueueCreate(512, sizeof(DfWord));
  // High priority for responsiveness, but the task yields on an idle line (above),
  // so loop() and the idle/watchdog tasks on this core still run. (No WDT disable.)
  xTaskCreatePinnedToCore(rxTask, "df_rx", 4096, nullptr, configMAX_PRIORITIES - 2, nullptr, 1);
}

bool df_rx_pop(DfWord* out) {
  return s_q && xQueueReceive(s_q, out, 0) == pdTRUE;
}
