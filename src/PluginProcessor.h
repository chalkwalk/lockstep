#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "core/ChannelMode.h"
#include "core/Clock.h"
#include "core/Metronome.h"
#include "core/Project.h"
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

        // Active pattern / part accessors — point into the live project.
        Sequence&       sequence()       { return activePattern().sequence; }
        const Sequence& sequence() const { return activePattern().sequence; }
        Pattern&        activePattern()  { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].patterns[static_cast<std::size_t>(activePatternIdx_)]; }
        const Pattern&  activePattern()  const { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].patterns[static_cast<std::size_t>(activePatternIdx_)]; }
        Part&           activePart()     { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].parts[static_cast<std::size_t>(activePattern().partRef)]; }
        const Part&     activePart()     const { return project_.banks[static_cast<std::size_t>(activeBankIdx_)].parts[static_cast<std::size_t>(activePattern().partRef)]; }
        Project&        project()        { return project_; }
        const Project&  project()        const { return project_; }

        int activeBankIdx()    const { return activeBankIdx_; }
        int activePatternIdx() const { return activePatternIdx_; }

        // Switch the active pattern (no-op if indices unchanged or out of range).
        // Syncs Track.baseParams from the new Part when the Part reference changes.
        void setActivePattern(int bankIdx, int patternIdx);

        // Fork the active Part: copy it into the first free Part slot so the
        // active pattern no longer shares its Part with any other pattern.
        // Returns true on success; false if the Part is not shared or all Part
        // slots are occupied.
        bool forkActivePart();

        // Returns the number of patterns in the active bank that reference the
        // same Part as the active pattern.
        int activePartShareCount() const;

        // Queue a pattern switch to fire at the next grid boundary (end of the
        // longest running track's cycle). Safe to call from the message thread.
        // cancelQueuedPattern() clears any pending switch.
        void queuePattern(int bankIdx, int patternIdx);
        void cancelQueuedPattern();
        bool hasQueuedPattern()      const;
        int  queuedPatternBankIdx()  const;
        int  queuedPatternPatIdx()   const;

        // Called on the message thread after a queued pattern switch fires.
        std::function<void()> onActivePatternChanged;

        // Chain mode: RAM-only ordered queue of upcoming pattern switches.
        // appendToChain adds to the back; the queue advances automatically as
        // each queued switch fires. clearChain cancels the remaining entries.
        // When chainLoopEnabled the current pattern is re-appended when consumed.
        void appendToChain(int bankIdx, int patternIdx);
        void clearChain();
        bool chainLoopEnabled() const { return chainLoopEnabled_; }
        void setChainLoopEnabled(bool v) { chainLoopEnabled_ = v; }
        int  chainLength()      const { return static_cast<int>(chain_.size()); }

        Clock&       clock()       { return clock_; }
        const Clock& clock() const { return clock_; }
        SamplePool& samplePool() { return samplePool_; }
        EditContext& editContext() { return editContext_; }
        CCMappingTable& ccMappingTable() { return ccMappingTable_; }

        int  focusTrack() const       { return focusTrack_; }
        void setFocusTrack(int track) { focusTrack_ = track; }

        // MD.10: Control-All — broadcast param writes to all tracks with a matching slot id.
        // Set true when Track scope is held without a specific track selected.
        // UI-thread only; no atomic needed.
        void setControlAllActive(bool v) { controlAllActive_ = v; }
        bool controlAllActive()    const { return controlAllActive_; }

        // MD.11: Checkpoint stack — RAM-only LIFO of (Pattern, Part) snapshots.
        // Per active pattern, capped at kMaxCheckpoints (oldest evicted on overflow).
        void pushCheckpoint();
        bool popCheckpoint();    // returns false if stack empty for the active pattern
        int  checkpointDepth() const;

        // MD.9: Fill scope state — set by the UI thread, read by the audio thread.
        void setFillActive(bool v) { fillActive_.store(v, std::memory_order_relaxed); }
        bool fillActive()    const { return fillActive_.load(std::memory_order_relaxed); }

        // MD.6: Global mutes — per-track, live in the APVTS (trackMute params).
        // getGlobalMute reads the APVTS param; setGlobalMute writes through APVTS.
        bool getGlobalMute(int track) const;
        void setGlobalMute(int track, bool muted);
        void toggleGlobalMute(int track);

        // MD.7: Pattern mutes — per-track, live in the active Pattern.
        bool getPatternMute(int track) const;
        void setPatternMute(int track, bool muted);
        void togglePatternMute(int track);

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

        // Remove all P-Lock overrides and trig overrides for a step.
        void clearStepLocks(int track, int step);

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
        // relinkSample replaces a missing (or any) pool entry in-place with a newly loaded file.
        // Call only when the sequencer is stopped to avoid audio-thread data races.
        bool relinkSample(int index, const juce::String& newPath);

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
        Project project_;          // full Project/Bank/Pattern/Part hierarchy
        int activeBankIdx_    = 0;
        int activePatternIdx_ = 0;
        Clock clock_;
        EditContext editContext_;
        CCMappingTable ccMappingTable_;
        int  focusTrack_       = -1;   // -1 = Global; 0-7 = Track
        bool controlAllActive_ = false;

        // MD.11: per-pattern checkpoint stacks; key = bankIdx * kPatternsPerBank + patIdx.
        struct CheckpointEntry { Pattern savedPattern; Part savedPart; };
        static constexpr int kMaxCheckpoints = 8;
        std::map<int, std::vector<CheckpointEntry>> checkpoints_;
        std::atomic<bool> fillActive_ { false };

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
        // Queued pattern switch: message thread writes, audio thread consumes at
        // the next grid boundary. -1/-1 means no switch is pending.
        std::atomic<int> queuedPatternBankIdx_ { -1 };
        std::atomic<int> queuedPatternPatIdx_  { -1 };

        // Chain queue: message-thread only. Pair = (bankIdx, patternIdx).
        std::deque<std::pair<int,int>> chain_;
        bool chainLoopEnabled_ = true;

        std::atomic<int> previewPoolIndex_ { -1 };
        std::atomic<int> previewReqTrack_  { 0 };

        // Preview playback state — audio thread only (no atomics needed).
        bool previewActive_           = false;
        int  previewTrack_            = 0;
        int  previewSampleIndex_      = -1;
        int  previewNoteOffRemaining_ = -1;  // samples until note-off; -1 = inactive
        int  previewNote_             = 60;

        Metronome metronome_;
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
