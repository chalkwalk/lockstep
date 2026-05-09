#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>

namespace lockstep
{
    // Transport clock. Polls the DAW playhead when available; synthesises PPQ
    // from localBpm_ when running standalone or in Auto mode. All audio-thread
    // values are mirrored to atomics so the UI thread can read safely.
    class Clock
    {
    public:
        Clock();

        // Called once per prepareToPlay.
        void prepare(double sampleRate);

        // Called once at the start of each processBlock. Reads the DAW
        // playhead (if present) and advances the local PPQ accumulator
        // (standalone / Auto). Sets ppqAtBlockStart / ppqAtBlockEnd for the
        // block, and ppqJumped if the timeline moved backward (DAW loop/jog).
        void update(juce::AudioPlayHead* playHead, int blockSize);

        // ---- Block-scope accessors (audio thread, set by update()) ----------
        double ppqAtBlockStart() const { return ppqBlockStart_; }
        double ppqAtBlockEnd()   const { return ppqBlockEnd_; }
        bool   ppqJumped()       const { return ppqJumped_; }

        // ---- Effective clock state ------------------------------------------
        double bpm()           const { return bpm_; }
        double samplesPerPpq() const;
        double sampleRate()    const { return sampleRate_; }

        bool hostPlaying()     const { return hostPlaying_; }
        bool inPluginPlaying() const { return inPluginPlaying_; }
        void setInPluginPlaying(bool p) { inPluginPlaying_ = p; }

        // ---- Standalone / Auto mode controls --------------------------------
        void setLocalBpm(double bpm);
        void resetPhase();

        // ---- Legacy UI accessors (UI thread, approximate) -------------------
        // Kept for the StepGrid playhead display until it migrates to PPQ.
        std::int64_t samplePosition() const
        {
            return samplePosition_.load(std::memory_order_relaxed);
        }
        double samplesPerStep() const;  // 16th note at current BPM

    private:
        double sampleRate_      = 0.0;
        double bpm_             = 120.0;
        double localBpm_        = 120.0;
        bool   hostPlaying_     = false;
        bool   inPluginPlaying_ = true;   // Stage 1 default: behaves like current free-run

        double ppqBlockStart_ = 0.0;
        double ppqBlockEnd_   = 0.0;
        double localPpq_      = 0.0;      // standalone PPQ accumulator
        bool   ppqJumped_     = false;

        std::atomic<std::int64_t> samplePosition_{0};  // legacy UI display
    };
}
