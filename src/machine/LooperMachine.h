#pragma once

#include "IMachine.h"
#include "ITempoAware.h"
#include "ILoopGridAware.h"
#include "InputSource.h"
#include "SamplePool.h"
#include <atomic>

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
    // which is meaningless for a looper).
    //
    // The loop lives in a shared volatile pool slot (target_buffer, B3) — the same
    // RAM-only REC bank the Recorder writes — so the captured loop is also playable
    // by a Sampler/Player pointed at that slot (the OT recording-buffer model). The
    // looper self-plays it too. Loop length is free-running here (the span recorded
    // before the first close); transport-quantize + varispeed sync land in C4. The
    // one-level undo backup stays machine-internal (not a playable slot).
    class LooperMachine : public IMachine, public ITempoAware, public ILoopGridAware
    {
    public:
        explicit LooperMachine(SamplePool& pool) : pool_(pool) {}

        // Halve/Double (S4) resize the loop *window* with no resample / no pitch
        // change — they detach the loop from grid-lock (manualLen_) and play native.
        enum class Cmd : int { None = 0, RecordCycle, PlayStop, Clear, Undo, Halve, Double };
        // Armed (appended) — a quantized Record press waits here for the next bar
        // boundary (#2). It is a monitoring state (live-thru passes per the monitor
        // mode) with no loop output yet.
        enum class State : int { Idle = 0, Recording, Playing, Overdubbing, Stopped, Armed };

        static constexpr const char* kMachineId = "lockstep.looper.v1";

        [[nodiscard]] const char* machineId() const noexcept override { return kMachineId; }
        [[nodiscard]] const char* badge() const noexcept override { return "LOOP"; }

        // ITempoAware — bar length + transport phase for varispeed sync (C4).
        void setTransport(const TransportInfo& t) noexcept override { transport_ = t; }
        [[nodiscard]] const TransportInfo& transport() const noexcept { return transport_; }

        // ILoopGridAware — the focused track's grid (S1). Sync-mode loop length =
        // lengthSteps × stepPpq quarter notes (no machine-owned div/step params).
        void setLoopGrid(int lengthSteps, double stepPpq) noexcept override
        {
            loopGridSteps_ = lengthSteps > 0 ? lengthSteps : 1;
            loopStepPpq_ = stepPpq > 0.0 ? stepPpq : 0.0;
        }

        void prepare(double sampleRate, int maxBlockSize) override;
        void reset() override;

        void process(const juce::MidiBuffer& events,
                     const ParamFrame& params,
                     juce::AudioBuffer<float>& buffer) override;

        [[nodiscard]] int numParams() const override { return kNumSlots; }
        [[nodiscard]] ParamSpec paramSpec(int index) const override;

        // kSrcSecIdx + 1, not a count: LockstepProcessor::section() gates the
        // machine-owned branch on `sectionIndex < numSections()`. All our params
        // live at kSrcSecIdx (=1), so returning 1 made the SRC panel unreachable
        // ("no source panel" bug). Thru already got this right (returns 2).
        [[nodiscard]] int numSections() const override { return kSrcSecIdx + 1; }
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

        // Message thread: post a control command (single-slot mailbox). `immediate`
        // (double-tap, #2) forces the edge now, overriding quantize — encoded as a
        // high bit so the mailbox stays a single atomic int.
        void postCommand(Cmd c, bool immediate = false) noexcept
        {
            pendingCmd_.store(static_cast<int>(c) | (immediate ? kImmediateBit : 0),
                              std::memory_order_release);
        }
        // Message thread: current state, for chrome (advisory; updated each block).
        [[nodiscard]] State state() const noexcept
        {
            return static_cast<State>(stateMirror_.load(std::memory_order_acquire));
        }
        [[nodiscard]] static const char* stateLabel(State s) noexcept;

        // Message thread (chrome/tests): current loop-window length in samples (0 when
        // empty). Halve/Double (S4) resize this window without resampling.
        [[nodiscard]] int loopLengthSamples() const noexcept
        {
            return loopLenMirror_.load(std::memory_order_acquire);
        }

        // Message thread (chrome): current playback phase 0..1, or -1 when not
        // playing. Drives the continuous loop-position playhead on the mini-seq (S2).
        [[nodiscard]] float phase01() const noexcept
        {
            return phaseMirror_.load(std::memory_order_acquire);
        }
        // Message thread (chrome): true when a quantized edge is pending (Armed, or a
        // scheduled stop/re-play waiting for the bar grid) — drives the mini-seq's
        // landing pip at the loop-start anchor (S2).
        [[nodiscard]] bool pendingEdge() const noexcept
        {
            return pendingMirror_.load(std::memory_order_acquire);
        }

        // Resolve whether live input passes through to the output this block, given
        // the monitor mode and the input source (Auto = the looper monitors only
        // when it is the source's sole path out — None/External insert use — and
        // stays loop-only for a Track/Master tap whose source is already audible).
        // Public + pure so the editor/controller can label it consistently.
        [[nodiscard]] static bool resolveMonitor(int monMode, InputSourceKind src) noexcept
        {
            switch (monMode)
            {
                case 1: return true;   // On  — always pass live input through
                case 2: return false;  // Off — loop-only output (never monitor)
                default:               // Auto
                    return src == InputSourceKind::None || src == InputSourceKind::External;
            }
        }

    private:
        static constexpr int kSlotInputSource = 0;
        static constexpr int kSlotTargetBuffer = 1;  // volatile REC slot the loop lives in
        static constexpr int kSlotLoopSync = 2;      // Free | Free Len | Sync (S1)
        static constexpr int kSlotMonitor = 3;       // Auto | On | Off (live-thru, #1)
        static constexpr int kSlotDecay = 4;         // 0 = no decay … 1 = full (#4)
        static constexpr int kSlotDecayMode = 5;     // Overdub | Always (#4)
        static constexpr int kNumSlots = 6;
        // loop_sync value: Sync = grid-locked to the track's own length × divider,
        // pushed via ILoopGridAware (S1). Any value >= this is grid-locked.
        static constexpr int kSyncGrid = 2;
        static constexpr double kLoopMaxSeconds = 12.0;
        static constexpr int kDecayOverdub = 0;      // decay only where you overdub
        static constexpr int kDecayAlways  = 1;      // whole loop fades every iteration
        // Mailbox high bit: a posted command with this bit set is "immediate"
        // (double-tap) — it bypasses quantize. Cmd values are small (0..4).
        static constexpr int kImmediateBit = 0x100;

        void applyCommand(Cmd c, bool immediate);
        // Begin a fresh recording take: (re)size + clear the slot, reset positions,
        // arm the N-bar auto-close. Shared by the immediate and boundary-fired paths.
        void startRecording();
        // Quantize period (samples) for a pending edge in the current sync mode:
        // N-bar for N Bar, one bar for Free Len, 0 for Free / unknown tempo.
        [[nodiscard]] double quantPeriodSamples() const;
        // #4 Always-decay: scale the whole stored loop by `g` once per iteration.
        void scaleLoop(float g);
        // Apply the scheduled quantized edge (record-start / stop / re-play) and clear it.
        void firePending();
        // Finalise an in-progress recording: set loopLen_, shrink the pool slot to
        // the loop, stamp sourceBars, and enter Playing (or Idle if empty).
        void closeRecording();
        // Snapshot the loop's first `loopLen_` samples for one-level overdub undo,
        // without resizing the pool buffer.
        void snapshotForUndo();
        // Linear-interpolated read of the loop at a fractional position [0,loopLen_).
        [[nodiscard]] float loopSample(int ch, double pos) const;
        // Target playback duration (output samples) the loop should occupy at the
        // current project tempo, for the active sync mode; 0 = native (no stretch).
        [[nodiscard]] double targetOutputSamples() const;
        // Loop length (samples) for the grid-locked modes (N Bar / Steps) at the
        // current tempo; 0 if unknown. N Bar = N·bar; Steps = stepCount·(bar/divSteps).
        [[nodiscard]] double syncedLengthSamples() const;

        SamplePool& pool_;
        double sampleRate_ = 44100.0;
        int capacity_ = 0;        // current target buffer capacity (samples)
        int targetSlot_ = -1;     // resolved volatile pool index for the loop
        juce::AudioBuffer<float>* target_ = nullptr;  // pool pcm for targetSlot_ (this block)

        State state_ = State::Idle;
        int loopLen_ = 0;
        double playPos_ = 0.0;    // fractional read position (varispeed, C4)
        double lastPos_ = 0.0;    // previous read position, for Always-decay wrap detect (#4)
        int recPos_ = 0;
        int recLenTarget_ = 0;    // auto-close length for bar-quantized record (0 = none)
        int syncMode_ = 0;        // resolved loop_sync this block
        int loopGridSteps_ = 16;  // track length (steps), pushed via ILoopGridAware (S1)
        double loopStepPpq_ = 0.25;  // quarter-note PPQ per step, pushed via ILoopGridAware (S1)
        // Pending quantized edge (#2): 0 none / 1 start-record / 2 stop / 3 re-play.
        // Fired by firePending() when the transport phase crosses a bar-grid boundary.
        int pendingAction_ = 0;
        double rate_ = 1.0;       // current (slewed) varispeed rate
        int xfadeLen_ = 0;        // loop-wrap crossfade length (samples), C5
        bool haveBackup_ = false;
        // S4: a HALF/DBL length edit detaches the loop from grid-lock — it then plays
        // native (rate 1, no phase-lock, no varispeed) so the cut/double has no pitch
        // change. Cleared on the next record (startRecording) or Clear.
        bool manualLen_ = false;

        // The loop lives in pool slot `targetSlot_` (resolved each block); backup_
        // is the internal one-level overdub undo; inScratch_ copies the input before
        // we overwrite the track buffer with loop playback.
        juce::AudioBuffer<float> backup_;
        juce::AudioBuffer<float> inScratch_;

        std::atomic<int> pendingCmd_{ 0 };
        std::atomic<int> stateMirror_{ 0 };
        std::atomic<int> loopLenMirror_{ 0 };      // loop-window length in samples (S4)
        std::atomic<float> phaseMirror_{ -1.0f };  // playback phase 0..1 (-1 = not playing), S2
        std::atomic<bool> pendingMirror_{ false }; // a quantized edge is pending (S2)

        TransportInfo transport_{};  // last block transport (C2)

        // loop_sync (S1): Free (native, ignores tempo) | Free Len (varispeed to
        // the recorded musical duration, floats) | Sync (varispeed + grid
        // phase-lock to the track's own length × divider, pushed via
        // ILoopGridAware). The old 1/2/4 Bar + Steps values collapsed into Sync
        // (any value >= kSyncGrid is grid-locked); old projects migrate on load.
        static constexpr std::array<const char* const, 3> kLoopSyncLabels = {
            "Free", "Free Len", "Sync"
        };

        // monitor: Auto (resolveMonitor — On for None/External insert, Off for a
        // Track/Master tap) | On (always pass live input through) | Off (loop-only).
        static constexpr std::array<const char* const, 3> kMonitorLabels = {
            "Auto", "On", "Off"
        };

        // decay_mode (#4): Overdub fades the old layer only where you overdub
        // (feedback knob); Always fades the whole loop once per iteration (tape echo).
        static constexpr std::array<const char* const, 2> kDecayModeLabels = {
            "Overdub", "Always"
        };
    };
}
