# Ableton Push 1 — MIDI Control Reference

The original **Ableton Push (Push 1)** is a class-compliant USB-MIDI device.
This document describes how to read every control and drive every LED, display
and touch strip on the device when driving it directly from an application
(i.e. *not* through Ableton Live).

> **Provenance.** The X-Touch reference (`XTOUCHMINI_MCU.md`) was confirmed by a
> physical `controller_probe` pass. This document is instead *derived from two
> reference implementations* and cross-checked between them:
> - Ableton's own `Push` / `pushbase` MIDI Remote Scripts
>   (`pushbase/elements.py`, `Push/sysex.py`) — the authoritative source for note
>   numbers, CC numbers and display SysEx.
> - `DrivenByMoss` (`PushControlSurface.java`, `PushColorManager.java`) — the
>   authoritative source for LED colour value tables.
>
> Everything below agrees between both sources, but the values have **not yet
> been confirmed against a physical unit**. A `tools/push_probe` pass is
> recommended before relying on it (see *Pending verification* at the end).

## Ports and modes

> **Port and mode are paired, not independent** (confirmed on hardware
> 2026-06-03 with `push_probe`). Earlier drafts of this doc got this wrong twice;
> this is the corrected, hardware-verified account.

### The matched-pair rule

Push 1 exposes **two USB-MIDI ports**, and has two firmware modes (toggled by the
**User** button CC 59, or set via the `MODE_CHANGE` SysEx). **The mode must match
the port you are connected through:**

| Port | Name | Matching mode (`MODE_CHANGE` byte) |
|---|---|---|
| Port 1 | "Ableton Push" (Live Port) | **Live mode** (`0`) |
| Port 2 | "Ableton Push User Port" / `MIDIIN2 (… Ableton Push)` | **User mode** (`1`) |

Within a matched pair the device is a **host-driven "dumb surface"** — the host
owns every LED and the display, and the controls just send raw MIDI (exactly like
the X-Touch in MCU mode). **Neither mode is "autonomous" in practice** — the names
are Ableton's and don't describe the behaviour we see; a matched pair behaves
identically as a dumb surface.

Observed symptom of a **mismatch**: the CC-addressed button LEDs still respond,
but the **pads** (note-addressed) go dark and stop lighting. So if pads aren't
lighting, the port/mode are mismatched.

```
MODE_CHANGE:  F0 47 7F 15 62 00 01 <mode> F7      0 = Live, 1 = User
```

(`LIVE_MODE = 0` / `USER_MODE = 1`, confirmed from `pushbase/sysex.py` and on
hardware.)

### Which pair Lockstep uses

**User Port (port 2) + User mode (`1`).** This is DrivenByMoss's port choice for
Push 1, it doesn't clash with Ableton Live (which claims the Live Port), and the
User Port already defaults to User mode so it works out of the box. (The Live
Port + Live mode pair is equally valid as a surface if the Live Port is free.)
Send `MODE_CHANGE 1` on connect as insurance against a device left mismatched by
a previous session.

### No handshake required

Push 1's full "handshake" (`Push/handshake_component.py`) ends with a proprietary
**dongle challenge/response** in which *Live* encrypts two random numbers with an
Ableton-private key and verifies the device's reply. That step is **Live
authenticating the device**, not the device gating its own function — so it is
**not required** to use Push 1 as a surface, and it is not replicable without
Ableton's key. Evidence: DrivenByMoss drives Push 1 as a complete surface on
Bitwig and its entire Push-1 startup is *(set pressure mode → send a Device
Inquiry → start driving LEDs)*. Lockstep follows the same recipe:

1. (optional) **Device Inquiry** `F0 7E 00 06 01 F7` → confirms a Push 1 and
   returns the firmware version.
2. (optional) **Pressure mode** `F0 47 7F 15 5C 00 01 <0=poly|1=mono> F7`.
3. Send the **`MODE_CHANGE` byte matching your port** (User Port → `1`) and drive
   the surface directly.

All note/CC messages below are sent and received on **MIDI channel 1**
(status nibble `0`) unless noted. The SysEx header for every Push 1 message is:

```
F0 47 7F 15 <command> ... F7
```

---

## Table 1: Hardware → Computer (MIDI Input)

Messages the device sends when a control is touched/moved/pressed.

| Hardware Control | Message | Parameter ID | Value |
|---|---|---|---|
| **Pad grid (8×8)** | Note On | 36–99 | Velocity 1–127 (pressure-sensitive); 0 = release. Polyphonic aftertouch also emitted while held |
| **Encoders 1–8 (turn)** | CC | 71–78 (`0x47`–`0x4E`) | Relative, two's-complement: `1`…`63` = CW (+1…+63), `127`…`64` = CCW (−1…−64) |
| **Master volume encoder (turn)** | CC | 79 (`0x4F`) | Relative two's-complement (as above) |
| **Tempo encoder (turn)** | CC | 14 (`0x0E`) | Relative two's-complement |
| **Swing encoder (turn)** | CC | 15 (`0x0F`) | Relative two's-complement |
| **Encoders 1–8 (touch)** | Note On | 0–7 (`0x00`–`0x07`) | 127 touch / 0 release |
| **Master volume encoder (touch)** | Note On | 8 (`0x08`) | 127 / 0 |
| **Swing encoder (touch)** | Note On | 9 (`0x09`) | 127 / 0 |
| **Tempo encoder (touch)** | Note On | 10 (`0x0A`) | 127 / 0 |
| **Touch strip (slide)** | Pitch Bend | — | 0–16383, absolute 14-bit position |
| **Touch strip (touch)** | Note On | 12 (`0x0C`) | 127 touch / 0 release |
| **Upper display buttons 1–8** | CC | 20–27 (`0x14`–`0x1B`) | 127 press / 0 release |
| **Lower display buttons 1–8** | CC | 102–109 (`0x66`–`0x6D`) | 127 press / 0 release |
| **Scene / side buttons (top→bottom)** | CC | 43→36 (`0x2B`→`0x24`) | 127 press / 0 release |
| **All function buttons** | CC | see Table 3 | 127 press / 0 release |
| **Footswitch (tip / ring)** | CC | 64 / 69 | 127 / 0 (or continuous, pedal-dependent) |

> **Note number collision.** Scene buttons are **CC** 36–43 and the bottom row of
> the pad grid is **Note** 36–43. They never collide because they use different
> MIDI status bytes (CC `0xB0` vs Note `0x90`). Always branch on message type
> first.

### Pad grid note layout

Pads are laid out bottom-left → top-right, ascending by 1 along each row and by 8
between rows:

```
row (top)   92 93 94 95 96 97 98 99
            84 85 86 87 88 89 90 91
            76 77 78 79 80 81 82 83
            68 69 70 71 72 73 74 75
            60 61 62 63 64 65 66 67
            52 53 54 55 56 57 58 59
            44 45 46 47 48 49 50 51
row (bot)   36 37 38 39 40 41 42 43
            └ note = 36 + (rowFromBottom * 8) + column
```

---

## Table 2: Computer → Hardware (LED / Display Output)

| Hardware Target | Message | Parameter ID | Value |
|---|---|---|---|
| **Pads (8×8)** | Note On | 36–99 | Colour palette index 0–127 (see *Pad / RGB palette*) |
| **Lower display buttons 1–8** | CC | 102–109 | Colour palette index 0–127 (full RGB, same palette as pads) |
| **Upper display buttons 1–8** | CC | 20–27 | Bi-colour value 0–24 (see *Bi-colour table*) |
| **Scene / side buttons** | CC | 36–43 | Bi-colour value 0–24 (see *Bi-colour table*) |
| **Function buttons (white)** | CC | see Table 3 | 0 off / 1 dim / 4 bright (+ blink, see below) |
| **Touch strip indicator** | Pitch Bend | — | 0–16383 position; lit per CUSTOM mode (reliable — see *Touch strip*) |
| **Touch strip LEDs (24 segments)** | SysEx `0x64` | — | Per-segment; **flaky on Push 1** — prefer pitch-bend above |
| **Display lines 1–4** | SysEx `0x18`–`0x1B` | — | 68 ASCII chars/line (see *Display*) |
| **Encoders (turn/push)** | — | — | *No LEDs — Push 1 encoders are unlit* |

---

## Table 3: Function buttons (CC, channel 1)

All are momentary (127 press / 0 release in, single white LED out unless flagged).
Labels are the physical silkscreen; the parenthetical is the source-code name.

| CC | Button | CC | Button |
|---|---|---|---|
| 3 | Tap Tempo | 59 | **User** (Live/User mode toggle) |
| 9 | Metronome | 60 | Mute |
| 28 | Master | 61 | Solo |
| 29 | Stop Clip (Track Stop) | 62 | In (device ◄) |
| 44 | ◄ (Left) | 63 | Out (device ►) |
| 45 | ► (Right) | 85 | Play |
| 46 | ▲ (Up) | 86 | Record |
| 47 | ▼ (Down) | 87 | New |
| 48 | Select | 88 | Duplicate |
| 49 | Shift | 89 | Automation |
| 50 | Note | 90 | Fixed Length |
| 51 | Session | 110 | Device |
| 52 | Add Device | 111 | Browse |
| 53 | Add Track | 112 | Track (single-track mix) |
| 54 | Octave Down | 113 | Clip |
| 55 | Octave Up | 114 | Volume |
| 56 | Repeat | 115 | Pan & Send |
| 57 | Accent | 116 | Quantize |
| 58 | Scales | 117 | Double |
| | | 118 | Delete |
| | | 119 | Undo |

The colour-capable rows (upper 20–27, lower 102–109, scene 36–43) are **not** in
this table — see Table 2.

---

## Pad / lower-button RGB palette

Pads (Note On 36–99) and the **lower** display buttons (CC 102–109) take a colour
**index 0–127** into a fixed 128-entry palette baked into Push 1 firmware. Index
`0` = off/black. The first 60 entries are the commonly-used named colours:

| Idx | Colour | Idx | Colour | Idx | Colour |
|---|---|---|---|---|---|
| 0 | Black (off) | 20 | Lime-green | 40 | Sky-ocean |
| 1 | Grey low | 21 | Green hi | 41 | Ocean hi |
| 2 | Grey light | 22 | Green | 42 | Ocean |
| 3 | White | 23 | Green low | 43 | Ocean low |
| 4 | Rose | 24 | Green-spring | 44 | Ocean-blue |
| 5 | Red hi | 25 | Spring hi | 45 | Blue hi |
| 6 | Red | 26 | Spring | 46 | Blue |
| 7 | Red low | 27 | Spring low | 47 | Blue low |
| 8 | Red-amber | 28 | Spring-turquoise | 48 | Blue-orchid |
| 9 | Amber hi | 29 | Turquoise low | 49 | Orchid hi |
| 10 | Amber | 30 | Turquoise | 50 | Orchid |
| 11 | Amber low | 31 | Turquoise hi | 51 | Orchid low |
| 12 | Amber-yellow | 32 | Turquoise-cyan | 52 | Orchid-magenta |
| 13 | Yellow hi | 33 | Cyan hi | 53 | Magenta hi |
| 14 | Yellow | 34 | Cyan | 54 | Magenta |
| 15 | Yellow low | 35 | Cyan low | 55 | Magenta low |
| 16 | Yellow-lime | 36 | Cyan-sky | 56 | Magenta-pink |
| 17 | Lime hi | 37 | Sky hi | 57 | Pink hi |
| 18 | Lime | 38 | Sky | 58 | Pink |
| 19 | Lime low | 39 | Sky low | 59 | Pink low |

Also useful: `103` = grey medium. Indices 60–127 hold additional / DAW-track
colours (full RGB table is in `DrivenByMoss` `PushColorManager.DEFAULT_PALETTE`).
The palette is **fixed** — Push 1 (unlike Push 2) cannot be sent arbitrary RGB
per pad; you choose the nearest palette index.

### Colouring the pads — static semantic table (not runtime matching)

The runtime does **not** match colours per frame. `Push1Surface::rgbPaletteFor`
is a **static table**: each cell's semantic `CellState` (grid families) or
`button` + state (modifiers / verbs / sections / nav) maps to a literal,
hand-picked palette index. Pad indices are fixed in firmware, so once chosen they
need no palette data at runtime.

**Why static, not nearest-neighbour.** The earlier dynamic path matched each
cell's on-screen RGB to the nearest palette entry in Oklab. The factory palette
has few usable *dim* entries, so distinct-but-similar UI colours collapsed onto
near-identical pads (worst in the low-value and high-value/low-saturation
regions). A static table lets every important state claim a **distinct bold**
device entry and — crucially — lets you tweak one role without globally shifting
the others.

**How the indices were chosen (offline authoring).** `DEFAULT_PALETTE`'s RGB are
*nominal* — what the firmware is told, not what the LED visibly emits — so the
indices are picked against the palette's **as-displayed** colours:

1. `tools/push_probe` (User Port) → **Palette 0-63** / **Palette 64-127** light
   each page; photograph both roughly top-down.
2. `tools/palette_capture` → mark the 4 pad-array corners (homography), tune the
   pad/gap + sample-square overlay, **Sample** (linear-light average per pad),
   **Export** `Push1Palette.h` (`kCapturedPalette[128]`, index 0 = off).
3. `tools/color_audit` resolves curated ideal anchors (bold/dim per hue +
   neutrals) to their nearest captured index in Oklab, and checks the picks are
   mutually distinct within each view (≈0.09 ΔE floor). Those literal indices are
   then written into the `pidx` table in `Push1Surface.cpp`.

`kCapturedPalette` (`Push1Palette.h`) is therefore an **offline reference** for
re-tuning indices, not a runtime input. Assignment is top-down: boldest/most
important states (selected=white 119, trig=green 21, playhead=amber 9,
muted=red 5) → highest-chroma entries; resting/secondary states → dark/dim
variants; states that never co-occur may reuse an entry. Modifier *active* = bold
hue, *resting* = dark hue. Mute Muted (bold red) vs Audible (dim grey) is now
unmistakable — the case the dynamic match could not separate. To change one pad
colour, edit that one role in `pidx`; nothing else moves.

---

## Bi-colour table (upper display + scene buttons)

The **upper** display buttons (CC 20–27) and the **scene/side** buttons
(CC 36–43) are red/green bi-colour LEDs. They take a value 0–24 selecting hue +
brightness + blink:

| Value | Meaning | Value | Meaning |
|---|---|---|---|
| 0 | Off | 13 | Yellow low |
| 1 | Red low | 14 | Yellow low, slow blink |
| 2 | Red low, slow blink | 15 | Yellow low, fast blink |
| 3 | Red low, fast blink | 16 | Yellow hi |
| 4 | Red hi | 17 | Yellow hi, slow blink |
| 5 | Red hi, slow blink | 18 | Yellow hi, fast blink |
| 6 | Red hi, fast blink | 19 | Green low |
| 7 | Orange low | 20 | Green low, slow blink |
| 8 | Orange low, slow blink | 21 | Green low, fast blink |
| 9 | Orange low, fast blink | 22 | Green hi |
| 10 | Orange hi | 23 | Green hi, slow blink |
| 11 | Orange hi, slow blink | 24 | Green hi, fast blink |
| 12 | Orange hi, fast blink | | |

Pattern: base hue every 6 (`1` red, `7` orange, `13` yellow, `19` green); `+0`/`+3`
= low/hi brightness; `+1`/`+2` = slow/fast blink of the low variant; `+4`/`+5` =
slow/fast blink of the hi variant.

---

## White function-button LEDs

The single-colour (white) function buttons interpret their CC value as
brightness/blink, following the same low-half of the bi-colour scale:

| Value | State |
|---|---|
| 0 | Off |
| 1 | Dim |
| 2 | Dim, slow blink |
| 3 | Dim, fast blink |
| 4 | Bright |
| 5 | Bright, slow blink |
| 6 | Bright, fast blink |

For Lockstep's purposes the load-bearing values are **0 / 1 / 4** (off / dim /
bright); blink codes `2 3 5 6` are available where a flashing state is wanted.

---

## Touch strip — mode, input and LEDs

The strip's **mode governs everything**: its input message, its feel, and *who
owns its LEDs*. Set it first, with command `0x63`:

```
F0 47 7F 15 63 00 01 <mode> F7
```

| `<mode>` | Name | LED owner | Input / feel |
|---|---|---|---|
| 0 | CUSTOM_PITCHBEND | **host** | absolute 14-bit position; host paints LEDs |
| 1 | CUSTOM_VOLUME | **host** | absolute position; host paints LEDs (fader-style) |
| 2 | CUSTOM_PAN | **host** | absolute position; host paints LEDs |
| 3 | CUSTOM_DISCRETE | **host** | stepped; host paints LEDs |
| 4 | CUSTOM_FREE | **host** | absolute position; host fully owns LEDs |
| 5 | PITCHBEND *(power-on default)* | device | pitch bend, springs to centre, self-lit |
| 6 | VOLUME | device | CC 7, stays put, self-lit bottom-up |
| 7 | PAN | device | CC 10, self-lit from centre |
| 8 | DISCRETE | device | stepped, self-lit |
| 9 | MODWHEEL | device | CC 1, stays put, self-lit bottom-up |

(Values confirmed from `pushbase/touch_strip_element.py`; `state_count = 24`.)

At power-on the strip is in **PITCHBEND (5)** — it springs to centre, sends 14-bit
**pitch bend**, and lights itself.

### Driving the strip LEDs — use the pitch-bend method (Push 1)

There are two ways to light the strip, and on Push 1 they are **not** equally
reliable (confirmed on hardware 2026-06-03):

1. **Pitch-bend value → device (recommended).** Set a **CUSTOM mode (0–3)**, then
   send a **14-bit pitch-bend message *to* the device**; it renders a lit
   indicator at that position, styled by the mode (DISCRETE = stepped, PAN = from
   centre, VOLUME = from bottom, PITCHBEND = from centre). This is exactly what
   DrivenByMoss does for Push 1 (`setRibbonMode` 0–3 + `setRibbonValue` →
   `sendPitchbend`), and it is **reliable**. The strip's finger position also
   arrives as 14-bit pitch-bend **input**, so the same value domain is used both
   ways.

2. **`0x64` per-segment write (avoid on Push 1).** The host paints all 24
   segments directly. This is the Push-2-era path; on **Push 1 it is flaky** —
   it only works intermittently and appears sensitive to mode-change timing
   (a mode switch immediately followed by a `0x64` write is often dropped). Push 1
   never needs it; prefer method 1.

**Recommended for Lockstep's fader:** **CUSTOM_DISCRETE (3)** (or CUSTOM_PAN (2)),
driven by pitch-bend value. DISCRETE suits stepped parameters; both stay where
you leave them and give clean, reliable LED feedback without `0x64`.

### `0x64` segment format (reference only)

24 addressable segments in one message:

```
F0 47 7F 15 64 00 08 <b0> <b1> <b2> <b3> <b4> <b5> <b6> <b7> F7
```

Per-segment 2-bit value: `0` = off, `1` = half, `3` = full. Within each byte,
segment *k* of the group occupies bits `2k`…`2k+1` (segment 0 = least
significant); 3 segments per byte × 8 bytes = 24. (Flaky on Push 1 — see above.)

---

## Display (4 × 68 characters)

Four monochrome text lines, each holding **68 ASCII characters**, written one
line at a time:

```
Write line N:   F0 47 7F 15  <18..1B>  00 45 00  <68 ASCII bytes>  F7
Clear line N:   F0 47 7F 15  <1C..1F>  00 00     F7
```

| Line | Write cmd | Clear cmd |
|---|---|---|
| 1 (top)   | `0x18` | `0x1C` |
| 2         | `0x19` | `0x1D` |
| 3         | `0x1A` | `0x1E` |
| 4 (bottom)| `0x1B` | `0x1F` |

(`0x45` = 69 is the firmware's framing byte; supply exactly 68 character bytes.)
Each line is visually divided into **four 17-character blocks**, one above each
pair of encoders. A block is `[enc:8][gap:1][enc:8]`: the 1-char gap sits in the
*middle* of each block (mirroring the physical gap between the two encoders under
it); there is **no** extra space between adjacent blocks. So the full 68-char
line is `8 + 1 + 8 + 8 + 1 + 8 + 8 + 1 + 8 + 8 + 1 + 8`, giving each encoder its
own **8-wide column** across all four lines (an 8×4 character cell per encoder).

### Lockstep's per-encoder layout

Each encoder's 8×4 cell shows, top to bottom:

| Line | Content |
|---|---|
| 1 (`0x18`) | **value bar** — a horizontal fill/graph of the parameter |
| 2 (`0x19`) | value text (e.g. `12.5ms`) |
| 3 (`0x1A`) | owning **section** name (e.g. `FILTER`) |
| 4 (`0x1B`) | parameter name (e.g. `CUTOFF`) |

The value bar is drawn with Push 1's built-in **block-bar glyphs** — control
codes `\x03`–`\x06` (ascending partial fills, full at `\x06`), the same set
pushbase uses for `GRAPH_VOL`/`GRAPH_PAN`/`GRAPH_SIN`. Bipolar params fill from
the centre; stepped/enum params draw a single caret. (Confirm the glyphs render
via push_probe **Glyph/Bars**; the surface has an ASCII-bar fallback flag.)

Display brightness via SysEx `0x08` is **unverified** and likely absent on Push 1
(see the no-brightness note under *Other useful SysEx*).

---

## Other useful SysEx

| Purpose | Command | Form |
|---|---|---|
| Identity request (reply confirms Push 1) | `0x06` | standard `F0 7E 00 06 01 F7`; reply prefix `F0 7E 00 06 02 47 15 00 19 …` |
| Welcome / Goodbye logo | `0x01` | `… 01 01 F7` (welcome) / `… 01 00 F7` (goodbye) |
| Aftertouch mode (poly/channel) | `0x5C` | `… 5C 00 01 <0=poly|1=mono> F7` |
| Pad sensitivity / velocity curve | `0x5D` | `… 5D 00 20 <8-byte threshold> <24-byte curve> F7` |

The header `F0 47 7F 15` is Akai/Ableton's Push-1 manufacturer block; all of the
above are Push-1-specific and are ignored by Push 2/3 (which use a different
header `F0 00 21 1D 01 01 …`).

> **No host-settable brightness on Push 1.** Earlier drafts listed an "LED
> brightness `0x06`" and a "display brightness `0x08`" command — these were
> wrong. `0x06` is the universal Identity/Device-Inquiry command (see above), not
> a Push brightness command. Cross-checked against both reference drivers
> (DrivenByMoss `PushControlSurface`/`PushColorManager` and Ableton's
> `Push`/`pushbase` scripts): **neither sends any LED- or display-brightness SysEx
> for Push 1.** A global LED-brightness command exists only on Push 2/3 (different
> header). On Push 1, **the palette index *is* the brightness** — a pad/button
> takes a single colour index and there is no separate brightness or per-pad
> intensity channel. To make a pad brighter you choose a brighter palette entry;
> the surface relies on accurate nearest-colour matching (see *Pad / RGB
> palette*) rather than any intensity control.

---

## Mapping notes for Lockstep's 10×4 surface

This is reference only (the controller-integration cycle is separate), but to
record the intent discussed:

- **Modifier cluster (2×4)** → top-left 2×4 of the 8×8 pad grid (notes
  `92 93 / 84 85 / 76 77 / 68 69`, i.e. the leftmost two columns of the top four
  rows). RGB pads can carry the scope-colour grammar directly.
- **Remaining 8×4 surface** → the lower four pad rows (notes 36–67, four rows of
  eight). Full RGB for `CellState` colours.
- **Fader** → touch strip in **CUSTOM_DISCRETE (3)** (or CUSTOM_PAN): 14-bit
  pitch-bend in for the finger position, and the lit indicator driven by sending
  14-bit pitch-bend back out (the reliable Push 1 method — not `0x64`).
- **8 parameter encoders + display** → the 8 endless encoders (CC 71–78) with the
  4-line display annotating name/value per encoder — a natural fit for the
  `ManipulationZone` 8-param band, though Push has one display row set vs the 4×2
  band (two encoder *pushes*/pages may be needed).
- **Convenience bindings** → the dedicated transport/function buttons map onto
  Lockstep verbs/modifiers as secondary aliases (lit whenever they have a live
  function), but per the project rule the whole surface must remain operable from
  the 8×8 grid + encoders + touch strip alone. Currently bound (`kMonoButtons`):

  | CC | Button | → Lockstep |
  |---|---|---|
  | 85 / 86 | Play / Record | VerbPlay / VerbRecord |
  | 44–47 | ◄►▲▼ | Nav Left/Right/Up/Down |
  | 54 / 55 | Octave Down / Up | NavLeft / NavRight (= octave shift in note/chromatic) |
  | 3 / 9 | Tap / Metronome | TapTempo / MetronomeToggle |
  | 87 / 118 / 119 | New / Delete / Undo | VerbClear / VerbDelete / VerbNo |
  | 49 | Shift | Func |
  | 60 | Mute | MuteScope (hold-to-mute, = grid key Z) |
  | 29 | Stop Clip | StopReset (stop + reset to top) |

  Deliberately **unbound** (no clean 1:1 to a live action; would need new
  plumbing or violate the no-bespoke-button rule): Master 28, Solo 61, Note 50 /
  Session 51 (track input-mode cycle is a `Track`+Nav *compound*, not a single
  button), Duplicate 88. Revisit if a generic controller-affordance seam is added.

The 8×8 + bi-colour-flanks + white-function-buttons split means Lockstep's
`SurfaceModel` → device renderer must pick **per-target colour spaces**: full
palette for pads & lower buttons, the 0–24 bi-colour scale for upper/scene
buttons, and 0/1/4(+blink) for white buttons. This is exactly the kind of
divergence the `CellState`-token + colour-fallback design (DESIGN §35.8) is meant
to absorb.

---

## Probe findings (first pass, 2026-06-03 — `tools/push_probe`)

Confirmed working on a physical unit:

- **Port/mode pairing** — see *Ports and modes*. Pads only light when the mode
  matches the port; this is now the documented rule.
- **Lower-row RGB** (CC 102–109), **bi-colour** (upper 20–27 + scenes 36–43),
  **mono** function buttons, and **per-button** input all respond as documented.
- **Display** writes (4 lines) work and show readable text.
- **Touch strip** input is **pitch bend** in the default mode, and the `0x64` LED
  writes are ignored until a CUSTOM strip mode is set — matching the model now in
  *Touch strip — mode, input and LEDs*.

Still to confirm on a focused pass (the probe has a control for each):

1. Pad bottom-left origin (expected note 36) and row/col order via *Pad Ident*.
2. Encoder relative direction and per-tick magnitude (accelerated?).
3. Exact on-device colours for the pad/lower palette 0–127 (eyeball *Pad
   Palette*) and that the bi-colour 0–24 table matches.
4. White-button blink codes (`2 3 5 6`) actually blink.
5. Which strip mode + lighting method gives a clean host-painted bottom-up fader
   (*Strip LED fill* in a CUSTOM mode vs *Strip value (pitch-bend)*).
6. Display block layout: does the *Ruler* render as four clean 17-char runs
   (linear buffer) or scrambled (column reordering)?
7. *As-displayed* palette colours via **Palette 0-63** / **Palette 64-127** (one
   photo each; see *Capturing the as-displayed palette*).
8. Display bar glyphs via **Glyph/Bars**: which low control codes (`\x03`–`\x06`
   per pushbase) render as partial-block bar segments vs the ASCII fallback.
9. Pad animation via **Blink/Pulse**: which MIDI channel(s) make a pad fade
   (pulse) vs hard-toggle (blink) between its ch-1 colour and a 2nd-channel
   colour, the rate per channel, and how **Stop anim** cancels it. (pushbase:
   Pulse on ch 6–10, Blink on ch 11–15, 0-based; DrivenByMoss uses ch 10/14 —
   the 0/1-based convention is what this confirms.)

---

## Fill + border collapse — single-colour resolution for the Push

**Status: input spec for the next plan (the static semantic→palette-index
table). No Push code change here.**

On screen a cell signals with a *body fill* **and** independent decoration
channels (`border` / `dot` / `strip` / `pip`, see `SurfaceCell`). A Push 1 pad
has exactly one colour and no border. So every place the screen uses a decoration
to carry meaning the fill does not, the Push must fold the combined state into a
**single** index. The forthcoming static table is authored against the precedence
and folds below; states already settled on screen by the Stage A2 contrast pass
(`tools/color_audit`) keep their distinctness once collapsed.

### Step cells — precedence for the one pad colour (high → low)
1. **Physical press** → white. Momentary touch feedback, always wins.
2. **Held-for-edit** (`CellState::StepHeld`) → white. Beats playhead (an active
   user hold is more important than the transient cursor). *Current Push code
   already orders press/held white above playhead amber — keep.*
3. **Playhead** (screen: amber `border` over the body) → amber. Unmistakable,
   transient.
4. **Body trig state** (no press/held/playhead): Empty / TrigCertain /
   TrigProbable (brightness-dimmed) / TrigSuppressed / FillAdd / FillSuppress /
   OutOfRange → the body colour. These are the Stage-A2-separated fills.

Decorations with no border/dot/strip on the Push — **open folds for the table
plan to decide** (each is a secondary signal layered on a step that already has a
body state):
- **P-Lock present** (screen: violet `dot`): show a distinct "trig-with-P-Lock"
  pad colour, or ignore the dot and show the plain trig? (Leaning: ignore on
  Push — automation presence is not performance-critical; the screen keeps it.)
- **Fill marker** (screen: orange/blue `strip` when fill mode held): the body
  already becomes FillAdd/FillSuppress, so the strip is largely redundant — fold
  to body.
- **Fill P-Lock** (cyan `strip`): same treatment as P-Lock dot.
- **Latch / virtual-hold** (screen: grey `pip`): a latched step behaves as held →
  resolve to the held colour, or ignore. (Leaning: ignore on Push.)

### Key cells (modifiers / verbs / sections / nav)
- **ModeActive** (screen: 2 px accent `border`): the *fill* already switches to
  the active colour, so the border is redundant — Push uses the active fill. No
  fold needed.
- **Compound-chord armed** (screen: amber `strip`): minor; fold to the key's
  active fill or ignore. (Leaning: ignore on Push.)
- **Latched modifier** (screen: scope-coloured `pip`): a latched modifier is
  effectively held → resolve to the modifier's **active** colour.
- **Master-active section** (screen: golden fill `0xFF404010` + golden accent):
  give it a dedicated golden pad index.

### Selector cells
- **SelectorHome** (screen: amber `kHomeAmber` `border` over the phrase body):
  the home/global phrase. Fold to a dedicated amber-tinted index, or amber-flood
  the home cell. (Leaning: dedicated index.)
- **SelectorDeviated** (screen: phrase-family border badge): a track playing off
  its home phrase. Fold to a distinct index or ignore on the pad grid.

### Not a contrast problem — a collapse problem
Mute **Muted vs Audible** read 0.284 ΔE apart on screen (fine) yet looked alike
on the Push: red `kScopeMute` and dark-slate `kStepInactive` both land near the
factory palette's clumped low region under nearest-neighbour. The static table
fixes this directly — Muted → a **bold** red index, Audible → a clearly **dim /
neutral** index — rather than touching the (already-distinct) screen colours.

---

## Deferred: palette reprogramming + calibration (RGB-reprogrammable controllers)

**Status: indefinitely deferred — not implemented, and not applicable to Push 1.**
Captured here so the design isn't lost if we later target a controller that *can*
be sent arbitrary RGB (e.g. **Push 2/3**). Push 1's palette is treated as fixed
(see *Pad / lower-button RGB palette*); the runtime matches against it in Oklab
rather than reprogramming it.

The richer scheme — useful only on a reprogrammable device — was: reprogram the
device palette to a perceptually-optimal set with per-unit colour + brightness
correction, so the software keeps working in plain RGB and renders faithfully.

1. **Palette-write SysEx.** Confirmed on **Push 2/3** only (header
   `F0 00 21 1D 01 01`): write one entry with cmd `03` —
   `… 01 01 03 <idx> <r_lo> <r_hi> <g_lo> <g_hi> <b_lo> <b_hi> <w_lo> <w_hi> F7`
   (each channel 14-bit, 7-low/7-high; includes a white-balance channel) — then
   apply with `… 01 01 05 F7`. Push 2 is even firmware-gated (only re-uploads on
   firmware < 1.0.63). Push 1 (header `F0 47 7F 15`) has **no** colour-upload
   command in either reference driver; a `push_probe` write-test (try the Push-2
   form + Push-1-header `F0 47 7F 15 <cmd> <idx> <r> <g> <b> F7` variants) would
   settle any lingering doubt before pursuing this.

2. **Per-device calibration** (offline, in a probe tool):
   - *White balance* — per brightness level, three independent ± passes (R, then
     G, then B), narrowing over ~3 rounds, to find the device triple that reads as
     neutral grey.
   - *Brightness linearity* — three per-channel gamma curves (encoders on the
     R/G/B gradient columns; W/C/M/Y columns as live cross-checks) tuned until each
     gradient is perceptually linear.
   - Combine into **three per-channel LUTs** `deviceVal_c = LUT_c(intensity)`
     (16–32 samples, interpolated) capturing both gamma and white balance.

3. **Oklab k-means palette generator** (`tools/palette_gen`, C++): build an sRGB
   grid (5/6/5-bit), map to Oklab, k-means to 128 with **anchors pinned**
   (black = off, white, R/G/B, C/M/Y, greys 25/50/75%), drop black → emit a
   127-entry palette constant. Anchors keep extremal colours exact (k-means alone
   never picks them).

4. **Connect-time upload** (once, never per frame — palette writes are ~0.6 ms/LED
   vs ~0.2 ms to recolour, so per-frame reprogramming blows the ~20 ms realtime
   budget): for each entry, Oklab → linear sRGB → per-channel LUT → device 7/14-bit
   RGB → palette-write SysEx, then apply.

Also deferred (separate cycle): **MIDI-channel colour layering** (a pad's colour
on a higher channel overrides ch 1 until note-off — base/overlay layers) and the
**flash/pulse** animation channels (pulse ch 6–10 fade, blink ch 11–15 toggle),
which need a `SurfaceModel` layer design.
