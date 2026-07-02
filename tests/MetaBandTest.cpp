// MetaBandTest — unit tests for the density routing SSOT and MetaRotary::applyView totality.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/ui/MetaBand.h"
#include "../src/ui/MetaRotary.h"
#include "../src/state/UiState.h"
#include "../src/io/EditContext.h"

namespace lockstep
{
    // -------------------------------------------------------------------------
    // resolveMetaBand precedence table

    static void testResolveMetaBandMasterSection()
    {
        UiState ui;
        ui.masterSection = 0;  CHECK(resolveMetaBand(ui) == MetaBand::Cond,      "masterSection 0 → Cond");
        ui.masterSection = 1;  CHECK(resolveMetaBand(ui) == MetaBand::Trig,      "masterSection 1 → Trig");
        ui.masterSection = 2;  CHECK(resolveMetaBand(ui) == MetaBand::Transport, "masterSection 2 → Transport");
        ui.masterSection = 3;  CHECK(resolveMetaBand(ui) == MetaBand::Divider,   "masterSection 3 → Divider");
        ui.masterSection = 4;  CHECK(resolveMetaBand(ui) == MetaBand::PhraseLen, "masterSection 4 → PhraseLen");
        ui.masterSection = 5;  CHECK(resolveMetaBand(ui) == MetaBand::Global,    "masterSection 5 → Global");
    }

    static void testResolveMetaBandTransientOutranksMasterSection()
    {
        // Transient overlays outrank the latched masterSection page so that
        // arming euclid (or holding a modifier) over a latched DIV/LEN page
        // updates the band immediately instead of going stale.
        {
            UiState ui;
            ui.masterSection = 1;        // latched Trig page
            ui.euclidHeld = true;        // but euclid is armed
            CHECK(resolveMetaBand(ui) == MetaBand::Euclidean,
                  "euclidHeld outranks latched masterSection");
        }
        {
            UiState ui;
            ui.masterSection = 3;        // latched Divider page
            ui.overlay = Overlay::Density;
            ui.densitySubPage = UiState::DensitySubPage::Amount;
            CHECK(resolveMetaBand(ui) == MetaBand::Density,
                  "density sticky outranks latched masterSection");
        }
        {
            // 9.10: Func+Song density peek removed; falls through to Swing (Song held).
            UiState ui;
            ui.masterSection = 4;        // latched PhraseLen page
            ui.funcHeld = true;
            ui.songHeld = true;
            CHECK(resolveMetaBand(ui) == MetaBand::Swing,
                  "Func+Song outranks latched masterSection (via Swing now, 9.10)");
        }
        {
            // With nothing transient active, the latched page still resolves.
            UiState ui;
            ui.masterSection = 1;
            CHECK(resolveMetaBand(ui) == MetaBand::Trig,
                  "latched masterSection resolves when no transient is active");
        }
    }

    static void testResolveMetaBandEuclid()
    {
        UiState ui;
        ui.euclidHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Euclidean, "euclidHeld → Euclidean");
    }

    // 9.17: Func+7 Transport band exposes the Set-level launch-quantize grid in
    // slot 4 — value resolution + write routing into project().launchQuant.
    static void testTransportLaunchQuantField()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;

        // Default project grid is Bar (== index 2); the field renders "Bar".
        auto f = buildMetaBand(MetaBand::Transport, 0, proc, 0, ctx, ui);
        CHECK(f[4].active && juce::String(f[4].label) == "LaunchQ", "slot 4 = LaunchQ");
        CHECK(f[4].stepped, "LaunchQ is stepped");
        CHECK(feq(f[4].minValue, 0.0f) && feq(f[4].maxValue, 5.0f),
              "LaunchQ range 0..5 (Set grid excludes PhraseEnd)");
        CHECK(feq(f[4].value, static_cast<float>(LaunchQuant::Bar)), "LaunchQ default value = Bar");
        CHECK(juce::String(f[4].valueText) == "Bar", "LaunchQ default value-text = Bar");

        // Write Beat (index 1) → project().launchQuant updated, value-text tracks.
        writeMetaField(MetaBand::Transport, 0, 4, 1.0f, proc, 0, ctx, ui);
        CHECK(proc.project().launchQuant == static_cast<int>(LaunchQuant::Beat),
              "writeMetaField slot 4 sets launchQuant to Beat");
        f = buildMetaBand(MetaBand::Transport, 0, proc, 0, ctx, ui);
        CHECK(juce::String(f[4].valueText) == "Beat", "LaunchQ value-text = Beat after write");

        // Out-of-range write clamps into the Set grid (never PhraseEnd=6).
        writeMetaField(MetaBand::Transport, 0, 4, 9.0f, proc, 0, ctx, ui);
        CHECK(proc.project().launchQuant == static_cast<int>(LaunchQuant::Bars8),
              "writeMetaField slot 4 clamps to Bars8 (max Set grid)");
    }

    // 10.7: Melodic generator band — resolution, field layout, write round-trip.
    static void testMelodicBand()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;

        // Resolution: melodicHeld → Melodic.
        ui.melodicHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Melodic, "melodicHeld → Melodic");

        // Six active fields with the expected stepped labels.
        const auto f = buildMetaBand(MetaBand::Melodic, 0, proc, 0, ctx, ui);
        CHECK(f[0].active && juce::String(f[0].label) == "DENSE", "field 0 = DENSE");
        CHECK(f[1].active && juce::String(f[1].label) == "CORE",  "field 1 = CORE");
        CHECK(f[2].active && juce::String(f[2].label) == "CNTR",  "field 2 = CNTR");
        CHECK(f[3].active && juce::String(f[3].label) == "OCTS",  "field 3 = OCTS");
        CHECK(f[4].active && juce::String(f[4].label) == "LEAP",  "field 4 = LEAP");
        CHECK(f[5].active && juce::String(f[5].label) == "SEED",  "field 5 = SEED");
        CHECK(f[6].active && juce::String(f[6].label) == "SRC",   "field 6 = SRC");
        CHECK(!f[7].active, "field 7 inactive");
        CHECK(juce::String(f[6].valueText) == "Gen", "SRC default value-text = Gen");

        // CORE value-text tracks the stepped enum (penta default).
        CHECK(juce::String(f[1].valueText) == "Penta", "CORE default value-text = Penta");

        // Write round-trip into the UiState staging area, with clamping.
        writeMetaField(MetaBand::Melodic, 0, 1, 2.0f, proc, 0, ctx, ui);  // CORE → Full
        CHECK(ui.melodyCore == 2, "writeMetaField CORE sets melodyCore");
        writeMetaField(MetaBand::Melodic, 0, 2, 3.0f, proc, 0, ctx, ui);  // CNTR → Walk
        CHECK(ui.melodyContour == 3, "writeMetaField CNTR sets melodyContour");
        writeMetaField(MetaBand::Melodic, 0, 3, 9.0f, proc, 0, ctx, ui);  // OCTS clamps to 4
        CHECK(ui.melodyOctaves == 4, "writeMetaField OCTS clamps to 4");
        writeMetaField(MetaBand::Melodic, 0, 5, 42.0f, proc, 0, ctx, ui); // SEED
        CHECK(ui.melodySeed == 42, "writeMetaField SEED sets melodySeed");
        writeMetaField(MetaBand::Melodic, 0, 6, 1.0f, proc, 0, ctx, ui);  // SRC → Keep
        CHECK(ui.melodySource == 1, "writeMetaField SRC sets melodySource");
    }

    // 10.8: Harmonic voice-mover band — resolution, field layout, write round-trip.
    static void testHarmonyBand()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;

        // Resolution: harmonyHeld → Harmony.
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};   // one default triad (length starts at 1)
        CHECK(resolveMetaBand(ui) == MetaBand::Harmony, "harmonyHeld → Harmony");

        // Field layout: V1..V3 active voices, V4 = OFF add-slot, then LEN/CUR/MOVE/OCT.
        const auto f = buildMetaBand(MetaBand::Harmony, 0, proc, 0, ctx, ui);
        CHECK(f[0].active && juce::String(f[0].label) == "V1", "field 0 = V1");
        CHECK(f[1].active && juce::String(f[1].label) == "V2", "field 1 = V2");
        CHECK(f[2].active && juce::String(f[2].label) == "V3", "field 2 = V3");
        CHECK(f[3].harmonyVoiceOff, "V4 add-slot is the blank add slot (3-voice default)");
        CHECK(f[4].active && juce::String(f[4].label) == "LEN", "field 4 = LEN");
        CHECK(juce::String(f[4].valueText) == "1", "LEN default = 1 (progression starts narrow)");
        CHECK(f[5].active && juce::String(f[5].label) == "CUR", "field 5 = CUR");
        CHECK(juce::String(f[5].valueText) == "1", "CUR default = 1 (1-based)");
        CHECK(f[6].active && juce::String(f[6].label) == "MOVE", "field 6 = MOVE");
        CHECK(f[7].active && juce::String(f[7].label) == "OCT",  "field 7 = OCT");

        // Add a 4th voice via the OFF slot. W6b: a newly-added voice ignores the
        // written knob index and instead starts near the chord's current register
        // (the ladder rung nearest the mean of the present voices), nudged off any
        // collision — so it must NOT land on the written index 6, and must be unique.
        writeMetaField(MetaBand::Harmony, 0, 3, 6.0f, proc, 0, ctx, ui);
        CHECK(ui.harmonyProg.chords[0].voiceCount == 4, "writing the OFF slot adds a voice");
        {
            const auto& ch = ui.harmonyProg.chords[0];
            CHECK(ch.voice[3] != 6,
                  "added voice ignores the written index (starts at chord's average register)");
            bool uniqueAdd = true;
            for (int j = 0; j < 3; ++j)
                if (ch.voice[static_cast<std::size_t>(j)] == ch.voice[3]) uniqueAdd = false;
            CHECK(uniqueAdd, "added voice does not collide with an existing voice");
        }

        // Off-detent removes the top voice again.
        writeMetaField(MetaBand::Harmony, 0, 3, -1.0f, proc, 0, ctx, ui);
        CHECK(ui.harmonyProg.chords[0].voiceCount == 3, "off-detent removes the top voice");

        // LEN grow clones the last chord; CUR selects; MOVE shifts all voices.
        writeMetaField(MetaBand::Harmony, 0, 4, 6.0f, proc, 0, ctx, ui);   // LEN → 6
        CHECK(ui.harmonyProg.length == 6, "LEN write grows the progression");
        writeMetaField(MetaBand::Harmony, 0, 5, 3.0f, proc, 0, ctx, ui);   // CUR → 3 (idx 2)
        CHECK(ui.harmonyProg.cursor == 2, "CUR write selects the chord (0-based store)");

        const auto before = ui.harmonyProg.chords[2].voice;
        writeMetaField(MetaBand::Harmony, 0, 6, 1.0f, proc, 0, ctx, ui);   // MOVE +1 degree
        bool shifted = true;
        for (int v = 0; v < ui.harmonyProg.chords[2].voiceCount; ++v)
            if (ui.harmonyProg.chords[2].voice[static_cast<std::size_t>(v)]
                != before[static_cast<std::size_t>(v)] + 1) shifted = false;
        CHECK(shifted, "MOVE shifts every voice of the cursor chord by one degree");
    }

    static void testHarmonyLosslessGrow()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};   // length 1, reach 1

        // Grow 1 -> 3: brand-new slots clone the previous chord.
        writeMetaField(MetaBand::Harmony, 0, 4, 3.0f, proc, 0, ctx, ui);
        CHECK(ui.harmonyProg.length == 3 && ui.harmonyProg.reach == 3, "grow advances length + reach");

        // Author slot 2 (cursor -> 3, MOVE +1) so it differs from the clone seed.
        writeMetaField(MetaBand::Harmony, 0, 5, 3.0f, proc, 0, ctx, ui);   // CUR -> 3 (idx 2)
        const auto authored = ui.harmonyProg.chords[2].voice;
        writeMetaField(MetaBand::Harmony, 0, 6, 1.0f, proc, 0, ctx, ui);   // MOVE +1
        bool moved = (ui.harmonyProg.chords[2].voice[0] == authored[0] + 1);
        CHECK(moved, "slot 2 authored away from its clone seed");
        const auto slot2 = ui.harmonyProg.chords[2].voice;

        // Shrink 3 -> 1 must NOT destroy slots 1/2 (reach stays 3).
        writeMetaField(MetaBand::Harmony, 0, 4, 1.0f, proc, 0, ctx, ui);
        CHECK(ui.harmonyProg.length == 1 && ui.harmonyProg.reach == 3, "shrink keeps reach (lossless)");

        // Grow 1 -> 3 again restores the authored slot 2 rather than re-cloning.
        writeMetaField(MetaBand::Harmony, 0, 4, 3.0f, proc, 0, ctx, ui);
        bool restored = true;
        for (int v = 0; v < kHarmonyVoices; ++v)
            if (ui.harmonyProg.chords[2].voice[static_cast<std::size_t>(v)]
                != slot2[static_cast<std::size_t>(v)]) restored = false;
        CHECK(restored, "re-grow restores the previously-authored chord (lossless)");
    }

    static void testHarmonyReelAndChroma()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};   // one default triad, 3 voices

        // Voice cells render as chord-reel columns; the 4th slot (past the 3
        // default voices) is the blank "add" slot.
        auto f = buildMetaBand(MetaBand::Harmony, 0, proc, 0, ctx, ui);
        CHECK(f[0].harmonyVoiceCell && f[1].harmonyVoiceCell, "V1/V2 = reel columns");
        CHECK(!f[0].reelNow.isEmpty(), "voice column shows its current-chord note");
        CHECK(f[3].harmonyVoiceCell && f[3].harmonyVoiceOff, "V4 = blank add slot");
        CHECK(!f[0].harmonyChromatic, "bare band is diatonic");

        // The reel rows are the prev/current/next CHORD: with one chord there are
        // no neighbours, so prev/next are blank.
        CHECK(f[0].reelPrev.isEmpty() && f[0].reelNext.isEmpty(), "single chord has no neighbours");
        ui.harmonyProg.length = 2;
        ui.harmonyProg.reach = 2;
        ui.harmonyProg.chords[1] = ui.harmonyProg.chords[0];
        ui.harmonyProg.cursor = 1;                       // current = chord 1
        f = buildMetaBand(MetaBand::Harmony, 0, proc, 0, ctx, ui);
        CHECK(!f[0].reelPrev.isEmpty(), "cursor on chord 2 shows chord 1 as the prev row");
        ui.harmonyProg = HarmonyProgression{};           // reset

        // Func held flips the voice cells to chromatic adjust.
        ui.funcHeld = true;
        f = buildMetaBand(MetaBand::Harmony, 0, proc, 0, ctx, ui);
        CHECK(f[0].harmonyChromatic, "Func held → chromatic voice adjust");

        // Func+voice chromatic nudge moves the pitch one semitone (borrowed tone).
        const KeySig key = proc.effectiveKeySig();
        const auto ladder = harmonyLadder(key, kHarmonyRootBase + key.root, kHarmonyOctaves);
        auto& ch0 = ui.harmonyProg.chords[0];
        const int before = resolveVoice(ladder, ch0.voice[0], ch0.chroma[0]);
        nudgeHarmonyChroma(proc, ui, 0, 1);
        const int after = resolveVoice(ladder, ch0.voice[0], ch0.chroma[0]);
        CHECK(after == before + 1, "Func+voice nudges the pitch one semitone");

        // A bare (diatonic) write lands on a free rung (1; the default chord holds
        // rungs 0/2/4) and clears the borrowed offset.
        ch0.chroma[0] = 2;
        writeMetaField(MetaBand::Harmony, 0, 0, 1.0f, proc, 0, ctx, ui);
        CHECK(ui.harmonyProg.chords[0].voice[0] == 1 && ui.harmonyProg.chords[0].chroma[0] == 0,
              "a bare diatonic write sets the rung and clears the offset");
    }

    // Item 9: the chord reel is cyclic — the last chord shows above the first and
    // the first below the last, with real (in-sequence) neighbours flagged apart
    // from wrapped ones so the renderer can dim the seam. CUR opts into wrap.
    static void testHarmonyCyclicReel()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};
        // Three distinct chords so wrapped vs real neighbours are unambiguous.
        ui.harmonyProg.length = 3;
        ui.harmonyProg.reach = 3;
        for (int k = 0; k < 3; ++k)
        {
            ui.harmonyProg.chords[static_cast<std::size_t>(k)] = ui.harmonyProg.chords[0];
            ui.harmonyProg.chords[static_cast<std::size_t>(k)].voice[0] = k;  // differ per chord
        }

        // Cursor on the FIRST chord: prev wraps to the last (wrapped), next is real.
        ui.harmonyProg.cursor = 0;
        auto f = buildMetaBand(MetaBand::Harmony, 0, proc, 0, ctx, ui);
        CHECK(!f[0].reelPrev.isEmpty(), "first chord shows the last chord above (cyclic)");
        CHECK(f[0].reelPrevWrapped, "prev at the first chord is flagged wrapped");
        CHECK(!f[0].reelNextWrapped, "next at the first chord is a real neighbour");

        // Cursor on the LAST chord: next wraps to the first (wrapped), prev is real.
        ui.harmonyProg.cursor = 2;
        f = buildMetaBand(MetaBand::Harmony, 0, proc, 0, ctx, ui);
        CHECK(!f[0].reelNext.isEmpty(), "last chord shows the first chord below (cyclic)");
        CHECK(f[0].reelNextWrapped, "next at the last chord is flagged wrapped");
        CHECK(!f[0].reelPrevWrapped, "prev at the last chord is a real neighbour");

        // Interior cursor: both neighbours are real (unwrapped).
        ui.harmonyProg.cursor = 1;
        f = buildMetaBand(MetaBand::Harmony, 0, proc, 0, ctx, ui);
        CHECK(!f[0].reelPrevWrapped && !f[0].reelNextWrapped,
              "interior cursor has two real neighbours");

        // CUR opts into encoder wrap (the cyclic-advance affordance).
        CHECK(f[5].wrap, "CUR field enables cyclic encoder wrap");
    }

    static void testHarmonySingleVoiceRemoval()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};
        auto& ch = ui.harmonyProg.chords[0];
        ch.voiceCount = 3;
        ch.voice = { { 1, 3, 5, 0 } };
        ch.chroma = {};

        // Turning the BASS (V1) below the floor removes ONLY that voice; the upper
        // voices shift down and survive (the old behaviour dropped them all).
        writeMetaField(MetaBand::Harmony, 0, 0, -1.0f, proc, 0, ctx, ui);
        CHECK(ch.voiceCount == 2, "removing the bass leaves the other voices");
        CHECK(ch.voice[0] == 3 && ch.voice[1] == 5, "upper voices shift down to fill");

        // An inner voice off: again only that one goes.
        ch.voiceCount = 3;
        ch.voice = { { 1, 3, 5, 0 } };
        writeMetaField(MetaBand::Harmony, 0, 1, -1.0f, proc, 0, ctx, ui);
        CHECK(ch.voiceCount == 2 && ch.voice[0] == 1 && ch.voice[1] == 5,
              "removing an inner voice keeps bass + top");

        // The last remaining voice clamps at the floor rather than emptying the chord.
        ch.voiceCount = 1;
        ch.voice = { { 4, 0, 0, 0 } };
        writeMetaField(MetaBand::Harmony, 0, 0, -1.0f, proc, 0, ctx, ui);
        CHECK(ch.voiceCount == 1 && ch.voice[0] == 0,
              "last voice clamps at floor, never empties");
    }

    static void testHarmonyChromaAll()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};

        const KeySig key = proc.effectiveKeySig();
        const auto ladder = harmonyLadder(key, kHarmonyRootBase + key.root, kHarmonyOctaves);
        auto& ch = ui.harmonyProg.chords[0];
        const int vc = ch.voiceCount;
        std::array<int, kHarmonyVoices> before{};
        for (int v = 0; v < vc; ++v)
            before[static_cast<std::size_t>(v)] =
                resolveVoice(ladder, ch.voice[static_cast<std::size_t>(v)],
                             ch.chroma[static_cast<std::size_t>(v)]);

        // Func+MOVE: every voice slides one semitone (whole-chord borrowed tone).
        nudgeHarmonyChromaAll(proc, ui, 1);
        bool allShifted = true;
        for (int v = 0; v < vc; ++v)
            if (resolveVoice(ladder, ch.voice[static_cast<std::size_t>(v)],
                             ch.chroma[static_cast<std::size_t>(v)])
                != before[static_cast<std::size_t>(v)] + 1) allShifted = false;
        CHECK(allShifted, "chroma-all shifts every voice one semitone");
    }

    static void testHarmonyNoRepeatedNotes()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};
        auto& ch = ui.harmonyProg.chords[0];
        ch.voiceCount = 3;
        ch.voice = { { 1, 3, 5, 0 } };   // three distinct rungs
        ch.chroma = {};

        // Diatonic note edit: dragging V1 onto V2's rung (3) must NOT create a
        // repeated note — it skips over to the next free rung (here, 4).
        writeMetaField(MetaBand::Harmony, 0, 0, 3.0f, proc, 0, ctx, ui);
        CHECK(ch.voice[0] != ch.voice[1] && ch.voice[0] != ch.voice[2],
              "diatonic edit never lands on another voice's pitch");
        CHECK(ch.voice[0] == 4, "edit skips the taken rung to the next free one");

        // Chromatic nudge: V1 at rung 4 nudged up a semitone toward V2 (rung 5).
        // If the borrowed pitch would collide it skips on; never a duplicate.
        ch.voice = { { 4, 5, 7, 0 } };
        ch.chroma = {};
        const KeySig key = proc.effectiveKeySig();
        const auto ladder = harmonyLadder(key, kHarmonyRootBase + key.root, kHarmonyOctaves);
        for (int n = 0; n < 4; ++n)
            nudgeHarmonyChroma(proc, ui, 0, 1);
        const int p0 = resolveVoice(ladder, ch.voice[0], ch.chroma[0]);
        const int p1 = resolveVoice(ladder, ch.voice[1], ch.chroma[1]);
        const int p2 = resolveVoice(ladder, ch.voice[2], ch.chroma[2]);
        CHECK(p0 != p1 && p0 != p2, "chromatic nudge never produces a repeated note");
    }

    static void testHarmonyTransposeRangeGuard()
    {
        EngineHarness h;
        auto& proc = h.processor();
        EditContext ctx;
        UiState ui;
        ui.harmonyHeld = true;
        ui.harmonyProg = HarmonyProgression{};

        const KeySig key = proc.effectiveKeySig();
        const auto ladder = harmonyLadder(key, kHarmonyRootBase + key.root, kHarmonyOctaves);
        const int ladderMax = static_cast<int>(ladder.size()) - 1;
        auto& ch = ui.harmonyProg.chords[0];

        // Top voice sits at the very top rung: a +1 diatonic MOVE would push it
        // past the ladder, so the WHOLE transpose is disallowed (no squashing).
        ch.voiceCount = 3;
        ch.voice = { { ladderMax - 2, ladderMax - 1, ladderMax, 0 } };
        ch.chroma = {};
        const auto kept = ch.voice;
        writeMetaField(MetaBand::Harmony, 0, 6, 1.0f, proc, 0, ctx, ui);  // MOVE +1
        CHECK(ch.voice == kept, "out-of-range MOVE leaves the chord untouched");

        // An octave shift up that would overflow is likewise rejected wholesale.
        writeMetaField(MetaBand::Harmony, 0, 7, 1.0f, proc, 0, ctx, ui);  // OCT +1
        CHECK(ch.voice == kept, "out-of-range OCT leaves the chord untouched");

        // A whole-chord chromatic slide off the ceiling is rejected too.
        nudgeHarmonyChromaAll(proc, ui, 1);
        CHECK(ch.voice == kept && ch.chroma == HarmonyChord{}.chroma,
              "out-of-range chroma-all leaves the chord untouched");

        // But an in-range transpose still moves the whole block.
        ch.voice = { { 0, 2, 4, 0 } };
        ch.chroma = {};
        writeMetaField(MetaBand::Harmony, 0, 6, 1.0f, proc, 0, ctx, ui);  // MOVE +1
        CHECK(ch.voice[0] == 1 && ch.voice[1] == 3 && ch.voice[2] == 5,
              "in-range MOVE shifts every voice as a rigid block");
    }

    static void testResolveMetaBandDensitySticky()
    {
        UiState ui;
        ui.overlay = Overlay::Density;
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(resolveMetaBand(ui) == MetaBand::Density,         "sticky Amount → Density");
        ui.densitySubPage = UiState::DensitySubPage::Musicality;
        CHECK(resolveMetaBand(ui) == MetaBand::DensityMode,     "sticky Musicality → DensityMode");
        ui.densitySubPage = UiState::DensitySubPage::Selection;
        CHECK(resolveMetaBand(ui) == MetaBand::DensitySelection, "sticky Selection → DensitySelection");
    }

    static void testResolveMetaBandFuncSong()
    {
        // 9.10: Func+Song transient density peek removed; falls through to Swing.
        UiState ui;
        ui.funcHeld = true;
        ui.songHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Swing, "Func+Song → Swing (density peek removed 9.10)");
    }

    static void testResolveMetaBandSwing()
    {
        UiState ui;
        ui.songHeld = true;
        ui.swingDismissed = false;
        CHECK(resolveMetaBand(ui) == MetaBand::Swing, "Song alone + !dismissed → Swing");

        ui.swingDismissed = true;
        CHECK(resolveMetaBand(ui) == MetaBand::None, "Song + dismissed → None");
    }

    static void testResolveMetaBandFuncAlone()
    {
        // 9.10: Func-alone transient density peek removed; Func alone → None.
        UiState ui;
        ui.funcHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::None, "Func alone → None (density peek removed 9.10)");
    }

    static void testResolveMetaBandNone()
    {
        UiState ui;
        CHECK(resolveMetaBand(ui) == MetaBand::None, "all-false → None");
    }

    // -------------------------------------------------------------------------
    // densityEditsMaster — pure routing predicate

    static void testDensityEditsMaster()
    {
        UiState ui;
        CHECK(!densityEditsMaster(ui), "no flags → per-track");
        ui.songHeld = true;
        CHECK(densityEditsMaster(ui), "songHeld → master");
        ui.funcHeld = true;
        CHECK(densityEditsMaster(ui), "songHeld+funcHeld → master");
        ui.songHeld = false;
        CHECK(!densityEditsMaster(ui), "only funcHeld → per-track");
    }

    // -------------------------------------------------------------------------
    // densityWriteTarget — paging formula

    static void testDensityWriteTargetMaster()
    {
        UiState ui;
        ui.songHeld = true;
        auto t = densityWriteTarget(ui, 3, 0);
        CHECK(t.master, "songHeld → master");
        CHECK(t.trackIdx == -1, "master target has trackIdx -1");
    }

    static void testDensityWriteTargetPage0()
    {
        UiState ui;
        // focusedTrack 0-7 → page 0
        auto t = densityWriteTarget(ui, 2, 5);
        CHECK(!t.master, "no song → per-track");
        CHECK(t.trackIdx == 2, "page0 field2 → track 2");
    }

    static void testDensityWriteTargetPage1()
    {
        UiState ui;
        // focusedTrack >= 8 → page 1
        auto t = densityWriteTarget(ui, 3, 8);
        CHECK(!t.master, "no song, track8 → per-track");
        CHECK(t.trackIdx == 11, "page1 field3 → track 11");
    }

    static void testDensityWriteTargetStickyBank()
    {
        UiState ui;
        ui.overlay = Overlay::Density;
        ui.densityBank = 1;
        auto t = densityWriteTarget(ui, 0, 0);  // focusedTrack 0 would be page0, but sticky overrides
        CHECK(!t.master, "sticky, no song → per-track");
        CHECK(t.trackIdx == 8, "sticky bank1 field0 → track 8");
    }

    // -------------------------------------------------------------------------
    // sectionSelectClearsDensitySticky — focus-change supersede policy

    static void testSectionSelectClearsDensitySticky()
    {
        UiState ui;

        // Not in sticky mode: predicate always false.
        ui.overlay = Overlay::None;
        for (int i = 0; i <= 5; ++i)
            CHECK(!sectionSelectClearsDensitySticky(ui, i), "not sticky → false for all sections");

        // In sticky mode: every section except MOD (4) supersedes; MOD cycles sub-page.
        ui.overlay = Overlay::Density;
        for (int i = 0; i <= 5; ++i)
            if (i != 4)
                CHECK(sectionSelectClearsDensitySticky(ui, i), "sticky + non-MOD section → true");
        CHECK(!sectionSelectClearsDensitySticky(ui, 4), "sticky + section 4 → false (MOD cycles subpage)");

        // Sequenced: predicate true → escape clears mode → resolveMetaBand returns None.
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(sectionSelectClearsDensitySticky(ui, 2), "pre-escape predicate fires");
        ui.overlay = Overlay::None;  // simulate escapeDensitySticky
        ui.densityBank = 0;
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(resolveMetaBand(ui) == MetaBand::None, "post-escape → MetaBand::None");

        // Sequenced: section 4 (MOD) does not supersede → mode persists → still a Density* band.
        ui.overlay = Overlay::Density;
        ui.densitySubPage = UiState::DensitySubPage::Amount;
        CHECK(!sectionSelectClearsDensitySticky(ui, 4), "section 4 (MOD) doesn't clear sticky");
        CHECK(resolveMetaBand(ui) == MetaBand::Density, "mode still active → Density band");
    }

    // -------------------------------------------------------------------------
    // Per-track side-effect via EngineHarness

    static void testWriteMetaFieldDensityPerTrack()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // Sanity: master starts at 0
        CHECK(feq(proc.masterDensity(), 0.0f), "masterDensity starts at 0");

        UiState ui;  // songHeld=false
        EditContext ctx;
        const int focusedTrack = 0;

        // Write field 0 to 0.5 (50 on the 0-100 scale)
        writeMetaField(MetaBand::Density, 0, 0, 50.0f, proc, focusedTrack, ctx, ui);

        CHECK(feq(proc.trackDensity(0), 0.5f), "writeMetaField Density sets track 0");
        CHECK(feq(proc.masterDensity(), 0.0f), "masterDensity unchanged after per-track write");
    }

    static void testWriteMetaFieldDensityMasterIsGuardedNoOp()
    {
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.songHeld = true;  // master mode
        EditContext ctx;

        const float trackBefore = proc.trackDensity(0);
        const float masterBefore = proc.masterDensity();

        // Should be a no-op: master writes are handled upstream by the mouse delta
        // tracker or the encoder rawDelta path, not by writeMetaField
        writeMetaField(MetaBand::Density, 0, 0, 75.0f, proc, 0, ctx, ui);

        CHECK(feq(proc.trackDensity(0), trackBefore), "master guard: track unchanged");
        CHECK(feq(proc.masterDensity(), masterBefore), "master guard: master unchanged");
    }

    // -------------------------------------------------------------------------
    // Regression: a PHRASELEN write must update the working Track.length, not just
    // the APVTS param. The Euclid generator reads the working struct, so a
    // param-only write left it capping pulses at the stale length (16).

    static void testWriteMetaFieldPhraseLenSyncsWorkingLength()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        EditContext ctx;
        const int track = 0;

        // Set the phrase length to 32 via the PHRASELEN band (LEN section).
        writeMetaField(MetaBand::PhraseLen, 0, 0, 32.0f, proc, track, ctx, ui);

        // Working struct (what the Euclid generator reads) must reflect 32.
        CHECK(proc.sequence().tracks[static_cast<std::size_t>(track)].length == 32,
              "PHRASELEN write syncs working Track.length");

        // And the Euclid band's PULSE field max must follow the new length.
        const auto euclid = buildMetaBand(MetaBand::Euclidean, 0, proc, track, ctx, ui);
        CHECK(static_cast<int>(euclid[0].maxValue) == 32,
              "Euclid PULSE max follows extended phrase length (not capped at 16)");
    }

    // -------------------------------------------------------------------------
    // MetaRotary::applyView totality — guards the bf20ac3 leak class

    static void testMetaRotaryApplyViewTotality()
    {
        MetaRotary mr;

        // Apply a fully-loaded view
        MetaRotary::View v;
        v.rangeLo = 10.0;
        v.rangeHi = 200.0;
        v.interval = 1.0;
        v.skew = 2.0;
        v.doubleClickEnabled = true;
        v.doubleClickValue = 50.0;
        v.value = 77.0;
        v.enabled = false;
        v.alpha = 0.4f;
        v.ringMode = RingMode::BipolarFromCentre;
        v.marks[0] = { true, 0.25f, 0xff00ff00, 1.0f };
        v.marks[1] = { true, 0.75f, 0xffff0000, 0.8f };
        v.densityCell = true;
        v.densityMasterOffset = 0.3f;
        v.densityEffective = 0.7f;
        mr.applyView(v);

        CHECK(feq(static_cast<float>(mr.getMinimum()), 10.0f),  "applyView: rangeLo");
        CHECK(feq(static_cast<float>(mr.getMaximum()), 200.0f), "applyView: rangeHi");
        CHECK(feq(static_cast<float>(mr.getValue()), 77.0f),    "applyView: value");
        CHECK(!mr.isEnabled(),                                   "applyView: enabled=false");
        CHECK(feq(mr.getAlpha(), 0.4f),                         "applyView: alpha");
        CHECK(mr.getRingMode() == RingMode::BipolarFromCentre,  "applyView: ringMode");
        CHECK(mr.getMarks()[0].present,                          "applyView: marks[0].present");
        CHECK(mr.isDensityCell(),                                "applyView: densityCell");
        CHECK(feq(mr.getDensityMasterOffset(), 0.3f),           "applyView: densityMasterOffset");
        CHECK(feq(mr.getDensityEffective(), 0.7f),              "applyView: densityEffective");

        // Apply a default (zeroed) view — every field must reset
        mr.applyView(MetaRotary::View{});

        CHECK(feq(static_cast<float>(mr.getMinimum()), 0.0f),   "reset: rangeLo");
        CHECK(feq(static_cast<float>(mr.getMaximum()), 1.0f),   "reset: rangeHi");
        CHECK(feq(static_cast<float>(mr.getValue()), 0.0f),     "reset: value");
        CHECK(mr.isEnabled(),                                    "reset: enabled=true");
        CHECK(feq(mr.getAlpha(), 1.0f),                         "reset: alpha");
        CHECK(mr.getRingMode() == RingMode::UnipolarFill,        "reset: ringMode");
        CHECK(!mr.getMarks()[0].present,                         "reset: marks[0] cleared");
        CHECK(!mr.isDensityCell(),                               "reset: densityCell=false");
        CHECK(feq(mr.getDensityMasterOffset(), 0.0f),           "reset: densityMasterOffset");
        CHECK(feq(mr.getDensityEffective(), 1.0f),              "reset: densityEffective");
    }

    // -------------------------------------------------------------------------
    // TIME sticky mode: resolveMetaBand precedence (unified model)

    static void testResolveMetaBandTime()
    {
        // overlay == Time → Time band; euclid outranks it via euclidHeld.
        // Mutual exclusion with Density/Vel is now structural (single field).
        {
            UiState ui;
            ui.overlay = Overlay::Time;
            CHECK(resolveMetaBand(ui) == MetaBand::Time, "overlay Time → Time");

            // outranks swing (swingDismissed guards, but even without it Time wins)
            ui.songHeld = true;
            ui.swingDismissed = false;
            CHECK(resolveMetaBand(ui) == MetaBand::Time, "Time outranks swing");

            // euclid outranks Time (via transient euclidHeld, not overlay field)
            ui.euclidHeld = true;
            CHECK(resolveMetaBand(ui) == MetaBand::Euclidean, "euclidHeld outranks Time");
            ui.euclidHeld = false;
        }
    }

    // -------------------------------------------------------------------------
    // timeScopeFor — modifier routing + entry-scope fallback

    static void testTimeScopeFor()
    {
        // Func+Song = Set (1), Song = Song (2), Scene = Scene (3).
        {
            UiState ui;
            ui.funcHeld = true;
            ui.songHeld = true;
            CHECK(timeScopeFor(ui) == 1, "Func+Song → Set (1)");
        }
        {
            UiState ui;
            ui.songHeld = true;
            CHECK(timeScopeFor(ui) == 2, "Song alone → Song (2)");
        }
        {
            UiState ui;
            ui.sceneHeld = true;
            CHECK(timeScopeFor(ui) == 3, "Scene alone → Scene (3)");
        }
        // No modifier: returns entry scope, never 0 (scope-0 regression guard).
        {
            UiState ui;
            ui.timeEntryScope = 3;  // entered via Scene+TRIG
            const int s = timeScopeFor(ui);
            CHECK(s != 0, "no modifier → never returns 0 (scope-0 regression)");
            CHECK(s == 3, "no modifier → entry scope (3)");
        }
        {
            UiState ui;
            ui.timeEntryScope = 2;  // entered via Song+TRIG
            const int s = timeScopeFor(ui);
            CHECK(s == 2, "no modifier → respects explicit entry scope");
        }
    }

    // -------------------------------------------------------------------------
    // resolveTapTempoScope (item 10): held modifier wins; else deepest override.

    static void testResolveTapTempoScope()
    {
        // Held scope modifiers behave exactly like timeScopeFor's 1/2/3.
        {
            UiState ui; ui.funcHeld = true; ui.songHeld = true;
            CHECK(resolveTapTempoScope(ui, false, false) == 1, "Func+Song → Set (1)");
        }
        {
            UiState ui; ui.songHeld = true;
            CHECK(resolveTapTempoScope(ui, true, true) == 2,
                  "Song held → Song (2), overriding deeper-override fallback");
        }
        {
            UiState ui; ui.sceneHeld = true;
            CHECK(resolveTapTempoScope(ui, false, false) == 3, "Scene held → Scene (3)");
        }
        // No modifier: land on the deepest scope that already overrides tempo.
        {
            UiState ui;
            CHECK(resolveTapTempoScope(ui, false, false) == 1,
                  "no override anywhere → global (1)");
            CHECK(resolveTapTempoScope(ui, true, false) == 2,
                  "only Song overrides → Song (2)");
            CHECK(resolveTapTempoScope(ui, false, true) == 3,
                  "only Scene overrides → Scene (3)");
            CHECK(resolveTapTempoScope(ui, true, true) == 3,
                  "both override → deepest (Scene, 3)");
        }
    }

    // -------------------------------------------------------------------------
    // buildMetaBand Time: two active fields (tempo + time-sig)

    static void testTimeBandFields()
    {
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;  // Song entry
        EditContext ctx;

        const auto fields = buildMetaBand(MetaBand::Time, 0, proc, 0, ctx, ui);
        CHECK(fields[0].active,   "Time band: field 0 (Tempo) is active");
        CHECK(fields[0].writable, "Time band: field 0 (Tempo) is writable");
        CHECK(!fields[0].stepped, "Time band: field 0 (Tempo) is continuous");
        CHECK(fields[1].active,   "Time band: field 1 (Sig) is active");
        CHECK(fields[1].writable, "Time band: field 1 (Sig) is writable");
        CHECK(fields[1].stepped,  "Time band: field 1 (Sig) is stepped");
        // 9.10: field 2 = CLICK (metronome toggle)
        CHECK(fields[2].active,   "Time band: field 2 (CLICK) is active");
        CHECK(fields[2].writable, "Time band: field 2 (CLICK) is writable");
        CHECK(fields[2].stepped,  "Time band: field 2 (CLICK) is stepped");
    }

    static void testTimeBandClickRoundTrip()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;
        EditContext ctx;

        // Default: metronome off.
        CHECK(!proc.clock().isMetronomeEnabled(), "metronome starts off");
        auto fields = buildMetaBand(MetaBand::Time, 0, proc, 0, ctx, ui);
        CHECK(feq(fields[2].value, 0.0f), "CLICK field starts at 0 (off)");

        // Write 1.0 (on) via writeMetaField.
        writeMetaField(MetaBand::Time, 0, 2, 1.0f, proc, 0, ctx, ui);
        CHECK(proc.clock().isMetronomeEnabled(), "write 1.0 → metronome on");
        fields = buildMetaBand(MetaBand::Time, 0, proc, 0, ctx, ui);
        CHECK(feq(fields[2].value, 1.0f), "CLICK field reads 1.0 after enable");

        // Write 0.0 (off).
        writeMetaField(MetaBand::Time, 0, 2, 0.0f, proc, 0, ctx, ui);
        CHECK(!proc.clock().isMetronomeEnabled(), "write 0.0 → metronome off");
        fields = buildMetaBand(MetaBand::Time, 0, proc, 0, ctx, ui);
        CHECK(feq(fields[2].value, 0.0f), "CLICK field reads 0.0 after disable");
    }

    // -------------------------------------------------------------------------
    // writeMetaField Time/Tempo (field 0): write round-trip, entry scope = Song

    static void testWriteMetaFieldTempoSongScope()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // Sanity: Song starts with no tempo override.
        CHECK(!proc.song().hasTempo, "song.hasTempo starts false");

        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;  // Song
        ui.songHeld = true;     // explicit Song scope
        EditContext ctx;

        const double globalBpm = proc.clock().bpm();
        const float targetBpm = static_cast<float>(globalBpm) * 0.75f;

        writeMetaField(MetaBand::Time, 0, 0, targetBpm, proc, 0, ctx, ui);

        CHECK(proc.song().hasTempo, "after write: song.hasTempo=true");
        const double expectedRatio = static_cast<double>(targetBpm) / globalBpm;
        const double storedRatio = proc.song().tempoRatio;
        CHECK(std::abs(storedRatio - expectedRatio) < 1e-6,
              "stored ratio = targetBpm / globalBpm");
    }

    static void testWriteMetaFieldTempoEntryScope()
    {
        // No modifier held: write should target entry scope (Song=2), not Scene.
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;  // Song entry
        // No songHeld / sceneHeld
        EditContext ctx;

        const float targetBpm = 100.0f;

        writeMetaField(MetaBand::Time, 0, 0, targetBpm, proc, 0, ctx, ui);

        CHECK(proc.song().hasTempo, "entry-scope write sets song.hasTempo (not scene)");
        CHECK(!proc.section().hasTempo, "entry-scope write does NOT touch section.hasTempo");
    }

    // -------------------------------------------------------------------------
    // writeMetaField Time/TimeSig (field 1): INHERIT (idx 0) clears override

    static void testWriteMetaFieldTimeSigInheritClearsOverride()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // Set a Song time-sig override.
        proc.song().hasTimeSig = true;
        proc.song().timeSig.numerator = 3;
        proc.song().timeSig.denominator = 4;

        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;  // Song
        ui.songHeld = true;
        EditContext ctx;

        // Write idx=0 (INHERIT) to Song scope via field 1 (time-sig).
        writeMetaField(MetaBand::Time, 0, 1, 0.0f, proc, 0, ctx, ui);

        CHECK(!proc.song().hasTimeSig, "INHERIT (idx 0) clears song.hasTimeSig");
    }

    static void testWriteMetaFieldTimeSigSetsValue()
    {
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 3;  // Scene
        ui.sceneHeld = true;
        EditContext ctx;

        // Write idx=1 at Scene scope (idx 0 = INHERIT, idx 1 = first curated entry).
        writeMetaField(MetaBand::Time, 0, 1, 1.0f, proc, 0, ctx, ui);

        CHECK(proc.section().hasTimeSig, "write at Scene: hasTimeSig=true");
    }

    // =========================================================================
    // CUJ sequence tests — drive UiState through the pure transition fns
    // and assert (band, scope) after each event.
    // =========================================================================

    // CUJ 1: enter via Scene+TRIG, retarget to Song then Set, release back to entry.
    static void testCujTimeEnterViaScene()
    {
        UiState ui;
        ui.sceneHeld = true;

        // Before entry: bare Scene → Swing (not dismissed yet).
        ui.swingDismissed = false;
        CHECK(resolveMetaBand(ui) == MetaBand::Swing, "pre-entry: Scene alone → Swing");

        // Entry: applyTimeEntry sets timeStickyMode, timeEntryScope=3, swingDismissed=true.
        applyTimeEntry(ui);
        CHECK((ui.overlay == Overlay::Time), "after entry: timeStickyMode=true");
        CHECK(ui.timeEntryScope == 3, "after entry via Scene: entry scope=3");
        CHECK(ui.swingDismissed, "after entry: swingDismissed=true");
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "after entry: resolves to Time");
        CHECK(timeScopeFor(ui) == 3, "Scene held: scope=3");

        // Retarget: hold Song modifier.
        ui.sceneHeld = false;
        ui.songHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "retarget Song: still Time");
        CHECK(timeScopeFor(ui) == 2, "Song held: scope=2");

        // Retarget to Set: hold Func+Song.
        ui.funcHeld = true;
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "retarget Set: still Time");
        CHECK(timeScopeFor(ui) == 1, "Func+Song: scope=1");

        // Release all modifiers: falls back to entry scope.
        ui.funcHeld = false;
        ui.songHeld = false;
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "release all: still Time (entry scope)");
        CHECK(timeScopeFor(ui) == 3, "release all: scope = entry scope (3)");

        // Exit: applyTimeEntry again → exits TIME.
        applyTimeEntry(ui);
        CHECK(!(ui.overlay == Overlay::Time), "after exit: timeStickyMode=false");
        CHECK(resolveMetaBand(ui) == MetaBand::None, "after exit: None");
    }

    // CUJ 2: enter via Song+TRIG; entry scope=2.
    static void testCujTimeEnterViaSong()
    {
        UiState ui;
        ui.songHeld = true;
        ui.swingDismissed = false;

        // Pre-entry: Song alone → Swing.
        CHECK(resolveMetaBand(ui) == MetaBand::Swing, "pre-entry: Song alone → Swing");

        applyTimeEntry(ui);
        CHECK((ui.overlay == Overlay::Time), "after Song+TRIG entry: timeStickyMode=true");
        CHECK(ui.timeEntryScope == 2, "Song entry: entry scope=2");
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "Song entry: → Time");
        CHECK(timeScopeFor(ui) == 2, "Song held: scope=2");

        // Release Song; no modifier: falls back to entry scope (2, not 0 or 3).
        ui.songHeld = false;
        CHECK(timeScopeFor(ui) == 2, "no modifier: scope = entry scope (2) — scope-0 regression guard");
        CHECK(timeScopeFor(ui) != 0, "no modifier: never 0");
    }

    // CUJ 3: scope retarget walk without re-entering.
    // timeScopeFor priority: Func+Song > Song > Scene > entry scope (same as swingScopeFor).
    static void testCujTimeScopeRetarget()
    {
        UiState ui;
        ui.songHeld = true;
        applyTimeEntry(ui);
        CHECK(ui.timeEntryScope == 2, "entry via Song: scope=2");

        // Song held → 2; Scene held simultaneously → Song wins.
        ui.sceneHeld = true;
        CHECK(timeScopeFor(ui) == 2, "Song+Scene both held: Song wins → scope=2");

        // Release Song, keep Scene → retargets to 3.
        ui.songHeld = false;
        CHECK(timeScopeFor(ui) == 3, "only Scene held → scope=3");

        // Release Scene: falls to entry scope.
        ui.sceneHeld = false;
        CHECK(timeScopeFor(ui) == 2, "no modifier → entry scope 2");

        // Func+Song → Set (1).
        ui.funcHeld = true;
        ui.songHeld = true;
        CHECK(timeScopeFor(ui) == 1, "Func+Song → Set (1)");
    }

    // CUJ 4: swing suppression — entering TIME sets swingDismissed; bare modifier
    // while TIME is closed does NOT re-trigger Swing until the flag clears.
    static void testCujTimeSwingSuppression()
    {
        UiState ui;
        ui.sceneHeld = true;
        ui.swingDismissed = false;

        // Initial: swing visible.
        CHECK(resolveMetaBand(ui) == MetaBand::Swing, "pre-entry: Swing visible");

        // Enter TIME: swingDismissed=true.
        applyTimeEntry(ui);
        CHECK(ui.swingDismissed, "after entry: swingDismissed=true");
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "in TIME: Time band");

        // Exit TIME: escapeTimeSticky keeps swingDismissed=true so bare modifier
        // held won't drop back to Swing immediately.
        escapeTimeSticky(ui);
        CHECK(!(ui.overlay == Overlay::Time), "after escape: timeStickyMode=false");
        CHECK(ui.swingDismissed, "after escape: swingDismissed still true");

        // With Scene held and swingDismissed: Swing band is suppressed.
        CHECK(resolveMetaBand(ui) == MetaBand::None, "swing suppressed after escape");

        // Clearing dismissed: Swing returns.
        ui.swingDismissed = false;
        CHECK(resolveMetaBand(ui) == MetaBand::Swing, "swingDismissed=false: Swing returns");
    }

    // CUJ 5: sticky exclusivity — entering density exits TIME.
    static void testCujTimeExclusivityDensity()
    {
        UiState ui;
        ui.songHeld = true;
        applyTimeEntry(ui);
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "TIME active");

        // Simulate density sticky entry (as in PluginEditor).
        ui.overlay = Overlay::Density;
        escapeTimeSticky(ui);

        CHECK(!(ui.overlay == Overlay::Time), "density entry: timeStickyMode cleared");
        CHECK(resolveMetaBand(ui) != MetaBand::Time, "density entry: not Time band");
    }

    // CUJ 6: sticky exclusivity — entering vel exits TIME; entering TIME exits vel.
    static void testCujTimeExclusivityVel()
    {
        UiState ui;
        ui.songHeld = true;
        applyTimeEntry(ui);
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "TIME active");

        // applyTimeEntry clears vel sticky.
        ui.overlay = Overlay::Vel;
        applyTimeEntry(ui);  // toggle off
        applyTimeEntry(ui);  // toggle on again from clean state
        CHECK(!(ui.overlay == Overlay::Vel), "entering TIME clears velStickyMode");

        // And vice-versa: entering vel clears TIME.
        ui.overlay = Overlay::Vel;
        escapeTimeSticky(ui);
        CHECK(!(ui.overlay == Overlay::Time), "entering vel: timeStickyMode cleared");
    }

    // CUJ 7: latch + TIME — latched Scene modifier keeps TIME scope at 3.
    static void testCujTimeLatchedModifier()
    {
        UiState ui;
        ui.latch.scene = true;
        ui.sceneHeld = true;  // effective held = physical OR latched

        applyTimeEntry(ui);
        CHECK(ui.timeEntryScope == 3, "latch: entry scope = Scene = 3");
        CHECK(timeScopeFor(ui) == 3, "latch: timeScopeFor still 3");
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "latch: TIME band active");
    }

    // Regression: TIME sticky must exit on a bare non-TRIG section press, at parity
    // with density/vel (the wires that shipped missing — "too sticky" bug). TRIG (0)
    // is excluded because Song/Scene+TRIG re-press toggles TIME via isTimeEntryChord.
    static void testSectionSelectClearsTimeSticky()
    {
        UiState ui;

        // Not in TIME: predicate always false.
        ui.overlay = Overlay::None;
        for (int i = 0; i <= 5; ++i)
            CHECK(!sectionSelectClearsTimeSticky(ui, i), "not sticky → false for all sections");

        // In TIME: every section except TRIG (0) supersedes; TRIG re-press toggles.
        ui.overlay = Overlay::Time;
        for (int i = 0; i <= 5; ++i)
            if (i != 0)
                CHECK(sectionSelectClearsTimeSticky(ui, i), "sticky + non-TRIG section → true");
        CHECK(!sectionSelectClearsTimeSticky(ui, 0), "sticky + section 0 → false (TRIG toggles)");

        // Sequenced: predicate true → escape clears mode → resolveMetaBand returns None,
        // and swingDismissed is set so we don't drop into Swing on the way out.
        ui.overlay = Overlay::Time;
        CHECK(sectionSelectClearsTimeSticky(ui, 2), "pre-escape predicate fires");
        escapeTimeSticky(ui);  // simulate the section-dispatch escape
        CHECK(!(ui.overlay == Overlay::Time), "post-escape: timeStickyMode cleared");
        CHECK(ui.swingDismissed, "post-escape: swingDismissed set");
        CHECK(resolveMetaBand(ui) == MetaBand::None, "post-escape → MetaBand::None");
    }

    // =========================================================================
    // Tempo INHERIT floor round-trip: write at floor clears hasTempo.
    // =========================================================================

    static void testTempoInheritFloor()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // Set a Song tempo override.
        proc.song().hasTempo = true;
        proc.song().tempoRatio = 0.8;

        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;  // Song
        ui.songHeld = true;
        EditContext ctx;

        // buildTempoBand at Song scope: should report hasOverride=true.
        {
            const auto fields = buildMetaBand(MetaBand::Time, 0, proc, 0, ctx, ui);
            CHECK(fields[0].hasOverride, "with override: hasOverride=true");
            CHECK(fields[0].minValue == 0.0f, "Song scope: minValue=0 (INHERIT floor)");
        }

        // Write at floor (0.0 = INHERIT): should clear hasTempo.
        writeMetaField(MetaBand::Time, 0, 0, 0.0f, proc, 0, ctx, ui);
        CHECK(!proc.song().hasTempo, "write at INHERIT floor: hasTempo cleared");

        // buildTempoBand after clear: hasOverride=false, value=0.
        {
            const auto fields = buildMetaBand(MetaBand::Time, 0, proc, 0, ctx, ui);
            CHECK(!fields[0].hasOverride, "after inherit: hasOverride=false");
            CHECK(fields[0].value == 0.0f, "after inherit: value=0 (floor)");
        }

        // Write a real BPM (above floor): should set hasTempo again.
        writeMetaField(MetaBand::Time, 0, 0, 120.0f, proc, 0, ctx, ui);
        CHECK(proc.song().hasTempo, "write 120 BPM: hasTempo=true again");
    }

    // =========================================================================
    // Bar-length order: verify kTimeSigs ascending via buildMetaBand field 1
    // =========================================================================

    static void testTimeSigsBarLengthOrder()
    {
        // Build the TIME band at Set scope (no INHERIT slot), iterate field 1 stepped values.
        // The bar length of entry i is num/den as a fraction of a 4/4 bar.
        // We can't access kTimeSigs directly, but we can sample via writeMetaField at Set
        // scope and check effectiveTimeSig is non-decreasing.
        EngineHarness h;
        auto& proc = h.processor();

        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 1;  // Set scope
        ui.funcHeld = true;
        ui.songHeld = true;
        EditContext ctx;

        const auto fields = buildMetaBand(MetaBand::Time, 0, proc, 0, ctx, ui);
        CHECK(fields[1].active,  "Sig field active at Set scope");
        CHECK(fields[1].stepped, "Sig field is stepped");

        const int maxIdx = static_cast<int>(fields[1].maxValue);
        CHECK(maxIdx >= 11, "at least 12 entries in the time-sig list");

        // Write each index, read back effectiveTimeSig, check non-decreasing bar length.
        double prevBarLen = 0.0;
        bool allNonDecreasing = true;
        for (int i = 0; i <= maxIdx; ++i)
        {
            writeMetaField(MetaBand::Time, 0, 1, static_cast<float>(i), proc, 0, ctx, ui);
            const TimeSig ts = proc.project().defaultTimeSig;
            const double barLen = static_cast<double>(ts.numerator)
                                / static_cast<double>(ts.denominator);
            if (barLen < prevBarLen - 1e-9)
                allNonDecreasing = false;
            prevBarLen = barLen;
        }
        CHECK(allNonDecreasing, "time-sig list ordered by non-decreasing bar length");
    }

    // =========================================================================
    // 9.10: TRIG band field 5 — RTG authored ratchet rate
    // =========================================================================

    static void testTrigBandRtgField()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.masterSection = 1;  // TRIG band
        EditContext ctx;

        // Without a held step: RTG field is inactive.
        {
            const auto fields = buildMetaBand(MetaBand::Trig, 0, proc, 0, ctx, ui);
            CHECK(!fields[5].active,  "RTG inactive when no step held");
            CHECK(!fields[5].writable, "RTG not writable when no step held");
        }

        // Hold step 3 on track 0.
        ctx.hold(0, 3);
        auto& step = proc.sequence().tracks[0].steps[3];

        // Default: hasRetrig = false → RTG value = 0 (OFF).
        {
            const auto fields = buildMetaBand(MetaBand::Trig, 0, proc, 0, ctx, ui);
            CHECK(fields[5].active,   "RTG active with step held");
            CHECK(fields[5].writable, "RTG writable with step held");
            CHECK(fields[5].stepped,  "RTG is stepped");
            CHECK(feq(fields[5].value, 0.0f), "RTG value = 0 when hasRetrig=false");
            CHECK(!fields[5].hasOverride, "RTG no override when off");
        }

        // Write value 5 (/16 = index 4 = retrigRate 0.25).
        writeMetaField(MetaBand::Trig, 0, 5, 5.0f, proc, 0, ctx, ui);
        CHECK(step.trigOverride.hasRetrig,       "write 5 → hasRetrig=true");
        CHECK(feq(static_cast<float>(step.trigOverride.retrigRate), 0.25f), "write 5 → retrigRate=/16");

        // Read back via buildMetaBand.
        {
            const auto fields = buildMetaBand(MetaBand::Trig, 0, proc, 0, ctx, ui);
            CHECK(feq(fields[5].value, 5.0f), "RTG reads back 5 (/16)");
            CHECK(fields[5].hasOverride, "RTG hasOverride=true after write");
        }

        // Write value 0 → off.
        writeMetaField(MetaBand::Trig, 0, 5, 0.0f, proc, 0, ctx, ui);
        CHECK(!step.trigOverride.hasRetrig, "write 0 → hasRetrig=false");
        {
            const auto fields = buildMetaBand(MetaBand::Trig, 0, proc, 0, ctx, ui);
            CHECK(feq(fields[5].value, 0.0f), "RTG reads back 0 (OFF)");
            CHECK(!fields[5].hasOverride, "RTG hasOverride=false after clear");
        }
    }

    // -------------------------------------------------------------------------
    // 9.14 fix 2: held-step move surfaces the Step-Position panel; its encoders
    // move the step (field 0, sequential bubble-swap) and nudge micro-time
    // (field 1). resolveMetaBand routes to it whenever stepMoveActive is set.
    static void testStepPositionBand()
    {
        EngineHarness h;
        auto& proc = h.processor();

        // resolveMetaBand: stepMoveActive outranks every other transient.
        {
            UiState ui;
            ui.stepMoveActive = true;
            CHECK(resolveMetaBand(ui) == MetaBand::StepPosition,
                  "stepMoveActive → StepPosition");
            ui.euclidHeld = true;
            CHECK(resolveMetaBand(ui) == MetaBand::StepPosition,
                  "stepMoveActive outranks euclid");
        }

        auto& trk = proc.sequence().tracks[0];
        const int len = trk.length;
        const int start = 2;
        const int between = start + 1;   // a cell the step passes — must stay put
        const int target = start + 3;
        CHECK(target < len, "test precondition: target within track length");

        // Distinct markers: trig at the start slot (the moved step), a different
        // probability at the in-between slot, and an occupied destination so we can
        // verify swap-with-destination (dest content trades back to the anchor).
        trk.steps[static_cast<std::size_t>(start)].trig = true;
        trk.steps[static_cast<std::size_t>(start)].microOffset = 0.0f;
        trk.steps[static_cast<std::size_t>(between)].condition.probabilityPercent = 42;
        trk.steps[static_cast<std::size_t>(target)].microOffset = 0.25f;  // dest marker

        UiState ui;
        ui.stepMoveActive = true;
        ui.pLockClearTrack = 0;
        ui.pLockClearStep = start;
        ui.stepMoveAnchor = start;   // home for swap-with-destination
        EditContext ctx;

        // buildMetaBand: field 0 = position (1-based), field 1 = micro-time.
        {
            const auto fields = buildMetaBand(MetaBand::StepPosition, 0, proc, 0, ctx, ui);
            CHECK(fields[0].active && fields[1].active,
                  "StepPosition exposes position + micro fields");
            CHECK(feq(fields[0].value, static_cast<float>(start + 1)),
                  "position field reads step+1");
            CHECK(feq(fields[0].maxValue, static_cast<float>(len)),
                  "position max = track length");
            CHECK(feq(fields[1].value, 0.0f), "micro field reads 0");
        }

        // Field 0 write → swap-with-destination: step lands at target, the cell
        // that was at target trades back to the anchor, cells in between untouched.
        writeMetaField(MetaBand::StepPosition, 0, 0,
                       static_cast<float>(target + 1), proc, 0, ctx, ui);
        const auto& steps = proc.sequence().tracks[0].steps;
        CHECK(ui.pLockClearStep == target, "pLockClearStep follows the moved step");
        CHECK(steps[static_cast<std::size_t>(target)].trig,
              "trig moved to target slot");
        CHECK(feq(steps[static_cast<std::size_t>(start)].microOffset, 0.25f),
              "destination content traded back to the anchor (swap)");
        CHECK(!steps[static_cast<std::size_t>(start)].trig,
              "anchor no longer carries the moved trig");
        CHECK(steps[static_cast<std::size_t>(between)].condition.probabilityPercent == 42,
              "in-between cell stays on the beat (not shifted)");

        // Field 1 write → micro-time on the moved step.
        writeMetaField(MetaBand::StepPosition, 0, 1, 0.3f, proc, 0, ctx, ui);
        CHECK(feq(proc.sequence().tracks[0].steps[static_cast<std::size_t>(target)].microOffset,
                  0.3f),
              "micro-time set on the moved step");
    }

    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // KEY band (DESIGN §4.10)

    static void testKeyBandResolveAndFields()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.overlay = Overlay::Time;
        ui.timeEntryScope = 2;
        EditContext ctx;

        // The KEY page resolves only when sigPage == Key.
        ui.sigPage = UiState::SigPage::Time;
        CHECK(resolveMetaBand(ui) == MetaBand::Time, "sigPage Time → Time band");
        ui.sigPage = UiState::SigPage::Key;
        CHECK(resolveMetaBand(ui) == MetaBand::Key, "sigPage Key → Key band");

        // Set scope (Func+Song): MZ holds Root, Tonality, Note-count (modifiers
        // moved to the step-grid KeyPanel).
        ui.funcHeld = true; ui.songHeld = true;
        const auto f = buildMetaBand(MetaBand::Key, 0, proc, 0, ctx, ui);
        CHECK(f[0].active && f[0].writable && f[0].stepped, "Key: Root field");
        CHECK(f[1].active && f[1].writable && f[1].stepped, "Key: Tonality field");
        CHECK(f[2].active && f[2].writable && f[2].stepped, "Key: Note-count field");
        // Default D Dorian (the symmetric centre): root D, tonality Dorian, 7-note.
        CHECK(f[0].valueText == "D", "Key: default root D");
        CHECK(f[1].valueText == "Dorian", "Key: default tonality Dorian");
        CHECK(f[2].valueText == "Diatonic", "Key: default note-count Diatonic");
    }

    static void testKeyBandWriteRoundTripSetScope()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.overlay = Overlay::Time;
        ui.sigPage = UiState::SigPage::Key;
        ui.funcHeld = true; ui.songHeld = true;   // Set scope (timeScopeFor → 1)
        EditContext ctx;

        // Root is dialed in circle-of-fifths order: default D (index 6) reads back
        // as the fifths index, and writing the fifths index of A sets root A(9).
        const auto def = buildMetaBand(MetaBand::Key, 0, proc, 0, ctx, ui);
        CHECK(feq(def[0].value, static_cast<float>(fifthsIndexOfRootPc(2))), "Root reads default D's fifths index");
        CHECK(def[0].valueText == "D", "Root reads 'D' by default");
        writeMetaField(MetaBand::Key, 0, 0, static_cast<float>(fifthsIndexOfRootPc(9)), proc, 0, ctx, ui);  // A
        writeMetaField(MetaBand::Key, 0, 1, 2.0f, proc, 0, ctx, ui);  // brightness Aeolian
        CHECK(proc.project().defaultKeySig.root == 9, "Set root written A via fifths index");
        CHECK(proc.project().defaultKeySig.brightness == kAeolian, "Set brightness Aeolian");

        // Note-count: set Pentatonic (index 1) then back to Diatonic (2).
        writeMetaField(MetaBand::Key, 0, 2, 1.0f, proc, 0, ctx, ui);
        CHECK(proc.project().defaultKeySig.scaleType == ScaleType::Pentatonic, "Note-count -> Pentatonic");
        writeMetaField(MetaBand::Key, 0, 2, 2.0f, proc, 0, ctx, ui);
        CHECK(proc.project().defaultKeySig.scaleType == ScaleType::Diatonic, "Note-count -> Diatonic");

        // Modifiers toggle via the grid helper (Harmonic 0 is the first cell).
        keyToggleModifier(ui, proc, 0);
        CHECK(proc.project().defaultKeySig.modifiers.size() == 1, "Harmonic added via grid");
        CHECK(proc.effectiveKeySig() == proc.project().defaultKeySig, "Set key is effective (no overrides)");

        // Toggle Harmonic off again (grid).
        keyToggleModifier(ui, proc, 0);
        CHECK(proc.project().defaultKeySig.modifiers.empty(), "Harmonic removed via grid");

        // Selecting a symmetric scale overrides; selecting it again returns to Diatonic.
        keyToggleSymmetric(ui, proc, ScaleType::WholeTone);
        CHECK(proc.project().defaultKeySig.scaleType == ScaleType::WholeTone, "Whole-tone selected");
        keyToggleSymmetric(ui, proc, ScaleType::WholeTone);
        CHECK(proc.project().defaultKeySig.scaleType == ScaleType::Diatonic, "Whole-tone deselect -> Diatonic");
    }

    static void testKeyBandSongOverrideAndInherit()
    {
        EngineHarness h;
        auto& proc = h.processor();
        UiState ui;
        ui.overlay = Overlay::Time;
        ui.sigPage = UiState::SigPage::Key;
        ui.songHeld = true;   // Song scope (timeScopeFor → 2)
        EditContext ctx;

        CHECK(!proc.song().hasKeySig, "song starts inheriting");
        // Editing brightness enables the override (seeded from effective). At
        // Song/Scene index 0 = INHERIT, so the real modes start at 1: value 3 =
        // Locrian(1)+2 = Aeolian.
        writeMetaField(MetaBand::Key, 0, 1, 3.0f, proc, 0, ctx, ui);   // Aeolian
        CHECK(proc.song().hasKeySig, "editing brightness enables the Song override");
        CHECK(proc.song().keySig.brightness == kAeolian, "Song override brightness set");

        // INHERIT parity: every facet can clear the override, not just Root.
        writeMetaField(MetaBand::Key, 0, 1, 0.0f, proc, 0, ctx, ui);   // Tonality → INHERIT
        CHECK(!proc.song().hasKeySig, "Tonality → INHERIT clears the Song override");

        // Note-count enables, then its INHERIT floor clears too.
        writeMetaField(MetaBand::Key, 0, 2, 2.0f, proc, 0, ctx, ui);   // Pentatonic (idx 1 + offset)
        CHECK(proc.song().hasKeySig, "editing note-count enables the Song override");
        CHECK(proc.song().keySig.scaleType == ScaleType::Pentatonic, "Song override note-count set");
        writeMetaField(MetaBand::Key, 0, 2, 0.0f, proc, 0, ctx, ui);   // Notes → INHERIT
        CHECK(!proc.song().hasKeySig, "Notes → INHERIT clears the Song override");

        // Root still clears it too.
        writeMetaField(MetaBand::Key, 0, 1, 3.0f, proc, 0, ctx, ui);   // re-establish override
        CHECK(proc.song().hasKeySig, "override re-established");
        writeMetaField(MetaBand::Key, 0, 0, 0.0f, proc, 0, ctx, ui);
        CHECK(!proc.song().hasKeySig, "Root → INHERIT clears the Song override");
    }

    void runMetaBandTests()
    {
        testKeyBandResolveAndFields();
        testKeyBandWriteRoundTripSetScope();
        testKeyBandSongOverrideAndInherit();
        testResolveMetaBandMasterSection();
        testResolveMetaBandTransientOutranksMasterSection();
        testResolveMetaBandEuclid();
        testTransportLaunchQuantField();
        testMelodicBand();
        testHarmonyBand();
        testHarmonyLosslessGrow();
        testHarmonyReelAndChroma();
        testHarmonyCyclicReel();
        testHarmonySingleVoiceRemoval();
        testHarmonyChromaAll();
        testHarmonyNoRepeatedNotes();
        testHarmonyTransposeRangeGuard();
        testResolveMetaBandDensitySticky();
        testResolveMetaBandFuncSong();
        testResolveMetaBandSwing();
        testResolveMetaBandFuncAlone();
        testResolveMetaBandNone();

        // TIME sticky mode precedence + scope routing
        testResolveMetaBandTime();
        testTimeScopeFor();
        testResolveTapTempoScope();

        // TIME band: three active fields (tempo + time-sig + click)
        testTimeBandFields();
        testTimeBandClickRoundTrip();

        // Write round-trips + entry-scope correctness
        testWriteMetaFieldTempoSongScope();
        testWriteMetaFieldTempoEntryScope();
        testWriteMetaFieldTimeSigInheritClearsOverride();
        testWriteMetaFieldTimeSigSetsValue();

        // CUJ sequence tests — enter, retarget, latch, swing suppression, exclusivity
        testCujTimeEnterViaScene();
        testCujTimeEnterViaSong();
        testCujTimeScopeRetarget();
        testCujTimeSwingSuppression();
        testCujTimeExclusivityDensity();
        testCujTimeExclusivityVel();
        testCujTimeLatchedModifier();

        // Regression: bare non-TRIG section press exits TIME sticky (the missing wire)
        testSectionSelectClearsTimeSticky();

        // Tempo INHERIT floor: write at floor clears hasTempo; round-trip
        testTempoInheritFloor();

        // Bar-length order assertion on kTimeSigs (via buildMetaBand field 1 order)
        testTimeSigsBarLengthOrder();

        // 9.10: TRIG band RTG field — authored ratchet rate round-trip
        testTrigBandRtgField();

        testWriteMetaFieldPhraseLenSyncsWorkingLength();

        testDensityEditsMaster();

        testDensityWriteTargetMaster();
        testDensityWriteTargetPage0();
        testDensityWriteTargetPage1();
        testDensityWriteTargetStickyBank();

        testSectionSelectClearsDensitySticky();

        testWriteMetaFieldDensityPerTrack();
        testWriteMetaFieldDensityMasterIsGuardedNoOp();

        testMetaRotaryApplyViewTotality();

        testStepPositionBand();
    }
}
