// ClockLocateTest — the transport gains an absolute position (11.1, PRINCIPLES §25.1).
//
// Position is the second fact the transport authority carries, beside rate and
// grid. Locate moves it, and the move must be visible to the processor as a jump
// (ppqJumped) so every track re-derives its phase as `position mod length`.
// Nothing is recalled: state does not travel with position, which is the fence-#1
// line and the reason a locate never reproduces "what the song sounded like
// there" — only what the tape sounded like there.
//
// Standalone (no playhead) is the case that gains the locate. Hosted and playing,
// the host owns position outright and the request is dropped: winding the tape is
// dragging the host's playhead.

#include "TestHarness.h"
#include "../src/core/Clock.h"

namespace lockstep
{
    namespace
    {
        // A minimal playhead that reports a fixed position, playing or stopped.
        class StubPlayHead : public juce::AudioPlayHead
        {
        public:
            StubPlayHead(double ppq, bool playing) : ppq_(ppq), playing_(playing) {}

            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p;
                p.setIsPlaying(playing_);
                p.setBpm(120.0);
                p.setPpqPosition(ppq_);
                return p;
            }

        private:
            double ppq_ = 0.0;
            bool playing_ = false;
        };
    }

    void runClockLocateTests()
    {
        constexpr int kBlock = 512;

        // ── Standalone: a locate lands, and reads as a jump ──────────────────
        {
            Clock c;
            c.prepare(44100.0);
            c.setLocalBpm(120.0);
            c.setInPluginPlaying(true);

            c.update(nullptr, kBlock);
            c.update(nullptr, kBlock);
            const double rolled = c.ppqAtBlockStart();
            CHECK(rolled > 0.0, "the standalone clock advances while playing");
            CHECK(! c.ppqJumped(), "and rolling forward is not a jump");

            c.locate(16.0);
            CHECK(c.locatePending(), "the request latches until a block consumes it");

            c.update(nullptr, kBlock);
            CHECK(feq(static_cast<float>(c.ppqAtBlockStart()), 16.0f),
                  "the block starts at the located position");
            CHECK(c.ppqJumped(), "and the processor is told to re-derive every phase");
            CHECK(! c.locatePending(), "the request is consumed exactly once");

            // The next block continues from there rather than re-locating.
            c.update(nullptr, kBlock);
            CHECK(c.ppqAtBlockStart() > 16.0 && ! c.ppqJumped(),
                  "playback resumes forward from the new position");
        }

        // ── Locating backwards is a jump too ─────────────────────────────────
        // The pre-existing backward-jump detector would have caught this one; the
        // forward case above is the one that needed the latch.
        {
            Clock c;
            c.prepare(44100.0);
            c.setInPluginPlaying(true);
            c.update(nullptr, kBlock);
            c.locate(0.0);
            c.update(nullptr, kBlock);
            CHECK(feq(static_cast<float>(c.ppqAtBlockStart()), 0.0f) && c.ppqJumped(),
                  "a locate to zero rewinds and jumps");
        }

        // ── A locate while parked is honoured ────────────────────────────────
        // The transport is not running; the position still moves. That is how you
        // cue a tape before rolling.
        {
            Clock c;
            c.prepare(44100.0);
            c.setInPluginPlaying(false);
            c.locate(8.0);
            c.update(nullptr, kBlock);
            CHECK(feq(static_cast<float>(c.ppqAtBlockStart()), 8.0f), "a parked deck cues");
            c.update(nullptr, kBlock);
            CHECK(feq(static_cast<float>(c.ppqAtBlockStart()), 8.0f), "and stays there");
        }

        // ── Hosted and playing: the host owns position ───────────────────────
        {
            Clock c;
            c.prepare(44100.0);
            StubPlayHead host{ 32.0, /*playing*/ true };

            c.locate(4.0);
            c.update(&host, kBlock);
            CHECK(feq(static_cast<float>(c.ppqAtBlockStart()), 32.0f),
                  "a hosted, playing transport ignores our locate — the DAW is the root");
            CHECK(! c.locatePending(), "and the request is dropped, not queued");
        }

        // ── Reset outranks a stale locate ────────────────────────────────────
        {
            Clock c;
            c.prepare(44100.0);
            c.setInPluginPlaying(true);
            c.locate(64.0);
            c.resetPhase();
            c.update(nullptr, kBlock);
            CHECK(feq(static_cast<float>(c.ppqAtBlockStart()), 0.0f),
                  "resetPhase discards a locate that never fired");
        }
    }
}
