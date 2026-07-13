#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "InspectorModel.h"

namespace lockstep
{
    // =========================================================================
    // InspectorBar — slim always-on 4-region context strip (DESIGN §19 / 9.11)
    //
    // Renders InspectorModel.{key, held, overlay, edit} as four equal-width
    // captioned columns. Paint is pure (no timer/animation): caller drives
    // updates by calling setModel(InspectorModel) + repaint() whenever state
    // changes. Follows the existing UITheme colour vocabulary.
    // =========================================================================

    class InspectorBar : public juce::Component
    {
    public:
        InspectorBar() = default;

        // Row 1 = the four captioned regions. Row 2 = the STATUS lane (9.30 st.1).
        static constexpr int kRegionRowH = 26;
        static constexpr int kStatusLaneH = 16;
        static constexpr int kHeight = kRegionRowH + kStatusLaneH;

        void setModel(const InspectorModel& m)
        {
            model_ = m;
            repaint();
        }
        [[nodiscard]] const InspectorModel& model() const noexcept { return model_; }

        void paint(juce::Graphics& g) override;

        // The confirm pop-over (§42.3). STATIC and pure, because it is painted by the
        // EDITOR, not by this component: it extends DOWNWARD out of the lane and over
        // the top of the MZ, and a child cannot paint outside its own bounds. Keeping
        // the renderer here keeps the lane and its pop-over one thing.
        //
        // Occlusion is safe by contract, not by luck: while a confirm is pending every
        // key either confirms or cancels it, so nothing underneath is a live target.
        static void paintConfirmPopover(juce::Graphics& g,
                                        juce::Rectangle<int> area,
                                        const InspectorModel& m);

    private:
        InspectorModel model_;

        static void paintColumn(juce::Graphics& g,
                                juce::Rectangle<int> area,
                                const char* caption,
                                const juce::String& text);
        static void paintStatusLane(juce::Graphics& g,
                                    juce::Rectangle<int> area,
                                    const InspectorModel& m);
    };

} // namespace lockstep
