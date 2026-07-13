// KeyBindingTest — golden-tuple tests for the key-binding table.
// Verifies: (button, mods, layer) → ActionId; table invariants (no duplicate
// rows, primary/hint non-null); heldModsFromUiState bitmask derivation.

#include "TestHarness.h"
#include "../src/command/KeyBindings.h"
#include "../src/state/UiState.h"
#include <bit>   // std::popcount

namespace lockstep
{
    using CB = ControllerButton;
    using AId = ActionId;
    using SL = SurfaceLayer;

    static AId resolve(CB btn, uint16_t mods, int idx = -1)
    {
        return resolveBinding(btn, idx, mods, SL::Base).action;
    }

    static AId resolveHold(CB btn, uint16_t mods, int idx = -1)
    {
        return resolveBinding(btn, idx, mods, SL::Base, Gesture::Hold).action;
    }

    // ── heldModsFromUiState ───────────────────────────────────────────────────
    static void testHeldMods()
    {
        {
            UiState ui;
            CHECK(heldModsFromUiState(ui) == kModNone, "empty UiState → kModNone");
        }
        {
            UiState ui;
            ui.funcHeld = true;
            CHECK(heldModsFromUiState(ui) == kModFunc, "funcHeld → kModFunc");
        }
        {
            UiState ui;
            ui.trackHeld = true;
            CHECK(heldModsFromUiState(ui) == kModTrack, "trackHeld → kModTrack");
        }
        {
            UiState ui;
            ui.morphHeld = true;
            ui.funcHeld = true;
            CHECK(heldModsFromUiState(ui) == (kModMorph | kModFunc), "morph+func bits");
        }
        {
            UiState ui;
            ui.muteHeld = true;
            ui.sceneHeld = true;
            CHECK(heldModsFromUiState(ui) == (kModMute | kModScene), "mute+scene bits");
        }
    }

    // ── Modifier cluster ──────────────────────────────────────────────────────
    static void testModifierCluster()
    {
        CHECK(resolve(CB::Func, kModNone) == AId::HoldFuncScope, "Func bare");
        CHECK(resolve(CB::TrackScope, kModNone) == AId::HoldTrackScope, "Track bare");
        CHECK(resolve(CB::TrackScope, kModFunc) == AId::HoldMachineScope, "Func+Track = MACHINE scope (9.29)");
        CHECK(resolve(CB::PhraseScope, kModNone) == AId::HoldPhraseScope, "Phrase bare");
        CHECK(resolve(CB::SceneScope, kModNone) == AId::HoldSceneScope, "Scene bare");
        CHECK(resolve(CB::MorphScope, kModNone) == AId::HoldMorphScope, "Morph bare");
        CHECK(resolve(CB::SongScope, kModNone) == AId::HoldSongScope, "Song bare");
        CHECK(resolve(CB::SongScope, kModFunc) == AId::FocusGlobal, "Func+Song = SET scope (9.29)");
        CHECK(resolve(CB::MuteScope, kModNone) == AId::HoldMuteScope, "Mute bare");
        CHECK(resolve(CB::MuteScope, kModScene) == AId::HoldSceneMuteView, "Scene+Mute = S-MUTE");
        CHECK(resolve(CB::FillScope, kModNone) == AId::HoldFillScope, "Fill bare");
    }

    // ── TAP ───────────────────────────────────────────────────────────────────
    static void testTap()
    {
        CHECK(resolve(CB::TapTempo, kModNone) == AId::TapTempo, "TAP bare");
        // Func+3 is reserved (metronome was here pre-9.10; now in TIME band).
        // No binding row → resolves to bare row (lowest popcount wins).
        CHECK(resolve(CB::TapTempo, kModFunc) == AId::TapTempo, "Func+TAP = bare fallback");

        // Labels — no MET hint post-9.11
        CHECK(juce::String(resolveBinding(CB::TapTempo, -1, kModNone, SL::Base).primary) == "TAP TEMPO", "TAP TEMPO primary");
        CHECK(juce::String(hintFor(CB::TapTempo, -1, kModNone, SL::Base)).isEmpty(), "TAP hint empty");
    }

    // ── NavUp / ^ ─────────────────────────────────────────────────────────────
    static void testNavUp()
    {
        CHECK(resolve(CB::NavUp, kModNone) == AId::NavTrackUp, "NavUp bare");
        CHECK(resolve(CB::NavUp, kModFunc) == AId::LengthDouble, "Func+NavUp = ×2");
        CHECK(resolve(CB::NavUp, kModTrack) == AId::CycleInputModeUp, "Track+NavUp = cycle");
        CHECK(resolve(CB::NavUp, kModMorph) == AId::MorphPickPoleA, "Morph+NavUp = A");
        // 7c: Func+Morph = A, not ×2. The cascade always checked morphHeld FIRST, so
        // the key picked the pole; the old row said ×2 and was simply wrong. Morph wins.
        CHECK(resolve(CB::NavUp, kModFunc | kModMorph) == AId::MorphPickPoleA,
              "Func+Morph+NavUp = A (Morph outranks Func — the row used to lie)");
        // 7c: Phrase+Nav = transpose (10.9), declared at last. Bare = octave, Func = semitone.
        CHECK(resolve(CB::NavUp, kModPhrase) == AId::TransposeUp, "Phrase+NavUp = TRANSPOSE");
        CHECK(resolve(CB::NavUp, kModPhrase | kModFunc) == AId::TransposeUp, "Func+Phrase+NavUp = TRANSPOSE");
        // Func+Track: explicit combined row = cycle (Track wins over Func ×2).
        CHECK(resolve(CB::NavUp, kModFunc | kModTrack) == AId::CycleInputModeUp, "Func+Track+NavUp = cycle");

        // Label content
        CHECK(juce::String(hintFor(CB::NavUp, -1, kModNone, SL::Base)) == juce::String(u8"×2"),
              "NavUp bare hint = ×2");
        CHECK(juce::String(hintFor(CB::NavUp, -1, kModTrack, SL::Base)).isEmpty(),
              "Track+NavUp has no hint");
    }

    // ── Nav left/right (E/T) ──────────────────────────────────────────────────
    static void testNavLeftRight()
    {
        CHECK(resolve(CB::NavLeft, kModNone) == AId::NavPageLeft, "NavLeft bare");
        CHECK(resolve(CB::NavLeft, kModFunc) == AId::RotateLeft, "Func+NavLeft = ←ROT");
        // 7c: Track+←/→ PAGES (the mode cycle is up/down only, README §5.17). The
        // CycleInputModeLeft/Right rows never matched dispatch and are gone.
        CHECK(resolve(CB::NavLeft, kModTrack) == AId::NavPageLeft, "Track+NavLeft = page (not cycle)");
        CHECK(resolve(CB::NavRight, kModNone) == AId::NavPageRight, "NavRight bare");
        CHECK(resolve(CB::NavRight, kModFunc) == AId::RotateRight, "Func+NavRight = ROT→");
        CHECK(resolve(CB::NavRight, kModTrack) == AId::NavPageRight, "Track+NavRight = page (not cycle)");

        // Hints present for bare, absent under Track and Func
        CHECK(!juce::String(hintFor(CB::NavLeft, -1, kModNone, SL::Base)).isEmpty(), "NavLeft has hint");
        CHECK(juce::String(hintFor(CB::NavLeft, -1, kModTrack, SL::Base)) == juce::String(u8"←ROT"),
              "Track+NavLeft falls back to the bare page row (hint = ←ROT)");
        CHECK(juce::String(hintFor(CB::NavLeft, -1, kModFunc, SL::Base)).isEmpty(), "Func+NavLeft no hint");
    }

    // ── NavDown / v (R) ───────────────────────────────────────────────────────
    static void testNavDown()
    {
        // 7c: bare NavDown moves the FOCUS TRACK down; it never shifted the octave.
        // (Octave is a LAYER behaviour on ←/→ inside NoteEdit / CHROMATIC — see below.)
        CHECK(resolve(CB::NavDown, kModNone) == AId::NavTrackDown, "NavDown bare = focus track down");
        CHECK(resolve(CB::NavDown, kModFunc) == AId::LengthHalve, "Func+NavDown = ÷2");
        CHECK(resolve(CB::NavDown, kModMorph) == AId::MorphPickPoleB, "Morph+NavDown = B");
        CHECK(resolve(CB::NavDown, kModFunc | kModMorph) == AId::MorphPickPoleB,
              "Func+Morph+NavDown = B (Morph outranks Func)");
        CHECK(resolve(CB::NavDown, kModPhrase) == AId::TransposeDown, "Phrase+NavDown = TRANSPOSE");
        // Octave lives on the NoteEdit / CHROMATIC layers, where ←/→ stop paging.
        CHECK(resolveBinding(CB::NavLeft, -1, kModNone, SL::NoteEdit).action == AId::NavOctaveDown,
              "NoteEdit layer: ← = OCT-");
        CHECK(resolveBinding(CB::NavRight, -1, kModNone, SL::ChromaticInput).action == AId::NavOctaveUp,
              "CHROMATIC layer: → = OCT+");
        CHECK(resolveBinding(CB::NavLeft, -1, kModFunc, SL::ChromaticInput).action == AId::NavOctaveDown,
              "CHROMATIC layer: Func+← keeps the octave role (rotate steps aside)");
        CHECK(resolve(CB::NavDown, kModTrack) == AId::CycleInputModeDown, "Track+NavDown = cycle");
    }

    // ── Verb row Y-P ──────────────────────────────────────────────────────────
    static void testVerbRow()
    {
        // Y: SNAP / RESTORE
        CHECK(resolve(CB::VerbSnapshot, kModNone) == AId::VerbSnapshot, "Y bare = SNAP");
        CHECK(resolve(CB::VerbSnapshot, kModFunc) == AId::VerbRestore, "Func+Y = RESTORE");

        // U: REC / COPY under scope (not Morph). Scene bare = BAKE; Func+Scene = COPY.
        CHECK(resolve(CB::VerbRecord, kModNone) == AId::VerbRecord, "U bare = REC");
        CHECK(resolve(CB::VerbRecord, kModTrack) == AId::VerbCopy, "Track+U = COPY");
        CHECK(resolve(CB::VerbRecord, kModPhrase) == AId::VerbCopy, "Phrase+U = COPY");
        CHECK(resolve(CB::VerbRecord, kModScene) == AId::VerbBakeScene, "Scene+U = BAKE");
        CHECK(resolve(CB::VerbRecord, kModScene | kModFunc) == AId::VerbCopy, "Func+Scene+U = COPY");
        CHECK(resolve(CB::VerbRecord, kModSong) == AId::VerbCopy, "Song+U = COPY");
        // Morph does NOT relabel REC to COPY
        CHECK(resolve(CB::VerbRecord, kModMorph) == AId::VerbRecord, "Morph+U stays REC");

        // I: PLAY / PASTE under scope (not Morph). Scene bare = inert (no row); Func+Scene = PASTE.
        CHECK(resolve(CB::VerbPlay, kModNone) == AId::VerbPlay, "I bare = PLAY");
        CHECK(resolve(CB::VerbPlay, kModTrack) == AId::VerbPaste, "Track+I = PASTE");
        CHECK(resolve(CB::VerbPlay, kModMorph) == AId::VerbPlay, "Morph+I stays PLAY");
        CHECK(resolve(CB::VerbPlay, kModScene) == AId::VerbPlay, "Scene+I = PLAY (no PASTE row; bare is inert in dispatch)");
        CHECK(resolve(CB::VerbPlay, kModScene | kModFunc) == AId::VerbPaste, "Func+Scene+I = PASTE");

        // O: CLEAR on the tap, DELETE on the HOLD (9.29). Func no longer turns Clear
        // into Delete -- Func+Track is the Machine scope, and its Clear is INIT.
        CHECK(resolve(CB::VerbClear, kModNone) == AId::VerbClear, "O bare = CLEAR");
        CHECK(resolve(CB::VerbClear, kModFunc) == AId::VerbClear,
              "Func+O = CLEAR (Func qualifies the clear; it is not a delete)");
        CHECK(resolve(CB::VerbClear, kModTrack) == AId::VerbScopedClear, "Track+O = CLEAR");
        CHECK(resolve(CB::VerbClear, kModTrack | kModFunc) == AId::MachineInit,
              "Machine (Func+Track) + O = INIT the sound");
        CHECK(resolveHold(CB::VerbClear, kModTrack) == AId::VerbDelete, "Track+hold O = DEL TRACK");
        CHECK(resolveHold(CB::VerbClear, kModPhrase) == AId::VerbDelete, "Phrase+hold O = DEL PHRASE");
        CHECK(resolveHold(CB::VerbClear, kModScene) == AId::VerbDelete, "Scene+hold O = DEL SCENE");
        CHECK(resolveHold(CB::VerbClear, kModSong) != AId::VerbDelete,
              "Song+hold O = no delete (Song owns no deletable entity)");
        CHECK(resolve(CB::VerbClear, kModMorph) == AId::VerbMorphBake, "Morph+O = BAKE");
        CHECK(resolve(CB::VerbClear, kModMorph | kModFunc) == AId::VerbMorphErase, "Func+Morph+O = ERASE");

        // P: CONFIRM / CANCEL
        CHECK(resolve(CB::VerbConfirm, kModNone) == AId::VerbConfirm, "P bare = CONFIRM");
        CHECK(resolve(CB::VerbConfirm, kModFunc) == AId::VerbCancel, "Func+P = CANCEL");
    }

    // ── Section keys ─────────────────────────────────────────────────────────
    static void testSectionKeys()
    {
        for (int s = 0; s < 5; ++s)
        {
            CHECK(resolve(CB::Section, kModNone, s) == AId::SelectSection, "Section bare");
            CHECK(resolve(CB::Section, kModFunc, s) == AId::SelectMetaSection, "Func+Section");
        }
        // Section 5 (FX): tap = navigate; hold = picker, scope-gated (9.14).
        // Func+FX retired (picker freed). Track+hold = track picker, Song+hold =
        // master picker; bare hold advertises no picker.
        CHECK(resolve(CB::Section, kModFunc, 5) == AId::SelectMetaSection, "Func+FX = meta-nav (picker freed)");
        CHECK(resolve(CB::Section, kModNone, 5) == AId::SelectSection, "FX bare = section select");
        CHECK(resolveHold(CB::Section, kModTrack, 5) == AId::OpenTrackFxPicker, "Track+hold FX = track picker");
        CHECK(resolveHold(CB::Section, kModSong, 5) == AId::OpenMasterFxPicker, "Song+hold-FX = master picker");
        CHECK(resolveHold(CB::Section, kModNone, 5) == AId::None, "bare hold FX = no picker (scope-gated)");

        // Section 1 (SRC): same rule, one section over (9.29). Track+hold picks the
        // machine this track runs. Bare hold(SRC) must advertise NO picker -- it is
        // the OnDemand machine console's own gesture, and the scope gate is the only
        // thing keeping the two apart.
        CHECK(resolve(CB::Section, kModTrack, 1) == AId::SelectSection,
              "Track+tap SRC = section select (tap navigates)");
        CHECK(resolveHold(CB::Section, kModTrack, 1) == AId::OpenMachinePicker,
              "Track+hold SRC = machine picker (9.29)");
        CHECK(resolveHold(CB::Section, kModNone, 1) == AId::None,
              "bare hold SRC = no picker (scope-gated; that hold is the machine console)");
    }

    // ── Universal hint rule ───────────────────────────────────────────────────
    // 9.12 st.8: the hint is no longer STORED, so there is no stored value to check
    // against the rule -- hintFor() IS the rule. What is worth pinning is that the rule
    // still says what the surface needs it to say, so these are the cases the old data
    // column existed to express, asked of the function instead.
    static void testHintRule()
    {
        auto hint = [](CB b, uint16_t mods) {
            return juce::String(hintFor(b, -1, mods, SL::Base));
        };
        CHECK(hint(CB::VerbSnapshot, kModNone) == "RESTORE", "Y: Func variant is RESTORE");
        CHECK(hint(CB::VerbConfirm, kModNone) == "CANCEL",  "P: Func variant is CANCEL");
        CHECK(hint(CB::TrackScope, kModNone) == "MACHINE",  "Track: Func variant is the MACHINE scope");
        CHECK(hint(CB::SongScope, kModNone) == "SET",       "Song: Func variant is the SET scope");
        CHECK(hint(CB::NavUp, kModNone) == juce::String(u8"×2"), "Nav up: Func variant is x2");
        CHECK(hint(CB::VerbClear, kModTrack) == "INIT",
              "Track+Clear: the Func variant is the MACHINE scope's INIT (9.29)");

        // No Func variant → no secondary. Adding Func must change the ACTION, not just
        // the label, or the key would advertise a gesture that does the same thing.
        CHECK(hint(CB::TapTempo, kModNone).isEmpty(), "TAP has no Func variant");
        CHECK(hint(CB::NavUp, kModTrack).isEmpty(),
              "Track+Nav: Func changes nothing (the row is the same action)");
        // A row that already carries Func cannot gain another one.
        CHECK(hint(CB::NavLeft, kModFunc).isEmpty(), "Func+Nav: no second Func to add");
    }

    // ── Label-length invariants ───────────────────────────────────────────────
    // Count UTF-8 code points (not bytes). Continuation bytes (10xxxxxx) are skipped.
    // Tap rows land in the large primary slot (15pt): hard limit = 8 code points.
    // Hold/DoubleTap rows land in secondary rail slots (smaller font): limit = 12.
    static std::size_t utf8Length(const char8_t* s) noexcept
    {
        if (s == nullptr) return 0;
        std::size_t n = 0;
        for (; *s != u8'\0'; ++s)
            if ((*s & 0xC0u) != 0x80u)
                ++n;
        return n;
    }

    static void testLabelLengths()
    {
        for (const auto& row : kKeyBindings)
        {
            // Section rows delegate labels to ScopedSectionMatrix — empty is correct.
            if (row.button == CB::Section) continue;

            CHECK(row.primary != nullptr, "primary non-null (testLabelLengths)");

            const std::size_t pLen = utf8Length(row.primary);

            // A row renders in the large 15pt PRIMARY slot only when its gesture is
            // the promoted one for its context (limit 8). Otherwise it lands in a
            // small secondary rail (limit 12) — e.g. TapTempo's Tap "TAP TEMPO" is
            // demoted because the Hold "GEN HUB" row is promoted to primary.
            const Gesture prom = promotedGesture(row.button, row.index,
                                                 row.requiredMods, row.layer);
            const std::size_t pLimit = (row.gesture == prom) ? 8u : 12u;
            CHECK(pLen <= pLimit, "primary label within slot limit");
            // The hint is a Func variant's PRIMARY, so its length is already covered by
            // the primary limit above — one fact, checked once (9.12 st.8).
        }
    }

    // ── Table invariants ─────────────────────────────────────────────────────
    static void testTableInvariants()
    {
        // 1. All rows have a non-null primary.
        for (const auto& row : kKeyBindings)
            CHECK(row.primary != nullptr, "primary non-null");

        // 2. No two rows for the same (button, index, layer, gesture) have identical requiredMods.
        for (std::size_t i = 0; i < kKeyBindings.size(); ++i)
        {
            const auto& a = kKeyBindings[i];
            for (std::size_t j = i + 1; j < kKeyBindings.size(); ++j)
            {
                const auto& b = kKeyBindings[j];
                if (a.button == b.button && a.index == b.index
                    && a.layer == b.layer && a.gesture == b.gesture)
                    CHECK(a.requiredMods != b.requiredMods,
                          "no duplicate (btn,idx,layer,gesture,mods) rows");
            }
        }

        // 3. Resolve against no-match always returns ActionId::None.
        CHECK(resolveBinding(CB::None, -1, kModNone, SL::Base).action == AId::None,
              "unrecognised button → None");
    }

    // ── Most-specific-wins priority ───────────────────────────────────────────
    static void testMostSpecificWins()
    {
        // Func+Track (popcount 2) beats both Func-only (1) and Track-only (1).
        CHECK(resolve(CB::NavUp, kModFunc | kModTrack) == AId::CycleInputModeUp,
              "Func+Track (pp2) beats Func and Track alone");
        // Song+Func (pp2) beats Song-only (pp1).
        CHECK(resolve(CB::SongScope, kModSong | kModFunc) == AId::FocusGlobal,
              "Song+Func: Func+Song row wins via popcount 2");
    }

    // ── Gesture axis (9.12) ──────────────────────────────────────────────────
    static AId resolveG(CB btn, uint16_t mods, Gesture g, int idx = -1)
    {
        return resolveBinding(btn, idx, mods, SL::Base, g).action;
    }

    static void testGestureResolution()
    {
        // TapTempo: tap = TAP TEMPO; hold = GEN HUB.
        CHECK(resolveG(CB::TapTempo, kModNone, Gesture::Tap)  == AId::TapTempo,      "TAP tap");
        CHECK(resolveG(CB::TapTempo, kModNone, Gesture::Hold) == AId::OpenGeneratorHub, "TAP hold = GEN HUB");
        CHECK(resolveG(CB::TapTempo, kModNone, Gesture::DoubleTap) == AId::None,     "TAP dbl = none");

        // Func: legacy Tap row = HoldFuncScope (backwards compat); hold = FUNC LAYER; dbl = ESCAPE.
        // (The Tap row exists so display code before Stage 2 still finds FUNC label.)
        CHECK(resolveG(CB::Func, kModNone, Gesture::Tap)       == AId::HoldFuncScope,"Func tap = legacy scope row");
        CHECK(resolveG(CB::Func, kModNone, Gesture::Hold)      == AId::HoldFuncScope,"Func hold");
        CHECK(resolveG(CB::Func, kModNone, Gesture::DoubleTap) == AId::FuncEscape,   "Func dbl = ESCAPE");

        // TrackScope: hold (primary); dbl = LATCH.
        CHECK(resolveG(CB::TrackScope, kModNone, Gesture::Hold)      == AId::HoldTrackScope,  "Track hold");
        CHECK(resolveG(CB::TrackScope, kModNone, Gesture::DoubleTap) == AId::LatchTrackScope, "Track dbl = LATCH");

        // PhraseScope dbl = LATCH.
        CHECK(resolveG(CB::PhraseScope, kModNone, Gesture::DoubleTap) == AId::LatchPhraseScope, "Phrase dbl");

        // VerbPlay layered stop (display rows; dispatch is editor-owned via playTapCount):
        // dbl = track CUT, triple = MASTER CUT. Rewind moved to VerbRecord hold = RESET.
        CHECK(resolveG(CB::VerbPlay, kModNone, Gesture::DoubleTap) == AId::TransportTrackCut,  "Play dbl = CUT");
        CHECK(resolveG(CB::VerbPlay, kModNone, Gesture::TripleTap) == AId::TransportMasterCut, "Play triple = MASTER CUT");
        CHECK(resolveG(CB::VerbRecord, kModNone, Gesture::Hold)    == AId::PlayStopReset,       "Record hold = RESET");

        // NavRight dbl = UNLOCK.
        CHECK(resolveG(CB::NavRight, kModNone, Gesture::DoubleTap) == AId::NavPageUnlock, "NavRight dbl = UNLOCK");

        // VerbSnapshot Func+hold = RESTORE → FLOOR.
        CHECK(resolveG(CB::VerbSnapshot, kModFunc, Gesture::Hold) == AId::RestoreFloor, "Func+Y hold = FLOOR");

        // RecordArm: tap = REC ARM; dbl = OVERDUB.
        CHECK(resolveG(CB::RecordArm, kModNone, Gesture::Tap)       == AId::RecordArmToggle,  "RecordArm tap");
        CHECK(resolveG(CB::RecordArm, kModNone, Gesture::DoubleTap) == AId::RecordArmOverdub, "RecordArm dbl");

        // PlayStop: tap = PLAY/STOP.
        CHECK(resolveG(CB::PlayStop, kModNone, Gesture::Tap) == AId::PlayStopToggle, "PlayStop tap");
    }

    static void testPromotedGesture()
    {
        // TapTempo: explicit promoted Hold row → Hold promoted.
        CHECK(promotedGesture(CB::TapTempo, -1, kModNone, SL::Base) == Gesture::Hold,
              "TapTempo: hold promoted (explicit)");

        // TrackScope: explicit promoted Hold row → Hold.
        CHECK(promotedGesture(CB::TrackScope, -1, kModNone, SL::Base) == Gesture::Hold,
              "TrackScope: hold promoted");

        // Func: Hold row present but not promoted → Hold still wins (pass 2).
        CHECK(promotedGesture(CB::Func, -1, kModNone, SL::Base) == Gesture::Hold,
              "Func: hold wins via pass-2");

        // VerbPlay: only tap + dbl-tap rows, no hold → Tap promoted.
        CHECK(promotedGesture(CB::VerbPlay, -1, kModNone, SL::Base) == Gesture::Tap,
              "VerbPlay: tap promoted (no hold row)");

        // NavRight bare: only tap + dbl-tap → Tap promoted.
        CHECK(promotedGesture(CB::NavRight, -1, kModNone, SL::Base) == Gesture::Tap,
              "NavRight: tap promoted");
    }

    void runKeyBindingTests()
    {
        testHeldMods();
        testModifierCluster();
        testTap();
        testNavUp();
        testNavLeftRight();
        testNavDown();
        testVerbRow();
        testSectionKeys();
        testTableInvariants();
        testMostSpecificWins();
        testHintRule();
        testLabelLengths();
        testGestureResolution();
        testPromotedGesture();
    }
}
