#include "TimelineStrip.h"

#include <cmath>

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

        // Markers — thin ticks across the full height. The next one ahead brightens
        // and widens as the playhead approaches it (§19 hardware proxy).
        const int cx = atX(model_.cursor01);
        for (const auto& m : model_.markers)
        {
            const int mx = atX(m.pos01);
            const bool isNext = mx > cx;  // the approach glow targets the one ahead
            const float glow = isNext ? model_.markerApproach : 0.0f;
            g.setColour(juce::Colour(0xFF4A78C0u).brighter(glow * 0.8f));
            g.fillRect(mx, reel.getY(), glow > 0.5f ? 2 : 1, reel.getHeight());
        }

        // Playhead cursor — bright, hot while recording. While recording it pulses
        // (a wall-clock sine; the strip repaints each tick as the cursor advances)
        // so the punch-armed state reads at a glance without an animated CellState.
        float pulse = 1.0f;
        if (model_.recording)
        {
            const double t = static_cast<double>(juce::Time::getMillisecondCounter()) * 0.006;
            pulse = 0.6f + 0.4f * static_cast<float>(0.5 * (1.0 + std::sin(t)));
        }
        auto cursorCol = juce::Colour(model_.recording ? 0xFFFF5050u : 0xFFE8E8E8u);
        g.setColour(cursorCol.withMultipliedBrightness(pulse));
        g.fillRect(cx - 1, reel.getY() - 1, 2, reel.getHeight() + 2);
    }
}
