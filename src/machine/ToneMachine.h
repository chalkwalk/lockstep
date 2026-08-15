#pragma once

#include "IMachine.h"
#include "../tonecore/ToneEngine.h"

#include <span>

namespace lockstep
{
    // ToneMachine — General MIDI, and the first machine in the catalogue whose
    // engine is NOT its own (DESIGN §29.3, ROADMAP 4.10).
    //
    // One `tone::ToneEngine` is shared by every track holding a Tone. This class
    // is the per-track face of it: it owns a channel (which IS its track index),
    // its program selection, and nothing else. It synthesises nothing itself.
    //
    // Per block the processor runs a PRE-PASS before any machine renders:
    //
    //   1. feedEngine() on every Tone track   — MIDI + params -> the engine
    //   2. engine->render(n) ONCE             — fills all 16 groups
    //   3. process() on every Tone track      — copies its own group out
    //
    // The split matters and is not an optimisation. `processTrackChain` runs in
    // ROUTING order, so "render on the first Tone track" would render before
    // some tracks had delivered their MIDI, and which tracks those are would
    // depend on the routing graph. Rendering once, up front, is the only shape
    // that is correct for every graph.
    //
    // The brief is a cheap home keyboard with a questionable GM bank, and the
    // character is the point — see assets/bank/README.md before "fixing" a sound.
    class ToneMachine : public IMachine
    {
    public:
        static constexpr const char* kMachineId = "lockstep.tone.v1";

        // Slot layout. SRC first so `program` is slot 0, the picker's operand.
        enum Slot : int
        {
            kProgram = 0,   // SRC: GM program 0..127
            kDrumKit,       // SRC: 0 = melodic, else the Nth kit (bank 128)
            kBrightness,    // FILTER: CC 74
            kResonance,     // FILTER: CC 71
            kAttack,        // AMP: CC 73
            kRelease,       // AMP: CC 72
            kModWheel,      // MOD: CC 1
            kPortamento,    // MOD: CC 5 (time); CC 65 switch rides with it
            kNumSlots
        };

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "TONE"; }

        void prepare(double, int) override {}
        void reset() override;

        // Bound once at install. The channel IS the track index — never allocated
        // from a free list (PRINCIPLES §20), which is why there is no channel
        // parameter anywhere else in this class.
        void bindEngine(tone::ToneEngine* engine, int trackIndex) noexcept
        {
            engine_ = engine;
            channel_ = trackIndex;
        }
        [[nodiscard]] int channel() const noexcept { return channel_; }

        // PRE-PASS step 1 (audio thread). Deliver this track's MIDI and push any
        // changed params as CCs / a program change. Never renders.
        void feedEngine(const juce::MidiBuffer& events, const ParamFrame& params);

        // PRE-PASS step 3 (audio thread). Copy this track's group out. The engine
        // has already rendered every group exactly once.
        void process(const juce::MidiBuffer& events, const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        // TRIG(0) SRC(1) FILTER(2) AMP(3) MOD(4) -> highest index + 1, NOT a
        // count (the numSections trap in CLAUDE.md). FX is deliberately absent:
        // the GM reverb/chorus sends are inert because the engine's internal
        // effects are off, so there is nothing to put there and §8 says leave it
        // empty. AnalogMachine returns 5 for the same reason.
        [[nodiscard]] int numSections() const override { return kModSecIdx + 1; }
        [[nodiscard]] SectionInfo section(int index) const override;

        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V4;   // GM is polyphonic; the pool is shared
        }

        // The canonical GM program names, 128 of them. Deliberately the STANDARD
        // names rather than the bank's own: they are stable, they exist before
        // the bank has finished loading, and the picker must be able to label a
        // cell either way.
        [[nodiscard]] static std::span<const char* const> programNames() noexcept;

    private:
        tone::ToneEngine* engine_ = nullptr;
        int channel_ = -1;

        // Last values pushed, so a block only sends what changed. GM state is
        // per-channel and sticky; re-sending every CC every block would be
        // wasted work and would fight a P-Lock that only meant to nudge one.
        std::array<float, kNumSlots> lastSent_{};
        bool primed_ = false;

        static constexpr int kFltrSecIdx = 2;
        static constexpr int kAmpSecIdx = 3;
        static constexpr int kModSecIdx = 4;
    };
}   // namespace lockstep
