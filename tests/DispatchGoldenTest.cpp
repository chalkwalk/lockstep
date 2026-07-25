// DispatchGoldenTest -- ROADMAP 9.12 Stage 5.
//
// Pins TODAY'S dispatch behaviour before the table migration (stages 6-8) starts
// rewriting it. The migration replaces ~2,700 lines of imperative branching in
// dispatchDown/dispatchUp with `resolve(..., gesture).action -> handleAction`,
// family by family. Without a net, "it still works" would rest on clicking
// around; with one, a family whose recorded behaviour shifts is a BUG FOUND, and
// the golden diff is the review artifact.
//
// The net drives a real (headless) LockstepEditor. That is possible -- 9.13's
// note that an editor harness "proved unviable headless (component-teardown
// segfault)" was a misdiagnosis: LockstepProcessor embeds Arrangement (~47 MB),
// so a stack-local processor overflows the stack in the enclosing function's
// prologue and dies before its first statement. Heap-allocate it and dispatch
// runs fine. See the spike commit.
//
// WHAT IS RECORDED. For each row of the binding table (so no family can be
// forgotten -- the matrix enumerates the grammar itself): press the row's
// required modifiers, tap the row's key, release, and record the UiState fields
// that CHANGED from a freshly-constructed baseline. Recording the delta rather
// than the whole state makes the golden readable: each line says what a gesture
// actually does.
//
// REGENERATING. The golden is checked in. To re-bless it after an intentional
// behaviour change:
//     LOCKSTEP_REGEN_GOLDEN=1 ./build/tests/lockstep_dispatch_tests
// then READ THE DIFF. A line that changes without you meaning it is the bug this
// file exists to catch, so re-blessing without reading is the one way to make
// this test worthless.

#include "EditorRig.h"

#include "../src/command/KeyBindings.h"

#include <cstdio>
#include <functional>
#include <map>
#include <memory>

namespace lockstep
{
namespace
{
    using CE = ControllerEvent;
    using CB = ControllerButton;

    // ---------------------------------------------------------------------------
    // Names. A switch (not a table) so -Wswitch makes a new ControllerButton a
    // compile error here rather than a silently unnamed golden row.
    // ---------------------------------------------------------------------------
    const char* buttonName(CB b) noexcept
    {
        switch (b)
        {
            case CB::Func:            return "Func";
            case CB::PhraseScope:     return "Phrase";
            case CB::MorphScope:      return "Morph";
            case CB::MuteScope:       return "Mute";
            case CB::TrackScope:      return "Track";
            case CB::SceneScope:      return "Scene";
            case CB::SongScope:       return "Song";
            case CB::FillScope:       return "Fill";
            case CB::CueScope:        return "Cue";
            case CB::VerbSnapshot:    return "VerbSnapshot";
            case CB::VerbRecord:      return "VerbRecord";
            case CB::VerbPlay:        return "VerbPlay";
            case CB::VerbStopLegacy:  return "VerbStopLegacy";
            case CB::VerbConfirm:     return "VerbConfirm";
            case CB::Snapshot:        return "Snapshot";
            case CB::Restore:         return "Restore";
            case CB::VerbClear:       return "VerbClear";
            case CB::VerbDelete:      return "VerbDelete";
            case CB::VerbPanic:       return "VerbPanic";
            case CB::NavUp:           return "NavUp";
            case CB::NavLeft:         return "NavLeft";
            case CB::NavDown:         return "NavDown";
            case CB::NavRight:        return "NavRight";
            case CB::Section:         return "Section";
            case CB::MetaSection:     return "MetaSection";
            case CB::Step:            return "Step";
            case CB::SelectTrack:     return "SelectTrack";
            case CB::ToggleMute:      return "ToggleMute";
            case CB::ForkPart:        return "ForkPart";
            case CB::RecordArm:       return "RecordArm";
            case CB::TapTempo:        return "TapTempo";
            case CB::MetronomeToggle: return "MetronomeToggle";
            case CB::PlayStop:        return "PlayStop";
            case CB::StopReset:       return "StopReset";
            case CB::None:            return "None";
        }
        return "?";
    }

    // A golden row only needs a stable label for the layer; layerBanner() owns the
    // exhaustive human-facing switch, and it needs a UiState we do not have here.
    juce::String layerName(SurfaceLayer l) { return "L" + juce::String(static_cast<int>(l)); }

    // Which modifier button carries each bit of KeyBinding::requiredMods.
    struct ModBit { std::uint16_t bit; CB button; };
    const ModBit kModBits[] = {
        { kModFunc,   CB::Func },        { kModTrack, CB::TrackScope },
        { kModPhrase, CB::PhraseScope }, { kModScene, CB::SceneScope },
        { kModMorph,  CB::MorphScope },  { kModSong,  CB::SongScope },
        { kModMute,   CB::MuteScope },   { kModFill,  CB::FillScope },
    };

    // ---------------------------------------------------------------------------
    // The state digest. Keys are field names; only fields that CHANGED against the
    // baseline are written to the golden.
    //
    // Drift guard: if UiState grows a field, sizeof changes and this fires. A new
    // field that dispatch writes but the digest omits would be an invisible hole in
    // the net -- the failure mode a golden test is supposed to make impossible.
    // If this static_assert trips: add the field below, then re-bless.
    // ---------------------------------------------------------------------------
    static_assert(sizeof(UiState) == 1336,
                  "UiState changed size: add the new field(s) to digest() below, then "
                  "regenerate the golden (LOCKSTEP_REGEN_GOLDEN=1) and read the diff.");

    using Digest = std::map<juce::String, juce::String>;

    void put(Digest& d, const char* k, int v) { d[k] = juce::String(v); }
    void put(Digest& d, const char* k, bool v) { d[k] = v ? "true" : "false"; }

    template <typename Arr>
    void putArray(Digest& d, const char* k, const Arr& a)
    {
        juce::String s;
        for (const auto& v : a)
            s += juce::String(static_cast<int>(v)) + ",";
        d[k] = s;
    }

    Digest digest(const UiState& u, const LockstepProcessor& p)
    {
        Digest d;
        put(d, "activeTrack", u.activeTrack);
        putArray(d, "trackSection", u.trackSection);
        put(d, "masterSection", u.masterSection);

        put(d, "funcHeld", u.funcHeld);
        put(d, "phraseScopeHeld", u.phraseScopeHeld);
        put(d, "phraseScopeUsed", u.phraseScopeUsed);
        put(d, "morphHeld", u.morphHeld);
        put(d, "morphNavQualifier", u.morphNavQualifier);
        put(d, "muteHeld", u.muteHeld);
        put(d, "relaunchHeld", u.relaunchHeld);
        put(d, "trackHeld", u.trackHeld);
        put(d, "sceneHeld", u.sceneHeld);
        put(d, "songHeld", u.songHeld);
        put(d, "fillHeld", u.fillHeld);
        put(d, "cueHeld", u.cueHeld);

        put(d, "latch.phrase", u.latch.phrase);
        put(d, "latch.morph", u.latch.morph);
        put(d, "latch.mute", u.latch.mute);
        put(d, "latch.track", u.latch.track);
        put(d, "latch.scene", u.latch.scene);
        put(d, "latch.song", u.latch.song);
        put(d, "latch.fill", u.latch.fill);

        put(d, "swingDismissed", u.swingDismissed);
        put(d, "stepHeld", u.stepHeld);
        put(d, "pLockClearMode", u.pLockClearMode);
        put(d, "pLockClearTrack", u.pLockClearTrack);
        put(d, "pLockClearStep", u.pLockClearStep);
        put(d, "pLockClearStaged", static_cast<int>(u.pLockClearStaged.size()));
        put(d, "stepMoveActive", u.stepMoveActive);
        put(d, "stepMoveAnchor", u.stepMoveAnchor);

        put(d, "machineScopeHeld", u.machineScopeHeld);
        put(d, "machinePickerOpen", u.machinePickerOpen);
        put(d, "funcSrcHeld", u.funcSrcHeld);
        put(d, "noteEditMode", u.noteEditMode);
        put(d, "noteEditOctave", u.noteEditOctave);
        put(d, "noteEditSteps", static_cast<int>(u.noteEditSteps.size()));
        put(d, "noteEditStaged", static_cast<int>(u.noteEditStaged.size()));

        putArray(d, "pendingPatternMuteToggle", u.pendingPatternMuteToggle);
        putArray(d, "trackInputMode", u.trackInputMode);
        put(d, "trigGridMode", static_cast<int>(u.trigGridMode));

        put(d, "funcFxHeld", u.funcFxHeld);
        put(d, "funcFxInsertSlot", u.funcFxInsertSlot);
        put(d, "masterFxPickerOpen", u.masterFxPickerOpen);
        put(d, "masterFxInsertSlot", u.masterFxInsertSlot);
        put(d, "fxPickerPage", u.fxPickerPage);
        put(d, "deckConsolePage", u.deckConsolePage);
        put(d, "machineConsoleOpen", u.machineConsoleOpen);
        put(d, "routeConsoleActive", u.routeConsoleActive);

        put(d, "overlay", static_cast<int>(u.overlay));
        put(d, "densityBank", u.densityBank);
        put(d, "densitySubPage", static_cast<int>(u.densitySubPage));
        put(d, "velBank", u.velBank);
        put(d, "velSubPage", static_cast<int>(u.velSubPage));
        put(d, "cueParamPage", u.cueParamPage);
        put(d, "cueBank", u.cueBank);
        put(d, "mixerBank", u.mixerBank);
        put(d, "samplePropsPoolIndex", u.samplePropsPoolIndex);
        put(d, "timeEntryScope", u.timeEntryScope);
        put(d, "sigPage", static_cast<int>(u.sigPage));

        // 5.3 identity overlay.
        put(d, "identityScope", static_cast<int>(u.identityScope));
        put(d, "identityIndex", u.identityIndex);
        put(d, "identityColourPage", u.identityColourPage);
        putArray(d, "identityMode", u.identityMode);
        put(d, "identityTopSeed", static_cast<int>(u.identityTopSeed));
        put(d, "identityBottomSeed", static_cast<int>(u.identityBottomSeed));
        put(d, "identityTopSel", u.identityTopSel);
        put(d, "identityBottomSel", u.identityBottomSel);
        put(d, "identityColourSel", u.identityColourSel);
        put(d, "identityRawActive", u.identityRawActive);
        put(d, "identityRawText", juce::String(u.identityRawText).length());

        // 5.3 browser overlay.
        put(d, "browserPage", static_cast<int>(u.browserPage));
        put(d, "browserTrack", u.browserTrack);
        put(d, "browserCursor", u.browserCursor);

        put(d, "generatorHubHeld", u.generatorHubHeld);
        put(d, "euclidHeld", u.euclidHeld);
        put(d, "euclidPulses", u.euclidPulses);
        put(d, "euclidOffset", u.euclidOffset);
        put(d, "euclidAccents", u.euclidAccents);
        put(d, "melodicHeld", u.melodicHeld);
        put(d, "melodyDensity", u.melodyDensity);
        put(d, "melodyCore", u.melodyCore);
        put(d, "melodySource", u.melodySource);
        put(d, "harmonyHeld", u.harmonyHeld);

        put(d, "confirm.kind", static_cast<int>(u.confirm.kind));
        put(d, "confirm.target", u.confirm.target);
        put(d, "deletePicker.active", u.deletePicker.active());

        // Processor-visible effects: dispatch writes to the engine as often as it
        // writes to UiState (181 refs vs 160), so a UiState-only digest would miss
        // half of what a gesture does.
        put(d, "proc.focusTrack", p.focusTrack());
        put(d, "proc.isPlaying", p.clock().inPluginPlaying());
        put(d, "proc.recordArmed", p.clock().isRecordArmed());
        return d;
    }

    // ── 7b: what a VERB actually does ────────────────────────────────────────
    // The UiState digest above is blind to the entire verb family. `Track+Record`
    // is COPY TRACK, and the copy lands in the clipboard; `Clear` rewrites the
    // phrase; `Snapshot` pushes a checkpoint. None of that is UiState, so the
    // golden recorded a scoped verb as "the modifier went down" and nothing else --
    // it would have stayed green through any breakage of copy/paste/clear/snapshot.
    // A net that cannot see the family it is about to protect is decoration.
    Digest verbDigest(const LockstepEditor& ed, const LockstepProcessor& p)
    {
        Digest d;
        const auto& c = DispatchProbe::clip(ed);
        put(d, "clip.type", static_cast<int>(c.type));
        put(d, "clip.stepEntries", static_cast<int>(c.stepEntries.size()));
        put(d, "clip.sectionSlots", static_cast<int>(c.sectionSlots.size()));

        const int ft = p.focusTrack();
        put(d, "ckpt.Song", p.checkpointDepth(CheckpointScope::Song, ft));
        put(d, "ckpt.Track", p.checkpointDepth(CheckpointScope::Track, ft));
        put(d, "ckpt.Scene", p.checkpointDepth(CheckpointScope::Scene, ft));
        put(d, "ckpt.Phrase", p.checkpointDepth(CheckpointScope::Phrase, ft));

        // Undo depth per scope (9.4 item E). Destructive ops (clear/paste/delete/bake/
        // generator print/transpose) now arm the UNDO stack, not the mark stacks above,
        // and Func+O walks it. Mirroring ckpt.* here makes that relocation visible: a
        // scenario that used to bump ckpt.Song now bumps undo.Song instead. Without this
        // line the move would read as a silent loss of the checkpoint bump.
        put(d, "undo.Song", p.undoDepth(CheckpointScope::Song, ft));
        put(d, "undo.Track", p.undoDepth(CheckpointScope::Track, ft));
        put(d, "undo.Scene", p.undoDepth(CheckpointScope::Scene, ft));
        put(d, "undo.Phrase", p.undoDepth(CheckpointScope::Phrase, ft));

        // Live scene deviations. SYNC (9.4 item C) exists to CLEAR these, and nothing
        // else it touches lands anywhere the digest could see -- so without this line the
        // net watches SYNC fire and records "no observable state change", which is what
        // it recorded for the old Scene+Y right up until the 9.4 session found that key
        // dispatching to nothing at all. A verb whose whole effect is invisible to the
        // golden is a verb the golden cannot protect.
        juce::String dev;
        for (std::size_t t = 0; t < kNumTracks; ++t)
            dev += p.arrangement().deviated[t] ? "1" : "0";
        d["scene.deviated"] = dev;

        // Queued scene launch. Cancel-queued-scene (9.4 item C) moved from the Clear key
        // to Func+P; making the queue observable is what lets the golden watch it clear.
        put(d, "queued.scene", p.hasQueuedScene());

        // Sequence content: a trig census per track, so Clear / Paste / init are
        // visible as content changes rather than as nothing at all.
        juce::String trigs;
        const auto& seq = p.sequence();
        for (std::size_t t = 0; t < kNumTracks; ++t)
        {
            int n = 0;
            const auto& trk = seq.tracks[t];
            for (int s = 0; s < trk.length && s < kMaxStepsPerTrack; ++s)
                if (trk.steps[static_cast<std::size_t>(s)].trig)
                    ++n;
            trigs += juce::String(n) + ",";
        }
        d["seq.trigsPerTrack"] = trigs;

        // 7c widening. The census above counts trigs; ROTATE moves them without
        // changing the count, so the net recorded `Func+Left` as doing NOTHING. The
        // nav family is full of state this digest could not see -- the step PAGE lives
        // in KeyboardArea, the octave in UiState.noteEditOctave (already caught), the
        // pattern LENGTH in the Track. A net that cannot see the family it is about to
        // protect is decoration, so widen BEFORE migrating, not after.
        juce::String pattern;   // trig positions on the focused track, as a bitmap
        {
            const auto& trk = seq.tracks[static_cast<std::size_t>(p.focusTrack())];
            for (int st = 0; st < trk.length && st < kMaxStepsPerTrack; ++st)
                pattern += trk.steps[static_cast<std::size_t>(st)].trig ? "x" : ".";
        }
        d["seq.focusPattern"] = pattern;

        juce::String lengths;
        for (std::size_t t = 0; t < kNumTracks; ++t)
            lengths += juce::String(seq.tracks[t].length) + ",";
        d["seq.trackLength"] = lengths;

        put(d, "kbd.page", DispatchProbe::page(ed));
        return d;
    }

    // The golden's state digest over a rig. Free function, not a Rig member: Rig
    // lives in EditorRig.h and is shared with the interaction harness, while
    // digest()/verbDigest() are this net's private business.
    Digest snap(const Rig& rig)
    {
        Digest d = digest(DispatchProbe::ui(*rig.editor), *rig.proc);
        for (auto& [k, v] : verbDigest(*rig.editor, *rig.proc))
            d[k] = v;
        return d;
    }

    juce::String renderCase(const KeyBinding& row, const Digest& base)
    {
        Rig rig;
        freeze(rig);

        // Hold the row's required modifiers, tap its key, release everything.
        std::vector<CB> held;
        for (const auto& m : kModBits)
            if ((row.requiredMods & m.bit) != 0)
                held.push_back(m.button);

        for (CB m : held)
            (void)DispatchProbe::down(*rig.editor, CE{ CE::Type::ButtonDown, m, 0, 0 });

        // BOTH EDGES, separately. Most of this grammar acts on key-DOWN and undoes
        // itself on key-UP (a held modifier is the obvious case), so a single
        // snapshot taken after the full tap records "nothing happened" for exactly
        // the gestures that matter most. `down` is the state while the key is held;
        // `up` is what survives the release.
        const int idx = row.index >= 0 ? row.index : 0;
        (void)DispatchProbe::down(*rig.editor, CE{ CE::Type::ButtonDown, row.button, idx, 0 });
        const Digest afterDown = snap(rig);

        DispatchProbe::up(*rig.editor, CE{ CE::Type::ButtonUp, row.button, idx, 0 });
        // Snapshot before releasing the row's modifiers: a held scope is half the
        // gesture, and releasing first would erase the state the row just produced.
        const Digest afterUp = snap(rig);

        for (auto it = held.rbegin(); it != held.rend(); ++it)
            DispatchProbe::up(*rig.editor, CE{ CE::Type::ButtonUp, *it, 0, 0 });

        juce::String modList;
        for (CB m : held)
            modList += juce::String(buttonName(m)) + "+";

        juce::String line;
        line << layerName(row.layer) << " | " << modList << buttonName(row.button)
             << " idx=" << row.index << " | action=" << static_cast<int>(row.action) << "\n";

        const auto emit = [&line](const char* label, const Digest& from, const Digest& to) {
            int changes = 0;
            for (const auto& [k, v] : to)
            {
                const auto f = from.find(k);
                if (f != from.end() && f->second == v)
                    continue;
                line << "    " << label << " " << k << ": "
                     << (f == from.end() ? juce::String("<new>") : f->second) << " -> " << v
                     << "\n";
                ++changes;
            }
            return changes;
        };

        const int downChanges = emit("down", base, afterDown);
        const int upChanges = emit("up  ", afterDown, afterUp);
        if (downChanges == 0 && upChanges == 0)
            line << "    (no observable state change)\n";
        return line;
    }

    // ── Scripted scenarios (7b) ──────────────────────────────────────────────
    // The row matrix fires ONE gesture on a fresh rig, which structurally cannot
    // see a verb that depends on history. Paste is the clearest case: with an empty
    // clipboard it is a no-op, so `Track+Play` (PASTE TRACK) recorded "no observable
    // state change" -- the net would have stayed green if paste stopped working
    // entirely. These scripts give the verbs the history they need.
    struct Press
    {
        std::vector<CB> mods;
        CB button;
        int index = -1;
    };

    juce::String renderScenario(const char* name, const std::vector<Press>& script,
                                const Digest& base,
                                const std::function<void(LockstepProcessor&)>& setup = {})
    {
        Rig rig;
        freeze(rig);
        // Some gestures act on state no button can reach in a headless rig (a queued
        // scene needs a playing transport to arm). The setup hook stages that state on
        // the processor directly, so the SCRIPT under test is the only behaviour the
        // diff attributes to the keys.
        if (setup) setup(*rig.proc);
        bool first = true;
        for (const auto& p : script)
        {
            // A script fires in microseconds -- so pressing the same modifier twice
            // (copy, then paste) looks like a DOUBLE-TAP and silently LATCHES the
            // scope. That is a harness artifact masquerading as behaviour: the first
            // run of these scenarios recorded `latch.phrase: false -> true` and I
            // nearly believed it. Space the steps past the 350 ms window so a script
            // is a sequence of deliberate presses, which is what a human does.
            // The rig's clock is frozen (freeze()), so this ADVANCES time rather than
            // spending it: same semantics, without the wall-clock wait or the race.
            if (!first)
                DispatchProbe::advance(*rig.editor, GestureRecognizer::kDoubleTapMs + 60.0);
            first = false;

            for (CB m : p.mods)
                (void)DispatchProbe::down(*rig.editor, CE{ CE::Type::ButtonDown, m, 0, 0 });
            (void)DispatchProbe::down(*rig.editor,
                                      CE{ CE::Type::ButtonDown, p.button, p.index, 0 });
            DispatchProbe::up(*rig.editor, CE{ CE::Type::ButtonUp, p.button, p.index, 0 });
            for (auto it = p.mods.rbegin(); it != p.mods.rend(); ++it)
                DispatchProbe::up(*rig.editor, CE{ CE::Type::ButtonUp, *it, 0, 0 });
        }

        const Digest after = snap(rig);
        juce::String line;
        line << "SCENARIO " << name << "\n";
        int changes = 0;
        for (const auto& [k, v] : after)
        {
            const auto b = base.find(k);
            if (b != base.end() && b->second == v)
                continue;
            line << "    " << k << ": " << (b == base.end() ? juce::String("<new>") : b->second)
                 << " -> " << v << "\n";
            ++changes;
        }
        if (changes == 0)
            line << "    (no observable state change)\n";
        return line;
    }
}   // namespace

// ── 9.30 st.6: height reconciliation, measured ───────────────────────────────
// The chrome was reordered and two bands were deleted; this asserts the grid still
// has real estate in ALL THREE display modes, and that the new geometry holds the
// invariants the layout depends on. Traced through resized() on a real headless
// editor -- the project's own rule: verify real pixel dimensions, do not assume them.
void runChromeLayoutTests(int& failed)
{
    // PluginEditor's own setSize() — the default window the user actually gets.
    constexpr int kShipWidth = 990;
    constexpr int kShipHeight = 626;

    auto check = [&failed](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "FAIL [ChromeLayout] %s\n", what); ++failed; }
    };

    Rig rig;
    freeze(rig);
    auto& ed = *rig.editor;

    // Measure at the size the product SHIPS (the editor's own setSize), not at whatever
    // the golden rig happens to use. The first version of this test ran at the rig's
    // 1400x900 and passed happily with a 300px project rail planted in it -- a test that
    // cannot fail is not a test, and a layout test at the wrong size is exactly that.
    ed.setSize(kShipWidth, kShipHeight);

    for (const auto mode : { GridDisplayMode::Staggered,
                             GridDisplayMode::Ortholinear,
                             GridDisplayMode::Clean })
    {
        DispatchProbe::setGridMode(ed, mode);
        const auto kb  = DispatchProbe::kbBounds(ed);
        const auto nav = DispatchProbe::navArea(ed);

        check(kb.getHeight() > 200, "keyboard area keeps a real height in every grid mode");
        check(!nav.isEmpty(), "the nav strip has real estate");
        check(nav.getBottom() <= kb.getHeight(), "the nav strip fits inside the keyboard area");
        // Nothing may hang off the bottom of the window.
        check(kb.getBottom() <= ed.getHeight(), "the keyboard area fits inside the window");
    }

    // The inspector is the MZ's caption: it must sit DIRECTLY above it (9.30 st.1).
    const auto insp = DispatchProbe::inspector(ed);
    const auto mz   = DispatchProbe::mz(ed);
    check(insp.getBottom() <= mz.getY(), "the inspector sits above the MZ");
    check(mz.getY() - insp.getBottom() < 12, "...and DIRECTLY above it (it is its caption)");

    // The confirm pop-over must actually reach over the MZ -- if it fits inside the
    // chrome it is not a pop-over, and the whole point (§42.3) is lost.
    const auto pop = DispatchProbe::popover(ed);
    check(pop.getBottom() > mz.getY(), "the confirm pop-over extends DOWN over the MZ");

    // --- the transport band (play-test WI-2) --------------------------------------
    // The Sg:Sc pill is right-justified and width-matched to the project rail's control
    // row above. That alignment is the entire reason for the pill's width, so it is
    // MEASURED here rather than asserted in a comment beside the constant (9.30 st.6:
    // real pixels traced through resized(), never arithmetic done in prose). If the rail
    // widths move, this is what says so.
    {
        const auto pill = DispatchProbe::songScene(ed);
        const auto cap  = DispatchProbe::captureIndicator(ed);
        const auto band = DispatchProbe::transportBand(ed);
        const auto sync = DispatchProbe::railSyncBox(ed);   // leftmost rail control
        const auto pool = DispatchProbe::railPoolBtn(ed);   // rightmost rail control

        check(pill.getX() == sync.getX(),
              "Sg:Sc pill's left edge lines up with the rail row's leftmost control");
        check(pill.getRight() == pool.getRight(),
              "Sg:Sc pill's right edge lines up with the rail row's rightmost control");

        // The capture indicator moved off the hard-right flank into the middle. Its
        // resting width is the thing that was worth having: it was 150 and the flank
        // read as dead space beside the pill.
        check(cap.getRight() <= pill.getX(),
              "the capture indicator sits LEFT of the pill, not beside it on the flank");
        check(cap.getWidth() > 150,
              "...and gained resting room by moving there (was 150 on the flank)");
        check(!cap.isEmpty() && band.contains(cap), "the capture indicator is inside the band");
        check(band.contains(pill), "the pill is inside the band");
    }
    check(pop.getHeight() >= 2 * InspectorBar::kStatusLaneH, "...at (at least) double height");

    // The meter is the output column: right of the crossfader, beside the MZ.
    const auto meter = DispatchProbe::meter(ed);
    check(meter.getWidth() > 0 && meter.getHeight() > 40, "the master meter is a vertical column");
    check(meter.getX() > mz.getRight(), "...in the MZ's right flank");
    check(meter.getY() >= mz.getY() - 8, "...level with the MZ, not a strip across the top");
}

// 6.4: the direct Cue+Mute cue-toggle gesture (DESIGN §31) is an editor
// intercept, not a binding-table row, so the golden net does not cover it.
// Drive it through the real dispatch path and assert the processor's cue balance.
void runCueGestureTests(int& failed)
{
    using CB = ControllerButton;
    using CE = ControllerEvent;
    auto check = [&failed](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "FAIL [CueGesture] %s\n", what); ++failed; }
    };
    auto ev = [](CB b) { return CE{ CE::Type::ButtonDown, b, -1, 0 }; };
    auto evUp = [](CB b) { return CE{ CE::Type::ButtonUp, b, -1, 0 }; };

    Rig rig;
    freeze(rig);
    auto& ed = *rig.editor;
    auto& proc = *rig.proc;

    // Enter the Cue scope: Func + 3 (TapTempo).
    DispatchProbe::down(ed, ev(CB::Func));
    DispatchProbe::down(ed, ev(CB::TapTempo));
    check(DispatchProbe::ui(ed).cueHeld, "Func+3 enters the Cue scope");

    const int t = DispatchProbe::activeTrack(ed);
    check(t >= 0, "a track is focused");
    check(proc.getCueBalance(t) == 0.0f, "focused track starts uncued");

    // Cue + Mute toggles the focused track's cue balance to 1.
    DispatchProbe::down(ed, ev(CB::MuteScope));
    DispatchProbe::up(ed, evUp(CB::MuteScope));
    check(proc.getCueBalance(t) == 1.0f, "Cue+Mute cues the focused track");
    // The chord must NOT have entered mute-view.
    check(!DispatchProbe::ui(ed).muteHeld, "Cue+Mute does not open mute-view");

    // Again toggles it back off.
    DispatchProbe::down(ed, ev(CB::MuteScope));
    DispatchProbe::up(ed, evUp(CB::MuteScope));
    check(proc.getCueBalance(t) == 0.0f, "Cue+Mute again uncues the focused track");
}

// 6.4: the cue console overlay — Cue + hold(AMP) opens it to the param page (the
// MIXER twin); re-pressing AMP pages the track bank; Nav toggles to the flip page,
// where a step key arms a track's cue flip; a foreign scope exits.
void runCueConsoleTests(int& failed)
{
    using CB = ControllerButton;
    using CE = ControllerEvent;
    auto check = [&failed](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "FAIL [CueConsole] %s\n", what); ++failed; }
    };
    auto ev = [](CB b, int i = -1) { return CE{ CE::Type::ButtonDown, b, i, 0 }; };
    const int kAmp = LockstepProcessor::kAmpSecIdx;

    Rig rig;
    freeze(rig);
    auto& ed = *rig.editor;
    auto& proc = *rig.proc;

    // Enter Cue scope (Func+3), then open the console by HOLDING AMP past the long-
    // press threshold (Func remaps Section→MetaSection, exactly as production does).
    DispatchProbe::down(ed, ev(CB::Func));
    DispatchProbe::down(ed, ev(CB::TapTempo));
    check(DispatchProbe::ui(ed).cueHeld, "Func+3 enters the momentary Cue scope");
    DispatchProbe::down(ed, ev(CB::Section, kAmp));
    check(DispatchProbe::ui(ed).overlay != Overlay::Cue, "a short AMP press does not open yet");
    DispatchProbe::advance(ed, GestureRecognizer::kLongPressMs + 60.0);
    DispatchProbe::up(ed, ev(CB::Section, kAmp));
    check(DispatchProbe::ui(ed).overlay == Overlay::Cue, "Cue+hold(AMP) opens the console");
    check(DispatchProbe::ui(ed).cueParamPage, "console opens straight to the param (mixer) page");
    check(!DispatchProbe::ui(ed).cueHeld, "opening the console leaves the momentary scope");

    // Release the Func+3 chord — the console is sticky and survives (real usage).
    DispatchProbe::up(ed, ev(CB::TapTempo));
    DispatchProbe::up(ed, ev(CB::Func));
    check(DispatchProbe::ui(ed).overlay == Overlay::Cue, "console survives releasing Func+3");

    // Re-press AMP pages the track bank 1-8 <-> 9-16 (only with a second bank).
    if (kNumTracks > 8)
    {
        const int bank0 = DispatchProbe::ui(ed).cueBank;
        DispatchProbe::down(ed, ev(CB::Section, kAmp));
        check(DispatchProbe::ui(ed).cueBank == (bank0 ^ 1), "re-press AMP toggles the cue bank");
    }

    // Nav toggles to the flip page; a step there arms track N's cue flip.
    DispatchProbe::down(ed, ev(CB::NavRight));
    check(!DispatchProbe::ui(ed).cueParamPage, "Nav toggles to the flip page");
    check(proc.getCueBalance(2) == 0.0f, "track 2 starts uncued");
    DispatchProbe::down(ed, ev(CB::Step, 2));
    check(proc.getCueBalance(2) == 1.0f, "flip-page step arms/flips the track's cue");

    // A foreign cluster scope exits the console.
    DispatchProbe::down(ed, ev(CB::TrackScope));
    check(DispatchProbe::ui(ed).overlay == Overlay::None, "foreign scope exits the console");
}

void runDispatchGoldenTests(int& failed)
{
    const Digest base = [] { Rig r; freeze(r); return snap(r); }();

    juce::String out;
    out << "# Dispatch golden -- ROADMAP 9.12. Generated; re-bless with "
           "LOCKSTEP_REGEN_GOLDEN=1 and READ THE DIFF.\n"
        << "# One block per binding-table row: the UiState/processor fields the "
           "gesture changes.\n\n";

    for (const auto& row : kKeyBindings)
        out << renderCase(row, base) << "\n";

    // Verbs that need history. SelectTrack moves focus, so a copy-then-paste lands
    // on a DIFFERENT track and the trig census shows the paste arriving.
    out << "\n# Scenarios: verbs whose effect depends on prior state.\n\n";
    out << renderScenario("copy track 0 -> paste onto track 5",
                          { { { CB::TrackScope }, CB::VerbRecord },      // COPY TRACK
                            { {}, CB::SelectTrack, 5 },                  // focus track 5
                            { { CB::TrackScope }, CB::VerbPlay } },      // PASTE TRACK
                          base)
        << "\n";
    out << renderScenario("copy phrase -> paste phrase onto track 5",
                          { { { CB::PhraseScope }, CB::VerbRecord },
                            { {}, CB::SelectTrack, 5 },
                            { { CB::PhraseScope }, CB::VerbPlay } },
                          base)
        << "\n";
    out << renderScenario("snapshot, then restore (Func+Snapshot pops it)",
                          { { {}, CB::VerbSnapshot },
                            { { CB::Func }, CB::VerbSnapshot } },
                          base)
        << "\n";
    // 9.4 item C: SYNC only does anything when there is a deviation to discard, so the
    // per-row case (which starts from a clean project) cannot see it work. This PAIR is
    // decisive where a single net-diff is not: the first scenario deviates and the change
    // PERSISTS (deviated flag up, focus phrase swapped); the second does the same deviation
    // and then SYNCs, and nets back to the base -- so SYNC is what erased it, not a no-op.
    // 5.3 Item C: Phrase+step is LAUNCH, and only OCCUPIED rows launch. A fresh
    // project has just the diagonal phrase 0, so deviating to phrase 2 is now inert.
    // Create scene 2 in the setup (its diagonal makes phrase row 2 exist, though its
    // phrases stay empty), so the deviation has a real target and reproduces the
    // original observable diff (track 0 falls silent). The base digest is taken WITH
    // the same setup, so scene-2 creation nets out and only the deviation shows.
    auto occupyPhrase2 = [](LockstepProcessor& p) { p.createDefaultScene(2); };
    const Digest baseP2 = [&] { Rig r; freeze(r); occupyPhrase2(*r.proc); return snap(r); }();
    out << renderScenario("deviate track 0 to phrase 2 (persists)",
                          { { { CB::PhraseScope }, CB::Step, 2 } },
                          baseP2, occupyPhrase2)
        << "\n";
    out << renderScenario("deviate track 0 to phrase 2, then Scene+Clear (SYNC) reverts it",
                          { { { CB::PhraseScope }, CB::Step, 2 },
                            { { CB::SceneScope }, CB::VerbClear } },
                          baseP2, occupyPhrase2)
        << "\n";
    // 9.4 item C: cancel-queued-scene moved from Scene+O / Phrase+O to Func+P. Same
    // decisive pair as SYNC -- the queue (staged directly, since a headless rig has no
    // playing transport to arm it) PERSISTS on its own, and Func+Scene+P clears it.
    out << renderScenario("queue a scene (persists)",
                          {},
                          base,
                          [](LockstepProcessor& p) { p.queueScene(1, false); })
        << "\n";
    out << renderScenario("queue a scene, then Func+Scene+P (CANCEL) drops it",
                          { { { CB::Func, CB::SceneScope }, CB::VerbConfirm } },
                          base,
                          [](LockstepProcessor& p) { p.queueScene(1, false); })
        << "\n";
    out << renderScenario("clear track (arms confirm), then confirm",
                          { { { CB::TrackScope }, CB::VerbClear },
                            { {}, CB::VerbConfirm } },
                          base)
        << "\n";
    // 7c: page navigation is invisible on a 16-step track -- there is only one page,
    // so prev/next clamp and the digest sees nothing. Grow the pattern to 32 steps
    // first (Func+Up), THEN page: this is the only scenario in the net where kbd.page
    // can move at all, and without it the whole page family migrates uncovered.
    // (A scenario prints the delta from the baseline to its END state, so
    // right-right-left would net back to page 0 and look like nothing happened --
    // paging clamps at the last page. Two rights on a 2-page pattern land on page 1.)
    out << renderScenario("grow to 2 pages, then page right (clamps at the last page)",
                          { { { CB::Func }, CB::NavUp },
                            { {}, CB::NavRight },
                            { {}, CB::NavRight } },
                          base)
        << "\n";
    out << renderScenario("grow to 2 pages, page right, then page back left",
                          { { { CB::Func }, CB::NavUp },
                            { {}, CB::NavRight },
                            { {}, CB::NavLeft } },
                          base)
        << "\n";
    // Rotate is content motion with NO change in trig count -- the census-only digest
    // recorded it as nothing at all. Two rotations left then one right must land one
    // step off where they started.
    out << renderScenario("rotate left twice, right once",
                          { { { CB::Func }, CB::NavLeft },
                            { { CB::Func }, CB::NavLeft },
                            { { CB::Func }, CB::NavRight } },
                          base)
        << "\n";
    out << renderScenario("double snapshot deepens the stack",
                          { { {}, CB::VerbSnapshot }, { {}, CB::VerbSnapshot } }, base)
        << "\n";

    const juce::File golden { juce::String(LOCKSTEP_TEST_DIR) + "/goldens/dispatch.txt" };

    if (std::getenv("LOCKSTEP_REGEN_GOLDEN") != nullptr)
    {
        golden.getParentDirectory().createDirectory();
        // LF, explicitly: replaceWithText() defaults to writing CRLF, which would
        // make the file never compare equal to the LF text we generate here -- a
        // golden that fails on correct code teaches you to ignore it.
        golden.replaceWithText(out, false, false, "\n");
        std::fprintf(stderr, "[golden] re-blessed %s (%d rows)\n",
                     golden.getFullPathName().toRawUTF8(),
                     static_cast<int>(kKeyBindings.size()));
        return;
    }

    if (!golden.existsAsFile())
    {
        std::fprintf(stderr, "FAIL [DispatchGolden] no golden at %s -- generate it with "
                             "LOCKSTEP_REGEN_GOLDEN=1\n",
                     golden.getFullPathName().toRawUTF8());
        ++failed;
        return;
    }

    const juce::String want = golden.loadFileAsString();
    if (want == out)
    {
        std::fprintf(stderr, "[golden] dispatch behaviour matches (%d rows)\n",
                     static_cast<int>(kKeyBindings.size()));
        return;
    }

    // Report the first differing line: enough to identify the family that moved.
    juce::StringArray a, b;
    a.addLines(want);
    b.addLines(out);
    for (int i = 0; i < juce::jmax(a.size(), b.size()); ++i)
    {
        const juce::String la = i < a.size() ? a[i] : juce::String("<end>");
        const juce::String lb = i < b.size() ? b[i] : juce::String("<end>");
        if (la != lb)
        {
            std::fprintf(stderr,
                         "FAIL [DispatchGolden] dispatch behaviour changed at line %d\n"
                         "  golden: %s\n"
                         "  actual: %s\n"
                         "If this change was intended, re-bless with LOCKSTEP_REGEN_GOLDEN=1.\n",
                         i + 1, la.toRawUTF8(), lb.toRawUTF8());
            break;
        }
    }
    ++failed;
}
}   // namespace lockstep

namespace lockstep
{
    // Defined in UiDriverSelfTest.cpp -- the interaction harness proving itself.
    void runUiDriverSelfTests(int& failed);
    // Defined in SyntheticKeyboardTest.cpp -- the real QWERTY path.
    void runSyntheticKeyboardTests(int& failed);
    // Defined in SyntheticMouseTest.cpp -- the real mouse path, at UI scale.
    void runSyntheticMouseTests(int& failed);
    // Defined in ProductTypefaceTest.cpp -- the embedded typeface actually resolves.
    void runProductTypefaceTests(int& failed);
    // Defined in SurfaceRenderTest.cpp -- the editor renders, reproducibly.
    void runSurfaceRenderTests(int& failed);
    // Defined in RegionOracleTest.cpp -- drawn cells match the model that describes them.
    void runRegionOracleTests(int& failed);
    // Defined in RepaintRegionTest.cpp -- the editor invalidates the pixels it paints.
    void runRepaintRegionTests(int& failed);
    // Defined in MzDriveTest.cpp -- the rotaries, driven by real drags.
    void runMzDriveTests(int& failed);
    // Defined in SceneGoldenTest.cpp -- whole blessed frames, fuzzily compared.
    void runSceneGoldenTests(int& failed);
    // Defined in CujTrigAuthoringTest.cpp -- Group A critical user journeys.
    void runCujTrigAuthoringTests(int& failed);
    // Defined in CujGeneratorsTest.cpp -- Group B critical user journeys.
    void runCujGeneratorsTests(int& failed);
    // Defined in CujRecordTest.cpp -- Group F realtime record journey.
    void runCujRecordTests(int& failed);
    // Defined in CujClipboardTest.cpp -- Group A clipboard journey.
    void runCujClipboardTests(int& failed);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    int failed = 0;
    lockstep::runDispatchGoldenTests(failed);
    lockstep::runChromeLayoutTests(failed);
    lockstep::runCueGestureTests(failed);
    lockstep::runCueConsoleTests(failed);
    lockstep::runUiDriverSelfTests(failed);
    lockstep::runSyntheticKeyboardTests(failed);
    lockstep::runSyntheticMouseTests(failed);
    lockstep::runProductTypefaceTests(failed);
    lockstep::runSurfaceRenderTests(failed);
    lockstep::runRegionOracleTests(failed);
    lockstep::runRepaintRegionTests(failed);
    lockstep::runMzDriveTests(failed);
    lockstep::runSceneGoldenTests(failed);
    lockstep::runCujTrigAuthoringTests(failed);
    lockstep::runCujGeneratorsTests(failed);
    lockstep::runCujRecordTests(failed);
    lockstep::runCujClipboardTests(failed);
    std::fprintf(stderr, failed == 0 ? "All dispatch golden tests passed.\n"
                                     : "%d dispatch golden test(s) FAILED.\n", failed);
    return failed == 0 ? 0 : 1;
}
