#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace lockstep
{
    // Transport clock. Polls the DAW playhead when available; synthesises PPQ
    // from localBpm_ when running standalone or in Auto mode.
    //
    // Threading model:
    //   inPluginPlaying_  — atomic<bool>: UI thread writes, audio thread reads.
    //   ppqUi_            — atomic<uint64_t> (double bits): audio thread writes,
    //                       UI thread reads via cumulativePpq().
    //   Everything else   — audio thread only.
    class Clock
    {
    public:
        Clock();

        // Optional MIDI clock state; passed by processBlock when clock is present.
        // When active, the standalone PPQ branch uses the MIDI-derived values
        // instead of synthesising from localBpm_.
        struct MidiClockInput
        {
            bool   active   = false;  // caller has a valid MIDI clock signal this block
            double ppqStart = 0.0;
            double ppqEnd   = 0.0;
            double bpm      = 0.0;   // 0 = unknown; when > 0, bpm_ is updated
        };

        // Called once per prepareToPlay.
        void prepare(double sampleRate);

        // Called once at the start of each processBlock. Reads the DAW
        // playhead (if present) and advances the local PPQ accumulator
        // (standalone / Auto). Sets ppqAtBlockStart / ppqAtBlockEnd for the
        // block, and ppqJumped if the timeline moved backward (DAW loop/jog).
        void update(juce::AudioPlayHead* playHead, int blockSize);
        void update(juce::AudioPlayHead* playHead, int blockSize, MidiClockInput midiClock);

        // ---- Block-scope accessors (audio thread only, set by update()) ----
        double ppqAtBlockStart() const { return ppqBlockStart_; }
        double ppqAtBlockEnd()   const { return ppqBlockEnd_; }
        bool   ppqJumped()       const { return ppqJumped_; }

        // ---- Effective clock state (audio thread) ---------------------------
        double bpm()           const { return bpm_; }
        double samplesPerPpq() const;
        double sampleRate()    const { return sampleRate_; }
        bool   hostPlaying()   const { return hostPlaying_; }

        // ---- In-plugin transport (cross-thread, atomic) ---------------------
        bool inPluginPlaying() const
        {
            return inPluginPlaying_.load(std::memory_order_relaxed);
        }
        void setInPluginPlaying(bool p)
        {
            inPluginPlaying_.store(p, std::memory_order_relaxed);
        }

        bool isRecordArmed() const
        {
            return recordArmed_.load(std::memory_order_relaxed);
        }
        void setRecordArmed(bool armed)
        {
            recordArmed_.store(armed, std::memory_order_relaxed);
            if (!armed)
                overdubArmed_.store(false, std::memory_order_relaxed);
        }

        bool isOverdubArmed() const
        {
            return overdubArmed_.load(std::memory_order_relaxed);
        }
        void setOverdubArmed(bool overdub)
        {
            overdubArmed_.store(overdub, std::memory_order_relaxed);
        }

        bool isMetronomeEnabled() const
        {
            return metronomeEnabled_.load(std::memory_order_relaxed);
        }
        void setMetronomeEnabled(bool enabled)
        {
            metronomeEnabled_.store(enabled, std::memory_order_relaxed);
        }

        // ---- Standalone / Auto mode controls (UI thread) -------------------
        void   setLocalBpm(double bpm);
        double localBpm() const { return localBpm_; }
        void   resetPhase();

        // ---- UI-safe PPQ read (atomic, UI thread) --------------------------
        // Returns the PPQ at the start of the last processed audio block.
        // Zero when stopped, monotonically increasing when playing.
        double cumulativePpq() const
        {
            const std::uint64_t bits = ppqUi_.load(std::memory_order_relaxed);
            double d;
            std::memcpy(&d, &bits, sizeof(d));
            return d;
        }

    private:
        double sampleRate_ = 0.0;
        double bpm_        = 120.0;
        double localBpm_   = 120.0;
        bool   hostPlaying_ = false;

        double ppqBlockStart_ = 0.0;
        double ppqBlockEnd_   = 0.0;
        double localPpq_      = 0.0;
        bool   ppqJumped_     = false;

        // Cross-thread state.
        std::atomic<bool>          inPluginPlaying_{false};
        std::atomic<bool>          recordArmed_{false};
        std::atomic<bool>          overdubArmed_{false};
        std::atomic<bool>          metronomeEnabled_{false};
        std::atomic<std::uint64_t> ppqUi_{0};  // double bits of ppqBlockStart_
    };
}
