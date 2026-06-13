#include "MetaRotary.h"
#include <cmath>

namespace lockstep
{
    void MetaRotaryLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                                 int x, int y, int width, int height,
                                                 float sliderPos,
                                                 float rotaryStartAngle,
                                                 float rotaryEndAngle,
                                                 juce::Slider& slider)
    {
        auto* mr = dynamic_cast<MetaRotary*>(&slider);

        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        const float radius = (juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f) - 2.0f;
        const float centreX = bounds.getCentreX();
        const float centreY = bounds.getCentreY();
        const float trackW = juce::jmax(2.0f, radius * 0.12f);

        const float valueAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float centreAngle = (rotaryStartAngle + rotaryEndAngle) * 0.5f;

        const RingMode mode = mr ? mr->ringMode : RingMode::UnipolarFill;

        // --- Background track ---
        {
            juce::Path bg;
            bg.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                             rotaryStartAngle, rotaryEndAngle, true);
            g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId)
                            .withAlpha(0.35f));
            g.strokePath(bg, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
        }

        // --- Value fill ---
        if (mode == RingMode::Dot)
        {
            // No arc fill — indicator only.
        }
        else if (mode == RingMode::BipolarFromCentre)
        {
            const float fromAngle = centreAngle;
            const float toAngle = valueAngle;
            if (std::abs(toAngle - fromAngle) > 0.005f)
            {
                juce::Path fill;
                fill.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                   juce::jmin(fromAngle, toAngle),
                                   juce::jmax(fromAngle, toAngle), true);
                g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
                g.strokePath(fill, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
        }
        else  // UnipolarFill
        {
            if (sliderPos > 0.001f)
            {
                juce::Path fill;
                fill.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                   rotaryStartAngle, valueAngle, true);
                g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
                g.strokePath(fill, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
        }

        // --- Reference ticks (marks[0] drawn first, then marks[1] on top) ---
        if (mr)
        {
            const float tickInner = radius - trackW * 0.5f;
            const float tickOuter = radius + trackW * 1.5f;
            const float halfPi = juce::MathConstants<float>::halfPi;

            for (const auto& mark : mr->marks)
            {
                if (!mark.present) continue;

                const float markAngle = rotaryStartAngle + mark.position * (rotaryEndAngle - rotaryStartAngle);
                const float cosA = std::cos(markAngle - halfPi);
                const float sinA = std::sin(markAngle - halfPi);

                const float x1 = centreX + tickInner * cosA;
                const float y1 = centreY + tickInner * sinA;
                const float x2 = centreX + tickOuter * cosA;
                const float y2 = centreY + tickOuter * sinA;

                const auto argb = juce::Colour(mark.colour).withAlpha(mark.alpha);
                g.setColour(argb);
                g.drawLine(x1, y1, x2, y2, trackW);
            }
        }

        // --- Density cell overlay (DESIGN §39) ---
        // Drawn before the indicator dot so the dot sits on top.
        if (mr && mr->densityCell)
        {
            const float halfPi = juce::MathConstants<float>::halfPi;
            const float master = mr->densityMasterOffset;  // [-1, 1]
            const float perTrack = juce::jlimit(0.0f, 1.0f, sliderPos);

            // Arc spans from pointer angle toward the master-offset direction.
            // masterEnd is the raw (unclamped) effective position.
            const float masterEndRaw = perTrack + master;
            const float masterEndClamped = juce::jlimit(0.0f, 1.0f, masterEndRaw);
            const float masterEndAngle = rotaryStartAngle
                + masterEndClamped * (rotaryEndAngle - rotaryStartAngle);

            // Draw the master-offset arc (dimmed fill colour).
            if (std::abs(master) > 0.005f)
            {
                const float arcFrom = juce::jmin(valueAngle, masterEndAngle);
                const float arcTo   = juce::jmax(valueAngle, masterEndAngle);
                if (arcTo - arcFrom > 0.005f)
                {
                    juce::Path arcPath;
                    arcPath.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                         arcFrom, arcTo, true);
                    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                                    .withAlpha(0.55f));
                    g.strokePath(arcPath, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
                }

                // Overshoot zone: if masterEndRaw exceeds [0, 1], draw a dimmed
                // continuation arc so "turning but pinned" is visible.
                if (masterEndRaw > 1.0f)
                {
                    juce::Path overshoot;
                    overshoot.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                            rotaryEndAngle,
                                            rotaryEndAngle + (masterEndRaw - 1.0f)
                                                * (rotaryEndAngle - rotaryStartAngle) * 0.2f,
                                            true);
                    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                                    .withAlpha(0.2f));
                    g.strokePath(overshoot, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                                  juce::PathStrokeType::rounded));
                }
                else if (masterEndRaw < 0.0f)
                {
                    juce::Path overshoot;
                    overshoot.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                                            rotaryStartAngle + masterEndRaw * (rotaryEndAngle - rotaryStartAngle) * 0.2f,
                                            rotaryStartAngle,
                                            true);
                    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                                    .withAlpha(0.2f));
                    g.strokePath(overshoot, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                                  juce::PathStrokeType::rounded));
                }
            }

            // Tick at the effective (clamped) position — the audible value.
            {
                const float effectiveAngle = rotaryStartAngle
                    + mr->densityEffective * (rotaryEndAngle - rotaryStartAngle);
                const float tickInner = radius - trackW * 1.2f;
                const float tickOuter = radius + trackW * 1.2f;
                const float cosA = std::cos(effectiveAngle - halfPi);
                const float sinA = std::sin(effectiveAngle - halfPi);
                g.setColour(slider.findColour(juce::Slider::thumbColourId).withAlpha(0.6f));
                g.drawLine(centreX + tickInner * cosA, centreY + tickInner * sinA,
                           centreX + tickOuter * cosA, centreY + tickOuter * sinA, trackW);
            }
        }

        // --- Indicator dot ---
        {
            const float halfPi = juce::MathConstants<float>::halfPi;
            const float dotRadius = juce::jmax(2.5f, trackW * 0.8f);
            const float dotX = centreX + radius * std::cos(valueAngle - halfPi);
            const float dotY = centreY + radius * std::sin(valueAngle - halfPi);
            g.setColour(slider.findColour(juce::Slider::thumbColourId)
                            .withAlpha(slider.isEnabled() ? 1.0f : 0.4f));
            g.fillEllipse(dotX - dotRadius, dotY - dotRadius,
                          dotRadius * 2.0f, dotRadius * 2.0f);
        }
    }
}
