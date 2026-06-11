#pragma once

#include <vector>
#include <algorithm>
#include <set>

namespace lockstep
{
    // Tracks which step(s) are currently held such that incoming parameter
    // writes land in Step Overrides (P-Locks) rather than the Track Base.
    //
    // Multi-step holds are maintained as an ordered list (press order).
    // The "primary" held step is the first one pressed.  All held steps
    // share the same track index (cross-track simultaneous holds are not
    // supported; starting a hold on a different track releases previous holds).
    //
    // MHZ.9.1: latched steps are virtual-holds that persist even after the
    // physical key is released. They remain in heldSteps_ until explicitly
    // cleared (escape gesture or explicit clearLatched/clearAllLatched call).
    class EditContext
    {
    public:
        // Returns true if at least one step is currently held.
        [[nodiscard]] bool isActiveForEditing() const { return !heldSteps_.empty(); }

        // Primary held step (first pressed), or -1 if none held.
        [[nodiscard]] int heldStepIndex() const
        {
            return heldSteps_.empty() ? -1 : heldSteps_.front();
        }

        [[nodiscard]] int heldTrackIndex() const { return heldTrack_; }
        [[nodiscard]] int activeSlot() const { return activeSlot_; }

        // Full ordered list of held step indices (press order).
        [[nodiscard]] const std::vector<int>& heldSteps() const { return heldSteps_; }

        void setActiveSlot(int slot) { activeSlot_ = slot; }

        // Hold a step on a track.  If trackIndex differs from the currently
        // held track, all existing holds are released first.
        void hold(int trackIndex, int stepIndex)
        {
            if (heldTrack_ != trackIndex)
            {
                heldSteps_.clear();
                paramWritten_ = false;
            }
            heldTrack_ = trackIndex;
            // Ignore duplicate presses (key-repeat).
            if (std::find(heldSteps_.begin(), heldSteps_.end(), stepIndex) == heldSteps_.end())
                heldSteps_.push_back(stepIndex);
        }

        // Release a specific step.  Param-written flag is cleared when the
        // last step is released.
        void release(int stepIndex)
        {
            heldSteps_.erase(
                std::remove(heldSteps_.begin(), heldSteps_.end(), stepIndex),
                heldSteps_.end());
            if (heldSteps_.empty())
            {
                heldTrack_ = -1;
                paramWritten_ = false;
            }
        }

        // Release all held steps unconditionally (e.g. focus change, escape).
        // Also clears the latch set so latched steps are dropped too.
        void release()
        {
            heldSteps_.clear();
            latchedSteps_.clear();
            heldTrack_ = -1;
            paramWritten_ = false;
        }

        // Called by the parameter-write path when a P-Lock is applied.
        void markParamWritten() { paramWritten_ = true; }
        [[nodiscard]] bool wasParamWritten() const { return paramWritten_; }

        // ---- MHZ.9.1: latched step API ----

        [[nodiscard]] bool isLatched(int stepIndex) const
        {
            return latchedSteps_.count(stepIndex) > 0;
        }

        void setLatched(int stepIndex)
        {
            latchedSteps_.insert(stepIndex);
        }

        void clearLatched(int stepIndex)
        {
            latchedSteps_.erase(stepIndex);
        }

        void clearAllLatched()
        {
            latchedSteps_.clear();
        }

        [[nodiscard]] const std::set<int>& latchedSteps() const { return latchedSteps_; }

        [[nodiscard]] bool hasAnyLatchedStep() const { return !latchedSteps_.empty(); }

    private:
        std::vector<int> heldSteps_;      // ordered by press time
        std::set<int> latchedSteps_;   // MHZ.9.1: subset of heldSteps_ that are virtual-held
        int heldTrack_ = -1;
        bool paramWritten_ = false;
        int activeSlot_ = -1;
    };
}
