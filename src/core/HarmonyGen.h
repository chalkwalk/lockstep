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

    // One chord slot: a contiguous stack of `voiceCount` voices (bass..top). Each
    // voice is a diatonic ladder rung (`voice[]`) plus a signed semitone
    // `chroma[]` offset for a borrowed (out-of-scale) tone — resolved MIDI is
    // `ladder[rung] + chroma`. In-scale voices always carry `chroma == 0` (the
    // write path canonicalizes), so the collective transpose / octave encoders
    // can keep nudging the rungs and a borrowed tone simply slides along with the
    // chord. Removing a voice trims the stack from the top.
    struct HarmonyChord
    {
        int voiceCount = 3;
        std::array<int, kHarmonyVoices> voice { { 0, 2, 4, 0 } };  // ladder rungs
        std::array<std::int8_t, kHarmonyVoices> chroma { };        // +/- semitone offsets
    };

    struct HarmonyProgression
    {
        int length = 1;   // active chord slots (1..kMaxHarmonyChords) — start narrow
        int cursor = 0;   // selected slot (0..length-1)
        int reach  = 1;   // high-water mark: slots [0,reach) hold authored chords,
                          // so shrink->grow restores rather than re-clones (lossless).
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

    // Resolve a voice (ladder rung + semitone offset) to an absolute MIDI note.
    [[nodiscard]] inline int
    resolveVoice(const std::vector<int>& ladder, int rung, int chroma) noexcept
    {
        if (ladder.empty()) return 0;
        const int ladderMax = static_cast<int>(ladder.size()) - 1;
        const int idx = std::clamp(rung, 0, ladderMax);
        return std::clamp(ladder[static_cast<std::size_t>(idx)] + chroma, 0, 127);
    }

    // After a chromatic nudge, fold an offset that lands back on an in-scale rung
    // into (rung, chroma=0) so in-scale tones never carry a residual offset. A
    // borrowed tone (no matching rung) keeps its rung + offset and slides under
    // diatonic MOVE/OCT. Mutates rung/chroma in place.
    inline void
    canonicalizeVoice(const std::vector<int>& ladder, int& rung, int& chroma) noexcept
    {
        if (ladder.empty()) { chroma = 0; return; }
        const int ladderMax = static_cast<int>(ladder.size()) - 1;
        const int target = resolveVoice(ladder, rung, chroma);
        for (int i = 0; i <= ladderMax; ++i)
            if (ladder[static_cast<std::size_t>(i)] == target)
            {
                rung = i;
                chroma = 0;
                return;
            }
        // Borrowed tone: anchor to the nearest rung at or below target, keep the
        // residual as the offset, so MOVE/OCT (rung nudges) carry it along.
        int anchor = std::clamp(rung, 0, ladderMax);
        while (anchor > 0 && ladder[static_cast<std::size_t>(anchor)] > target) --anchor;
        while (anchor < ladderMax && ladder[static_cast<std::size_t>(anchor + 1)] <= target) ++anchor;
        rung = anchor;
        chroma = target - ladder[static_cast<std::size_t>(anchor)];
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

    // Print the progression onto `length` steps. Placement is BAR-ALIGNED when the
    // phrase has at least K bars: chord k lands on bar k's downbeat (k * stepsPerBar).
    // When there are fewer bars than chords — or stepsPerBar <= 0 (caller opted out,
    // the test default) — it falls back to even spacing floor(k * length / K). Each
    // chord sustains until the next onset (or the phrase end). Voices resolve through
    // the ladder + chroma offset; duplicate pitches collapse and notes come out
    // ascending. Empty ladder / length => no steps.
    [[nodiscard]] inline std::vector<HarmonyStep>
    printHarmony(const HarmonyProgression& prog, const std::vector<int>& ladder,
                 int length, int stepsPerBar = 0)
    {
        std::vector<HarmonyStep> out(static_cast<std::size_t>(std::max(0, length)));
        if (length <= 0 || ladder.empty())
            return out;

        const int K = std::clamp(prog.length, 1, kMaxHarmonyChords);
        const int bars = (stepsPerBar > 0) ? std::max(1, length / stepsPerBar) : 0;
        const bool barAligned = (stepsPerBar > 0 && K <= bars);

        std::array<int, kMaxHarmonyChords> onset {};
        for (int k = 0; k < K; ++k)
            onset[static_cast<std::size_t>(k)] = barAligned
                ? std::clamp(k * stepsPerBar, 0, length - 1)
                : std::clamp((k * length) / K, 0, length - 1);

        for (int k = 0; k < K; ++k)
        {
            const int step = onset[static_cast<std::size_t>(k)];
            const int next = (k + 1 < K) ? onset[static_cast<std::size_t>(k + 1)] : length;
            const int gap = std::max(1, next - step);
            const auto& ch = prog.chords[static_cast<std::size_t>(k)];

            std::vector<int> notes;
            const int vc = std::clamp(ch.voiceCount, 0, kHarmonyVoices);
            for (int v = 0; v < vc; ++v)
                notes.push_back(resolveVoice(ladder,
                                             ch.voice[static_cast<std::size_t>(v)],
                                             ch.chroma[static_cast<std::size_t>(v)]));
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
