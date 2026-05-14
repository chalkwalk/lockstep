#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>

#include "core/ChannelMode.h"
#include "core/Clock.h"
#include "core/Sequence.h"
#include "core/SyncMode.h"
#include "io/CCMappingTable.h"
#include "io/EditContext.h"
#include "io/MidiClockReceiver.h"
#include "io/MidiInput.h"
#include "machine/IMachine.h"
#include "machine/SamplePool.h"
#include "machine/VoiceChoke.h"

namespace lockstep
{
    // Info returned when querying whether a ManipulationZone widget has a CC mapping.
    struct WidgetMappingInfo
    {
        bool    exists     = false;
        CCScope scope      = CCScope::Track;
        int     trackIndex = -1;  // for Track scope badge label
        int     slot       = -1;
        int     mzPosition = -1;
        int     ccNumber   = -1;
    };

    class LockstepProcessor : public juce::AudioProcessor
    {
    public:
        LockstepProcessor();
        ~LockstepProcessor() override;

        void prepareToPlay(double sampleRate, int samplesPerBlock) override;
        void releaseResources() override;
        bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
        void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "Lockstep"; }
        bool acceptsMidi() const override                    { return true; }
        bool producesMidi() const override                   { return false; }
        bool isMidiEffect() const override                   { return false; }
        double getTailLengthSeconds() const override         { return 0.0; }

        int getNumPrograms() override                        { return 1; }
        int getCurrentProgram() override                     { return 0; }
        void setCurrentProgram(int) override                 {}
        const juce::String getProgramName(int) override      { return {}; }
        void changeProgramName(int, const juce::String&) override {}

        void getStateInformation(juce::MemoryBlock& dest) override;
        void setStateInformation(const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& apvts() { return apvts_; }
        Sequence& sequence() { return sequence_; }
        const Sequence& sequence() const { return sequence_; }
        Clock&       clock()       { return clock_; }
        const Clock& clock() const { return clock_; }
        SamplePool& samplePool() { return samplePool_; }
        EditContext& editContext() { return editContext_; }
        CCMappingTable& ccMappingTable() { return ccMappingTable_; }

        int  focusTrack() const       { return focusTrack_; }
        void setFocusTrack(int track) { focusTrack_ = track; }

        // Called from ManipulationZone (UI thread) when its slot offset changes.
        // Records which absolute slot each of the 4 display positions currently shows.
        void setMZSlots(int slotOffset);

        // MIDI Learn: arms the next CC received as a mapping.
        // slot and mzPosition are mutually exclusive (only one applies per scope).
        void startLearn(CCScope scope, int trackIndex, int slot, int mzPosition = -1);
        void cancelLearn();
        bool isLearning() const { return learnActive_.load(std::memory_order_acquire); }

        // Query whether a ManipulationZone widget column has a CC mapping.
        // slot is the absolute slot index; mzPosition is the widget's display position (0-3).
        WidgetMappingInfo queryWidgetMapping(int slot, int mzPosition) const;

        // Route a parameter write to the correct layer. If EditContext is
        // active for the given track, the value lands in the held step's
        // P-Lock; otherwise it updates the track's base params.
        void writeParam(int track, int slot, float value);

        // Remove the P-Lock override for one slot on a specific step.
        void clearParam(int track, int step, int slot);

        // Trigger a one-shot preview of the sample at poolIndex on the given track.
        // Safe to call from the message thread; the audio thread consumes the request
        // on the next processBlock call and injects a note-on + scheduled note-off.
        void triggerPreview(int poolIndex, int track);

        // Sample pool helpers — message-thread only.
        // sampleShortName returns the filename stem for a given pool index, or "(none)".
        juce::String sampleShortName(int poolIndex) const;
        // removeSample remaps all sequence sample references before erasing the entry.
        void removeSample(int poolIndex);
        // swapSamples remaps references and swaps two pool entries (reorder).
        void swapSamples(int a, int b);

        // Schema query helpers — forward to the machine on the given track.
        int         numParams(int track)              const;
        ParamSpec   paramSpec(int track, int index)   const;
        int         numSections(int track)            const;
        SectionInfo section(int track, int sectionIndex) const;

        // Slot identity bridge — forwarded to the machine on the given track.
        // Used by the M8 serializer to translate between runtime indices and
        // stable string ids. Returns {} / -1 for out-of-range inputs.
        juce::String idForSlot(int track, int index)       const;
        int slotForId(int track, const juce::String& id)   const;

        using juce::AudioProcessor::processBlock;

    private:
        juce::AudioProcessorValueTreeState apvts_;
        SamplePool samplePool_;
        Sequence sequence_;
        Clock clock_;
        EditContext editContext_;
        CCMappingTable ccMappingTable_;
        int focusTrack_ = -1;  // -1 = Global; 0-7 = Track

        // Current slot index for each MZ display position.
        // Written by the UI thread, read by the audio thread (atomic).
        std::array<std::atomic<int>, 4> mzSlots_;

        // Pending MIDI Learn request. UI thread writes fields then raises
        // learnActive_ (release); audio thread reads after acquire.
        struct PendingLearnRequest
        {
            CCScope scope      = CCScope::Track;
            int trackIndex     = -1;
            int slot           = -1;
            int mzPosition     = -1;
        };
        std::atomic<bool>  learnActive_ { false };
        PendingLearnRequest learnRequest_;

        // Preview request: message thread writes both fields (track first, then
        // poolIndex with release ordering); audio thread consumes with acq_rel exchange.
        std::atomic<int> previewPoolIndex_ { -1 };
        std::atomic<int> previewReqTrack_  { 0 };

        // Preview playback state — audio thread only (no atomics needed).
        bool previewActive_           = false;
        int  previewTrack_            = 0;
        int  previewSampleIndex_      = -1;
        int  previewNoteOffRemaining_ = -1;  // samples until note-off; -1 = inactive
        int  previewNote_             = 60;

        MidiInput midiInput_;
        MidiClockReceiver midiClockReceiver_;
        std::array<std::unique_ptr<IMachine>, kNumTracks> machines_;
        // Per-track choke faders for monophonic re-trigger (MA.4).
        // Sequencer uses these in MA.5 when emitting MIDI note-ons.
        std::array<VoiceChoke, kNumTracks> trackChokes_;

        // Pending sequencer-scheduled note-offs that spill past the current block boundary.
        struct PendingNoteOff
        {
            int samplesRemaining = -1;  // -1 = none; else samples from start of next block
            int noteNumber       = 60;
        };
        std::array<PendingNoteOff, kNumTracks> pendingNoteOffs_{};
        std::array<double, kNumTracks> nextTriggerPpq_{};
        std::array<bool, kNumTracks>   lastStepFired_{};
        double anchorPpq_ = 0.0;
        bool   wasInPluginPlaying_ = false;

        std::atomic<float>* syncModeParam_    = nullptr;
        std::atomic<float>* channelModeParam_ = nullptr;

        std::array<std::atomic<float>*, kNumTracks> trackLengthParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackDividerParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackMuteParams_{};
        std::array<std::atomic<float>*, kNumTracks> trackSoloParams_{};

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gainSmoothed_;
        std::array<float, 2> dcX1_{};
        std::array<float, 2> dcY1_{};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LockstepProcessor)
    };
}
