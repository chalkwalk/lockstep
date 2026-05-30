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
| **Buttons 1–8 (top row)** | 1 | Note On | 86–93 (`0x56`–`0x5D`) | 127 press / 0 release |
| **Buttons 9–16 (bottom row)** | 1 | Note On | MCU transport/function notes (e.g. 91–95) | 127 press / 0 release |
| **Layer A button** | 1 | Note On | 46 (`0x2E`) | 127 press / 0 release |
| **Layer B button** | 1 | Note On | 47 (`0x2F`) | 127 press / 0 release |
| **Master fader** | 9 | Pitch Bend | — | 0–16383 (absolute position) |

## Table 2: Computer → Hardware (LED Output)

Messages the application must send to drive device LEDs.

| Hardware Target | MIDI Ch | Message | Parameter ID | Value |
|---|---|---|---|---|
| **Encoders 1–8 (LED ring)** | 1 | CC | 48–55 (`0x30`–`0x37`) | Bit-packed byte — see below |
| **Encoders 1–8 (push LED)** | — | — | — | *Not illuminated (hardware limitation)* |
| **Buttons 1–8 (top row)** | 1 | Note On | 86–93 (`0x56`–`0x5D`) | 127 on / 0 off |
| **Buttons 9–16 (bottom row)** | 1 | Note On | Matching input note | 127 on / 0 off |
| **Layer A button** | 1 | Note On | 46 (`0x2E`) | 127 on / 0 off |
| **Layer B button** | 1 | Note On | 47 (`0x2F`) | 127 on / 0 off |

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
