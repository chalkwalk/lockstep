// SurfaceRenderTest -- the editor renders, and renders the SAME, twice.
//
// This is the foundation the rest of Tier 3 stands on and it is worth stating why it
// is a test rather than an assumption. Everything visual downstream -- the region
// oracle, the scene goldens -- asks "does this frame match what it should be?". That
// question is only meaningful if the frame is a function of the state. The moment any
// paint reads something the test does not control (the wall clock was the real one;
// an uninitialised value would do just as well), the answer stops being reproducible:
// the suite goes green, then red, then green, and nobody can reproduce either. A
// flaky visual test is worse than none, because it teaches people to re-run the suite
// until it passes -- and by then it is not a test, it is a ritual.
//
// So: render twice, demand the bytes match, and do it at two scales in case the
// nondeterminism hides in the transform path.
//
// This test and ClockFunnelGuardTest split the job. The guard is what actually
// PREVENTS the failure -- it forbids a clock read in a painting component at build
// time, which is the only way to stop one being added back. This test proves the
// resulting property holds end to end for the whole editor, including whatever a
// source scan cannot see. Neither is redundant: a guard that never rendered anything
// would be enforcing a rule with no evidence it was the right rule.

#include "UiDriver.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::UiDriver;

    // Design canvas (PluginEditor kDesignW/H). At exactly this size uiScale is 1.0;
    // 1.2x is what the product actually ships at.
    constexpr int kDesignW = 990;
    constexpr int kDesignH = 626;

    // Byte-exact, with the first offender located: "the images differ" is not a
    // finding, it is the start of an afternoon.
    bool identical(const juce::Image& a, const juce::Image& b, juce::String& whereOut)
    {
        if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        {
            whereOut = "size " + juce::String(a.getWidth()) + "x" + juce::String(a.getHeight())
                     + " vs " + juce::String(b.getWidth()) + "x" + juce::String(b.getHeight());
            return false;
        }

        for (int y = 0; y < a.getHeight(); ++y)
        {
            for (int x = 0; x < a.getWidth(); ++x)
            {
                const auto pa = a.getPixelAt(x, y);
                const auto pb = b.getPixelAt(x, y);
                if (pa == pb)
                    continue;

                whereOut = "at (" + juce::String(x) + "," + juce::String(y) + ") "
                         + pa.toDisplayString(true) + " vs " + pb.toDisplayString(true);
                return false;
            }
        }
        return true;
    }

    void testRenderProducesAnImage(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [SurfaceRender/basic] %s\n", what); ++failed; }
        };

        UiDriver d;
        const auto img = d.renderAt(kDesignW, kDesignH);

        check(img.isValid(), "render() produces a valid image");
        check(img.getWidth() == kDesignW && img.getHeight() == kDesignH,
              "the image is the size of the editor");

        // A blank image would satisfy every assertion above and prove nothing. The
        // surface is dark chrome on a dark ground, so "not transparent" is the honest
        // floor: it says paint actually ran.
        int opaque = 0;
        for (int y = 0; y < img.getHeight(); y += 4)
            for (int x = 0; x < img.getWidth(); x += 4)
                if (img.getPixelAt(x, y).getAlpha() > 0)
                    ++opaque;

        check(opaque > 0, "something was actually painted (not a blank image)");
    }

    void testRenderIsDeterministic(int& failed)
    {
        auto check = [&failed](bool ok, const juce::String& what) {
            if (!ok)
            {
                std::fprintf(stderr, "FAIL [SurfaceRender/determinism] %s\n", what.toRawUTF8());
                ++failed;
            }
        };

        // Both scales: 1.0 renders through an identity-ish transform, 1.2 through the
        // real one, in case anything irreproducible hides in the transform path.
        for (const double scale : { 1.0, 1.2 })
        {
            UiDriver d;
            const int w = static_cast<int>(kDesignW * scale);
            const int h = static_cast<int>(kDesignH * scale);

            const auto first = d.renderAt(w, h);

            // Time passes; STATE does not. Deliberately no timerCallback() here: the
            // editor's 30 Hz tick decays the VU meters and ages the status toast, so
            // ticking it would be asking two genuinely different states to render the
            // same and the failure would be the test's fault. (It was, first time
            // round.) What is being asserted is narrower and is the exact property the
            // scene goldens need: the frame is a function of the state, and of nothing
            // else the test cannot see.
            d.advanceMs(5000.0);

            const auto second = d.render();

            juce::String where;
            check(identical(first, second, where),
                  "at " + juce::String(scale, 2) + "x: the same state renders to the same "
                  "pixels twice (" + where + ")");
        }
    }
}   // namespace

void runSurfaceRenderTests(int& failed)
{
    testRenderProducesAnImage(failed);
    testRenderIsDeterministic(failed);
}
}   // namespace lockstep
