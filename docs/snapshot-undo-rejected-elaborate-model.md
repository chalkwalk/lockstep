# Snapshot / undo — the elaborate model we rejected

> **Status: REJECTED (2026-07-14).** This is not the design we are building. The
> shipped model is DESIGN §13.6 (independent per-scope stacks). This document
> preserves the more elaborate model that the 9.4 design session first specced and
> then, on reflection, set aside — so that if a future CUJ demands what it offered,
> the design already exists and does not have to be re-derived.
>
> Read the **Why we rejected it** and **What would bring it back** sections first:
> they are the reason this file exists.

The 9.4 session ran in two passes. The first pass (recorded here) reasoned from
first principles about consistency and reversibility and arrived at a globally
consistent, branch-preserving checkpoint graph. The second pass asked *which of
this does a live performer actually reach?* and cut everything that served a
consistency abstraction the performer never experiences as a problem. The second
pass is DESIGN §13.6. This is the first.

---

## 1. The elaborate model

### 1.1 Two mechanisms, one priority

Marks and undo are separated, because one stack cannot serve both a **safety net**
("undo the last thing", near-automatic, linear) and a **performance scratchpad**
("return to *my* mark", explicit, gestural) — a flurry of marks buries the
pre-destruction point, and "restore" cannot mean both *pop one* and *return to my
mark*. The governing priority:

> **Explicit beats implicit.** Snapshot and Restore are things you *asked for*, and
> must always do exactly what you expect given your marks. Undo is something the
> system did *on your behalf*, and must never interfere with them — nor silently
> destroy work you did after it was armed.

- **Marks** — per-scope LIFO stacks, pushed by `Y`, walked by `Func+Y`.
- **Undo** — entries armed *automatically* before destructive ops, reached by
  `Func+O`. Each carries the scope of the op that armed it, so undo *is* scoped;
  what makes it feel unscoped is that your fingers never name the scope.

### 1.2 The state tree and "overlap"

Payloads are whole-struct copies at a granularity, so two entries collide exactly
when one **contains** the other:

```
              Song           (tracks + scenes + song swing / time-sig)
             /    \
        Track      Scene     Scene = activeMask, time-sig, key-sig
          |                   Track = kit + phrases[]
       Phrase
```

`Scene` and `Track` are **disjoint** (a Scene records *who plays* and the musical
grid, not phrase content). So:

> Two entries **overlap** iff one scope is an ancestor of the other (or they are
> the same scope+target). `Track 3` and `Track 7` never overlap. `Track` and
> `Scene` never overlap. `Song` overlaps everything.

### 1.3 The one rule (derived validity + epoch)

Every entry carries a **scope**, a **target**, and an **epoch** (a global monotonic
counter). Validity is **derived**, never stored:

> **Applying an entry invalidates every entry that overlaps it and is newer than
> it.** Those are pre-states of a branch you abandoned; keeping them would let you
> graft a state that never existed.

| | Guard (may it apply?) | Effect on newer overlapping entries |
|---|---|---|
| **Restore** (explicit) | always — it is your mark | **drops them** (branch abandoned deliberately) |
| **Undo** (implicit) | **only if nothing newer overlaps** | none — if it cannot apply cleanly it *refuses* (`UNDO EXPIRED — track 3 changed since`) |

Worked example: snapshot Song (epoch 1, holds track 3 as `S0`), then snapshot
Track 3 (epoch 5, holds `S1`). Restore the Song mark → track 3 returns to `S0`, and
the Track-3 mark is dropped (it would graft an epoch-5 track into an epoch-1 song).
Restore the Track-3 mark instead → track 3 becomes `S1`, and the older Song mark
survives. Because the invalidation is *derived*, removing the shadower (restoring
the Song) can **revive** an undo that a newer overlapping mark had shadowed.

### 1.4 Non-destructive restore + the redo stack (unrestore)

The linchpin that makes navigation lossless: a snapshot is a *state overlay, not a
diff*, so restore overwrites live state — and must not silently discard it.

> **Restore auto-captures live before overwriting**, and pushes the exact live
> state onto a **redo stack**. The redo stack therefore *is* the reverse of your
> path, which is what makes unrestore exact.

- **SNAP** (`Y`): append `copy(live)`; cursor→top; **clear redo**.
- **RESTORE** (`Func+Y`): push `copy(live)` onto redo; live ← previous mark.
- **UNRESTORE**: pop redo → live (exact inverse of the last restore).
- **UNDO** (`Func+O`): apply newest valid overlapping `Auto`; push pre-undo state
  onto redo.
- **REDO**: pop redo — reverses the last back-motion (restore *or* undo), LIFO.

This gives the invariants the performer asked for: *restore N, unrestore N* returns
to the exact original; edits made mid-scrub reappear at the point they were made
(they were auto-captured onto the redo stack) and are never silently lost; deleting
a track mid-scrub arms an undo that survives the scrub, and redo-after-undo
re-applies the delete.

### 1.5 The vim-style second axis (branches + chronological)

Instead of truncating the forward path when you snapshot after restoring, keep it —
the history becomes a **tree**, and there are **two navigations**, exactly as vim's
undo tree offers:

- **Linear** along the current branch — `u` / `Ctrl-R`, our mark line + redo stack.
- **Chronological** across *all* states regardless of branch — vim's `g-` / `g+`
  and `:earlier 5m` / `:later`, our **global epoch** used as a time-ordered log.

Nothing is ever deleted except by the depth/memory cap, so every abandoned limb
remains reachable by walking the epoch axis ("the state as it was N steps / N
minutes ago").

---

## 2. Why we rejected it

The performer's real CUJs are narrow: **undo a mistake** (≈90% of the value, mostly
one level deep), **redo** (fix fixing the mistake), **mark a good state and return
to it**, and **reload saved** (walk to the floor). Against those:

1. **The overlap / epoch / derived-validity axis solves a problem the performer
   never has.** Its job is to stop you "grafting a state that never existed." But
   every payload is a *complete, valid* overlay for its scope — a saved Track
   grafted onto any Song is a valid Song; nothing corrupts. Musically, "restore
   track 3 → track 3 becomes the thing I saved" is exactly what a performer expects.
   The invalidation rule would instead *stop* them, or *silently drop a mark they
   can see* — which is more surprising than just letting them do it. The entire
   epoch counter, the ancestor-overlap predicate, and the revive scenario exist to
   serve a global-consistency abstraction the performer did not ask for.

2. **The vim tree is too much for live use** (the user's own call, and correct): a
   second navigation axis and a "which branch / how many minutes ago" mental model
   is a text-editor luxury, not a stage tool.

3. **Deep multi-level unrestore is speculative.** How often does a performer restore
   and then want to walk the un-restore path back several steps, live? The shipped
   model keeps *one* level of fat-finger insurance; more was unproven.

The shipped model keeps the one genuinely load-bearing idea from all of this —
**marks and undo are separate**, so a flurry of `Y`s never buries the pre-mistake
point — and throws away the consistency machinery around it. Its mental model fits
in a sentence: *each thing remembers its own snapshots; undo fixes mistakes; both
are independent.*

---

## 3. What would bring it back

Revisit this document if real usage produces any of these CUJs:

- **Cross-scope consistency complaints.** Users report that mixing a Song restore
  with a Track restore produces "wrong" combined states they consider a bug (not
  just a combination they didn't save). That is the exact problem the overlap rule
  solves — §1.2–1.3 are ready.
- **Deep reversible scrubbing on stage.** Users want to walk *many* steps back and
  forth through a scope's history live, not just one level of unrestore. §1.4's
  redo-stack-as-path is ready.
- **"Take the state from earlier" navigation.** Users want to reach a state they
  snapshotted-over — the abandoned limb — without having curated a mark for it.
  That is the chronological axis; §1.5 is ready, and the shipped model's per-scope
  stacks are a strict subset of it, so the migration is additive.

Because the shipped model is a subset (independent per-scope stacks with no epoch),
adopting any part of this is **additive** — an epoch stamp and an overlap predicate
layered over the existing stacks — not a rewrite.
