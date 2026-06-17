# Snapshot design session — study brief

> **Status:** Scratch / seed document. Not shipped docs. This brief seeds the
> dedicated snapshot design session (ROADMAP 9.4). Output of that session lands
> in `README.md` (workflow CUJs) and `DESIGN.md §13.6` (rationale + UX spec).
>
> Do not update this file incrementally — replace it with the session output
> once decided.

---

## The dual-purpose problem

Snapshot mechanics today: `Y` pushes a checkpoint to a per-scope scratch stack
(depth 8) above an immovable floor; `Func+Y` restores (brief tap = pop one;
hold + release = jump to floor — the "hold = all the way" grammar convention,
DESIGN §13). Scoped: `Scene+Y`, `Track+Y`, `Phrase+Y`; bare `Y` ≡ `Song+Y`.

One stack + one verb is being asked to serve two jobs whose needs differ:

| Journey | Abbreviated name | Shape of use | What restore means |
|---|---|---|---|
| **J1** | Safety net / undo | Near-automatic capture (often pre-destructive), linear step-back, hard to accidentally lose | Pop one — "undo the last thing" |
| **J2** | Performance scratch / variation | Explicit, gestural, deliberate mark while improvising; return *to the mark* | "Return to my mark," not necessarily the most-recent push |

**Where they collide today:**

- **Shared stack, depth 8.** A flurry of J2 marks can bury / evict the J1
  pre-destruction point.
- **"Restore" means different things.** J1 wants *pop one*; J2 wants *return
  to my mark*. Restore = pop one, so J2 only behaves correctly if nothing was
  pushed after the mark.
- **Auto-captures interleave with manual marks** on the same stack — at restore
  time the user cannot tell an auto pre-X point from their own deliberate mark.

---

## Auto-capture site audit

Sites in `PluginEditor.cpp` that push a checkpoint without a user-initiated
gesture:

| Line | Trigger | Scope | Notes |
|---|---|---|---|
| `:185` | Pre-undo-session start | Song | Pushed before a multi-step undo begins |
| `:2691` | Pre-scene create (tap) | Song | Before a new Scene slot is created on `Scene+step` |
| `:2704` | Pre-scene create (baseline) | Song | `Func+Scene+step` baseline create path |
| `:2715` | Pre-scene create (copy-bake) | Song | Scene Record/bake commit path |
| `:3338` | Pre-bare-Y snapshot | Song | The snapshot verb itself (not a pre-X capture) |
| `:3360` | Pre-Phrase-scope snapshot | Phrase | Phrase snapshot verb |

Note: `:3338` and `:3360` are the verb dispatches themselves, not pre-action
guards — they are not "automatic" in the J1 sense. The pre-action captures are
`:185`, `:2691`, `:2704`, `:2715`.

---

## Candidate CUJs (study input — not docs-to-ship)

These are the journeys to validate the chosen model against. They are ordered
coarsest to finest, matching the four stack scopes.

1. **Song — "checkpoint before a structural edit"** (bare `Y`). Whole-project
   safe point before reorganising Scenes/order; hold `Func+Y` to fall all the
   way to the floor if a multi-step edit spiralled.

2. **Scene — "audition a variation"** (`Scene+Y`). Snapshot the Scene floor,
   then mangle mutes/morph/core-timing live; restore to return to the
   known-good arrangement. Pairs with the existing double-tap-Scene floor
   launch.

3. **Track — "A/B a sound or pattern"** (`Track+Y`). Snapshot a track's
   content + Kit, redesign it, restore to compare. Sound-design within a
   running set.

4. **Phrase — "trusted live undo for one track"** (`Phrase+Y`). Before
   improvising retrigs / P-Locks into a track's phrase during a build, mark a
   safe point; `Func+Y` snaps back if the fill didn't land. Highest-frequency,
   lowest-stakes journey.

---

## Three resolutions

### A — Unify + make legible *(recommended starting point)*

Keep one stack. **Label every entry** (manual mark vs auto pre-X label) and
show what restore will revert in the **MZ header** (§26.4.1 — "Restore ← your
mark" / "Restore ← pre-clear"). The complaint is a *legibility* failure; fixing
that is cheap and doesn't require a second history system.

Hold `Func+Y` = floor is already the "all the way" escape.

*When A breaks down:* if labelling is insufficient because J1 and J2 are
genuinely different user models (not just different labels), escalate to B.

### B — Separate two mechanisms

Automatic linear **Undo** (system-managed, for destructive ops) + manual
**Snapshot** marks (performance, return-to-mark). Cleaner mental model, second
stack, second verb (or repurpose `Func+P`?), more serialisation surface.

*Pushback:* a parallel history is a large commitment for what may be a
labelling problem — only justified if A still feels muddy in use.

### C — Deprecate one half

If a true Undo lands (B), Snapshot collapses to pure performance (return-to-
mark, ephemeral) and destructive ops route only to Undo. Cleanest end-state;
biggest behavioural change.

---

## Open questions for the session

1. **Primary framing.** Is Snapshot a *performance tool* (fast, ephemeral,
   gestural) or an *editing safety net* (undo-like)? The current design straddles
   both — pick the primary story.

2. **Relationship to Checkpoint / undo.** Are Snapshot and an eventual Undo
   the same feature (A), siblings (B), or does one subsume the other (C)?

3. **Visual feedback.** Should there be a persistent cue that a snapshot stack
   has entries above the floor (a pip on the scope key, or a depth count in the
   MZ header)? The `CK:N` chip in the transport bar exists but is global (Song
   scope). Scope-specific depth feedback at the scope key would close the loop.

4. **Restore semantics.** User lean (non-committed): "both" — tap = step back
   one; hold = all the way (already the A1b "hold = all the way" convention).
   Confirm this in the session.

5. **Auto-capture labelling.** If going with A, decide how to surface the
   auto-capture label (`:185 / :2691 / :2704 / :2715`) — a fixed string ("pre-
   scene", "pre-undo"), a timestamp, or derivable from context?

---

## Session prerequisites

- ROADMAP 9.3 B2 shipped (MZ header strip available for restore-label display).
- User has used Snapshot live at least once at each of the four scope levels to
  develop an opinion on the J1 vs J2 tension.
