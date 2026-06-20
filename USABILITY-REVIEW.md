# Lockstep — Usability Review (deep-dive evidence)

> **Status:** working review, 2026-06-19. This is the *evidence* document for a
> goal-sharpening pass on `PRINCIPLES.md` / `NON-GOALS.md` / `DESIGN.md`. It
> audits every user-facing feature on **frequency × value/stakes × access-cost**
> plus a **visual-grammar (dual-target)** axis, names the constitutional tensions
> it found, and records the design decisions taken to resolve them.
>
> It exists because the surface and grammar grew over phases 1–9, and the worry
> was that *what sits on a cheap rung vs. a deep chord is an accident of
> implementation order, not an optimal allocation*. The audit confirmed that
> worry in at least one concrete case (the phantom Accent generator, §6) and
> surfaced a clean set of fixable drifts.
>
> Read order: **`PRINCIPLES.md` → `DESIGN.md` → `ROADMAP.md`**. This review feeds
> the first of those. Citations `§N` = PRINCIPLES; `fence #N` = NON-GOALS.

---

## 1. North Star

Distilled from the project owner's framing, as the single sentence the 18
principles serve:

> **Lockstep is a performance-first step sequencer you _play_, not configure:
> one small scope + verb grammar, learned by hand and read by colour, that turns
> practice into live expression. Every key earns its place in that grammar —
> nothing is a single-purpose button — and the gestures that edit are the
> gestures that perform. Efficient, opinionated, grammatically _and visually_
> consistent; equally a standalone instrument and a DAW plugin.**

Three load-bearing clauses for this review:
- *"learned by hand and read by colour"* — muscle memory + the colour grammar
  must let a fluent player work **without looking at the screen** (§4 yardstick).
  This is why the visual grammar is a first-class axis here, not an afterthought.
- *"every key earns its place — nothing is a single-purpose button"* — the
  direct test that flagged the tap-tempo key (§4 vs §10 contradiction, T1).
- *"turns practice into live expression"* — the §14 *reward-mastery* test, the
  lens for the retrig necessity analysis (§7).

---

## 2. Method

- **Axes.** Each feature scored on **Frequency** (how often in a live set: H/M/L),
  **Value/Stakes** (how much it matters when used, incl. safety-critical weight:
  H/M/L), and **Access rung** (§15 ladder, 1–6). A *mismatch* is a high
  frequency×value feature on an expensive rung, or a low one on a cheap rung.
- **Visual-grammar axis (dual-target).** For each meaningful state: is it a
  `CellState` token (SSOT) that resolves to **both** screen (text + colour +
  chrome) and hardware (RGB-LED colour + brightness + blink)? Does its
  *performable subset* survive when text is gone (the hardware downgrade)?
- **Lens.** Balanced (expert-speed, discoverability, feature-value weighed
  roughly equally).
- **Scope.** Gesture grammar, the parameter surface (Manipulation Zone), and the
  visual grammar.

---

## 3. Tension catalogue (status)

| # | Tension | Status |
|---|---|---|
| **T1** | §4 forbids single-purpose buttons; §10 blesses tap-tempo/metronome as "ambient utilities" — `3` and `Func+3` are the only truly single-purpose keys. | **Resolved** → `3` = generator hub on hold, tap-tempo on tap (§5 below); metronome → TIME overlay. §4/§10 wording to be reconciled. |
| **T2** | README still advertises the removed Accent generator as shipped; `Func+Fill` freed but documented as bound. | **Fixed** (README purged, 2026-06-19). |
| **T3** | Generator entry is ad-hoc (Euclid=`Phrase+Fill`, Density=`Func+MOD`, Vel=`Func+AMP`); Density's entry already rebound once after a collision. | **Resolved** → unified **generator hub** on `3` (§5). |
| **T4** | §15 ties cost to *frequency* only; says nothing about *value/stakes* (Panic vs metronome score the same). | **To amend** → §15 gains a value/stakes clause + cheapness floor for safety-critical actions. |
| **T5** | Snapshot is dual-purpose (live-undo + record-onto-step); intent unresolved. | **Decide** → owner leans "both"; confirm + record ruling. |
| **T6** | Retrig/stutter live tension (fence #11) "under review." | **Open — analysed in §7**; recommendation pending owner decision. |
| **T7** | Bare-`Func`-hold flashes the Density band, on the most-composed modifier. | **Resolved by T3** → Density entry moves to the hub; bare-Func-hold reclaimed. |
| **T8** | "A tool you learn" has no principle naming orthogonality / predictable composition. | **To amend** → orthogonality clause added to §2. |
| **T9** | "Visually consistent" + the hardware-LED target have no constitutional home. | **To add** → new principle §19 (visual grammar, dual-target, graceful-degradation). |

---

## 4. Access-cost audit (frequency × value/stakes × rung)

Representative; the full feature list lives in README §5 / §7. Flags mark
mismatches between cost and frequency×value.

| Feature | Freq | Value | Rung (current) | Verdict |
|---|---|---|---|---|
| Step toggle | H | H | 1 | ✓ correct |
| Play / Record-arm | H | H | 1 | ✓ |
| Mute (`Mute+step`) | H | H | 3 | ✓ (perf-critical, cheap chord) |
| Fill (`Fill` held) | H | H | 1 (state) | ✓ |
| P-lock write (step+encoder) | H | H | hold+turn | ✓ |
| Copy/Paste/Clear (scope+verb) | M | H | 3 | ✓ |
| Scene / Song launch (`scope+step`) | H | H | 3 | ✓ |
| Section select (tap) | H | H | 1 | ✓ |
| **Tap-tempo (`3`)** | L | M | **1 (prime)** | ⚠ over-cheap single-purpose → **regraded** (§5: tap of a dual-role key) |
| **Metronome (`Func+3`)** | L | L | 2 | ⚠ cheaper feature than tap, harder to reach (inverted) → **moves to TIME** |
| **Tempo / time-sig edit** | M | H | buried in `mod+TRIG` overlay | ⚠ high-value, no direct path → TIME overlay stays but is the metronome's new home; revisit a direct path in follow-on |
| **Euclid (`Phrase+Fill`)** | M | H | 3 + inconsistent entry | ⚠ → **generator hub** (§5) |
| **Density (`Func+MOD`)** | M | H | 2, ad-hoc | ⚠ → **generator hub** |
| **Velocity overlay (`Func+AMP`)** | M | H | 2, ad-hoc | ⚠ → **generator hub** |
| Panic (`Song+O`) | L | **critical** | 3 | ✓ but only by luck — motivates T4 (value, not just frequency, must keep it cheap) |
| Latch (modifier double-tap) | M | M | double-tap | ✓ |
| Func escape (Func double-tap) | M | **critical** | double-tap | ✓ (always-available exit) |
| Note-edit (`Func+SRC+step`) | M | H | 4 | acceptable (Digitakt-class density) |
| Machine picker (`Func+Track`) | L | H | 2 | ✓ |
| **Retrig live stutter (`Fill+TRIG`)** | ? | ? | 3 + overlay | ⚠ **necessity unclear → §7** |

---

## 5. Reclaimed real-estate map (after the generator-hub decision)

The single decision *"`3` = generator hub on hold; tap = tap-tempo"* cascades
into a surprising amount of freed surface, because the generators vacate their
scattered homes:

| Slot | Was | Becomes |
|---|---|---|
| **`3`** | tap-tempo (single-purpose) | **generator hub** (hold) + **tap-tempo** (tap) — dual-role, no longer single-purpose |
| **`Func+3`** | metronome | **freed** (metronome → TIME overlay) |
| **`Func+MOD`** | Density sticky entry | **freed** (Density → hub) |
| **`Func+AMP`** | Velocity overlay entry | **freed** (Vel → hub) |
| **`Phrase+Fill`** | Euclid entry | **freed** (Euclid → hub) |
| **bare `Func`-hold** | transient Density band | **freed** (T7 resolved) |
| **`Func+Fill`** | (already) Accent generator | **freed** (Accent removed, §6) |
| **`CueScope`** | unbound enum value | still **unbound** — bind or retire (follow-on) |

This is a large, deliberate reclamation. **What goes back into these slots is a
follow-on binding decision** (ROADMAP "grammar-allocation pass") to be made
*after* the principles land — the point of the goal-sharpening is to set the
rules first, then re-allocate to fit them. Candidate uses are deferred; the
discipline is that anything placed here must be high-frequency×value (§15) and
must compose in the grammar (§2), not just "fill the hole."

> **Design note — the generator hub mechanic.** `3` held → MZ/grid re-skin to the
> generator family (Euclid / Density / Vel), consistent with *"the hold is the
> mode"* (§5). A quick tap (not held to threshold) = a tempo tap; repeated taps
> set tempo. The two never collide: hold-past-threshold = mode, sub-threshold
> taps = tempo. The transient-vs-sticky behaviour of each generator inside the
> hub (Euclid is latch-until-commit; Density/Vel are durable sticky overlays)
> carries over via the existing `Overlay`/latch machinery (§18) — detailed in
> the follow-on pass, not here.

---

## 6. The velocity cluster — resolved taxonomy

The Accent velocity generator (`Func+Fill`) was **already removed** and folded
into the live Velocity overlay (ROADMAP 995-997; DESIGN 5626) — the README just
hadn't caught up (now fixed). The remaining velocity features are **distinct, not
redundant**, and the review documents the distinction so it stays clear:

| Feature | Access | What it is | Destructive? |
|---|---|---|---|
| **Velocity overlay** | generator hub (was `Func+AMP`) | *Live, computed* per-track metric "feel" (Depth/Center/Mode/Blend), applied at emit time. An expression layer. | No |
| **Euclid ACCENT** | generator hub → Euclid, 3rd encoder | Accent *baked* into trig data while generating a rhythm (N onsets @ vel 100 vs 64). Authored content. | Yes |
| **LEVELS input mode** | `Track+↑/↓` → LEVELS | Per-step velocity authoring **without a MIDI controller** (grid = velocity buckets). | Yes |
| **P-lock / MIDI capture** | step-held + encoder / note-on | Per-step velocity authoring **with** a controller. | Yes |

**Decision (owner):** Euclid's **ACCENT is kept** — bake-during-generation
(authored) and the live overlay (computed) are genuinely different intents.

---

## 7. Retrig / beat-repeat — necessity analysis

The owner asked for a real dig into whether this earns its place ("we'd be okay
without it"). Here it is.

### 7.1 What it actually is (two bundled jobs)

`Fill + TRIG` opens a ratchet-rate picker (/4 … /32T). Two different things hang
off it:

1. **Per-step ratchet (authored).** *Hold a step first, press a rate* → bakes a
   per-step ratchet P-Lock: that step fires N times within its slot. This is
   classic step-sequencer ratcheting (hi-hat rolls, snare buzzes). It is
   **ordinary, deterministic, hand-editable trig data** — fully inside the
   grammar (§7, §11). No fence concern.
2. **Live free-running stutter (performed).** *Press a rate with no step held* →
   the whole focused track stutters live while the rate is held. This is the
   **fence #11** concern (prompted by Sonicware stutter / MC-707 Scatter): a
   momentary performance-FX gesture.

### 7.2 The §14 test (reward mastery — crutch or dead weight?)

- The **per-step ratchet** passes cleanly: you author it, it prints to state, you
  perform it via P-locks/conditions/copy-paste like any other step data. It
  rewards the same skills as the rest of the editing grammar.
- The **live stutter** is the weak one. It is a "press and it stutters" move —
  immediate, fun, but it does the rhythmic work *for* you in the moment and bakes
  to nothing unless you explicitly hold a step. Its skill ceiling is low.

### 7.3 Overlap with features Lockstep already has

The live-variation value the stutter provides is largely **already served**:
- **Fill** — momentary additive variation while held (the canonical "drop a
  variation in live" gesture).
- **Density** — live subtractive thinning, deterministic.
- **Mute / per-scene mute** — live removal.
- The owner's **Korg-Volca "active step"** reference maps to *these* (temporary
  step (de)activation for a live variation), **not** to retrig. So "active-step
  style" performance is already covered — which weakens the case that a live
  stutter is *needed*.

### 7.4 Options

- **A. Keep both as-is.** Status quo; leaves fence #11 permanently flagged.
- **B. Split them (recommended).** Promote the **per-step ratchet to an ordinary
  TRIG-section, P-lockable parameter** (it is just trig data; it does not need a
  bespoke `Fill+TRIG` mode at all), and **drop the free-running live stutter** as
  a distinct mode. Live variation stays with Fill/Density/Mute. This *sharpens*
  the instrument (§14), **resolves fence #11 cleanly** (the contentious mode is
  gone, the authored capability survives in-grammar), and **frees `Fill+TRIG`**.
- **C. Redesign the live stutter** to be more grammar-native (e.g. always bakes,
  or becomes a generator-hub member). Keeps the gesture but pays design cost.

### 7.5 Recommendation

**Option B.** Ratcheting is core step-sequencer vocabulary and must stay — but as
**authored per-step state**, not a special live mode. The free-running stutter is
the part that trips fence #11 and overlaps Fill/Density/Mute; cutting it removes a
low-ceiling crutch and a standing fence tension at once. **This is the owner's
call** (they flagged genuine uncertainty); the review's job is to make the case
that the *authored* half is essential and the *live free-running* half is the
optional, fence-tripping one.

---

## 8. Visual-grammar audit (dual-target)

### 8.1 The principle this audit serves (T9 → new §19)

**Visual grammar is token-first and dual-target, with graceful degradation.**
Every meaningful state is a `CellState` token (the SSOT, DESIGN §35.8.7) that
resolves to **both**:
- **Screen** — full detail: text + hint-text + scope colour + chrome.
- **Hardware** — RGB-LED colour + brightness + blink **only**.

The honest constraint (owner's correction): **colour-only cannot carry every
detail, and that is accepted.** The commitment is narrower and truer: the
*performable subset* — what you need to play without looking — must survive on
**colour + brightness + blink + muscle memory**. The screen is where you look for
the rest. This is exactly the §4 yardstick ("many things without looking, almost
anything without the mouse") expressed for the visual layer.

### 8.2 What already supports this

- **Scope-colour grammar (§6.6, shipped):** one hue per modality, three
  brightness levels (resting/active/accent). A held scope recolours the keys it
  rebinds. This is the backbone of "read by colour."
- **CellState appearance table (DESIGN §35.8.7):** `LS_CELLSTATE(token, value,
  screenFill, screenAccent, pushPad, xtouchVel)` — one row per token mapping to
  **both** screen colours and hardware LED palette indices. Degrades safely
  (unknown token → baseColour; undisplayable channel → ignored). The architecture
  already anticipates the LED downgrade; what's missing is the *constitutional
  commitment* that every meaningful state be expressible there.

### 8.3 Audit lens + representative hardware-risk flags

For each state, ask: *is the performable meaning carried by colour/brightness/
blink, or only by text?* Text-only states are the hardware-risk set.

- **Carried by colour (safe):** held-scope identity, latch (border/pip),
  record-arm, mute, fill, active scene, modal-entry-inert (`ModalEntryInert`
  token). ✓
- **Hardware-risk (text-leaning) — to flag in the detailed pass:** numeric
  read-outs (BPM, P-lock values, checkpoint depth `CK:N`), the
  generator/overlay *sub-page labels* (e.g. Density AMOUNT/MUSIC/SELECT, Vel
  CENTER/MODE/BLEND), the deletion-confirm *entity name/number*. These are
  legitimately screen-only detail — but the audit must confirm each has a
  *performable colour proxy* (e.g. which sub-page you're on encoded by hue/blink,
  even if the exact value is screen-only).

> A full per-token pass (cross-referencing `CellStates.def` once it lands, 8.6)
> is a follow-on; this review establishes the lens and the principle.

### 8.4 Home-key orientation cues (owner idea)

Add **home-key "bumps"** — the F/J-style orientation nibs of a typing keyboard —
as a persistent visual anchor on screen **and** on the eventual hardware, so a
player transitioning between a normal typing keyboard and Lockstep keeps their
hands oriented. On screen this is a small static decoration on the home-row
anchor keys; on hardware it is literal tactile nibs plus a matching visual cue.
Low cost, directly serves "learned by hand." → follow-on (chrome + hardware
spec).

---

## 9. Parameter-surface (Manipulation Zone) audit

The MZ is the other half of the grammar (how *values* are reached, vs how
*actions* are triggered). Quick regularity check:

- **Main params vs meta-bands.** Section keys page main machine params; meta-bands
  (Density, Vel) overlay per-track rotaries. Post generator-hub, meta-band entry
  becomes regular (via the hub) — an improvement over today's `Func+section`
  split. ✓ direction.
- **Aliases / role-fallback.** Control-All resolves id-primary, role-fallback —
  the only "alias" mechanism, and it extends OEB (§7). Regular. ✓
- **Hierarchical overlays.** OEB + Morph lerp + scene/phrase deviation all
  traverse finest-to-coarsest (§13). Consistent. ✓
- **Watch-item (from memory):** the MZ reuses 8 sliders across bands; machine-only
  skew/double-click has leaked into meta bands before (the density "double-rate"
  bug). Any new meta-band (generator-hub members) must reset slider state on band
  switch. → carry into the follow-on as a test requirement.

---

## 10. Recommendations → next steps

1. **Amend `PRINCIPLES.md`** (headline product):
   - North-Star preamble (§1 above).
   - **§2** — add an *orthogonality clause* (T8): scopes compose predictably with
     verbs; an undefined compound is *reserved/inert*, never surprising.
   - **§4 / §10** — reconcile the single-purpose-button rule (T1): "ambient
     utility" = *multi-purpose, always-available* (nav); `3` is now a dual-role
     grammatical key, not an exception.
   - **§15** — cost tracks *frequency × stakes*, with a cheapness floor for
     safety-critical actions (T4).
   - **new §19** — visual grammar, token-first, dual-target, graceful
     degradation (T9); home-key cues as a consequence.
   - Record the **Snapshot** intent ruling (T5).
2. **`NON-GOALS.md`** — generator-family note (T3); **fence #11** updated per the
   owner's retrig decision (§7).
3. **`DESIGN.md`** — generator hub (§13/§39), the §6.6/§35.8 cross-link to §19.
4. **`ROADMAP.md`** — add the **grammar-allocation pass** (re-allocate the §5
   freed slots; bind/retire `CueScope`; retrig split per §7; home-key cues),
   each item carrying the CLAUDE.md modality-test triad.
