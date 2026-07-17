// SceneGoldenTest -- whole frames, blessed and compared.
//
// The region oracle next door answers "is this cell the colour the model says", which
// is most of what matters and needs no golden file. But most of the SURFACE is not
// cells: the inspector, the transport, the timeline strip, the MZ's rotaries and their
// labels, the banners, the hint rails. None of it has a model to be checked against,
// and all of it can silently break. This is the net under that -- deliberately coarse,
// because a whole-frame comparison is the bluntest instrument here and should not
// pretend otherwise.
//
// WHY FUZZY. Text is the problem. Even with Inter embedded (WI-1), glyph rasterisation
// depends on the FreeType build, its hinting configuration, and subpixel positioning --
// none of which the repo pins. An exact comparison would fail on a machine that renders
// the same surface a shade differently at one antialiased edge, and a golden that fails
// on correct code is worse than no golden: it teaches you to re-bless without looking,
// which is the one way to make this file worthless.
//
// So: box-downscale 6x, then per-channel tolerance 10. The tolerance is what does the
// real work -- a one-pixel difference in a glyph's antialiasing, averaged over a 6x6
// block, moves that block by about 3, comfortably inside 10 -- while a moved or
// recoloured widget moves its blocks by tens and survives the averaging.
//
// The mismatch BUDGET (how many blocks may still differ) is only a backstop, and it is
// deliberately small. It was 0.5% on the first pass, which sounds strict and is not: at
// 1188x751 that is ~124 blocks, so any change smaller than roughly 67x67 px could pass
// unseen. Measured, a chrome-colour canary cleared it by a factor of 1.2 -- nearly a
// miss. At 0.05% the same canary fails by a factor of 12, and an unchanged render still
// passes with room to spare (renders here are byte-identical, so the budget's only real
// job is absorbing another machine's stray blocks -- and per the note below, another
// machine re-blesses anyway).
//
// WHAT A FAILURE MEANS. Probably that you changed the look on purpose, in which case
// re-bless and LOOK AT THE IMAGE. The full-resolution actual frame is written next to
// the golden on failure precisely so that "read the diff" is possible for a picture:
//     LOCKSTEP_REGEN_GOLDEN=1 ./build/tests/lockstep_dispatch_tests
//
// KNOWN LIMIT, accepted: these are blessed on one machine. Another machine may need to
// re-bless or widen the tolerance. Single-dev repo; revisit if CI elsewhere ever
// arrives.
//
// EVERY SCENE VERIFIES ITSELF FIRST. A scene that failed to reach its state still
// renders a perfectly good picture -- of the wrong thing -- and would be blessed as the
// truth. The `reached` predicate is what stops this file quietly becoming six copies of
// the default surface.

#include "UiDriver.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    constexpr int kDesignW = 990;
    constexpr int kDesignH = 626;

    // 1.2x -- the scale the product actually ships at, so the blessed frames are the
    // ones a user sees.
    constexpr int kShipW = 1188;
    constexpr int kShipH = 751;

    constexpr int kBlock = 6;      // downscale factor
    constexpr int kChannelTol = 10;
    constexpr double kMaxMismatchFraction = 0.0005;

    struct Scene
    {
        const char* name;
        int width;
        int height;
        std::function<void(UiDriver&)> setUp;
        std::function<bool(UiDriver&)> reached;   // did setUp actually get there?
    };

    // Mean of each kBlock x kBlock block. Averaging is the point: it is exactly what
    // makes a rasterisation difference vanish and a moved widget survive.
    std::vector<juce::uint8> downscale(const juce::Image& img)
    {
        const int bw = (img.getWidth()  + kBlock - 1) / kBlock;
        const int bh = (img.getHeight() + kBlock - 1) / kBlock;
        std::vector<juce::uint8> out;
        out.reserve(static_cast<std::size_t>(bw * bh * 3));

        for (int by = 0; by < bh; ++by)
        {
            for (int bx = 0; bx < bw; ++bx)
            {
                int r = 0, g = 0, b = 0, n = 0;
                for (int y = by * kBlock; y < juce::jmin((by + 1) * kBlock, img.getHeight()); ++y)
                {
                    for (int x = bx * kBlock; x < juce::jmin((bx + 1) * kBlock, img.getWidth()); ++x)
                    {
                        const auto p = img.getPixelAt(x, y);
                        r += p.getRed(); g += p.getGreen(); b += p.getBlue();
                        ++n;
                    }
                }
                if (n == 0) n = 1;
                out.push_back(static_cast<juce::uint8>(r / n));
                out.push_back(static_cast<juce::uint8>(g / n));
                out.push_back(static_cast<juce::uint8>(b / n));
            }
        }
        return out;
    }

    bool writePng(const juce::File& f, const juce::Image& img)
    {
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream os(f);
        if (!os.openedOk())
            return false;
        juce::PNGImageFormat png;
        return png.writeImageToStream(img, os);
    }

    void runScene(const Scene& scene, bool regen, int& failed)
    {
        auto fail = [&](const juce::String& why) {
            std::fprintf(stderr, "FAIL [SceneGolden/%s] %s\n", scene.name, why.toRawUTF8());
            ++failed;
        };

        UiDriver d;
        d.editor().setSize(scene.width, scene.height);
        scene.setUp(d);

        if (!scene.reached(d))
        {
            // Loud, and NOT blessed: a scene that never got where it was going would
            // otherwise be recorded as the expected picture of a state it is not in.
            fail("the scene never reached its state -- refusing to compare or bless it");
            return;
        }

        const auto actual = d.render();

        // Determinism gate. If a frame is not reproducible within one process, no
        // amount of tolerance makes it meaningful across runs; better to say so here
        // than to let a golden flap forever.
        const auto again = d.render();
        if (downscale(actual) != downscale(again))
        {
            fail("renders differently twice in a row -- something in paint is not a "
                 "function of the state (see SurfaceRenderTest)");
            return;
        }

        const juce::File golden { juce::String(LOCKSTEP_TEST_DIR) + "/goldens/scenes/"
                                  + scene.name + ".png" };

        if (regen)
        {
            if (!writePng(golden, actual))
                fail("could not write the golden");
            else
                std::fprintf(stderr, "[scene] blessed %s (%dx%d)\n",
                             golden.getFileName().toRawUTF8(), actual.getWidth(), actual.getHeight());
            return;
        }

        if (!golden.existsAsFile())
        {
            fail("no golden yet -- create it with LOCKSTEP_REGEN_GOLDEN=1");
            return;
        }

        const auto want = juce::ImageFileFormat::loadFrom(golden);
        if (!want.isValid())
        {
            fail("the golden could not be read");
            return;
        }
        if (want.getWidth() != actual.getWidth() || want.getHeight() != actual.getHeight())
        {
            fail("size changed: golden is " + juce::String(want.getWidth()) + "x"
                 + juce::String(want.getHeight()) + ", rendered "
                 + juce::String(actual.getWidth()) + "x" + juce::String(actual.getHeight()));
            return;
        }

        const auto a = downscale(want);
        const auto b = downscale(actual);
        if (a.size() != b.size())
        {
            fail("internal: downscaled sizes disagree");
            return;
        }

        std::size_t bad = 0;
        int worst = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            const int diff = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
            worst = juce::jmax(worst, diff);
            if (diff > kChannelTol)
                ++bad;
        }

        const double frac = static_cast<double>(bad) / static_cast<double>(a.size());
        if (frac <= kMaxMismatchFraction)
            return;

        // Write what was actually drawn, so the diff can be LOOKED at -- "0.8% of blocks
        // differ" is not something anyone can act on. It goes in the working directory
        // (the build tree, under ctest), NEVER beside the golden: a failure would
        // otherwise litter the source tree with near-identical PNGs that are easy to
        // commit by accident and impossible to tell apart afterwards.
        const juce::File actualFile = juce::File::getCurrentWorkingDirectory()
                                          .getChildFile(juce::String(scene.name) + ".actual.png");
        writePng(actualFile, actual);

        fail(juce::String(frac * 100.0, 2) + "% of blocks differ (budget "
             + juce::String(kMaxMismatchFraction * 100.0, 2) + "%), worst channel delta "
             + juce::String(worst) + ".\n    Rendered frame written to "
             + actualFile.getFullPathName()
             + "\n    -- open it next to the golden. If the change was intended, re-bless "
               "with LOCKSTEP_REGEN_GOLDEN=1 and LOOK at the result.");
    }

    std::vector<Scene> scenes()
    {
        return {
            // The plain surface, at the design canvas (transform is identity) and at
            // the shipped scale. Between them these two catch anything that breaks the
            // chrome outright, and they are the frames most likely to be looked at.
            { "default-1x", kDesignW, kDesignH,
              [](UiDriver&) {},
              [](UiDriver&) { return true; } },

            { "default-ship-1.2x", kShipW, kShipH,
              [](UiDriver&) {},
              [](UiDriver&) { return true; } },

            // The Func layer: the whole surface relabels to its secondaries. Nothing
            // else in the suite renders this.
            { "func-held", kShipW, kShipH,
              [](UiDriver& d) { d.press(CB::Func); },
              [](UiDriver& d) { return d.ui().funcHeld; } },

            // The cue console (6.4): Func+3 for the momentary Cue scope, then a
            // long-press on AMP, which opens straight to the param page.
            { "cue-console", kShipW, kShipH,
              [](UiDriver& d) {
                  d.press(CB::Func).press(CB::TapTempo);
                  d.longPress(CB::Section, LockstepProcessor::kAmpSecIdx);
              },
              [](UiDriver& d) { return d.ui().overlay == Overlay::Cue && d.ui().cueParamPage; } },

            // The Time overlay, latched (Song + TRIG is the entry chord). A sticky
            // overlay with its own meta band in the MZ.
            { "time-overlay", kShipW, kShipH,
              [](UiDriver& d) { d.chord({ CB::SongScope }, CB::Section, 0); },
              [](UiDriver& d) { return d.ui().overlay == Overlay::Time; } },

            // The capture indicator, ARMED (Func+Song+Record). It moved off the band's
            // hard-right flank into the middle (play-test WI-2), and it is the one
            // readout no other scene can show: at rest it paints nothing at all, so
            // every frame above is a picture of its absence. Armed also renders the
            // detail line ("starts on Play - N stems"), which is the text that overflows
            // leftward across the band -- the behaviour most at risk from the move.
            { "capture-armed", kShipW, kShipH,
              [](UiDriver& d) { d.chord({ CB::Func, CB::SongScope }, CB::VerbRecord); },
              [](UiDriver& d) {
                  return DispatchProbe::capturePhase(d.editor())
                         == CaptureController::Phase::Armed;
              } },

            // A held Song selector, with a named + coloured song (play-test WI-3). This
            // is the frame that pictures the fix: before it, a held Song showed bare slot
            // numbers in a flat tint (Scene already showed names). Set up two named,
            // identity-coloured songs, then hold Song. reached asserts both the layer and
            // that the identity actually took, so a scene that silently reverted could
            // not be blessed as the fix.
            { "song-held-named", kShipW, kShipH,
              [](UiDriver& d) {
                  auto& arr = d.proc().arrangement();
                  arr.songs[1].initialised = true;
                  arr.songs[2].initialised = true;
                  d.proc().setSongName(1, "VERSE");
                  d.proc().setSongColour(1, 3);
                  d.proc().setSongName(2, "CHORUS");
                  d.proc().setSongColour(2, 6);
                  d.press(CB::SongScope);
              },
              [](UiDriver& d) {
                  return d.ui().songHeld
                         && std::string(d.proc().arrangement().songs[1].name) == "VERSE"
                         && d.proc().arrangement().songs[1].colour == 3;
              } },

            // A real machine's param page: eight live rotaries with labels and values,
            // which is the MZ's actual job and is otherwise never rendered by a test
            // (the rig's stub machine has no params to show).
            { "fm-params", kShipW, kShipH,
              [](UiDriver& d) {
                  installRealMachine(d.rig());
                  DispatchProbe::frame(d.editor());
              },
              [](UiDriver& d) {
                  return juce::String(d.proc().getMachineIdRaw(0)) == FMMachine::kMachineId;
              } },
        };
    }
}   // namespace

void runSceneGoldenTests(int& failed)
{
    const bool regen = std::getenv("LOCKSTEP_REGEN_GOLDEN") != nullptr;
    for (const auto& s : scenes())
        runScene(s, regen, failed);
}
}   // namespace lockstep
