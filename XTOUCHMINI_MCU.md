# Behringer X-Touch Mini — Mackie Control (MCU) Mode Reference

The X-Touch Mini must be in **Mackie Control (MCU) mode** before connecting to
Lockstep. To activate: hold the **MC** button while powering on the device. The
mode is entirely reversible — power cycle without holding MC to return to
standard mode.

## Table 1: Hardware → Computer (MIDI Input)

Messages sent by the device when physical controls are moved.

| Hardware Control | MIDI Ch | Message | Parameter ID | Value |
|---|---|---|---|---|
| **Encoders 1–8 (turn)** | 1 | CC | 16–23 (`0x10`–`0x17`) | Signed-magnitude relative: CW = 1–63, CCW = 65–127 |
| **Encoders 1–8 (push)** | 1 | Note On | 32–39 (`0x20`–`0x27`) | 127 press / 0 release |
| **Buttons 1–8 (physical top row, near encoders)** | 1 | Note On | 40–45 (`0x28`–`0x2D`), 94–95 (`0x5E`–`0x5F`) | 127 press / 0 release |
| **Buttons 9–16 (physical bottom row, far from encoders)** | 1 | Note On | 86–93 (`0x56`–`0x5D`) | 127 press / 0 release |
| **Layer A button** | 1 | Note On | 84 (`0x54`) | 127 press / 0 release |
| **Layer B button** | 1 | Note On | 85 (`0x55`) | 127 press / 0 release |
| **Master fader** | 9 | Pitch Bend | — | 0–16383 (absolute position) |

## Table 2: Computer → Hardware (LED Output)

Messages the application must send to drive device LEDs.

| Hardware Target | MIDI Ch | Message | Parameter ID | Value |
|---|---|---|---|---|
| **Encoders 1–8 (LED ring)** | 1 | CC | 48–55 (`0x30`–`0x37`) | Bit-packed byte — see below |
| **Encoders 1–8 (push LED)** | — | — | — | *Not illuminated (hardware limitation)* |
| **Buttons 1–8 (physical top row)** | 1 | Note On | 40–45, 94–95 (matching input notes) | 127 on / 0 off |
| **Buttons 9–16 (physical bottom row)** | 1 | Note On | 86–93 (matching input notes) | 127 on / 0 off |
| **Layer A button** | 1 | Note On | 84 (`0x54`) | 127 on / 0 off |
| **Layer B button** | 1 | Note On | 85 (`0x55`) | 127 on / 0 off |

## Encoder LED Ring — Bit-Packing

The CC value for each ring is a single byte in the format `0b00pmvvvv`,
constructed as `pattern_mode | position`.

**Pattern modes (`pm`, bits 4–5):**

| Value | Mode | Constant |
|---|---|---|
| `0x00` | Single dot | `0x00 \| position` |
| `0x10` | Boost/cut (centred) | `0x10 \| position` |
| `0x20` | Wrap (fills from left) | `0x20 \| position` |
| `0x30` | Spread (fills from centre) | `0x30 \| position` |

**Position (`vvvv`, bits 0–3):** integer 0–11 (11 LEDs accessible in MCU mode).

Example — single dot at position 6: send CC value `0x00 | 6` = `6`.  
Example — boost/cut at position 9: send CC value `0x10 | 9` = `25`.

## Probe findings (confirmed 2026-06-02)

All values above confirmed by running `controller_probe` against a physical device.
The note assignments are non-sequential — do not assume note N = step N.

**Step grid: physical position → note (input and output use the same note)**

| Physical position | Step index | Note |
|---|---|---|
| Top row (near encoders), button 0 (left) | 0 | 89 |
| Top row, button 1 | 1 | 90 |
| Top row, button 2 | 2 | 40 |
| Top row, button 3 | 3 | 41 |
| Top row, button 4 | 4 | 42 |
| Top row, button 5 | 5 | 43 |
| Top row, button 6 | 6 | 44 |
| Top row, button 7 (right) | 7 | 45 |
| Bottom row, button 8 (left) | 8 | 87 |
| Bottom row, button 9 | 9 | 88 |
| Bottom row, button 10 | 10 | 91 |
| Bottom row, button 11 | 11 | 92 |
| Bottom row, button 12 | 12 | 86 |
| Bottom row, button 13 | 13 | 93 |
| Bottom row, button 14 | 14 | 94 |
| Bottom row, button 15 (right) | 15 | 95 |
| Layer A | — | 84 |
| Layer B | — | 85 |

**Layer A** = note 84, **Layer B** = note 85 (original docs said 46/47 — wrong for this device in MCU mode).

**Fader range:** 0 (bottom) to 16256 (top). Not the full 14-bit 16383.

**Encoder delta range:** up to ±7 per message (accelerated encoder).

**Button LED states — exactly three legal velocities:**

| Velocity | LED state |
|---|---|
| 0 | Off |
| 1 | Flash (rapid blink — confirmed on all 18 buttons) |
| 127 | Solid on |

**Any other velocity value leaves the LED unchanged** (the device ignores it and the
LED stays in whatever state it was in). Only 0, 1, and 127 have any effect.
There is no brightness gradient. `SurfaceCell::level` cannot be expressed on this
device; LED state maps to off / flash / solid from `CellState`.

**Encoder LED ring modes — visual behaviour:**

| Mode | `pm` bits | Visual behaviour |
|---|---|---|
| Single | `0x00` | Single dot at position — tick indicator (unipolar pointer) |
| Boost/cut | `0x10` | Fills from the centre outward to the position — bipolar fill (left of centre = negative, right = positive) |
| Wrap | `0x20` | Fills from the far-left up to the position — standard unipolar LED ring |
| Spread | `0x30` | Fills outward from the centre in both directions to the position — width indicator |

All four modes confirmed working on all 8 rings.
