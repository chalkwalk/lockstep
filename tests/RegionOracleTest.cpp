// RegionOracleTest -- the cell you can see is the cell the model describes.
//
// WHY THIS AND NOT A GOLDEN IMAGE. A golden says "these pixels changed"; it cannot say
// what was wrong, and it goes red for every deliberate visual change too, so it decays
// into a re-bless reflex. This layer asks a question with an answer instead: for each
// cell, the model says what colour it should be, the hit test says where it is, and
// the render says what is actually there. No file on disk is involved, nothing needs
// re-blessing when a colour is deliberately changed (the oracle moves with it), and a
// failure names the cell.
//
// It also spans the join that has no other test. buildSurfaceModel is unit-tested and
// stepCellAt is used by the mouse tests, but "the cell drawn at the place the hit test
// claims has the colour the model asked for" involves all three and was checked by
// nobody. That join is exactly where Item E's UI scale broke things: a child whose
// AffineTransform is wrong still builds a correct model and still hit-tests correctly
// in design space -- and draws in the wrong place. Hence the three scales below.
//
// WHAT THIS IS NOT: a position check, and the difference is worth knowing before you
// trust it. Sliding every sampled region sideways was expected to turn it red; measured,
// it takes ~40 px of shift to fail even ONE cell, because neighbouring steps mostly
// share a fill -- landing on the wrong cell usually finds the same colour. So this is a
// COLOUR oracle whose positional tolerance is roughly a quarter of a cell. It is not
// weak where it counts: neutering applyChildScale (the Item E bug, reproduced) fails 40
// checks here, because a lost transform does not nudge a cell onto its neighbour, it
// throws it across the surface onto unrelated chrome. Fine-grained position is
// SyntheticMouseTest's job -- it resolves clicks through the same transform stack -- and
// the two together cover the join. Do not add a tighter inset to chase positional
// sensitivity this design cannot honestly provide.
//
// TWO ORACLES, DELIBERATELY. Step cells are filled from cell.baseColour
// (paintGridCellFill); key cells go through paintKeyButton, whose fill is
// cellFillColour(cell) -- a different answer, because groupForCell/state resolve it.
// KeyButton.h:64 says so outright ("Not for step-grid cells"). Using one oracle for
// both would be green on one family by luck and red on the other for no reason.
//
// MEDIAN, NOT MEAN. The sampled region contains the fill plus whatever the cell draws
// on top -- a label, a press ring, a P-Lock dot. A mean is pulled off the fill by all
// of them; a median ignores anything covering less than half the region, which is what
// almost every decoration does by construction. So the assertion tracks the FILL, and
// does not need a tolerance wide enough to hide a real colour regression.
//
// THE PLAYHEAD IS THE EXCEPTION, and it is instructive. It is not an edge decoration:
// it washes the WHOLE cell with amber at 0.35 alpha over the fill, so the rendered
// median is legitimately not the model's colour. (Step 0 failed exactly this way on the
// first run -- the playhead rests there when stopped.) The fix is NOT to teach the test
// that the wash is 0.35 amber: that would copy a paint constant into a test whose entire
// purpose is to not re-implement the paint path, and it would go red for a deliberate
// restyle while proving nothing. Instead the test asserts what the MODEL claims -- this
// cell has a playhead border -- by requiring the render to differ from the bare fill and
// to have moved toward the declared decoration colour. That holds at any alpha, catches
// a playhead that stops being drawn, and never needs updating for a restyle.

#include "UiDriver.h"

#include "../src/ui/KeyButton.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace lockstep
{
namespace
{
    using test::UiDriver;

    constexpr int kDesignW = 990;
    constexpr int kDesignH = 626;

    // Per-channel slack. Wide enough for the renderer's own rounding and for a
    // decoration that clips a corner of the sampled region; far too narrow to pass a
    // cell that is genuinely the wrong colour (the surface's colours are tens of units
    // apart, not units).
    constexpr int kTol = 12;

    // The interior half of the cell. The border, the rounded corners and the
    // decorations that live at the cell's edges (strip along the top, pip bottom-left)
    // are all outside this, so the region is fill plus, at most, some label.
    juce::Rectangle<int> interiorOf(juce::Rectangle<int> cell)
    {
        return cell.reduced(cell.getWidth() / 4, cell.getHeight() / 4);
    }

    // KeyboardArea-local design rect -> physical pixels, via the SAME uiScale the
    // editor stamped on the child. Getting this from the editor rather than computing
    // it from the window size is the difference between testing the product and
    // testing the test.
    juce::Rectangle<int> toPhysical(LockstepEditor& ed, juce::Rectangle<int> kbLocal)
    {
        const auto org = DispatchProbe::kbBounds(ed).getTopLeft().toFloat();
        const auto s = static_cast<float>(DispatchProbe::uiScale(ed));
        return ((kbLocal.toFloat() + org) * s).toNearestInt();
    }

    struct Sample
    {
        bool valid = false;
        juce::Colour median;
    };

    Sample medianOf(const juce::Image& img, juce::Rectangle<int> r)
    {
        const auto clipped = r.getIntersection(img.getBounds());
        if (clipped.getWidth() < 2 || clipped.getHeight() < 2)
            return {};

        std::vector<int> red, green, blue;
        for (int y = clipped.getY(); y < clipped.getBottom(); ++y)
        {
            for (int x = clipped.getX(); x < clipped.getRight(); ++x)
            {
                const auto p = img.getPixelAt(x, y);
                red.push_back(p.getRed());
                green.push_back(p.getGreen());
                blue.push_back(p.getBlue());
            }
        }
        if (red.empty())
            return {};

        const auto mid = [](std::vector<int>& v) {
            const auto n = v.size() / 2;
            std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n), v.end());
            return static_cast<juce::uint8>(v[n]);
        };
        return { true, juce::Colour(mid(red), mid(green), mid(blue)) };
    }

    bool near(juce::Colour a, juce::Colour b)
    {
        return std::abs(a.getRed()   - b.getRed())   <= kTol
            && std::abs(a.getGreen() - b.getGreen()) <= kTol
            && std::abs(a.getBlue()  - b.getBlue())  <= kTol;
    }

    juce::String describe(juce::Colour c)
    {
        return "#" + c.toDisplayString(false);
    }

    // Squared RGB distance -- only ever compared against another distance, so the
    // square root would be ceremony.
    int distance(juce::Colour a, juce::Colour b)
    {
        const int dr = a.getRed() - b.getRed();
        const int dg = a.getGreen() - b.getGreen();
        const int db = a.getBlue() - b.getBlue();
        return dr * dr + dg * dg + db * db;
    }

    int checkAllCells(UiDriver& d, double scale, int& failed)
    {
        auto& ed = d.editor();
        const auto img = d.renderAt(static_cast<int>(kDesignW * scale),
                                    static_cast<int>(kDesignH * scale));
        const auto model = d.surface();
        int checked = 0;

        auto fail = [&](const juce::String& what, const juce::String& why) {
            std::fprintf(stderr, "FAIL [RegionOracle] %s at %.3gx: %s\n",
                         what.toRawUTF8(), scale, why.toRawUTF8());
            ++failed;
        };

        auto assertCell = [&](juce::Rectangle<int> kbLocal, const SurfaceCell& cell,
                              juce::Colour expected, const juce::String& what) {
            if (kbLocal.isEmpty())
                return;   // not on screen in this mode/page: nothing claimed, nothing checked

            // A translucent fill composites over whatever is behind it, so the rendered
            // pixel is legitimately not the cell's colour. Skipping is honest; pretending
            // to check it would not be.
            if (expected.getAlpha() != 255)
                return;

            const auto got = medianOf(img, toPhysical(ed, interiorOf(kbLocal)));
            if (!got.valid)
                return;

            ++checked;

            // A full-cell decoration (the playhead wash) legitimately covers the fill.
            // Assert the model's CLAIM instead -- see the header note on why this does
            // not simply hard-code the wash.
            if (cell.border.present)
            {
                const juce::Colour decoration { cell.border.colour };
                if (near(got.median, expected))
                    fail(what, "the model marks a " + describe(decoration) + " border here, "
                               "but the cell rendered as the bare fill " + describe(expected)
                             + " -- the decoration never reached the screen");
                else if (distance(got.median, decoration) >= distance(expected, decoration))
                    fail(what, "drawn " + describe(got.median) + " is no closer to the "
                               "declared border colour " + describe(decoration) + " than the "
                               "bare fill " + describe(expected) + " is");
                return;
            }

            if (!near(got.median, expected))
                fail(what, "drawn " + describe(got.median) + ", model says " + describe(expected));
        };

        // Step cells: filled from baseColour.
        for (int s = 0; s < 16; ++s)
        {
            const auto& cell = model.step[static_cast<std::size_t>(s)];
            assertCell(DispatchProbe::stepCellBounds(ed, s), cell,
                       juce::Colour(cell.baseColour), "step " + juce::String(s));
        }

        // Section keys (cells 4..9 = TRIG/SRC/FILTER/AMP/MOD/FX): filled via
        // paintKeyButton, so the oracle is cellFillColour, not baseColour.
        for (int s = 0; s < IMachine::kMaxSections; ++s)
        {
            const auto& cell = model.section[static_cast<std::size_t>(s)];
            assertCell(DispatchProbe::sectionCellBounds(ed, 4 + s), cell,
                       juce::Colour(cellFillColour(cell)), "section key " + juce::String(s));
        }

        return checked;
    }

    void testCellsRenderTheirModelColour(int& failed)
    {
        // 1.0 = the design canvas (transform is a no-op); 1.2 = what the product ships
        // at; 1.4141 = the rig's own size, deliberately non-integer so the transform
        // lands cells on fractional pixel boundaries. If a cell survives all three, the
        // scale path is honest.
        for (const double scale : { 1.0, 1.2, 1400.0 / 990.0 })
        {
            UiDriver d;
            const int checked = checkAllCells(d, scale, failed);

            // Self-check: if the scan silently stopped finding cells, every assertion
            // above would vacuously pass forever. 16 steps + 6 section keys = 22, minus
            // any translucent fills skipped.
            if (checked < 16)
            {
                std::fprintf(stderr,
                             "FAIL [RegionOracle] at %.3gx only %d cells were checked -- the "
                             "scan is finding nothing, so the greens above mean nothing\n",
                             scale, checked);
                ++failed;
            }
        }
    }
}   // namespace

void runRegionOracleTests(int& failed)
{
    testCellsRenderTheirModelColour(failed);
}
}   // namespace lockstep
