#pragma once

#include "IMachine.h"
#include "InputSource.h"
#include <atomic>
#include <array>

namespace lockstep
{
    // LooperMachine — overdub looper (Octatrack pickup machine, DESIGN §29.2).
    // Unlike the Recorder (overwrite, trig-driven), the Looper is a verb-driven
    // *state machine* encapsulated in the machine: Idle → Record → Play → Overdub,
    // plus Clear and Undo. It consumes input_source (records/overdubs it) and
    // produces audio (plays the loop back), so process() writes the loop into the
    // track buffer.
    //
    // Control is message-thread → audio-thread via a single-slot lock-free command
    // mailbox: the editor routes Track+Record / Track+Play / Track+Clear (when a
    // Looper track is focused) to postCommand(); process() drains it. No new
    // grammar, no new keys (the loop's RAM content shadows the track clipboard,
    // which is meaningless for a looper). The loop buffer is internal RAM, lost on
    // quit. Loop length is free-running: it is the span recorded before the first
    // close.
    class LooperMachine : public IMachine
    {
    public:
        enum class Cmd : int { None = 0, RecordCycle, PlayStop, Clear, Undo };
        enum class State : int { Idle = 0, Recording, Playing, Overdubbing, Stopped };

        static constexpr const char* kMachineId = "lockstep.looper.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "LOOP"; }

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        [[nodiscard]] int numSections() const override { return 1; }
        [[nodiscard]] SectionInfo section(int index) const override
        {
            if (index == kSrcSecIdx) return { "SRC" };
            return {};
        }

        // Continuous, verb-driven — not trig-driven.
        [[nodiscard]] Polyphony currentVoices(const ParamFrame&) const override
        {
            return Polyphony::V0;
        }

        // Message thread: post a control command (single-slot mailbox).
        void postCommand(Cmd c) noexcept
        {
            pendingCmd_.store(static_cast<int>(c), std::memory_order_release);
        }
        // Message thread: current state, for chrome (advisory; updated each block).
        [[nodiscard]] State state() const noexcept
        {
            return static_cast<State>(stateMirror_.load(std::memory_order_acquire));
        }
        [[nodiscard]] static const char* stateLabel(State s) noexcept;

    private:
        static constexpr int kSlotInputSource = 0;
        static constexpr int kNumSlots = 1;
        static constexpr double kLoopMaxSeconds = 12.0;

        void applyCommand(Cmd c);

        double sampleRate_ = 44100.0;
        int capacity_ = 0;  // loop buffer capacity in samples

        State state_ = State::Idle;
        int loopLen_ = 0;
        int playhead_ = 0;
        int recPos_ = 0;
        bool haveBackup_ = false;

        juce::AudioBuffer<float> loop_;     // the loop (stereo)
        juce::AudioBuffer<float> backup_;   // one-level undo of the last overdub
        juce::AudioBuffer<float> inScratch_;  // input copy (read before we overwrite)

        std::atomic<int> pendingCmd_{ 0 };
        std::atomic<int> stateMirror_{ 0 };
    };
}
