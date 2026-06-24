#pragma once

#include <functional>
#include <juce_events/juce_events.h>

namespace lockstep
{
    // SurfaceDispatcher — the single channel for *when* the surface redraws
    // (PRINCIPLES §22, DESIGN §35.9). §35.8 unified *what* is drawn (one
    // buildSurfaceModel, rendered to every sink); this unifies the trigger for
    // it.
    //
    // Discrete state changes (a step toggles, a scope is held, a CC moves a
    // parameter, the playhead crosses into a new step) all call invalidate().
    // JUCE's AsyncUpdater coalesces any number of invalidate() calls within one
    // message-loop cycle into exactly one handleAsyncUpdate(), which is the one
    // place a frame is produced — the onFrame callback (build the model once,
    // render screen + controllers from it). No component schedules its own
    // repaint() for shared surface state; everything feeds this one channel.
    //
    // Pure and testable: flushIfPending() exposes JUCE's synchronous delivery so
    // the coalescing contract (N invalidate ⇒ 1 frame) can be asserted headless,
    // without spinning a message loop.
    class SurfaceDispatcher : private juce::AsyncUpdater
    {
    public:
        explicit SurfaceDispatcher(std::function<void()> onFrame)
            : onFrame_(std::move(onFrame))
        {
        }

        ~SurfaceDispatcher() override { cancelPendingUpdate(); }

        // Mark the surface dirty. Cheap and idempotent within a cycle — call it
        // freely from every discrete mutation site; the coalescing makes
        // redundant calls free.
        void invalidate() { triggerAsyncUpdate(); }

        // Synchronously deliver a pending frame if one is queued, else no-op.
        // Used by tests and by any seam that must produce a frame before reading
        // surface-derived state. Message-thread only.
        void flushIfPending() { handleUpdateNowIfNeeded(); }

        // True iff an invalidate() is queued but not yet delivered.
        [[nodiscard]] bool isPending() const { return isUpdatePending(); }

    private:
        void handleAsyncUpdate() override
        {
            if (onFrame_)
            {
                onFrame_();
            }
        }

        std::function<void()> onFrame_;
    };
}
