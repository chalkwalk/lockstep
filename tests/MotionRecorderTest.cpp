// MotionRecorderTest -- live P-Lock (motion) recording, the pure half (9.27 A3).
//
// The recorder owns exactly one question: which (track, slot, step, value) locks
// should be written right now? It knows nothing about steps' contents — the
// processor's sink decides that a recorded motion also promotes an empty step to a
// trigless trig. Everything below drives an explicit clock, so the 150 ms gesture
// tail is tested rather than waited on.

#include "TestHarness.h"
#include "../src/core/MotionRecorder.h"
#include <vector>

namespace lockstep
{
    namespace
    {
        // Collect the locks a paint emits.
        struct Sink
        {
            std::vector<MotionRecorder::Lock> locks;
            void operator()(const MotionRecorder::Lock& l) { locks.push_back(l); }
        };
    }

    void runMotionRecorderTests()
    {
        // A window opens on the first write and paints the step under the playhead.
        {
            MotionRecorder mr;
            CHECK(!mr.isOpen(0, 5), "no window before any motion");

            mr.arm(0, 5, 0.75f, 1.000);
            CHECK(mr.isOpen(0, 5), "a write opens the slot's motion window");
            CHECK(mr.openCount(0) == 1 && mr.openCount(1) == 0,
                  "the window belongs to one track's slot only");

            Sink s;
            mr.paintDirty(0, 3, s);
            CHECK(s.locks.size() == 1, "the moved slot paints once");
            CHECK(s.locks[0].track == 0 && s.locks[0].slot == 5
                      && s.locks[0].step == 3 && feq(s.locks[0].value, 0.75f),
                  "the lock carries the live value onto the current step");

            // Nothing moved since: a dirty-only paint writes nothing.
            Sink s2;
            mr.paintDirty(0, 3, s2);
            CHECK(s2.locks.empty(), "an unmoved knob does not re-paint the same step");
        }

        // A knob held still keeps painting the steps the playhead crosses — that is
        // what records a whole loop from one gesture.
        {
            MotionRecorder mr;
            mr.arm(0, 2, 0.4f, 0.000);

            Sink s;
            for (int step = 0; step < 4; ++step)
                mr.paintStep(0, step, s);

            CHECK(s.locks.size() == 4, "every crossed step gets the value");
            for (int i = 0; i < 4; ++i)
                CHECK(s.locks[static_cast<std::size_t>(i)].step == i,
                      "steps are painted in the order they are crossed");
        }

        // The last value seen while on a step is the one that step keeps.
        {
            MotionRecorder mr;
            Sink s;

            mr.arm(0, 1, 0.2f, 0.000);
            mr.paintDirty(0, 7, s);     // step 7 gets 0.2
            mr.arm(0, 1, 0.9f, 0.010);
            mr.paintDirty(0, 7, s);     // ... then 0.9, same step
            CHECK(s.locks.size() == 2 && s.locks[1].step == 7
                      && feq(s.locks[1].value, 0.9f),
                  "a later value overwrites the same step");
        }

        // The window closes windowSeconds after the last motion, and painting stops.
        {
            MotionRecorder mr;
            mr.setWindowSeconds(0.15);
            mr.arm(0, 0, 1.0f, 10.000);

            mr.closeExpired(0, 10.100);
            CHECK(mr.isOpen(0, 0), "the window survives inside the gesture tail");

            mr.closeExpired(0, 10.200);
            CHECK(!mr.isOpen(0, 0), "the window closes once the gesture goes quiet");

            Sink s;
            mr.paintStep(0, 1, s);
            CHECK(s.locks.empty(), "a closed window paints nothing");
        }

        // Every touched slot records independently; an untouched one never does.
        {
            MotionRecorder mr;
            mr.arm(2, 4, 0.1f, 0.0);
            mr.arm(2, 9, 0.2f, 0.0);

            Sink s;
            mr.paintStep(2, 0, s);
            CHECK(s.locks.size() == 2, "two open windows paint two locks");
            CHECK(s.locks[0].slot == 4 && s.locks[1].slot == 9,
                  "locks are emitted in slot order");
            CHECK(mr.openCount(2) == 2, "openCount reports both");
        }

        // Out-of-range coordinates are inert, never UB.
        {
            MotionRecorder mr;
            mr.arm(-1, 0, 1.0f, 0.0);
            mr.arm(0, MotionRecorder::kMaxSlots, 1.0f, 0.0);
            mr.arm(kNumTracks, 0, 1.0f, 0.0);
            CHECK(mr.openCount(0) == 0, "out-of-range writes open nothing");

            Sink s;
            mr.arm(0, 0, 1.0f, 0.0);
            mr.paintStep(0, -1, s);
            CHECK(s.locks.empty(), "a negative step paints nothing");
        }

        // reset / resetTrack close windows without emitting.
        {
            MotionRecorder mr;
            mr.arm(0, 1, 0.5f, 0.0);
            mr.arm(1, 1, 0.5f, 0.0);
            mr.resetTrack(0);
            CHECK(!mr.isOpen(0, 1) && mr.isOpen(1, 1), "resetTrack closes one track");
            mr.reset();
            CHECK(!mr.isOpen(1, 1), "reset closes them all");
        }
    }
}
