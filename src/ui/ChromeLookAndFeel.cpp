#include "ChromeLookAndFeel.h"

#include <LockstepFonts.h>

namespace lockstep
{
namespace
{
    // Parsed once and kept forever. createSystemTypefaceFor reads the whole font file,
    // and getTypefaceForFont is called for every Font the surface builds -- which, on a
    // paint path that constructs its fonts inline, is a lot of them per frame.
    juce::Typeface::Ptr interRegular()
    {
        static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
            LockstepFonts::InterRegular_ttf, LockstepFonts::InterRegular_ttfSize);
        return t;
    }

    // The real Bold cut, not a synthesised one. `.boldened()` / withStyle("Bold") are
    // all over the surface; resolving them to the regular face would silently render
    // every bold label as regular, which is precisely the sort of "looks fine, isn't"
    // that no test would have caught before this arc.
    juce::Typeface::Ptr interBold()
    {
        static const juce::Typeface::Ptr t = juce::Typeface::createSystemTypefaceFor(
            LockstepFonts::InterBold_ttf, LockstepFonts::InterBold_ttfSize);
        return t;
    }
}   // namespace

juce::Typeface::Ptr ChromeLookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    // Substitute only for the unnamed default sans. Nothing in the product names a
    // family today, so this guard changes no current behaviour -- it is here so that
    // the day someone asks for a monospace face they get one, instead of Inter.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        if (auto t = f.isBold() ? interBold() : interRegular())
            return t;

    return LookAndFeel_V4::getTypefaceForFont(f);
}

void installProductLookAndFeel()
{
    // Process lifetime, and destroyed at exit rather than leaked. The instance must
    // outlive every editor -- JUCE's default-look pointer is read long after a plugin
    // window closes, which is why the editor's own chromeLnf_ member can never serve --
    // but `new` with no delete trips JUCE's leak detector at teardown, and a shipped
    // Debug build that cries wolf about five objects is how a real leak gets ignored
    // later. Destruction is safe in either order: Desktop holds a WeakReference, so if
    // this dies first the pointer nulls rather than dangles.
    static ChromeLookAndFeel productLnf;

    if (&juce::LookAndFeel::getDefaultLookAndFeel() != &productLnf)
        juce::LookAndFeel::setDefaultLookAndFeel(&productLnf);
}
}   // namespace lockstep
