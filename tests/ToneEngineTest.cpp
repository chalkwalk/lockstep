// ToneEngineTest — does the shared SoundFont engine actually work?
//
// The whole `Tone` design rests on one claim about FluidLite that was read out
// of its source rather than measured: that MIDI channel N renders into audio
// group N, so channel == track == group is an identity and sixteen tracks can
// share one instance while staying separately processable. Everything else in
// the machine — per-track FX, cue, capture, stems — is downstream of that.
//
// So this file proves it end to end against the real bundled bank: load, park
// different programs on different channels, play a note on ONE channel, and
// assert that its group has audio and the others are silent. If group routing
// were not real, that assertion is exactly what would fail, and the fallback
// (one synth per track, ~30 MB each) would be back on the table.
//
// It also guards the real-time claim that made the preset cache necessary: a
// program change must not allocate, because it happens on the audio thread
// every time a knob moves while the pattern rolls.

#include "TestHarness.h"

#include "../src/tonecore/ToneEngine.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace lockstep
{
namespace
{
    using tone::ToneEngine;

    // The bank lives in the repo, not the build tree; tests read it from disk
    // rather than depending on the binary-data target (which belongs to the
    // plugin). Same bytes either way.
    std::vector<char> loadBankBytes()
    {
        const juce::File f = juce::File(LOCKSTEP_ASSET_DIR)
                                 .getChildFile("bank/GeneralUser-GS.sf3");
        std::vector<char> bytes;
        if (! f.existsAsFile()) return bytes;
        juce::FileInputStream in(f);
        if (! in.openedOk()) return bytes;
        bytes.resize(static_cast<std::size_t>(f.getSize()));
        in.read(bytes.data(), static_cast<int>(bytes.size()));
        return bytes;
    }

    float peakOf(const float* p, int n)
    {
        float m = 0.0f;
        for (int i = 0; i < n; ++i) m = std::max(m, std::abs(p[i]));
        return m;
    }
}   // namespace

void runToneEngineTests()
{
    const auto bank = loadBankBytes();
    CHECK(! bank.empty(), "the bundled SF3 bank is readable");
    if (bank.empty()) return;

    ToneEngine eng;
    eng.setMaxBlockSize(512);

    const bool ok = eng.load(bank.data(), static_cast<int>(bank.size()), 48000.0, 256);
    CHECK(ok, juce::String("the bank loads (") + eng.lastError() + ")");
    if (! ok) return;

    CHECK(eng.ready(), "the engine is ready after load");

    // The bank is a General MIDI bank: it must offer the full melodic set and
    // at least one drum kit, or the program picker has nothing to show.
    CHECK(eng.melodic().size() >= 100,
          juce::String("the bank carries a melodic set (")
              + juce::String((int) eng.melodic().size()) + ")");
    CHECK(! eng.drumKits().empty(),
          juce::String("...and drum kits (")
              + juce::String((int) eng.drumKits().size()) + ")");
    CHECK(eng.melodic().front().name.length() > 0,
          "presets carry their names, so the picker can label cells");

    // ── The load-bearing claim: channel N -> group N ──────────────────────
    // Park a different program on two channels, play a note on channel 3 only,
    // and look at every group. If FluidLite's `auchan = channel % audio_groups`
    // routing were absent, the audio would land in group 0 (or all groups) and
    // sixteen tracks could not share one instance.
    eng.selectProgram(3, ToneEngine::kMelodicBank, 0);    // piano
    eng.selectProgram(7, ToneEngine::kMelodicBank, 48);   // strings

    eng.noteOn(3, 60, 110);
    float peak3 = 0.0f;
    std::array<float, ToneEngine::kNumChannels> peaks{};
    for (int block = 0; block < 8; ++block)
    {
        eng.render(512);
        for (int c = 0; c < ToneEngine::kNumChannels; ++c)
        {
            const float l = peakOf(eng.groupLeft(c), 512);
            const float r = peakOf(eng.groupRight(c), 512);
            peaks[static_cast<std::size_t>(c)] =
                std::max(peaks[static_cast<std::size_t>(c)], std::max(l, r));
        }
    }
    peak3 = peaks[3];

    CHECK(peak3 > 0.0f, "a note on channel 3 makes sound");

    bool othersSilent = true;
    int firstNoisy = -1;
    for (int c = 0; c < ToneEngine::kNumChannels; ++c)
    {
        if (c == 3) continue;
        if (peaks[static_cast<std::size_t>(c)] > 0.0f)
        {
            othersSilent = false;
            if (firstNoisy < 0) firstNoisy = c;
        }
    }
    CHECK(othersSilent,
          juce::String("...and ONLY in its own group (channel == group == track); "
                       "first leak on group ")
              + juce::String(firstNoisy));

    // A second voice on a different channel lands in a different group, which
    // is what lets two tracks carry two instruments through one engine.
    eng.noteOn(7, 67, 110);
    std::array<float, ToneEngine::kNumChannels> peaks2{};
    for (int block = 0; block < 8; ++block)
    {
        eng.render(512);
        for (int c = 0; c < ToneEngine::kNumChannels; ++c)
            peaks2[static_cast<std::size_t>(c)] =
                std::max(peaks2[static_cast<std::size_t>(c)],
                         peakOf(eng.groupLeft(c), 512));
    }
    CHECK(peaks2[7] > 0.0f, "a second channel sounds into its own group");
    CHECK(peaks2[3] > 0.0f, "...without silencing the first");

    eng.allNotesOff(3);
    eng.allNotesOff(7);

    // ── The program cache: a program change must not allocate ─────────────
    // This is why tone_preset_swap.c exists. fluid_synth_program_change would
    // malloc a preset and free the old one on EVERY change, and a change lands
    // on the audio thread whenever the program knob moves while rolling.
    // Asserted behaviourally: sweeping the whole melodic set must leave the
    // engine working and the channel on a real preset.
    for (const auto& inst : eng.melodic())
        eng.selectProgram(5, inst.bank, inst.program);

    eng.selectProgram(5, ToneEngine::kMelodicBank, 0);
    eng.noteOn(5, 60, 110);
    float afterSweep = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        eng.render(512);
        afterSweep = std::max(afterSweep, peakOf(eng.groupLeft(5), 512));
    }
    CHECK(afterSweep > 0.0f,
          "the channel still sounds after sweeping every program (the cache holds)");

    // A drum kit is reachable on ANY channel, not just 9 -- that is what
    // synth.drums-channel.active = "no" buys, and channel-per-track needs it.
    if (! eng.drumKits().empty())
    {
        const auto& kit = eng.drumKits().front();
        eng.selectProgram(9, kit.bank, kit.program);
        eng.selectProgram(2, kit.bank, kit.program);
        eng.noteOn(2, 36, 120);
        float drumPeak = 0.0f;
        for (int block = 0; block < 8; ++block)
        {
            eng.render(512);
            drumPeak = std::max(drumPeak, peakOf(eng.groupLeft(2), 512));
        }
        CHECK(drumPeak > 0.0f,
              "a drum kit sounds on a channel that is NOT 9 (the GM lock is lifted)");
    }

    // An unknown (bank, program) is ignored rather than silencing the channel:
    // the bank does not carry all 128 slots in every bank.
    eng.selectProgram(5, ToneEngine::kMelodicBank, 0);
    eng.selectProgram(5, 77, 99);        // no such bank
    eng.noteOn(5, 64, 110);
    float afterBogus = 0.0f;
    for (int block = 0; block < 8; ++block)
    {
        eng.render(512);
        afterBogus = std::max(afterBogus, peakOf(eng.groupLeft(5), 512));
    }
    CHECK(afterBogus > 0.0f, "an unknown program leaves the channel on what it had");
}
}   // namespace lockstep
