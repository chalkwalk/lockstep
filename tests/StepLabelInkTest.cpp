// StepLabelInkTest — does a label the model writes actually reach the screen?
//
// Every surface test in this repo stops at the model: `surface().step[8].primary
// == "Piano"` proves the builder wrote a label, and proves nothing at all about
// whether anything draws it. That gap shipped a picker whose 128 instruments
// rendered as sixteen unlabelled blocks -- the model was perfect, and the
// paint path never asked it a question.
//
// The mechanism is worth stating, because the classifier was supposed to prevent
// exactly this. `layerStepRender` sorts each SurfaceLayer into Sequencer /
// Labeled / Custom, and Labeled layers are drawn generically from `c.primary`
// with no bespoke branch. MachineConsole was Custom -- a promise that a bespoke
// branch exists -- and its branch was gated on `isTapeTrack`. But MachineConsole
// is THREE consoles (Tape's transport, Route's matrix, Tone's program picker),
// so the promise was kept for one machine and silently broken for the other two,
// which fell through to the sequencer grid: right colours, no text.
//
// A per-layer classification cannot say "labelled for some machines", so the
// fix was to make it labelled for all of them. This file is the assertion that
// keeps it that way, and it is stated as a RULE rather than a fixture:
//
//     a step cell whose model carries `primary` must have ink on the screen.
//
// Ink, not glyphs. The check counts pixels in the cell's centre that differ from
// the cell's own fill -- so it needs no golden, survives a font change, a colour
// change and a relabelling, and fails only for the thing it is about. It is
// measured, not assumed: with the fix reverted this reports 0% ink on 16 of 16
// labelled cells, and a cell that IS labelled runs 4-12%.

#include "UiDriver.h"

#include "../src/machine/RouteMachine.h"
#include "../src/machine/TapeMachine.h"
#include "../src/machine/ToneMachine.h"

#include <cstdio>

namespace lockstep
{
namespace
{
    using test::CB;
    using test::UiDriver;

    // Ink threshold. A 9 pt label in a ~50x30 cell covers a few percent of the
    // centre band; a bare fill covers 0.0%. Anti-aliasing and the rounded corners
    // put nothing here, so the gap between "labelled" and "not" is the whole
    // range and 1% sits comfortably inside it.
    constexpr double kMinInkFraction = 0.01;

    // How far from the fill a pixel must be to count as ink. Text is white at
    // 0.65-0.90 alpha over a mid-tone fill, so real ink is hundreds of units away
    // in squared distance; this only rejects the renderer's own rounding.
    constexpr int kInkDistance = 30 * 30;

    juce::Rectangle<int> toPhysical(LockstepEditor& ed, juce::Rectangle<int> kbLocal)
    {
        const auto org = DispatchProbe::kbBounds(ed).getTopLeft().toFloat();
        const auto s = static_cast<float>(DispatchProbe::uiScale(ed));
        return ((kbLocal.toFloat() + org) * s).toNearestInt();
    }

    // The centre band, where drawText centres its glyphs. Excludes the border, the
    // rounded corners, the key hint in the corner and the P-Lock dot at the right
    // edge -- all of which are ink that is not the label.
    juce::Rectangle<int> centreBandOf(juce::Rectangle<int> cell)
    {
        return cell.reduced(cell.getWidth() / 5, cell.getHeight() / 3);
    }

    double inkFraction(const juce::Image& img, juce::Rectangle<int> r, juce::Colour fill)
    {
        const auto clipped = r.getIntersection(img.getBounds());
        if (clipped.getWidth() < 2 || clipped.getHeight() < 2) return -1.0;

        int inked = 0, total = 0;
        for (int y = clipped.getY(); y < clipped.getBottom(); ++y)
        {
            for (int x = clipped.getX(); x < clipped.getRight(); ++x)
            {
                const auto p = img.getPixelAt(x, y);
                const int dr = p.getRed()   - fill.getRed();
                const int dg = p.getGreen() - fill.getGreen();
                const int db = p.getBlue()  - fill.getBlue();
                if (dr * dr + dg * dg + db * db >= kInkDistance) ++inked;
                ++total;
            }
        }
        return total > 0 ? static_cast<double>(inked) / total : -1.0;
    }

    // The rule, applied to whatever state the driver is currently in. Returns how
    // many labelled cells it actually looked at, so a caller can catch a scan that
    // silently found nothing and passed vacuously.
    int checkLabelsAreDrawn(UiDriver& d, const char* where, int& failed)
    {
        auto& ed = d.editor();
        const auto img = d.render();
        const auto model = d.surface();
        int checked = 0;

        for (int s = 0; s < 16; ++s)
        {
            const auto& cell = model.step[static_cast<std::size_t>(s)];
            if (cell.primary.isEmpty()) continue;

            const auto bounds = DispatchProbe::stepCellBounds(ed, s);
            if (bounds.isEmpty()) continue;

            const juce::Colour fill { cell.baseColour };
            if (fill.getAlpha() != 255) continue;   // composites; not ours to judge

            const double ink = inkFraction(img, toPhysical(ed, centreBandOf(bounds)), fill);
            if (ink < 0.0) continue;

            ++checked;
            if (ink < kMinInkFraction)
            {
                std::fprintf(stderr,
                             "FAIL [StepLabelInk] %s: step cell %d is labelled \"%s\" in the "
                             "model but rendered %.2f%% ink over its own fill -- the label "
                             "never reached the screen\n",
                             where, s, cell.primary.toRawUTF8(), ink * 100.0);
                ++failed;
            }
        }

        if (checked == 0)
        {
            std::fprintf(stderr,
                         "FAIL [StepLabelInk] %s: no labelled step cell was found, so this "
                         "leg asserted nothing -- the state was never reached\n", where);
            ++failed;
        }
        return checked;
    }
}   // namespace

void runStepLabelInkTests(int& failed)
{
    // ── Tone's program picker: the console that shipped blank ────────────────
    {
        UiDriver d;
        d.proc().setTrackMachine(0, ToneMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);
        if (!test::expectReached(d, [](UiDriver& dd) {
                                     return dd.proc().kit(0).machineId == ToneMachine::kMachineId;
                                 }, "track 0 holds a Tone", failed))
            return;

        d.gap();
        d.longPress(CB::Section, IMachine::kSrcSecIdx);
        if (!test::expectReached(d, [](UiDriver& dd) { return dd.ui().machineConsoleOpen; },
                                 "the Tone console opens", failed))
            return;

        // Press one: sixteen family names.
        checkLabelsAreDrawn(d, "Tone console / family page", failed);

        // Press two: eight instrument names plus BACK.
        d.clickStep(13);
        checkLabelsAreDrawn(d, "Tone console / program page", failed);
    }

    // ── Route's matrix: broken the same way, and by the same gate ────────────
    // Nobody filed this one -- the destinations (OFF / MST / T3) were built into
    // the model and drawn nowhere, so the matrix showed which cells were routed
    // but never where. Covered here because it is the same defect, not a
    // second one.
    {
        UiDriver d;
        d.proc().setTrackMachine(0, RouteMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);
        d.gap();
        d.longPress(CB::Section, IMachine::kSrcSecIdx);   // OnDemand, same rail as Tone
        if (test::expectReached(d, [](UiDriver& dd) {
                                    return dd.surface().activeLayer == SurfaceLayer::MachineConsole;
                                }, "the Route matrix is on the grid", failed))
            checkLabelsAreDrawn(d, "Route matrix", failed);
    }

    // ── Tape's transport: the one console that DID paint ─────────────────────
    // Its bespoke branch is the code the fix deleted, so this leg is the
    // regression guard on the deletion: the generic path must draw what the
    // special-cased path drew.
    {
        UiDriver d;
        d.proc().setTrackMachine(0, TapeMachine::kMachineId);
        d.tap(CB::SelectTrack, 0);
        if (test::expectReached(d, [](UiDriver& dd) { return dd.proc().isTapeTrack(0); },
                                "track 0 holds a Tape", failed))
            checkLabelsAreDrawn(d, "Tape console", failed);
    }
}
}   // namespace lockstep
