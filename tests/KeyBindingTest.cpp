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
        CHECK(resolve(CB::TrackScope, kModFunc) == AId::OpenMachinePicker, "Func+Track = KIT");
        CHECK(resolve(CB::PhraseScope, kModNone) == AId::HoldPhraseScope, "Phrase bare");
        CHECK(resolve(CB::SceneScope, kModNone) == AId::HoldSceneScope, "Scene bare");
        CHECK(resolve(CB::MorphScope, kModNone) == AId::HoldMorphScope, "Morph bare");
        CHECK(resolve(CB::SongScope, kModNone) == AId::HoldSongScope, "Song bare");
        CHECK(resolve(CB::SongScope, kModFunc) == AId::FocusGlobal, "Func+Song = GLOBAL");
        CHECK(resolve(CB::MuteScope, kModNone) == AId::HoldMuteScope, "Mute bare");
        CHECK(resolve(CB::MuteScope, kModScene) == AId::HoldSceneMuteView, "Scene+Mute = S-MUTE");
        CHECK(resolve(CB::FillScope, kModNone) == AId::HoldFillScope, "Fill bare");
    }

    // ── TAP ───────────────────────────────────────────────────────────────────
    static void testTap()
    {
        CHECK(resolve(CB::TapTempo, kModNone) == AId::TapTempo, "TAP bare");
        CHECK(resolve(CB::TapTempo, kModFunc) == AId::MetronomeToggle, "Func+TAP = MET");

        // Labels
        CHECK(juce::String(resolveBinding(CB::TapTempo, -1, kModNone, SL::Base).primary) == "TAP", "TAP primary");
        CHECK(juce::String(resolveBinding(CB::TapTempo, -1, kModNone, SL::Base).hint) == "MET", "TAP hint=MET");
        CHECK(juce::String(resolveBinding(CB::TapTempo, -1, kModFunc, SL::Base).primary) == "MET", "MET primary");
        CHECK(juce::String(resolveBinding(CB::TapTempo, -1, kModFunc, SL::Base).hint).isEmpty(), "MET hint empty");
    }

    // ── NavUp / ^ ─────────────────────────────────────────────────────────────
    static void testNavUp()
    {
        CHECK(resolve(CB::NavUp, kModNone) == AId::NavTrackUp, "NavUp bare");
        CHECK(resolve(CB::NavUp, kModFunc) == AId::LengthDouble, "Func+NavUp = ×2");
        CHECK(resolve(CB::NavUp, kModTrack) == AId::CycleInputModeUp, "Track+NavUp = cycle");
        CHECK(resolve(CB::NavUp, kModMorph) == AId::MorphPickPoleA, "Morph+NavUp = A");
        // Func+Morph: explicit combined row preserves dispatch order (×2 not A).
        CHECK(resolve(CB::NavUp, kModFunc | kModMorph) == AId::LengthDouble, "Func+Morph+NavUp = ×2 (not A)");
        // Func+Track: explicit combined row = cycle (Track wins over Func ×2).
        CHECK(resolve(CB::NavUp, kModFunc | kModTrack) == AId::CycleInputModeUp, "Func+Track+NavUp = cycle");

        // Label content
        CHECK(juce::String(resolveBinding(CB::NavUp, -1, kModNone, SL::Base).hint) == juce::String(u8"×2"),
              "NavUp bare hint = ×2");
        CHECK(juce::String(resolveBinding(CB::NavUp, -1, kModTrack, SL::Base).hint).isEmpty(),
              "Track+NavUp has no hint");
    }

    // ── Nav left/right (E/T) ──────────────────────────────────────────────────
    static void testNavLeftRight()
    {
        CHECK(resolve(CB::NavLeft, kModNone) == AId::NavPageLeft, "NavLeft bare");
        CHECK(resolve(CB::NavLeft, kModFunc) == AId::RotateLeft, "Func+NavLeft = ←ROT");
        CHECK(resolve(CB::NavLeft, kModTrack) == AId::CycleInputModeLeft, "Track+NavLeft = cycle");
        CHECK(resolve(CB::NavRight, kModNone) == AId::NavPageRight, "NavRight bare");
        CHECK(resolve(CB::NavRight, kModFunc) == AId::RotateRight, "Func+NavRight = ROT→");
        CHECK(resolve(CB::NavRight, kModTrack) == AId::CycleInputModeRight, "Track+NavRight = cycle");

        // Hints present for bare, absent under Track and Func
        CHECK(!juce::String(resolveBinding(CB::NavLeft, -1, kModNone, SL::Base).hint).isEmpty(), "NavLeft has hint");
        CHECK(juce::String(resolveBinding(CB::NavLeft, -1, kModTrack, SL::Base).hint).isEmpty(), "Track+NavLeft no hint");
        CHECK(juce::String(resolveBinding(CB::NavLeft, -1, kModFunc, SL::Base).hint).isEmpty(), "Func+NavLeft no hint");
    }

    // ── NavDown / v (R) ───────────────────────────────────────────────────────
    static void testNavDown()
    {
        CHECK(resolve(CB::NavDown, kModNone) == AId::NavOctaveDown, "NavDown bare");
        CHECK(resolve(CB::NavDown, kModFunc) == AId::LengthHalve, "Func+NavDown = ÷2");
        CHECK(resolve(CB::NavDown, kModMorph) == AId::MorphPickPoleB, "Morph+NavDown = B");
        CHECK(resolve(CB::NavDown, kModFunc | kModMorph) == AId::LengthHalve, "Func+Morph+NavDown = ÷2 (not B)");
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

        // O: CLEAR / DEL. Scope+Func = DEL (Track/Phrase/Scene). Morph = BAKE; Morph+Func = ERASE.
        CHECK(resolve(CB::VerbClear, kModNone) == AId::VerbClear, "O bare = CLEAR");
        CHECK(resolve(CB::VerbClear, kModFunc) == AId::VerbDelete, "Func+O = DEL");
        CHECK(resolve(CB::VerbClear, kModTrack) == AId::VerbScopedClear, "Track+O = CLEAR");
        CHECK(resolve(CB::VerbClear, kModTrack | kModFunc) == AId::VerbDelete, "Func+Track+O = DEL");
        CHECK(resolve(CB::VerbClear, kModPhrase | kModFunc) == AId::VerbDelete, "Func+Phrase+O = DEL");
        CHECK(resolve(CB::VerbClear, kModScene | kModFunc) == AId::VerbDelete, "Func+Scene+O = DEL");
        CHECK(resolve(CB::VerbClear, kModMorph) == AId::VerbMorphBake, "Morph+O = BAKE");
        CHECK(resolve(CB::VerbClear, kModMorph | kModFunc) == AId::VerbMorphErase, "Func+Morph+O = ERASE");

        // P: YES / NO
        CHECK(resolve(CB::VerbConfirm, kModNone) == AId::VerbConfirm, "P bare = YES");
        CHECK(resolve(CB::VerbConfirm, kModFunc) == AId::VerbCancel, "Func+P = NO");
    }

    // ── Section keys ─────────────────────────────────────────────────────────
    static void testSectionKeys()
    {
        for (int s = 0; s < 5; ++s)
        {
            CHECK(resolve(CB::Section, kModNone, s) == AId::SelectSection, "Section bare");
            CHECK(resolve(CB::Section, kModFunc, s) == AId::SelectMetaSection, "Func+Section");
        }
        // Section 5 (FX): Func = FX picker
        CHECK(resolve(CB::Section, kModFunc, 5) == AId::OpenTrackFxPicker, "Func+FX = picker");
        CHECK(resolve(CB::Section, kModNone, 5) == AId::SelectSection, "FX bare = section select");
    }

    // ── Universal hint rule ───────────────────────────────────────────────────
    // hint == Func-variant primary when the action differs; otherwise hint is empty.
    // Section rows are skipped (labels live in ScopedSectionMatrix).
    // Rows with Func already in requiredMods are skipped (adding Func is a no-op).
    static void testHintRule()
    {
        for (const auto& row : kKeyBindings)
        {
            if (row.button == CB::Section) continue;
            if (row.requiredMods & kModFunc) continue;  // Func-held rows: no hint expected

            const auto& fv = resolveBinding(row.button, row.index,
                                            row.requiredMods | kModFunc,
                                            row.layer);
            const bool hasFuncVariant = (fv.action != AId::None && fv.action != row.action);
            if (hasFuncVariant)
            {
                const juce::String expected(fv.primary);
                CHECK(juce::String(row.hint) == expected,
                      "hint must equal Func-variant primary");
            }
            else
            {
                CHECK(juce::String(row.hint).isEmpty(),
                      "no Func-variant → hint must be empty");
            }
        }
    }

    // ── Label-length invariants ───────────────────────────────────────────────
    // Count UTF-8 code points (not bytes). Continuation bytes (10xxxxxx) are skipped.
    // ≤6 code points preferred for 15 pt primaries; 8 is the hard limit.
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
            CHECK(row.hint != nullptr, "hint non-null (testLabelLengths)");

            const std::size_t pLen = utf8Length(row.primary);
            const std::size_t hLen = utf8Length(row.hint);

            CHECK(pLen <= 8, "primary label ≤8 code points");
            CHECK(hLen <= 8, "hint label ≤8 code points");
        }
    }

    // ── Table invariants ─────────────────────────────────────────────────────
    static void testTableInvariants()
    {
        // 1. All rows have non-null primary and hint.
        for (const auto& row : kKeyBindings)
        {
            CHECK(row.primary != nullptr, "primary non-null");
            CHECK(row.hint != nullptr, "hint non-null");
        }

        // 2. No two rows for the same (button, index, layer) have identical requiredMods.
        for (std::size_t i = 0; i < kKeyBindings.size(); ++i)
        {
            const auto& a = kKeyBindings[i];
            for (std::size_t j = i + 1; j < kKeyBindings.size(); ++j)
            {
                const auto& b = kKeyBindings[j];
                if (a.button == b.button && a.index == b.index && a.layer == b.layer)
                    CHECK(a.requiredMods != b.requiredMods, "no duplicate (btn,idx,layer,mods) rows");
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
    }
}
