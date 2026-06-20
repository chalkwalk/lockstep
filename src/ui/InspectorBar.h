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

        void setModel(const InspectorModel& m)
        {
            model_ = m;
            repaint();
        }

        void paint(juce::Graphics& g) override;

    private:
        InspectorModel model_;

        static void paintColumn(juce::Graphics& g,
                                juce::Rectangle<int> area,
                                const char* caption,
                                const juce::String& text);
    };

} // namespace lockstep
