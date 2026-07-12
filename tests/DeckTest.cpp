// DeckTest — dc::Deck, the state machine behind Record / Loop / Tape (DESIGN §40.1).
//
// The three faces differ in the medium's topology and in what posts the verbs.
// They do NOT differ in arm, punch, overdub, undo, or the quantized edge, and
// this file is where that claim is stated: one table of transitions, ported
// one-for-one from the shipped looper so the re-seat cannot silently change a
// timing.
//
// The deck decides; the host does. A command returns a DeckEdge describing the
// work (start recording, fold the layer, restore the undo take) and the host —
// which owns the medium and the pool slot — performs it. That split is what lets
// this be a pure test with no plugin, no buffers, and no audio.

#include "TestHarness.h"
#include "../src/deckcore/Deck.h"

namespace lockstep
{
    namespace
    {
        dc::TransportSnapshot rolling(double quantPeriod)
        {
            dc::TransportSnapshot t;
            t.running = true;
            t.launchQuantPeriodSamples = quantPeriod;
            return t;
        }
    }

    void runDeckTests()
    {
        constexpr bool kTake = true;    // the medium holds a recorded take
        constexpr bool kEmpty = false;

        // ── Unquantized: verbs fire the instant they are pressed ─────────────
        {
            dc::Deck d;
            const auto t = rolling(0.0);  // Free / Instant grid

            auto e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK(e.startRecording && d.state() == dc::DeckState::Recording,
                  "record on an idle deck starts recording now");

            e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kTake);
            CHECK(e.closeRecording && d.state() == dc::DeckState::Playing,
                  "record again closes the take and plays it");

            e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kTake);
            CHECK(e.beginOverdub && d.state() == dc::DeckState::Overdubbing,
                  "record over a playing take opens an overdub layer");

            e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kTake);
            CHECK(e.endOverdub && d.state() == dc::DeckState::Playing,
                  "and again folds it down");

            e = d.applyCommand(dc::DeckCmd::PlayStop, false, t, kTake);
            CHECK(! e.any() && d.state() == dc::DeckState::Stopped, "play/stop stops");

            e = d.applyCommand(dc::DeckCmd::PlayStop, false, t, kTake);
            CHECK(e.restartPlayback && d.state() == dc::DeckState::Playing,
                  "and starts the take again from its head");
        }

        // ── A deck with nothing on it cannot play ────────────────────────────
        {
            dc::Deck d;
            const auto t = rolling(0.0);
            d.setState(dc::DeckState::Stopped);
            const auto e = d.applyCommand(dc::DeckCmd::PlayStop, false, t, kEmpty);
            CHECK(! e.restartPlayback && d.state() == dc::DeckState::Stopped,
                  "play on an empty stopped deck does nothing");
        }

        // ── Quantized: the edge waits for the grid ───────────────────────────
        {
            dc::Deck d;
            const auto t = rolling(22050.0);  // half a second of grid

            auto e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK(! e.any() && d.state() == dc::DeckState::Armed && d.pendingEdge(),
                  "record arms rather than starting");

            e = d.firePending();
            CHECK(e.startRecording && d.state() == dc::DeckState::Recording && ! d.pendingEdge(),
                  "the grid fires it");

            e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kTake);
            CHECK(! e.any() && d.state() == dc::DeckState::Recording && d.pendingEdge(),
                  "punch-out arms for the next boundary rather than cutting");

            e = d.firePending();
            CHECK(e.closeRecording && d.state() == dc::DeckState::Playing,
                  "and closes on the boundary");

            e = d.applyCommand(dc::DeckCmd::PlayStop, false, t, kTake);
            CHECK(! e.any() && d.state() == dc::DeckState::Playing && d.pendingEdge(),
                  "stop is quantized too — the take keeps playing until the bar");
            e = d.firePending();
            CHECK(d.state() == dc::DeckState::Stopped, "then stops");

            e = d.applyCommand(dc::DeckCmd::PlayStop, false, t, kTake);
            CHECK(! e.any() && d.pendingEdge(), "re-play arms");
            e = d.firePending();
            CHECK(e.restartPlayback && d.state() == dc::DeckState::Playing, "and fires on the bar");
        }

        // ── A stopped transport can still arm ────────────────────────────────
        // Arming while the transport is parked is how a take begins on the roll.
        // But nothing else quantizes against a grid that is not moving.
        {
            dc::Deck d;
            auto t = rolling(22050.0);
            t.running = false;

            auto e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK(d.state() == dc::DeckState::Armed, "record arms even with the transport stopped");

            d.setState(dc::DeckState::Playing);
            d.cancelPending();
            e = d.applyCommand(dc::DeckCmd::PlayStop, false, t, kTake);
            CHECK(d.state() == dc::DeckState::Stopped && ! d.pendingEdge(),
                  "but a stop does not wait for a grid that is not moving");
        }

        // ── The double-tap override (§25) ────────────────────────────────────
        {
            dc::Deck d;
            const auto t = rolling(22050.0);

            d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kEmpty);  // → Armed
            auto e = d.applyCommand(dc::DeckCmd::RecordCycle, true, t, kEmpty);
            CHECK(e.startRecording && d.state() == dc::DeckState::Recording && ! d.pendingEdge(),
                  "a double-tap on an armed deck starts it now");

            e = d.applyCommand(dc::DeckCmd::RecordCycle, true, t, kTake);
            CHECK(e.closeRecording && d.state() == dc::DeckState::Playing,
                  "and a double-tap punch-out cuts now, not on the bar");
        }

        // ── Cancelling an arm ────────────────────────────────────────────────
        // A single tap on an armed deck backs out. Where it lands depends on
        // whether there is a take to go back to.
        {
            dc::Deck d;
            const auto t = rolling(22050.0);

            d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kEmpty);
            auto e = d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kEmpty);
            CHECK(! e.any() && d.state() == dc::DeckState::Idle && ! d.pendingEdge(),
                  "cancelling an arm on an empty deck returns it to idle");

            d.setState(dc::DeckState::Stopped);
            d.applyCommand(dc::DeckCmd::RecordCycle, false, t, kTake);
            CHECK(d.state() == dc::DeckState::Armed, "arming over a take");
            e = d.applyCommand(dc::DeckCmd::PlayStop, false, t, kTake);
            CHECK(d.state() == dc::DeckState::Stopped && ! d.pendingEdge(),
                  "play/stop cancels the arm and leaves the take alone");
        }

        // ── Clear and undo ───────────────────────────────────────────────────
        {
            dc::Deck d;
            const auto t = rolling(22050.0);
            d.setState(dc::DeckState::Overdubbing);

            auto e = d.applyCommand(dc::DeckCmd::Undo, false, t, kTake);
            CHECK(e.undo && d.state() == dc::DeckState::Playing,
                  "undo drops the layer and leaves the take playing");

            e = d.applyCommand(dc::DeckCmd::Clear, false, t, kTake);
            CHECK(e.clear && d.state() == dc::DeckState::Idle && ! d.pendingEdge(),
                  "clear empties the deck and cancels any pending edge");

            e = d.applyCommand(dc::DeckCmd::Undo, false, t, kEmpty);
            CHECK(! e.any(), "there is nothing to undo on an empty deck");
        }

        // ── Halve / Double only make sense with a take, and not while recording ─
        {
            dc::Deck d;
            const auto t = rolling(0.0);
            d.setState(dc::DeckState::Recording);
            CHECK(! d.applyCommand(dc::DeckCmd::Halve, false, t, kTake).halve,
                  "the loop window is not resized mid-record");

            d.setState(dc::DeckState::Playing);
            CHECK(d.applyCommand(dc::DeckCmd::Halve, false, t, kTake).halve, "halve on a take");
            CHECK(d.applyCommand(dc::DeckCmd::Double, false, t, kTake).doubleLen, "double on a take");
            CHECK(! d.applyCommand(dc::DeckCmd::Double, false, t, kEmpty).doubleLen,
                  "and neither on an empty deck");
        }

        // ── Monitoring ───────────────────────────────────────────────────────
        // Auto is state-aware for an insert source: the take replaces the live
        // input once it plays back. A tap source is already audible, so Auto never
        // doubles it. On/Off are absolute.
        {
            using M = dc::Deck::Monitor;
            CHECK(dc::Deck::resolveMonitor(M::Auto, true, dc::DeckState::Recording),
                  "auto monitors an insert while recording");
            CHECK(! dc::Deck::resolveMonitor(M::Auto, true, dc::DeckState::Playing),
                  "and drops it once the take plays");
            CHECK(dc::Deck::resolveMonitor(M::Auto, true, dc::DeckState::Overdubbing),
                  "but restores it for an overdub");
            CHECK(! dc::Deck::resolveMonitor(M::Auto, false, dc::DeckState::Recording),
                  "auto never monitors a tap — you can already hear it");
            CHECK(dc::Deck::resolveMonitor(M::On, false, dc::DeckState::Playing),
                  "On is absolute");
            CHECK(! dc::Deck::resolveMonitor(M::Off, true, dc::DeckState::Recording),
                  "Off is absolute");
        }

        // ── Sub-tracks: four, defaulting to one ──────────────────────────────
        {
            // §40.11: the core declares no width. A deck is built at the capacity its
            // host asks for — Lockstep's Loop and Tape ask for four.
            dc::Deck d{ 4 };
            CHECK(d.subTrackCapacity() == 4, "a deck is built at the host's capacity");
            CHECK(d.subTrackCount() == 1, "and still defaults to one sub-track in use");
            CHECK(d.subTrack(0).armed, "which is armed");
            // §40.3: only sub 0 is armed by default — the rest are disarmed so a
            // multi-sub overdub targets sub 0 alone until the console arms others.
            CHECK(! d.subTrack(1).armed && ! d.subTrack(2).armed && ! d.subTrack(3).armed,
                  "sub-tracks 1-3 are disarmed by default");
            d.setSubTrackCount(9);
            CHECK(d.subTrackCount() == 4, "and never exceeds its capacity");
            d.setSubTrackCount(0);
            CHECK(d.subTrackCount() == 1, "nor drops below one");
        }

        // §40.11: capacity is the host's to choose, and Record's choice is ONE — the
        // linear 1-track face is a capacity-1 deck, not a 4-deck with three unused
        // subs. A wider host (the mixer variant) is the same code at a bigger number.
        {
            dc::Deck one;  // the default: Record's shape
            CHECK(one.subTrackCapacity() == 1, "a default deck is a 1-track deck");
            one.setSubTrackCount(4);
            CHECK(one.subTrackCount() == 1, "which cannot be widened from process()");
            // Out-of-range subs clamp rather than run off the end — under the old
            // fixed array this was unreachable; with runtime capacity it is not.
            CHECK(one.subTrack(3).armed, "an out-of-range sub clamps into the deck");

            dc::Deck wide{ 24 };
            CHECK(wide.subTrackCapacity() == 24, "and a mixer-width deck just works");
            wide.setSubTrackCount(24);
            CHECK(wide.subTrackCount() == 24, "all 24 usable");
            CHECK(! wide.subTrack(23).armed, "with only sub 0 armed by default");
        }
    }
}
