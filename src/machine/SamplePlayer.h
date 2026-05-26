#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cstdint>

namespace lockstep
{
    // Per-voice DSP core for sample-based machines (Sampler, Slicer).
    // Holds playback position, window bounds, loop bounds, and AHDSR envelope.
    // The caller owns VoiceChoke and multiplies its gain into the value returned
    // by step(). One SamplePlayer per voice slot in the machine's voice array.
    struct SamplePlayer
    {
        enum class Stage : std::uint8_t { Idle, Attack, Hold, Decay, Sustain, Release };

        // Loop mode enum — mirrors the samp_loop_mode / slicer_loop_mode slot.
        // Off: no looping.
        // Sust: loop during sustain only; release exits to one-shot.
        // SustAndRel: loop continues into release until envelope reaches zero.
        // All: loop from first note-on through the whole envelope.
        enum class LoopMode : std::uint8_t { Off, Sust, SustAndRel, All };

        // Supplied to trigger() to start a voice. Fields not relevant for a
        // given machine (e.g. no loop) stay at their zero-value defaults.
        struct Spec
        {
            int    sampleIndex   = -1;
            double positionStart = 0.0;  // initial position in source samples
            // Exclusive playback window. 0.0 = "use full sample length" (step 2
            // default). Steps 5/6 set this from samp_start + samp_length.
            double windowEnd     = 0.0;
            double rate          = 1.0;  // negative = reverse
            float  level         = 1.0f;
            // AHDSR
            int    attackSamples  = 0;
            int    holdSamples    = 0;
            int    decaySamples   = 0;
            float  sustainLevel   = 0.5f;
            int    releaseSamples = 0;
            // Loop window and mode (step 6 wires these from samp_loop_* slots).
            // Expressed in absolute source samples relative to positionStart's sample.
            double   loopStart  = 0.0;
            double   loopEnd    = 0.0;
            LoopMode loopMode   = LoopMode::Off;
        };

        // Playback state
        bool    active        = false;
        int     sampleIndex   = -1;
        double  position      = 0.0;
        double  rate          = 1.0;
        double  windowEnd     = 0.0;  // 0 = use full sample (see Spec above)
        float   level         = 1.0f;
        double  loopStart     = 0.0;
        double  loopEnd       = 0.0;
        LoopMode loopMode     = LoopMode::Off;

        // Envelope
        Stage stage               = Stage::Idle;
        float envLevel            = 0.0f;
        float releaseStartLevel   = 0.0f;
        int   stageRemaining      = 0;
        int   attackSamples       = 0;
        int   holdSamples         = 0;
        int   decaySamples        = 0;
        float sustainLevel        = 0.5f;
        int   releaseSamples      = 0;

        void  trigger(const Spec& spec);
        void  release();
        // Returns one output sample (mono). Multiply by chokeGain before summing.
        // Sets active=false when the voice goes idle.
        [[nodiscard]] float step(const juce::AudioBuffer<float>& pcm);
        [[nodiscard]] bool  isActive() const noexcept { return active; }

    private:
        void  advanceStage();
        float nextEnvSample();
    };
}
