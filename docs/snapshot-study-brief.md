# Snapshot design session — CLOSED (2026-07-14)

> This brief seeded the 9.4 design session. **The session has run.** Per the
> brief's own instruction ("replace it with the session output once decided"),
> the content is superseded:
>
> - **Spec / rationale:** `DESIGN.md` §13.6 (rewritten).
> - **Workflow / CUJs:** `README.md` §5.15.
> - **Build items:** `ROADMAP.md` 9.4 (items A–H).
>
> Kept only as a pointer, so nothing links into a dead file.

## What the session concluded, in one paragraph

The J1 (safety net) / J2 (performance scratchpad) tension was real but the brief
mis-framed it as a *labelling* problem. It is a **separation** problem, and it was
partly a phantom besides: the scoped-snapshot half of the feature had never been
wired (`Track+Y` and `Phrase+Y` dispatched to nothing), so J2 was unreachable below
Song scope. Marks and undo are now two mechanisms under one rule. *Explicit beats
implicit*: Snapshot/Restore are yours and always do what you expect; undo is the
system's, is scoped by the operation that armed it (not by your fingers), lands on
`Func+O`, and refuses rather than eating newer work. Every entry carries a scope and
an epoch, and **applying an entry invalidates every entry that overlaps it and is
newer than it** — restore *drops* those (you abandoned that branch deliberately),
undo *refuses* (you didn't). Because `Scene` and `Track` are disjoint in the state
tree, an undo survives unrelated work instead of expiring on any keypress.

## The three bugs it found (all live, all filed as 9.4 build items)

1. `Track+Y` / `Phrase+Y` — **no-ops** wearing a "SNAP" label.
2. Restore *is* scope-aware, so an empty Track/Phrase stack **silently reverts the
   scope to the project baseline**. One keypress, no confirm, no message.
3. `Scene+O` — a **phantom "CLEAR" label** that dispatches to nothing. It became
   SYNC's new home (Record bakes deviations; Clear discards them).
