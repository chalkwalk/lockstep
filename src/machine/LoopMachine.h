#pragma once

#include "../deckcore/Deck.h"
#include "IMachine.h"
#include "ITempoAware.h"
#include "ILoopGridAware.h"
#include "InputSource.h"
#include "SamplePool.h"
#include <atomic>

namespace lockstep
{
    // LoopMachine — overdub looper (Octatrack pickup machine, DESIGN §29.2).
    // Unlike the Record (overwrite, trig-driven), the Loop is a verb-driven
    // *state machine* encapsulated in the machine: Idle → Record → Play → Overdub,
    // plus Clear and Undo. It consumes input_source (records/overdubs it) and
    // produces audio (plays the loop back), so process() writes the loop into the
    // track buffer.
    //
    // Control is message-thread → audio-thread via a lock-free SPSC command FIFO
    // (LooperPerfCmd): the always-on looper console routes discrete transport verbs
    // (postCommand → pressed edge) and momentary performance actions (postPerf →
    // press/release edges + a rate value) onto the same queue; process() drains it.
    // A single-slot mailbox couldn't carry momentary press/release + a rate index
    // (S5 beat-repeat / S6 tape FX), so the FIFO replaced it (plan S3).
    //
    // The loop lives in a shared volatile pool slot (target_buffer, B3) — the same
    // RAM-only REC bank the Record writes — so the captured loop is also playable
    // by a Sample/Player pointed at that slot (the OT recording-buffer model). The
    // looper self-plays it too. Loop length is free-running here (the span recorded
    // before the first close); transport-quantize + varispeed sync land in C4. The
    // one-level undo backup stays machine-internal (not a playable slot).
    class LoopMachine : public IMachine, public ITempoAware, public ILoopGridAware
    {
    public:
        explicit LoopMachine(SamplePool& pool) : pool_(pool) {}

        // Discrete transport verbs (RecordCycle..Double) fire on the press edge.
        // Halve/Double (S4) resize the loop *window* with no resample / no pitch
        // change. Momentary performance actions (BeatRepeat..Reverse) toggle on the
        // press/release edges: BeatRepeat (S5) loops a grid cell while held; the tape
        // family (S6) drives the playback rate envelope while held. ABI: add-only.
        enum class Cmd : int { None = 0, RecordCycle, PlayStop, Clear, Undo, Halve, Double,
                               BeatRepeat, TapeStop, Dip, HalfSpeed, Reverse };

        // One control edge crossing the message→audio boundary. Discrete verbs use
        // pressed=true (a single edge); momentary actions carry both press and release.
        struct PerfCmd
        {
            Cmd action = Cmd::None;
            std::uint8_t value = 0;   // beat-repeat rate index (0=1/16 … 3=1/2)
            bool pressed = true;      // momentary on/off; discrete verbs are pressed=true
            bool immediate = false;   // double-tap: bypass quantize (discrete only)
        };
        // The looper's state IS the deck's state (11.2, DESIGN §40.1): Record, Loop
        // and Tape are three faces of one engine, and the state machine lives in
        // dc::Deck. The enumerators and their order are unchanged, so stateMirror_'s
        // int cast still means what the editor thinks it means.
        //
        // Armed — a quantized Record press waits here for the next bar boundary
        // (#2). It is a monitoring state (live-thru passes per the monitor mode)
        // with no loop output yet.
        using State = dc::DeckState;

        static constexpr const char* kMachineId = "lockstep.loop.v1";

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
        // ("no source panel" bug). Route already got this right (returns 2).
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

        // Message thread: post a discrete transport verb (fires on the press edge).
        // `immediate` (double-tap, #2) forces the edge now, overriding quantize.
        void postCommand(Cmd c, bool immediate = false) noexcept
        {
            pushPerf({ c, 0, /*pressed*/ true, immediate });
        }
        // Message thread: post a momentary performance edge (BeatRepeat / tape FX).
        // `value` = beat-repeat rate index; `pressed` drives the effect on/off.
        void postPerf(Cmd action, bool pressed, int value = 0) noexcept
        {
            pushPerf({ action, static_cast<std::uint8_t>(value), pressed, false });
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

        // Message thread (chrome): the active beat-repeat rate index (0=1/16 … 3=1/2),
        // or -1 when beat-repeat is not held (S5). Lights the held console rate cell.
        [[nodiscard]] int beatRepeatRate() const noexcept
        {
            return brRateMirror_.load(std::memory_order_acquire);
        }

        // Message thread (chrome): the held tape-fx cell index (0=TapeStop, 1=Dip,
        // 2=HalfSpeed, 3=Reverse), or -1 when no tape fx is held (S6).
        [[nodiscard]] int tapeFx() const noexcept
        {
            return tapeMirror_.load(std::memory_order_acquire);
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

        // Resolve whether live input passes through to the output, given the monitor
        // mode, input source, and loop state. Auto (S7) is state-aware for an insert
        // source (None/External): monitor in every state EXCEPT while the captured
        // loop plays back (Playing) — the take replaced the live source; monitoring is
        // restored in Idle/Armed/Recording/Overdubbing/Stopped. A Track/Master tap is
        // always loop-only under Auto (its source is already audible). Manual On/Off
        // are absolute, state-independent. Public + pure so the editor/controller can
        // label it consistently.
        [[nodiscard]] static bool resolveMonitor(int monMode, InputSourceKind src,
                                                 State state = State::Idle) noexcept
        {
            // An insert source (None/External) is not audible except through us;
            // a Track/Master tap already is. That is the only thing dc::Deck needs
            // to know about Lockstep's routing.
            const bool sourceIsInsert =
                (src == InputSourceKind::None || src == InputSourceKind::External);
            const auto mode = (monMode == 1) ? dc::Deck::Monitor::On
                            : (monMode == 2) ? dc::Deck::Monitor::Off
                                             : dc::Deck::Monitor::Auto;
            return dc::Deck::resolveMonitor(mode, sourceIsInsert, state);
        }

    private:
        static constexpr int kSlotInputSource = 0;
        static constexpr int kSlotTargetBuffer = 1;  // volatile REC slot the loop lives in
        static constexpr int kSlotLoopSync = 2;      // Free | Free Len | Sync (S1)
        static constexpr int kSlotMonitor = 3;       // Auto | On | Off (live-thru, #1)
        static constexpr int kSlotDecay = 4;         // 0 = no decay … 1 = full (#4)
        static constexpr int kSlotDecayMode = 5;     // Overdub | Always (#4)
        static constexpr int kSlotFreewheel = 6;     // 0 = follow transport, 1 = freewheel
        static constexpr int kSlotSubTrackCount = 7; // 1..4 deck sub-tracks (§40.3)
        static constexpr int kNumSlots = 8;
        // loop_sync value: Sync = grid-locked to the track's own length × divider,
        // pushed via ILoopGridAware (S1). Any value >= this is grid-locked.
        static constexpr int kSyncGrid = 2;
        static constexpr double kLoopMaxSeconds = 12.0;
        static constexpr int kDecayOverdub = 0;      // decay only where you overdub
        static constexpr int kDecayAlways  = 1;      // whole loop fades every iteration
        // S6 DIP is tape WOW: a periodic pitch wobble around unity while held, not a
        // plateau. (It used to target 0.5 with the same glide as HalfSpeed, i.e. it
        // WAS HalfSpeed.) Wow is the slow flavour of tape speed error — the fast one
        // is flutter — so the rate sits near the low end of the audible-as-pitch
        // range and the depth is a performance depth, not a defect depth.
        static constexpr double kWowRateHz = 5.0;    // wobble frequency
        static constexpr double kWowDepth  = 0.08;   // +/- 8% rate (~ +/- 1.3 semitones)

        // S6 tape-FX glide time-constants, in SECONDS (one-pole; larger = slower
        // glide). W4: the engage/return glide was ~6 ms — near-instant, so half-speed
        // and reverse snapped instead of sweeping. These are deliberately tape-like so
        // the pitch slur is audible; tune here to taste.
        static constexpr double kTapeGlideSec  = 0.10;  // half/reverse/dip engage + return-to-1
        static constexpr double kTapeStopSec   = 0.14;  // tape-stop brake to standstill
        static constexpr double kTapeResyncSec = 0.06;  // post-release catch-up to the grid

        // C6: the seam splice. `kSeamSpliceSec` of the input immediately BEFORE the
        // take is faded into the loop's end, so the wrap is continuous (DESIGN
        // §40.3 / deckcore/Seam.h). Long enough to hide a seam, short enough that
        // the ring-out it replaces is not missed. The loop's head — the downbeat —
        // is never touched.
        static constexpr double kSeamSpliceSec = 0.005;

        void applyCommand(Cmd c, bool immediate);
        // Audio thread: dispatch one drained FIFO edge — discrete verbs to
        // applyCommand (press only), momentary actions to the effect state.
        void handlePerf(const PerfCmd& c);
        // Message thread: enqueue one control edge onto the lock-free FIFO.
        void pushPerf(const PerfCmd& c) noexcept;
        // Beat-repeat (S5): capture the grid cell under the playhead and loop it while
        // held; release resyncs to the free-running position. No sound jump at press.
        void startBeatRepeat(int rateIdx);
        void stopBeatRepeat();
        // Tape FX (S6): a momentary playback-rate envelope. Press engages the effect;
        // release resyncs (accelerates to catch the grid) unless tape-stop already
        // braked to a graceful Stopped.
        void startTapeFx(Cmd fx);
        void stopTapeFx(Cmd fx);
        // Begin a fresh recording take: (re)size + clear the slot, reset positions,
        // arm the N-bar auto-close. Shared by the immediate and boundary-fired paths.
        void startRecording();
        // 9.17: the per-edge quantize period now comes from the shared launch grid
        // (TransportInfo::launchQuantPeriodSamples), not a private looper grid.
        // syncedLengthSamples()/targetOutputSamples() still own loop *length*.
        // #4 Always-decay: scale the whole stored loop by `g` once per iteration.
        void scaleLoop(float g);
        // Apply the scheduled quantized edge (record-start / stop / re-play) and clear it.
        void firePending();
        // Turn one dc::DeckEdge — the deck's work order — into looper work. The
        // state machine decides; the medium, the pool slot and the overdub layer
        // are ours, so the doing lives here.
        void applyEdge(const dc::DeckEdge& e);
        void doClear();
        void doUndo();
        void doHalve();
        void doDouble();
        // Finalise an in-progress recording: set loopLen_, shrink the pool slot to
        // the loop, stamp sourceBars, and enter Playing (or Idle if empty).
        void closeRecording();
        // Snapshot the loop's first `loopLen_` samples for one-level overdub undo,
        // without resizing the pool buffer.
        void snapshotForUndo();
        // C6: fade the captured pre-roll into the loop's end so the wrap is
        // continuous. Called once, at closeRecording, before the slot is shrunk.
        void spliceSeam();
        // C6: push one input frame into the rolling pre-roll ring (always running,
        // in every state — a take can begin on any sample).
        void pushPreRoll(const juce::AudioBuffer<float>& in, int sample, int chans);
        // Bandlimited read of the loop at a fractional position [0,loopLen_).
        // `readRate` = effective per-sample advance of the head: above unity the
        // read routes through the shared polyphase (rate-aware cutoff, 9.28.2);
        // at or below it a circular Hermite read suffices (anti-imaging only).
        [[nodiscard]] float loopSample(int ch, double pos, double readRate) const;
        // Same read law over an arbitrary loop-length buffer (the overdub layer
        // B) at a fractional position — no wrap crossfade (that is A's job).
        [[nodiscard]] float readLayer(const juce::AudioBuffer<float>& buf, int ch,
                                      double pos, double readRate) const;
        // R4 overdub layering: commit the fresh overdub layer B into the committed
        // loop A (add-only) and clear B; or drop B unchanged. commit is a per-loop-
        // iteration fold at the wrap (and on exit from Overdubbing) so the
        // bandlimited fractional write in B is decoupled from A's decay/feedback.
        void commitOverdubLayer();
        void dropOverdubLayer();
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

        // The state machine, the pending quantized edge, and the sub-track table.
        dc::Deck deck_;
        int loopLen_ = 0;
        double playPos_ = 0.0;    // fractional read position (varispeed, C4)
        double lastPos_ = 0.0;    // previous read position, for Always-decay wrap detect (#4)
        int recPos_ = 0;
        int recLenTarget_ = 0;    // auto-close length for bar-quantized record (0 = none)
        int syncMode_ = 0;        // resolved loop_sync this block
        int loopGridSteps_ = 16;  // track length (steps), pushed via ILoopGridAware (S1)
        double loopStepPpq_ = 0.25;  // quarter-note PPQ per step, pushed via ILoopGridAware (S1)
        double rate_ = 1.0;       // current (slewed) varispeed rate
        // Effective per-sample head advance actually applied (rate_ times the
        // tape-FX multiplier, or the resync catch-up sum) — the rate the head
        // reads and writes at (9.28.2). Updated at each advance site; reads use
        // the previous sample's value (slewed, so the one-sample lag is inert).
        double effRate_ = 1.0;
        // C6: the pre-roll. `preRing_` always holds the last `preLen_` input frames;
        // `preSnap_` is the copy taken when a take begins, chronologically ordered,
        // and is what the loop's end is spliced against at close. A playback
        // crossfade cannot fix a discontinuous seam (the circular read's kernel
        // wraps across it), so the fix lands in the content, once.
        int preLen_ = 0;          // pre-roll length (samples)
        int preWrite_ = 0;        // ring write cursor
        bool preSnapped_ = false; // preSnap_ holds this take's pre-roll
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
        juce::AudioBuffer<float> preRing_;   // C6: rolling pre-roll (circular)
        juce::AudioBuffer<float> preSnap_;   // C6: this take's pre-roll, in order
        // R4: the fresh overdub layer B (bandlimited fractional writes accumulate
        // here add-only; folded into the committed loop A once per iteration).
        juce::AudioBuffer<float> overdubLayer_;
        bool overdubPending_ = false;  // B holds uncommitted overdub content

        // Lock-free SPSC command FIFO (message → audio). Capacity is generous: at most
        // a handful of edges per block (one gesture), drained fully each process().
        static constexpr int kPerfFifoCap = 64;
        juce::AbstractFifo perfFifo_{ kPerfFifoCap };
        std::array<PerfCmd, kPerfFifoCap> perfSlots_{};

        // Beat-repeat (S5) state. brCellStart_/brCellLen_ define the captured grid
        // cell (loop-relative samples); brShadow_ is the free-running position tracked
        // in parallel so release resyncs to where the loop would be.
        bool brActive_ = false;
        int brRateIdx_ = 0;
        double brCellStart_ = 0.0;
        double brCellLen_ = 0.0;
        bool brCaptured_ = false;   // have we hit the first boundary and started looping?
        double brShadow_ = 0.0;

        // Tape FX (S6): a momentary playback-rate envelope. tapeAction_ is the held
        // effect (None = idle); tapeMult_ is the slewed rate multiplier; tapeGridPos_
        // tracks where the loop would be so a release can catch up (tapeResync_).
        Cmd tapeAction_ = Cmd::None;
        bool tapeResync_ = false;
        double tapeMult_ = 1.0;
        // S6 wow: the wobble's phase, and its depth envelope (slewed, so engaging
        // and releasing the effect eases the wobble in and out instead of jumping).
        double wowPhase_ = 0.0;
        double wowDepth_ = 0.0;
        double tapeGridPos_ = 0.0;

        std::atomic<int> stateMirror_{ 0 };
        std::atomic<int> loopLenMirror_{ 0 };      // loop-window length in samples (S4)
        std::atomic<int> brRateMirror_{ -1 };      // active beat-repeat rate index / -1 (S5)
        std::atomic<int> tapeMirror_{ -1 };        // active tape-fx cell index / -1 (S6)
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
        static constexpr std::array<const char* const, 2> kFreewheelLabels = {
            "Follow", "Free"
        };
    };
}
