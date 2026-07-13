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

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"
#include "../src/command/KeyBindings.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include "../src/ui/mode/GestureRecognizer.h"
#include <cstdio>
#include <map>
#include <memory>

namespace lockstep
{
    // The one seam into dispatch (friended in PluginEditor.h): the entry points stay
    // private to production code.
    struct DispatchProbe
    {
        // FIDELITY (7b). Every real input path -- QWERTY, mouse, controller -- runs
        // the raw event through resolveLayer() BEFORE dispatch (ButtonLayers.h says
        // so in its first line). The first version of this probe called dispatchDown
        // directly and skipped it, so `Func+Y` never became CB::Restore and the
        // golden recorded RESTORE as "pushes a second checkpoint" -- a path the
        // product never takes. A net that models a different input path than the
        // instrument is worse than no net: it is green about fiction.
        static ControllerEvent layered(const LockstepEditor& ed, ControllerEvent raw)
        {
            const UiState& u = ed.uiState_;
            const LayerContext lctx{ u.funcHeld,
                                     u.trackHeld || u.latch.track,
                                     u.muteHeld || u.latch.mute };
            return resolveLayer(raw, lctx);
        }
        static bool down(LockstepEditor& ed, ControllerEvent ev)
        {
            return ed.dispatchDown(layered(ed, ev), 0);
        }
        static void up(LockstepEditor& ed, ControllerEvent ev)
        {
            ed.dispatchUp(layered(ed, ev), 0);
        }
        static const UiState& ui(const LockstepEditor& ed) { return ed.uiState_; }
        static const Clipboard& clip(const LockstepEditor& ed) { return ed.clipboard_; }
        // 7c: the step page lives in KeyboardArea, not UiState, so a UiState-only
        // digest recorded every page-nav gesture as "(no observable state change)".
        static int page(const LockstepEditor& ed) { return ed.keyboardArea_.currentPage(); }
    };
}

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
    static_assert(sizeof(UiState) == 1240,
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
        put(d, "samplePropsPoolIndex", u.samplePropsPoolIndex);
        put(d, "timeEntryScope", u.timeEntryScope);
        put(d, "sigPage", static_cast<int>(u.sigPage));

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

    // ---------------------------------------------------------------------------
    // A fresh editor per case: gestures mutate state, so cases must not contaminate
    // each other. Heap, always (see the Arrangement note at the top).
    // ---------------------------------------------------------------------------
    struct Rig
    {
        std::unique_ptr<LockstepProcessor> proc;
        std::unique_ptr<LockstepEditor> editor;

        Rig()
        {
            proc = std::make_unique<LockstepProcessor>();
            proc->setRateAndBufferSizeDetails(44100.0, 512);

            // PRIOR STATE (7b). A blank project makes destructive verbs invisible:
            // Clear on an empty phrase changes nothing, so the golden would record
            // "no observable state change" and stay green even if Clear stopped
            // working. Seed trigs so Clear/Paste/Init have something to destroy.
            auto& seq = proc->sequence();
            for (std::size_t t = 0; t < 4; ++t)
                for (int s = 0; s < 16; s += 4)
                    seq.tracks[t].steps[static_cast<std::size_t>(s)].trig = true;

            editor = std::make_unique<LockstepEditor>(*proc);
            editor->setSize(1400, 900);
        }
        ~Rig() { editor.reset(); }   // editor before processor: it holds a reference

        Digest snap() const
        {
            Digest d = digest(DispatchProbe::ui(*editor), *proc);
            for (auto& [k, v] : verbDigest(*editor, *proc))
                d[k] = v;
            return d;
        }
    };

    juce::String renderCase(const KeyBinding& row, const Digest& base)
    {
        Rig rig;

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
        const Digest afterDown = rig.snap();

        DispatchProbe::up(*rig.editor, CE{ CE::Type::ButtonUp, row.button, idx, 0 });
        // Snapshot before releasing the row's modifiers: a held scope is half the
        // gesture, and releasing first would erase the state the row just produced.
        const Digest afterUp = rig.snap();

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
                                const Digest& base)
    {
        Rig rig;
        bool first = true;
        for (const auto& p : script)
        {
            // GestureRecognizer reads the wall clock, and a script fires in
            // microseconds -- so pressing the same modifier twice (copy, then paste)
            // looks like a DOUBLE-TAP and silently LATCHES the scope. That is a
            // harness artifact masquerading as behaviour: the first run of these
            // scenarios recorded `latch.phrase: false -> true` and I nearly believed
            // it. Space the steps past the 350 ms window so a script is a sequence of
            // deliberate presses, which is what a human does.
            if (!first)
                juce::Thread::sleep(static_cast<int>(GestureRecognizer::kDoubleTapMs) + 60);
            first = false;

            for (CB m : p.mods)
                (void)DispatchProbe::down(*rig.editor, CE{ CE::Type::ButtonDown, m, 0, 0 });
            (void)DispatchProbe::down(*rig.editor,
                                      CE{ CE::Type::ButtonDown, p.button, p.index, 0 });
            DispatchProbe::up(*rig.editor, CE{ CE::Type::ButtonUp, p.button, p.index, 0 });
            for (auto it = p.mods.rbegin(); it != p.mods.rend(); ++it)
                DispatchProbe::up(*rig.editor, CE{ CE::Type::ButtonUp, *it, 0, 0 });
        }

        const Digest after = rig.snap();
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

void runDispatchGoldenTests(int& failed)
{
    const Digest base = Rig{}.snap();

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

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    int failed = 0;
    lockstep::runDispatchGoldenTests(failed);
    std::fprintf(stderr, failed == 0 ? "All dispatch golden tests passed.\n"
                                     : "%d dispatch golden test(s) FAILED.\n", failed);
    return failed == 0 ? 0 : 1;
}
