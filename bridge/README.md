# Dataflash Bridge (firmware)

Art-Net / sACN **target** that re-emits a DMX universe as the original Lightwave
Research **Dataflash** 9-bit / 375 kbaud RS-485 protocol. Built as a
validator/sink for existing controllers (TouchDesigner, lighting desks) so we can
prove the protocol on real fixtures before designing custom hardware.

Protocol reference: `../protocol/dataflash-protocol-spec.md`.

## Hardware
- **Olimex ESP32-PoE-ISO** (classic ESP32, LAN8720, PoE) — Ethernet primary, WiFi-AP fallback for config.
- **MAX485** (or MAX488/MAX490/MAX3485/SN75176 — NOT slew-limited MAX483/487, too slow for 375k).
- Future front panel: SSD1306 OLED + rotary encoder (stubbed in `ui.h`).

### Wiring (verify pins in `config.h` against your board — UEXT recommended)
```
ESP32 DF_TX_PIN (GPIO4)  -> MAX485 DI
ESP32 DF_DE_PIN (GPIO5)  -> MAX485 DE + RE   (transmit-only; may also tie HIGH in HW)
MAX485 A  -> fixture Data In  Pin 3 (Data+)
MAX485 B  -> fixture Data In  Pin 2 (Data-)
GND       -> fixture Data In  Pin 1 (shield)
```
Each fixture is an active repeater, so only fixture #1's Data In is driven; the
chain self-repeats. If output is inverted/garbled, swap A/B. Add 120R across A/B
at the fixture for long runs (bench runs usually fine on the fixture's bias).

## Why RMT, not UART
The 9th bit selects control(1) vs data(0). Classic ESP32 UART has only computed
even/odd parity (no stick parity), so it can't hold a constant 9th bit. We emit
each bit with the **RMT** peripheral at 213 ticks/bit (clk_div=1 -> 375.6 kbaud,
+0.16%). See `dataflash_tx.cpp`. Alternatives if RMT proves awkward: SPI or I2S
as a bit serializer.

## Build / flash (PlatformIO)
```
cd bridge
pio run -t upload          # build + flash over USB
pio device monitor         # 115200 serial log
```
First boot with no Ethernet link -> WiFi AP `dataflash-bridge` / pass `dataflash`,
config UI at http://192.168.4.1. With PoE/Ethernet, the serial log prints the IP.

## Configure / test (web UI)
- Status: link, active source (artnet/sacn), packet counters, TX refresh/heartbeat counts.
- Config: universe, start channel, fixture count, refresh Hz, nibble-swap, HTP merge.
- Output test: All-on / Chase / Single fixture at a 0-15 level + master output enable —
  for validating the wire without a controller.

## Architecture
```
inputs.cpp   Art-Net(6454) + sACN/E1.31(5568) -> g_dmx (HTP merge, source timeout)
main.cpp     scheduler: build 4-bit intensities (live or test) -> refresh + heartbeats
dataflash_tx RMT 9-bit/375k framing: ARM,START,<2 fixtures/byte>,STOP + heartbeats
dataflash_rx 9-bit RX SNIFFER (bit-bang on core 1; `esp32-s3-sniff`/`DF_SNIFF_MODE`): recover value + 9th bit
net.cpp      ETH + WiFi-AP fallback        webui.cpp  async server + embedded page
config.h     NVS-persisted settings        ui.h       OLED+encoder (stub)
```

## Validation plan (the point of this build)
1. **Scope DF_TX_PIN**: confirm bit width ~2.667 us and the 11-bit frame; confirm the 9th bit is 1 on control bytes, 0 on data.
2. **Logic-analyzer the RS-485 A/B** and decode as UART 375000-8N1; sanity-check ARM/START/data/STOP byte order.
3. **Compare to a real controller capture** (`../captures/`) once taken — align ordering, heartbeat cadence, and the nibble mapping.
4. **On fixtures**: set a DIP address, send a single-fixture test, confirm it strobes at the expected intensity. Use `nibbleSwap` if even/odd fixtures land wrong.

## Known TODOs / unknowns to confirm on hardware
- [ ] 9th-bit polarity & frame correctness on the scope (RMT timing).
- [ ] Exact even/odd -> high/low **nibble mapping** (the `nibbleSwap` flag exists to flip it).
- [ ] Packet **ordering / heartbeat cadence** vs a real controller capture (spec item still [?]).
- [ ] Whether ARM/START must repeat every refresh or can be sent once (currently every refresh).
- [ ] sACN universe change currently needs a reboot to re-join multicast.
- [ ] RMT write is blocking (~4 ms/refresh); move to non-blocking double-buffer if needed.
- [ ] Front panel (OLED + encoder) not implemented (`ui.h`).
