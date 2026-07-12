#pragma once

#include <cstdint>
#include <vector>

namespace dc
{
    // The transport, as the deck sees it (DESIGN §40.2, PRINCIPLES §25/§25.1).
    //
    // A POD snapshot, filled by whoever owns the transport authority — the DAW
    // when hosted, the app's own clock when standalone. deck_core is a *client*
    // of that authority and never an owner: it holds no clock, and it cannot
    // start, stop, or relocate anything.
    //
    // Grow this struct by APPENDING only. A host that fills the first six fields
    // of a nine-field snapshot must keep working.
    struct TransportSnapshot
    {
        double sampleRate = 44100.0;
        double samplesPerBar = 0.0;
        double barPpq = 4.0;
        bool running = false;

        // Absolute position: how far into the performance we are, in samples from
        // a zero. Hosted, this is the host's play head, which is why winding the
        // tape is dragging the host playhead (§40.2).
        double positionSamples = 0.0;

        // The one launch grid (§25). Period 0 means "no quantize — fire now".
        double launchQuantPeriodSamples = 0.0;
        double launchQuantPhaseOffsetSamples = 0.0;
    };

    // What a deck is doing. Faces (Record / Loop / Tape) differ in the medium's
    // topology and in what drives the commands, not in these states.
    enum class DeckState
    {
        Idle,
        Recording,
        Playing,
        Overdubbing,
        Stopped,
        Armed  // a quantized edge is pending; the deck fires it on the grid
    };

    // The verbs a deck understands. Not a surface: the console, a trig, or the
    // partner app's front panel all post the same handful.
    enum class DeckCmd
    {
        None,
        RecordCycle,  // arm / start / punch out / overdub / end overdub
        PlayStop,
        Clear,
        Undo,
        Halve,
        Double
    };

    // What a command asks the HOST to do, right now. The state machine owns the
    // decision; the host owns the medium, the pool slot, and the layer stack, so
    // it owns the doing. Nothing here allocates or touches audio.
    struct DeckEdge
    {
        bool startRecording = false;
        bool closeRecording = false;  // take ends, playback begins
        bool beginOverdub = false;    // snapshot for undo, open a fresh layer
        bool endOverdub = false;      // fold the layer at the next iteration
        bool restartPlayback = false; // rewind to the take's start and play
        bool clear = false;
        bool undo = false;
        bool halve = false;
        bool doubleLen = false;

        [[nodiscard]] bool any() const noexcept
        {
            return startRecording || closeRecording || beginOverdub || endOverdub
                || restartPlayback || clear || undo || halve || doubleLen;
        }
    };

    // A deck's per-sub-track state. How MANY sub-tracks is the host's call, not
    // the core's (§40.11) — Lockstep builds decks four wide and uses one by
    // default, a mixing host may build them twenty-four wide, and Record is a
    // capacity-1 deck. An unused sub-track costs nothing under the medium's lazy
    // commit, so capacity is cheap and depth stays opt-in.
    struct SubTrack
    {
        // Overdub/punch target the ARMED sub-tracks. Sub 0 is armed by default
        // (the Deck ctor), the rest disarmed — so a single-track looper never
        // thinks about arming and behaves exactly as before; the TRACKS console
        // arms the others (§40.3).
        bool armed = false;
        bool muted = false;
        bool soloed = false; // any solo mutes the un-soloed (a performance state)
        float level = 1.0f;
        float pan = 0.0f;
        int sourceTap = 0;   // index into the host's input_source enum (§27)
    };

    class Deck
    {
    public:
        // Capacity is fixed at construction — on the message thread, where a deck
        // face is built — so that NOTHING on the process path can allocate. The
        // host picks the width (§40.11): Lockstep's Loop and Tape construct at
        // kMaxInputSubTracks, Record takes the default and *is* the 1-track face.
        explicit Deck(int capacity = 1)
            : subs_(static_cast<std::size_t>(capacity < 1 ? 1 : capacity))
        {
            subs_[0].armed = true;  // sub 0 armed by default (§40.3)
        }

        [[nodiscard]] DeckState state() const noexcept { return state_; }
        void setState(DeckState s) noexcept { state_ = s; }

        // True while a quantized edge waits for the grid. Chrome reads this.
        [[nodiscard]] bool pendingEdge() const noexcept
        {
            return pendingAction_ != Pending::None || state_ == DeckState::Armed;
        }

        void cancelPending() noexcept { pendingAction_ = Pending::None; }

        [[nodiscard]] int subTrackCapacity() const noexcept
        {
            return static_cast<int>(subs_.size());
        }

        [[nodiscard]] int subTrackCount() const noexcept { return subTrackCount_; }

        // Called from process(): clamps into [1, capacity], never allocates.
        void setSubTrackCount(int n) noexcept
        {
            const int cap = subTrackCapacity();
            subTrackCount_ = n < 1 ? 1 : (n > cap ? cap : n);
        }

        // Clamped, not raw-indexed. Under a fixed array an out-of-range sub was
        // unreachable; with runtime capacity it is a live possibility (ask a
        // capacity-1 Record deck for sub 2), so the core defends its own bound.
        [[nodiscard]] SubTrack& subTrack(int i) noexcept
        {
            return subs_[static_cast<std::size_t>(clampSub(i))];
        }
        [[nodiscard]] const SubTrack& subTrack(int i) const noexcept
        {
            return subs_[static_cast<std::size_t>(clampSub(i))];
        }

        // Apply a verb. `immediate` is the §25 universal override (a double-tap):
        // it fires the edge now and cancels anything pending. `haveTake` says
        // whether the medium holds recorded content — a deck with nothing on it
        // cannot play, and cancelling an arm drops it to Idle rather than Stopped.
        DeckEdge applyCommand(DeckCmd c, bool immediate, const TransportSnapshot& t,
                              bool haveTake) noexcept;

        // Fire a pending quantized edge. The host calls this when the transport
        // phase crosses the launch grid.
        DeckEdge firePending() noexcept;

        // Whether live input passes through to the output, given the monitor mode,
        // whether the source is an insert (rather than a tap of something already
        // audible), and the deck's state.
        //
        // Auto is state-aware for an insert: monitor in every state EXCEPT while
        // the captured take plays back — the take replaced the live source. A tap
        // source is always take-only under Auto, because you can already hear it.
        // On/Off are absolute.
        enum class Monitor : int { Auto = 0, On = 1, Off = 2 };

        [[nodiscard]] static bool resolveMonitor(Monitor mode, bool sourceIsInsert,
                                                 DeckState state) noexcept
        {
            switch (mode)
            {
                case Monitor::On: return true;
                case Monitor::Off: return false;
                case Monitor::Auto:
                    if (! sourceIsInsert) return false;
                    return state != DeckState::Playing;
            }
            return false;
        }

    private:
        // What Armed is waiting to do. Mirrors the shipped looper's pendingAction_
        // integers one for one, so the re-seat cannot change a timing.
        enum class Pending : int
        {
            None = 0,
            StartRecording,   // Armed → Recording
            Stop,             // quantized stop
            Replay,           // quantized re-play from the take's start
            CloseRecording    // quantized punch-out → Playing
        };

        [[nodiscard]] int clampSub(int i) const noexcept
        {
            const int last = subTrackCapacity() - 1;
            return i < 0 ? 0 : (i > last ? last : i);
        }

        DeckState state_ = DeckState::Idle;
        Pending pendingAction_ = Pending::None;
        int subTrackCount_ = 1;
        std::vector<SubTrack> subs_;
    };
}
