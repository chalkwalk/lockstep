#pragma once

#include "CommandContext.h"
#include "CommandEffects.h"
#include "../io/EditMode.h"
#include "../io/ClipboardType.h"
#include "../io/ControllerEvent.h"

namespace lockstep::verbs
{
  // ── What Record / Play would do RIGHT NOW (9.14 st.5) ──────────────────────
  //
  // For the verb family the binding table's action name is only a LABEL: every
  // verb action funnels through `handleVerb(primaryScope, button)`, so the real
  // authority on what a verb key does is the scope x verb matrix below, not the
  // table. That gap is not theoretical -- it is how the Song rows came to
  // advertise COPY/PASTE for keys that `verbs::song` never handled, and it is
  // why the discoverability banner must read the matrix. A banner sourced from
  // the table would faithfully reprint the lie.
  //
  // So: ONE description of the copy/paste affordance, read by the paste guards
  // in VerbCommands.cpp *and* by the inspector hint. They cannot drift.
    struct ClipAffordance
    {
        bool canCopy = false;               // Record copies in this scope
        bool canPaste = false;              // Play would paste the CURRENT clipboard
        ClipboardType native = ClipboardType::None;  // the type this scope's copy makes
    };

  // `scope` = EditMode::primaryScope(); `func` = the Func modifier; `clip` = what
  // the clipboard currently holds. Pure.
    [[nodiscard]] ClipAffordance clipAffordance(EditMode::PrimaryScope scope,
                                                bool func,
                                                ClipboardType clip) noexcept;

  // The paste guard, in one place: a scope accepts its own copy type, plus `All`
  // (the omni grab). Machine is the exception -- it takes only a Machine clip.
    [[nodiscard]] bool pasteAccepts(EditMode::PrimaryScope scope,
                                    bool func,
                                    ClipboardType clip) noexcept;

  // Per-scope verb handler free functions.
  // Each returns true if the verb was handled (caller must stop processing).
  // Add a function here and call it from CommandCore::handleVerb as each
  // scope migrates from PluginEditor::dispatchVerb.

  // 9.37 item C — Trig + hold(Clear): drop every P-Lock on the held step(s) and
  // leave the trig, condition and fill state alone. Was Trig+Func+Clear until 9.4
  // put UNDO on that chord (DESIGN §13.6, "The three rulings of 9.37").
    [[nodiscard]] bool clearStepLocks(CommandContext& ctx);

    [[nodiscard]] bool trig(ControllerButton verb,
                            CommandContext& ctx,
                            CommandEffects& fx);

    [[nodiscard]] bool track(ControllerButton verb,
                             CommandContext& ctx,
                             CommandEffects& fx);

    [[nodiscard]] bool phrase(ControllerButton verb,
                              CommandContext& ctx,
                              CommandEffects& fx);

    [[nodiscard]] bool scene(ControllerButton verb,
                             CommandContext& ctx,
                             CommandEffects& fx);

    [[nodiscard]] bool song(ControllerButton verb,
                            CommandContext& ctx,
                            CommandEffects& fx);

    [[nodiscard]] bool noScope(ControllerButton verb,
                               CommandContext& ctx,
                               CommandEffects& fx);

    [[nodiscard]] bool morph(ControllerButton verb,
                             CommandContext& ctx,
                             CommandEffects& fx);

    [[nodiscard]] bool section(ControllerButton verb,
                               CommandContext& ctx,
                               CommandEffects& fx);

  // 9.29 — the Machine scope (Func+Track). Track owns identity, Machine owns the
  // sound, so these act on the machine's param set only: no steps, no length, no
  // routing. Record = copy, Play = paste (loading the machine first if the target
  // runs a different one), Clear = init to the machine's own defaults.
    [[nodiscard]] bool machine(ControllerButton verb,
                               CommandContext& ctx,
                               CommandEffects& fx);
}
