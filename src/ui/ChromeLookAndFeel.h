#pragma once

// The chrome look: one owner for every plain button and combo box on the surface.
//
// The three button groups at the top of the window had grown three different looks,
// because each was styled where it was built: the file bar hard-coded its own fill
// and text colour, the rail buttons and combo boxes inherited raw JUCE defaults, and
// the transport asked the DEFAULT LookAndFeel for its resting colours (so it could
// not inherit anything). Same widget, three answers -- and three different font
// sizes, which is why "Click", "Save As...", "SND..." and "Pool..." were all
// rendering as ellipses.
//
// This is the PRINCIPLES §20 single-owner rule applied to chrome: a control's look
// is not a property of the file it happens to be constructed in.
//
// It is a BASE, not a straitjacket. Every draw reads the button's own colour ids, so
// a component that means something by its colour still wins:
//   - the track cells set buttonColourId transparent (the VU meter shows through);
//   - the transport sets buttonColourId when playing / armed / clicking;
//   - a toggle in the on state gets buttonOnColourId.
// What the base fixes is everything nobody had an opinion about: geometry, font,
// border, hover -- the parts that were different only by accident.

#include "UITheme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace lockstep
{
    class ChromeLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        // One font size for every chrome control. The old sizes ran from ~11 px (file
        // bar) to ~17 px (transport) with no reason behind the spread, and the big end
        // was what truncated the labels.
        static constexpr float kFontH = 12.5f;
        static constexpr float kCorner = 3.5f;

        // One height for every chrome control, whichever band it sits in. The bands
        // differ in importance (the transport is hot, the project rail is cold) and
        // they say so with POSITION and spacing -- not by growing the buttons, which
        // just made the same widget look like two different widgets.
        static constexpr int kControlH = 22;

        // A control-height row, vertically centred in its band, inset horizontally.
        [[nodiscard]] static juce::Rectangle<int> cell(juce::Rectangle<int> area,
                                                       int inset = 3) noexcept
        {
            return area.reduced(inset, 0).withSizeKeepingCentre(
                juce::jmax(0, area.getWidth() - 2 * inset),
                juce::jmin(kControlH, area.getHeight()));
        }

        static constexpr juce::uint32 kFill    = 0xFF2A2F38u;  // resting body
        static constexpr juce::uint32 kBorder  = 0xFF3C4553u;
        static constexpr juce::uint32 kText    = 0xFFBFCCDAu;
        static constexpr juce::uint32 kTextDim = 0xFF7C8896u;  // disabled

        ChromeLookAndFeel()
        {
            setColour(juce::TextButton::buttonColourId,   juce::Colour(kFill));
            setColour(juce::TextButton::buttonOnColourId, juce::Colour(kFill).brighter(0.45f));
            setColour(juce::TextButton::textColourOffId,  juce::Colour(kText));
            setColour(juce::TextButton::textColourOnId,   juce::Colours::white);

            setColour(juce::ComboBox::backgroundColourId, juce::Colour(kFill));
            setColour(juce::ComboBox::outlineColourId,    juce::Colour(kBorder));
            setColour(juce::ComboBox::textColourId,       juce::Colour(kText));
            setColour(juce::ComboBox::arrowColourId,      juce::Colour(kText).withAlpha(0.7f));
            setColour(juce::ComboBox::buttonColourId,     juce::Colour(kFill));

            setColour(juce::PopupMenu::backgroundColourId,     juce::Colour(0xFF1E222A));
            setColour(juce::PopupMenu::textColourId,           juce::Colour(kText));
            setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(kFill).brighter(0.35f));
            setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        }

        // The product typeface. Every Font in src/ is the unnamed default sans, so this
        // one hook decides what the WHOLE surface renders with -- not just the chrome
        // this class is otherwise about.
        //
        // It has to live on a LookAndFeel because that is the only seam JUCE offers: a
        // typeface-less Font resolves through LookAndFeel::getDefaultLookAndFeel(), NOT
        // through the component's look-and-feel (juce_Font.cpp:110). Hence
        // installProductLookAndFeel() below -- the editor's own chromeLnf_ member can
        // never serve, whatever it is set on.
        juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

        juce::Font getTextButtonFont(juce::TextButton&, int /*buttonHeight*/) override
        {
            return juce::Font(juce::FontOptions(kFontH));
        }

        juce::Font getComboBoxFont(juce::ComboBox&) override
        {
            return juce::Font(juce::FontOptions(kFontH));
        }

        juce::Font getPopupMenuFont() override
        {
            return juce::Font(juce::FontOptions(kFontH));
        }

        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                  const juce::Colour& /*backgroundColour*/,
                                  bool shouldDrawButtonAsHighlighted,
                                  bool shouldDrawButtonAsDown) override
        {
            // Read the BUTTON's colour, not the one JUCE resolved for us: that is what
            // keeps a semantic colour (armed red, playing green, a transparent track
            // cell) winning over the chrome base.
            const auto id = button.getToggleState() ? juce::TextButton::buttonOnColourId
                                                    : juce::TextButton::buttonColourId;
            auto fill = button.findColour(id);

            if (shouldDrawButtonAsDown)             fill = fill.brighter(0.35f);
            else if (shouldDrawButtonAsHighlighted) fill = fill.brighter(0.15f);

            const auto r = button.getLocalBounds().toFloat().reduced(0.5f);
            g.setColour(fill);
            g.fillRoundedRectangle(r, kCorner);

            // A transparent body means the button is a window onto something behind it
            // (the per-track VU). Drawing a border there would frame the meter.
            if (fill.getAlpha() > 0)
            {
                g.setColour(juce::Colour(kBorder));
                g.drawRoundedRectangle(r, kCorner, 1.0f);
            }
        }

        void drawComboBox(juce::Graphics& g, int width, int height, bool /*isDown*/,
                          int, int, int, int, juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float>(0.0f, 0.0f,
                                                  static_cast<float>(width),
                                                  static_cast<float>(height)).reduced(0.5f);
            g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
            g.fillRoundedRectangle(r, kCorner);
            g.setColour(box.findColour(juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle(r, kCorner, 1.0f);

            // A chevron, sized off the box rather than the JUCE default's fixed arrow --
            // the default is what made the two rail combo boxes read as a heavier
            // control than the buttons beside them.
            const float cx = static_cast<float>(width) - 12.0f;
            const float cy = static_cast<float>(height) * 0.5f;
            juce::Path chevron;
            chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
            chevron.lineTo(cx, cy + 2.5f);
            chevron.lineTo(cx + 4.0f, cy - 2.0f);
            g.setColour(box.findColour(juce::ComboBox::arrowColourId));
            g.strokePath(chevron, juce::PathStrokeType(1.4f));
        }

        // Keep the text clear of the chevron, and left-aligned like the buttons.
        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds(6, 0, box.getWidth() - 22, box.getHeight());
            label.setFont(getComboBoxFont(box));
            label.setJustificationType(juce::Justification::centredLeft);
        }
    };

    // Install the product look (and with it the embedded typeface) as JUCE's default.
    // Idempotent; call it from the editor's constructor. See ChromeLookAndFeel.cpp for
    // why the instance is a process-lifetime one and not the editor's member.
    void installProductLookAndFeel();
}
