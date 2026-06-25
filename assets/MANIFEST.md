# Asset manifest

Source: ETC legacy archive — High End Systems / Lightwave Research Dataflash
`https://www.etcconnect.com/Products/Legacy/Live-Events-High-End-Systems/Lighting-Fixtures/Dataflash-AF1000.aspx`

Ingested and organized from `ingest/`. Duplicate second-downloads moved to `_duplicates/`.

## Manuals (`assets/manuals/`)

| File | Original name | Identity | Pages | Text layer | Read plan |
|---|---|---|---|---|---|
| `dataflash-original-user-manual.pdf` | `dataflash.pdf` | **Original Dataflash User Manual** (target system) | 35 | None — scanned images | OCR required |
| `af1000-user-manual.pdf` | `af1000.pdf` | AF1000 User Manual (DMX-native cousin, cross-ref) | 42 | Yes, but shifted-font encoding | Decode font shift to recover text |

## Schematics (`assets/schematics/`)

| File | Original name | Identity | Pages | Format | Read plan |
|---|---|---|---|---|---|
| `dataflash-original-schematics.pdf` | `dataflash_sch.pdf` | **Original Dataflash Schematics** (key asset) | 35 | Scanned images | Rasterize per sheet, read visually + OCR labels |
| `af1000-schematics.pdf` | `af1kv22.pdf` | AF1000 Schematics (ViewDraw export, cross-ref) | 6 | Vector line-art, no text layer | Rasterize per sheet, read visually |

## Firmware (`assets/firmware/`)

| File | Identity | Notes |
|---|---|---|
| `df282.exe` | ZIP archive (mislabeled `.exe`), 1994-02-16 | Contains `Df31f2.82`, a 65536-byte 27C512 EPROM image |
| `dataflash-strobe-head-fw-2.82-16k.hex` | De-mirrored 16 KB image, Intel HEX | Tool-ready (8051 disassemblers / EPROM programmers). Generated from the unique 16 KB; the 64 KB dump is this 16 KB mirrored 4×. |

**Identified:** Rev 2.82 "Data Flash strobe head," Lightwave Research 1989, author Steve Tulk.
**CPU:** 8051. **Parser:** polled UART RX, frame parser at 0x0BC0–0x0CA0. See `firmware-analysis/01-first-pass.md`.

## Notes
- `af1000.pdf` self-reports title "Dataflash AF1000 User's Manual" (FrameMaker 5.5.3 → Distiller 3.01).
- `af1kv22.pdf` self-reports as a ViewDraw schematic ("sch\55r234"), confirming it is AF1000 schematics rather than a manual despite the ambiguous filename.
- The two scanned 35-page PDFs (original manual + original schematics) are pure raster; OCR/rasterization is the gate to reading them.
- `df282.exe` unzips locally to `Df31f2.82` (raw 64 KB) if you want the binary directly: `unzip df282.exe`.
