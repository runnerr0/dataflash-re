# TouchOSC controller — OSC schema & layout plan

The bridge runs the pattern engine; TouchOSC is the control surface + pattern
builder. Patterns free-run on the ESP32, so the show survives an iPad dropout.
Pattern design + tiers: see `bridge/PATTERNS.md` (grounded in the original gear).

## TouchOSC connection setup
- TouchOSC → Connections → OSC → **UDP**.
  - **Host** = bridge IP (serial log / web UI; or AP `192.168.4.1`).
  - **Send port = 8000** (bridge listens). **Receive port = 9000** (bridge feedback).
- iPad + bridge on the same network (PoE LAN, or the bridge's WiFi AP).

## OSC schema — iPad → bridge
### Transport
| Address | Args | Meaning |
|---|---|---|
| `/df/output` | i/T/F | master output enable |
| `/df/blackout` | T/F | instant all-off (engine keeps running) |
| `/df/flash` | T/F | momentary all-at-max bump (T on press, F on release) |

### Pattern + core params
| Address | Args | Meaning |
|---|---|---|
| `/df/pattern` | i | select pattern (index below) |
| `/df/intensity` | f 0..1 | per-flash brightness (master) |
| `/df/speed` | f 0..1 | Auto advance rate |
| `/df/density` | f 0..1 | chase/comet width, sparkle prob, wave window |
| `/df/bpm` | f | beat advance / strobe rate |
| `/df/tap` | T | tap tempo |
| `/df/single` | i | head index for `single` |

### Effect modifiers (apply across sequencer modes — mirror the original controller)
| Address | Args | Meaning |
|---|---|---|
| `/df/advance` | i/T/F | 0=Auto (rate), 1=Beat (bpm) |
| `/df/factor` | i 1..8 | Multiply: repeat each stage N times |
| `/df/random` | T/F | Random: shuffle stage order |
| `/df/modulate` | T/F | intensity tracks audio amplitude |
| `/df/audio` | f 0..1 | audio level (stub until audio-in wired) |

### Pattern builder (Program / Stages)
| Address | Args | Meaning |
|---|---|---|
| `/df/grid/steps` | i 1..32 | number of stages |
| `/df/grid/clear` | T | clear the Program |
| `/df/grid/cell` | i stage, i head, f level0..1 | set one cell |

### Pattern indices (`/df/pattern`)
NATIVE: `0 live` · `1 all` · `2 single` · `3 seq` (Program playback)
FRAMEWORK: `4 chase` · `5 alternate` · `6 build` · `7 strobe` · `8 sparkle`
ALTERNATE: `9 wave` · `10 pingpong` · `11 comet`

## OSC schema — bridge → iPad (feedback, ~10 Hz)
`/df/status/fps` (f) · `/df/status/output` (f 0/1) · `/df/status/pattern` (f) · `/df/status/source` (s).
Bind a label/LED control's OSC **receive** address to show status.

## Suggested layout (3 pages)

### Page 1 — PERFORM
- **Pattern selector** (button group): each sends `/df/pattern <index>`. Group them by tier (native / framework / alternate) per `PATTERNS.md`.
- **Faders** (FLOAT 0..1): Intensity→`/df/intensity`, Speed→`/df/speed`, Density→`/df/density`.
- **Tempo**: BPM fader→`/df/bpm`, TAP→`/df/tap`, Advance toggle (Auto/Beat)→`/df/advance`.
- **Modifiers**: Multiply stepper→`/df/factor`, Random toggle→`/df/random`, Modulate toggle→`/df/modulate`.
- **Transport**: FLASH (momentary)→`/df/flash`, BLACKOUT→`/df/blackout`, OUTPUT→`/df/output`.
- **Status**: labels bound to `/df/status/fps`, `/df/status/source`; LED bound to `/df/status/output`.

### Page 2 — BUILDER (Program / Stages)
- A **GRID** of toggles, `steps` columns × heads rows. Each cell → `/df/grid/cell stage head level`.
- **Steps** stepper→`/df/grid/steps`; **CLEAR**→`/df/grid/clear`; **PLAY**→`/df/pattern 3`.
- Per-cell Lua (derive stage/head from the cell's grid position):
```lua
function onValueChanged(key)
  if key ~= 'x' then return end
  sendOSC({ '/df/grid/cell',
    { {tag='i', value=self.STAGE}, {tag='i', value=self.HEAD}, {tag='f', value=self.values.x} } })
end
```
(For a TouchOSC GRID control, iterate children and set STAGE/HEAD from the child index —
ask Claude Code to generate the exact script once you pick the grid size.)

### Page 3 — LIVE (optional manual)
- A bank of vertical faders per head (add a `/df/live/head i f` handler to the firmware
  for direct per-head control), or just `single` + intensity for spot checks.

## Notes
- Can't ship a binary `.tosc` (authored in the TouchOSC editor) — the schema above is the
  full contract; ask Claude Code for the builder-grid Lua + feedback routing for your grid size.
- Firmware: `osc.cpp` (receiver/feedback, ports 8000/9000), `patterns.cpp` (engine), `PATTERNS.md` (catalog).
- No-firmware alternative: TouchOSC → TouchDesigner → Art-Net → bridge (uses the LIVE path), but needs a host running TD.
