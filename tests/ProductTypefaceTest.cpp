// ProductTypefaceTest -- the surface renders with the typeface the repo ships.
//
// WHY THIS IS A TEST AND NOT A GLANCE. Until now every Font in the product named no
// family, so each machine drew the surface with whatever its platform called the
// default sans. That is two problems wearing one hat: a look nobody chose (it differs
// per distro), and rendered frames that cannot be compared against a golden, because
// the glyphs are not the same glyphs twice. Embedding Inter fixes both -- but only if
// it actually RESOLVES, and a font that silently falls back looks like a font that
// works. The whole visual-verification layer is built on the assumption asserted here.
//
// The bold cut gets its own assertion for the same reason. `.boldened()` and
// withStyle("Bold") are all over the surface; had the hook returned one face for
// every style, bold text would have rendered regular -- legible, plausible, wrong,
// and invisible to a test that only checked "is it Inter".

#include "../src/ui/ChromeLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace lockstep
{
void runProductTypefaceTests(int& failed)
{
    auto check = [&failed](bool ok, const char* what) {
        if (!ok) { std::fprintf(stderr, "FAIL [ProductTypeface] %s\n", what); ++failed; }
    };

    installProductLookAndFeel();

    // A plain Font, built the way every paint site in src/ builds one: a size and
    // nothing else. Resolution runs through the default look-and-feel, so this is the
    // real path and not a direct call to the hook.
    const juce::Font plain { juce::FontOptions(10.0f) };
    const auto plainFace = plain.getTypefacePtr();
    check(plainFace != nullptr, "a default-sans Font resolves to a typeface at all");

    if (plainFace != nullptr)
        check(plainFace->getName() == "Inter",
              "an unnamed Font resolves to the embedded Inter, not the platform sans");

    // The bold cut must be the real one. Same family, different face.
    const auto boldFace = plain.boldened().getTypefacePtr();
    check(boldFace != nullptr, "a boldened Font resolves to a typeface");

    if (boldFace != nullptr && plainFace != nullptr)
    {
        check(boldFace->getName() == "Inter", "the bold cut is Inter too");
        check(boldFace->getStyle() == "Bold",
              "boldened() reaches the real Bold face (a synthesised/regular fallback "
              "would render every bold label on the surface as regular)");
        check(boldFace != plainFace, "bold and regular are distinct faces");
    }

    // withStyle("Bold") is the other bold idiom in use (InspectorBar). It must land on
    // the same face -- two spellings of one intent.
    const juce::Font styled { juce::FontOptions(10.0f).withStyle("Bold") };
    check(styled.getTypefacePtr() == boldFace,
          "withStyle(\"Bold\") and boldened() resolve to the same face");

    // Loading is cached, not re-parsed per Font: getTypefaceForFont runs for every font
    // the paint path builds inline, and re-reading the file there would be a per-frame
    // cost that only shows up on a slow machine.
    const juce::Font again { juce::FontOptions(22.0f) };
    check(again.getTypefacePtr() == plainFace,
          "the same face instance is reused across Fonts (parsed once)");
}
}   // namespace lockstep
