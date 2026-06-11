#pragma once

#include <algorithm>
#include <vector>

namespace lockstep::dsp
{

// Tracks held-key state for mono-mode voice allocation and determines the
// correct envelope action on each note-on / note-off event.
    class MonoGate
    {
    public:
        enum class OnAction
        {
    // held_ was empty before this note-on → phrase is starting.
    // Caller should retrigger the envelope regardless of Legato/Retrig mode.
            FirstTrigger,

    // At least one other note was already held → phrase is continuing.
    // In Legato: slide pitch only. In Retrig: ghost-fade + hard-retrigger.
            OverlapTrigger,
        };

        enum class OffAction
        {
    // held_ is now empty → release all envelopes.
            Release,

    // Released note was the active pitch and other keys are still held.
    // Caller should slide to topHeldNote().
            SlideTo,

    // Released note was not the active pitch (background held key).
    // No audible change required.
            Ignore,
        };

        void reset() noexcept
        {
            held_.clear();
            active_ = -1;
        }

        OnAction noteOn(int note)
        {
            const bool wasEmpty = held_.empty();

    // Ignore duplicate note-ons (e.g. from MIDI loops).
            if (std::find(held_.begin(), held_.end(), note) == held_.end())
                held_.push_back(note);

            active_ = note;
            return wasEmpty ? OnAction::FirstTrigger : OnAction::OverlapTrigger;
        }

        OffAction noteOff(int note)
        {
            held_.erase(std::remove(held_.begin(), held_.end(), note), held_.end());

            if (held_.empty())
            {
                active_ = -1;
                return OffAction::Release;
            }

            if (note == active_)
            {
                active_ = held_.back();
                return OffAction::SlideTo;
            }

            return OffAction::Ignore;
        }

        [[nodiscard]] bool empty() const noexcept { return held_.empty(); }
        [[nodiscard]] int topHeldNote() const noexcept { return held_.empty() ? -1 : held_.back(); }
        [[nodiscard]] int activeNote() const noexcept { return active_; }

    private:
        std::vector<int> held_;
        int active_ = -1;
    };

} // namespace lockstep::dsp
