#pragma once

// HarmonyGen.h — scale-constrained multi-voice chord buffer (Phase 10.8 / DESIGN §39.12).
//
// A *print* tool in the Euclid / MelodyGen mould, but it operates on a short
// PROGRESSION of chord slots rather than a single melodic line. It carries NO
// chord theory — no quality menu, no progression templates, no auto-voicer.
// Each chord is just up to four voices, every voice an index into the diatonic
// LADDER (the scale expanded across octaves), so voices are always in-key. The
// user moves voices by ear (the editor's encoders); this core only builds the
// ladder, seeds a default diatonic stack, and prints the progression onto evenly
// spaced steps as ordinary, hand-editable chord steps.

#include "MusicalGate.h"
#include "Scale.h"
#include "Step.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace lockstep
{
    inline constexpr int kMaxHarmonyChords = 8;             // progression slots
    inline constexpr int kHarmonyVoices    = kMaxNotesPerStep;  // 4-voice ceiling
    inline constexpr int kHarmonyRootBase  = 48;            // C3 — ladder octave floor
    inline constexpr int kHarmonyOctaves   = 3;             // ladder span in octaves

    // One chord slot: a contiguous stack of `voiceCount` voices (bass..top), each
    // an index into the diatonic ladder. Removing a voice trims the stack from the
    // top; the collective transpose / octave encoders simply nudge every voice
    // index together, so a chord never needs a separate offset accumulator.
    struct HarmonyChord
    {
        int voiceCount = 3;
        std::array<int, kHarmonyVoices> voice { { 0, 2, 4, 0 } };  // ladder indices
    };

    struct HarmonyProgression
    {
        int length = 4;   // active chord slots (1..kMaxHarmonyChords)
        int cursor = 0;   // selected slot (0..length-1)
        std::array<HarmonyChord, kMaxHarmonyChords> chords {};
    };

    // One printed step: a chord (0..4 notes, ascending) plus its gate.
    struct HarmonyStep
    {
        bool        trig = false;
        int         noteCount = 0;
        std::array<int, kHarmonyVoices> notes {};
        MusicalGate gate = MusicalGate::None;
    };

    // Ladder steps per octave = number of pitch classes in the key's scale.
    [[nodiscard]] inline int harmonyScaleSize(const KeySig& key) noexcept
    {
        const uint16_t mask = pcMask(key);
        int n = 0;
        for (int pc = 0; pc < 12; ++pc)
            if (maskHas(mask, pc)) ++n;
        return n > 0 ? n : 12;
    }

    // Ascending absolute-MIDI ladder of every in-scale pitch across `octaves`
    // octaves from rootMidi. A voice is an index into this list.
    [[nodiscard]] inline std::vector<int>
    harmonyLadder(const KeySig& key, int rootMidi, int octaves)
    {
        const uint16_t mask = pcMask(key);
        std::vector<int> pcs;
        for (int pc = 0; pc < 12; ++pc)
            if (maskHas(mask, pc)) pcs.push_back(pc);

        std::vector<int> ladder;
        const int oct = std::clamp(octaves, 1, 5);
        for (int o = 0; o < oct; ++o)
            for (int pc : pcs)
            {
                const int note = rootMidi + o * 12 + ((((pc - rootMidi) % 12) + 12) % 12);
                if (note >= 0 && note <= 127) ladder.push_back(note);
            }
        std::sort(ladder.begin(), ladder.end());
        ladder.erase(std::unique(ladder.begin(), ladder.end()), ladder.end());
        return ladder;
    }

    // Map a chord's sustain in steps (a step = a 1/16th) to a MusicalGate. Chords
    // hold until the next chord's onset, so this is the gap between slots.
    [[nodiscard]] inline MusicalGate harmonyGate(int steps) noexcept
    {
        switch (std::clamp(steps, 1, 16))
        {
            case 1:  return MusicalGate::G1_16;
            case 2:  return MusicalGate::G1_8;
            case 3:  return MusicalGate::G1_8d;
            case 4:  return MusicalGate::G1_4;
            case 5:
            case 6:  return MusicalGate::G1_4d;
            case 7:
            case 8:  return MusicalGate::G1_2;
            default: return MusicalGate::G1_2d;
        }
    }

    // Print the progression onto `length` steps. Chord k lands on the evenly
    // spaced onset floor(k * length / K) and sustains until chord k+1 (or the
    // phrase end). Voices map through the ladder; duplicate pitches after clamping
    // collapse, and notes come out ascending. Empty ladder / length => no steps.
    [[nodiscard]] inline std::vector<HarmonyStep>
    printHarmony(const HarmonyProgression& prog, const std::vector<int>& ladder,
                 int length)
    {
        std::vector<HarmonyStep> out(static_cast<std::size_t>(std::max(0, length)));
        if (length <= 0 || ladder.empty())
            return out;

        const int K = std::clamp(prog.length, 1, kMaxHarmonyChords);
        const int ladderMax = static_cast<int>(ladder.size()) - 1;

        std::array<int, kMaxHarmonyChords> onset {};
        for (int k = 0; k < K; ++k)
            onset[static_cast<std::size_t>(k)] = std::clamp((k * length) / K, 0, length - 1);

        for (int k = 0; k < K; ++k)
        {
            const int step = onset[static_cast<std::size_t>(k)];
            const int next = (k + 1 < K) ? onset[static_cast<std::size_t>(k + 1)] : length;
            const int gap = std::max(1, next - step);
            const auto& ch = prog.chords[static_cast<std::size_t>(k)];

            std::vector<int> notes;
            const int vc = std::clamp(ch.voiceCount, 0, kHarmonyVoices);
            for (int v = 0; v < vc; ++v)
            {
                const int idx = std::clamp(ch.voice[static_cast<std::size_t>(v)], 0, ladderMax);
                notes.push_back(ladder[static_cast<std::size_t>(idx)]);
            }
            std::sort(notes.begin(), notes.end());
            notes.erase(std::unique(notes.begin(), notes.end()), notes.end());

            auto& s = out[static_cast<std::size_t>(step)];
            if (notes.empty())
                continue;
            s.trig = true;
            s.noteCount = std::min(static_cast<int>(notes.size()), kHarmonyVoices);
            for (int i = 0; i < s.noteCount; ++i)
                s.notes[static_cast<std::size_t>(i)] = notes[static_cast<std::size_t>(i)];
            s.gate = harmonyGate(gap);
        }
        return out;
    }
}
