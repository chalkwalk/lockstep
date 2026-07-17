#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TimelineModel.h"

namespace lockstep
{
    // TimelineStrip — the display-only tape timeline (DESIGN §40.6).
    //
    // Renders a TimelineModel as one horizontal reel: the recorded extent as a
    // filled bar, the playhead as a bright cursor, markers as ticks, and the
    // bars.beats position as a caption. Pure paint — no timer; the editor calls
    // setModel + repaint when state changes. You never click it (fence #5); it is
    // chrome, and it hides entirely when there is no tape.
    class TimelineStrip : public juce::Component
    {
    public:
        TimelineStrip() = default;

        void setModel(const TimelineModel& m)
        {
            model_ = m;
            repaint();
        }

        [[nodiscard]] bool wantsRow() const noexcept { return model_.active; }

        // The animation phase clock, pushed from the editor's ONE clock (§20; see
        // LockstepEditor::nowMs). It drives the recording cursor's pulse and nothing
        // else -- but reading the wall clock at paint time meant two paints of the
        // same state produced different pixels, which no rendered-frame comparison can
        // survive. Resting at 0 is a valid phase.
        void setAnimClockMs(double ms) noexcept { animClockMs_ = ms; }

        void paint(juce::Graphics& g) override;

    private:
        TimelineModel model_;
        double animClockMs_ = 0.0;
    };
}
