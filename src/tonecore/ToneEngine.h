#pragma once

// ToneEngine — ONE multi-timbral SoundFont engine, shared by every track that
// holds a `Tone` machine (DESIGN §29.3, ROADMAP 4.10).
//
// This is the first engine in the catalogue that is not per-track. The shape it
// enables is an identity: FluidLite routes a voice to
// `left_buf[channel_num % audio_groups]`, so with audio-channels and
// audio-groups both at 16, **MIDI channel N renders into group N** — and
// Lockstep assigns channel N to track N, statically, never from a free list.
// The track index IS the channel is the whole design (PRINCIPLES §20).
//
// JUCE-free, like deck_core: the C++ standard library and FluidLite, nothing
// else. Audio leaves as pointer views over engine-owned buffers.
//
// ── Threading (FluidLite has NO internal locking; every mutex in
//    fluid_synth.c is commented out) ────────────────────────────────────────
//
//   load()      message thread ONLY. Does file-shaped I/O over a memory block
//               and decodes every SF3 sample to PCM, so it is slow and must
//               never touch the audio thread. Publish `ready()` before the
//               audio thread uses anything else here.
//   everything  audio thread ONLY, once ready(). None of it allocates:
//   else        voices are pre-allocated from `polyphony` at construction, and
//               program changes go through a preset cache resolved at load
//               (see tone_preset_swap.h for why that is not optional).
//
// ── What this engine deliberately does NOT do ─────────────────────────────
//
// Reverb and chorus are OFF, and that is mandatory rather than stylistic.
// `fluid_synth_nwrite_float` never touches its fx_left/fx_right parameters —
// they appear only in its signature — and it always renders with "do not mix fx
// to out". On the multi-group path the effects are computed and then DISCARDED,
// so leaving them on would burn CPU on something inaudible. Lockstep's per-track
// FX section is where reverb belongs anyway (PRINCIPLES §9).

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace lockstep::tone
{
    // One instrument the bank offers. `bank` is 0 for the melodic set and 128
    // for drum kits; `program` is the GM program number.
    struct Instrument
    {
        int bank = 0;
        int program = 0;
        std::string name;
    };

    class ToneEngine
    {
    public:
        // Sixteen, and the number is load-bearing: it is kNumTracks, the MIDI
        // channel count, and the audio-group count at once. See DESIGN §29.3 —
        // channel-per-track spends this space exactly.
        static constexpr int kNumChannels = 16;
        static constexpr int kMelodicBank = 0;
        static constexpr int kDrumBank = 128;
        static constexpr int kNumPrograms = 128;

        ToneEngine();
        ~ToneEngine();

        ToneEngine(const ToneEngine&) = delete;
        ToneEngine& operator=(const ToneEngine&) = delete;

        // ---- message thread ------------------------------------------------

        // Load the bank from a memory block the caller owns and keeps alive for
        // the engine's lifetime (the embedded BinaryData blob). Resolves every
        // preset into the cache. Slow: it decodes the whole SF3 to PCM.
        // Returns false and leaves the engine unusable on failure.
        bool load(const void* sf3Data, int sf3Bytes, double sampleRate, int polyphony);

        [[nodiscard]] bool ready() const noexcept { return ready_; }
        [[nodiscard]] const std::string& lastError() const noexcept { return lastError_; }

        // Melodic instruments the bank actually contains, and its drum kits.
        // Both are resolved at load, so a picker can be built without touching
        // the synth.
        [[nodiscard]] const std::vector<Instrument>& melodic() const noexcept { return melodic_; }
        [[nodiscard]] const std::vector<Instrument>& drumKits() const noexcept { return drumKits_; }

        // ---- audio thread, after ready() ------------------------------------

        void noteOn(int chan, int note, int velocity) noexcept;

        // Note-ons FluidLite refused. Should be zero; anything else is a note the
        // sequencer thinks it played and the listener never heard.
        [[nodiscard]] int noteOnFailures() const noexcept
        {
            return noteOnFailures_.load(std::memory_order_relaxed);
        }
        void noteOff(int chan, int note) noexcept;
        void controlChange(int chan, int cc, int value) noexcept;
        void pitchBend(int chan, int value14) noexcept;   // 0..16383, 8192 centre
        void allNotesOff(int chan) noexcept;

        // Install a preset from the cache. No allocation, no free — the whole
        // reason tone_preset_swap exists. A (bank, program) the bank does not
        // carry is ignored, leaving the channel on what it had.
        void selectProgram(int chan, int bank, int program) noexcept;

        // Render every channel's group in ONE call. `numSamples` may be any
        // block size. After this, group(chan) views this block's audio.
        void render(int numSamples) noexcept;

        [[nodiscard]] const float* groupLeft(int chan) const noexcept;
        [[nodiscard]] const float* groupRight(int chan) const noexcept;

        // Capacity the render buffers were sized for. render() clamps to it.
        [[nodiscard]] int maxBlockSize() const noexcept { return maxBlock_; }
        void setMaxBlockSize(int n);   // message thread; reallocates

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;

        bool ready_ = false;
        int maxBlock_ = 0;
        std::atomic<int> noteOnFailures_{ 0 };
        std::string lastError_;
        std::vector<Instrument> melodic_;
        std::vector<Instrument> drumKits_;
    };
}   // namespace lockstep::tone
