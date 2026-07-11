#include "TimelineStrip.h"

namespace lockstep
{
    void TimelineStrip::paint(juce::Graphics& g)
    {
        if (! model_.active) return;  // no tape → the strip draws nothing

        auto area = getLocalBounds();

        // A slim caption on the left ("bars.beats"), the reel filling the rest.
        auto captionArea = area.removeFromLeft(64);
        g.setColour(juce::Colour(0xFF0E1014u));
        g.fillRect(getLocalBounds());

        g.setColour(juce::Colour(model_.recording ? 0xFFE05050u : 0xFF9AA4B0u));
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        g.drawText(model_.position, captionArea.reduced(6, 0),
                   juce::Justification::centredLeft, false);

        auto reel = area.reduced(4, 5);
        const int x0 = reel.getX();
        const int w = reel.getWidth();
        const auto atX = [&](float f) { return x0 + juce::roundToInt(f * static_cast<float>(w)); };

        // The reel track.
        g.setColour(juce::Colour(0xFF1A1E24u));
        g.fillRoundedRectangle(reel.toFloat(), 3.0f);

        // Recorded extent — a filled bar reddening in the last 10% (medium-full).
        if (model_.recordedExtent01 > 0.0f)
        {
            auto filled = reel.withWidth(atX(model_.recordedExtent01) - x0);
            const bool nearFull = model_.mediumFull01 > 0.9f;
            g.setColour(juce::Colour(nearFull ? 0xFF7A2A2Au : 0xFF244A44u));
            g.fillRoundedRectangle(filled.toFloat(), 3.0f);
        }

        // Markers — thin ticks across the full height.
        g.setColour(juce::Colour(0xFF4A78C0u));
        for (const auto& m : model_.markers)
        {
            const int mx = atX(m.pos01);
            g.fillRect(mx, reel.getY(), 1, reel.getHeight());
        }

        // Playhead cursor — bright, hot while recording.
        const int cx = atX(model_.cursor01);
        g.setColour(juce::Colour(model_.recording ? 0xFFFF5050u : 0xFFE8E8E8u));
        g.fillRect(cx - 1, reel.getY() - 1, 2, reel.getHeight() + 2);
    }
}
