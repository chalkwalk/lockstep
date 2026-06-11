#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    // Scans a MidiBuffer for system-realtime clock messages (0xF8/0xFA/0xFB/0xFC)
    // and maintains a running PPQ accumulator + smoothed BPM estimate.
    // Designed to be called once per processBlock, before Clock::update().
    class MidiClockReceiver
    {
    public:
        struct BlockResult
        {
            bool running = false;  // MIDI transport is running (Start/Continue, not stopped)
            bool didStart = false;  // 0xFA received this block  → caller should reset phase
            bool didStop = false;  // 0xFC received this block  → caller should freeze
            bool dropout = false;  // running but no pulse for > kDropoutSeconds
            bool hasClock = false;  // any MIDI clock message ever received
            double ppqStart = 0.0;    // PPQ at the start of this block
            double ppqEnd = 0.0;    // PPQ at the end   of this block
            double bpm = 0.0;    // smoothed BPM (0 until first inter-pulse interval)
        };

        // Scan midi for clock messages; advance internal state.
        // Must be called before Clock::update() each block.
        BlockResult advance(const juce::MidiBuffer& midi,
                            int blockSize, double sampleRate);

        void reset();

    private:
        static constexpr double kPpqPerPulse = 1.0 / 24.0;
        static constexpr double kDropoutSeconds = 0.5;
        static constexpr double kEmaAlpha = 0.2;  // BPM smoothing weight

        double ppqAccum_ = 0.0;
        double smoothedBpm_ = 0.0;
        bool haveBpm_ = false;
        bool running_ = false;
        bool hasClock_ = false;

        bool hadAnyPulse_ = false;  // received at least one 0xF8
        int samplesSinceLastPulse_ = 0;     // valid only when hadAnyPulse_
        int samplesSinceAnyPulse_ = 0;     // for dropout detection
    };
}
