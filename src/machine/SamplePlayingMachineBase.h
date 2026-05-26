#pragma once

#include "IMachine.h"
#include "ISliceable.h"
#include "SamplePlayer.h"
#include "SamplePool.h"
#include "VoiceChoke.h"
#include <array>
#include <cstdint>

namespace lockstep
{
    // Shared base for sample-based machines (SamplerMachine, SlicerMachine).
    // Owns: sample pool reference, 4-slot voice array (SamplePlayer + VoiceChoke
    // + pending-trigger state per slot), ISliceable implementation, and the
    // common prepare/reset/isVoiceActive helpers.
    //
    // Subclasses provide: their own schema (paramSpec/section/numParams),
    // machineId/badge, currentVoices(), and process(). They call the base
    // helpers (allocVoice, findVoiceByNote, the VoiceSlot accessors) from
    // their own process() implementation.
    class SamplePlayingMachineBase : public IMachine, public ISliceable
    {
    public:
        static constexpr int kMaxVoices = 4;
        static constexpr int kMaxSlices = 16;

        explicit SamplePlayingMachineBase(SamplePool& pool);
        ~SamplePlayingMachineBase() override;

        // IMachine
        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;
        bool isVoiceActive() const override;

        // Snap a normalised [0..1] position value to the nearest zero-crossing
        // in the currently-loaded sample, searching within ±5 ms of the
        // requested position. Returns v unchanged if no sample is loaded.
        // Called by writeParam() when paramSpec(slot).zeroCrossingSnap is true.
        [[nodiscard]] float snapWrittenValue(int slot,
                                             float v,
                                             const SamplePool& pool,
                                             const ParamFrame& baseParams) const;

        // ISliceable
        [[nodiscard]] int numSlices() const override { return numSlices_; }
        void setEqualSlices(int count) override;
        void clearSlices()             override;
        // Populates slicePositions_ from transient-detected onsets in the
        // currently-loaded sample (uses Sample::analysis cached at load time).
        // count is the number of slices to place; capped by kMinSliceMs.
        void detectTransientSlices() override;
        // Variant that also accepts an explicit count (used by SlicerMachine).
        void detectTransientSlices(int count) override;

    protected:
        // Per-voice state: SamplePlayer, choke, and pending re-trigger.
        struct VoiceSlot
        {
            SamplePlayer player{};
            VoiceChoke   choke{};
            bool         hasPending   = false;
            int          pendingNote  = 60;
            ParamFrame   pendingParams{};
            std::uint64_t age        = 0;
            int          midiNote    = -1;  // note currently sounding (-1 = idle)
        };

        // Idle slot first; if all busy, steals the oldest active slot.
        [[nodiscard]] int allocVoice();
        // Returns the voice index holding the given note (newest if duplicated),
        // or -1 if not found.
        [[nodiscard]] int findVoiceByNote(int midiNote) const;

        static int msToSamples(float ms, double sampleRate)
        {
            return static_cast<int>(static_cast<double>(ms) * 0.001 * sampleRate);
        }

        SamplePool& pool_;
        double      sampleRate_ = 0.0;
        std::array<VoiceSlot, kMaxVoices> voices_{};
        std::uint64_t voiceCounter_ = 0;
        // Last sample index used (updated by subclasses from const build-spec paths).
        // Lets detectTransientSlices() find the sample without requiring baseParams.
        mutable int currentSampleIndex_ = 0;

        // MG.3 / ISliceable: normalized slice start positions [0.0, 1.0].
        std::array<float, kMaxSlices> slicePositions_{};
        int numSlices_ = 0;
    };
}
