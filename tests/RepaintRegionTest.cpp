// RepaintRegionTest -- does the editor invalidate the pixels it then paints?
//
// THE BUG THIS EXISTS FOR. The VU meters (per-track and master) stopped animating.
// Not subtly: the master meter sat frozen through an entire take. The decay maths in
// timerCallback was correct, the paint was correct, and every existing test was green,
// because the fault was in neither -- it was in the REGION the editor asked to have
// redrawn. resized() caches masterChromeRegion_ / trackRowChromeRegion_ in LOGICAL
// design-canvas coordinates (Item E), and Component::repaint(Rectangle) speaks
// PHYSICAL pixels. At the default 1.2x scale those do not overlap: the meter is
// painted at physical x~1140 and the editor was invalidating logical x~950.
//
// WHY NOTHING CAUGHT IT. Every visual test in the suite renders via
// paintEntireComponent, which repaints unconditionally -- it never consults an invalid
// region, so the region can be arbitrarily wrong and the pixels still come out right.
// The region is only observable through the invalidation channel itself.
//
// THE SEAM. juce::Component::setCachedComponentImage installs a CachedComponentImage
// whose invalidate(Rectangle) is virtual and is called by Component::internalRepaint
// with the exact rect requested. That is the observation point, and it is the only one:
// JUCE exposes no accessor for a non-top-level component's pending invalid region.
//
// TWO TRAPS, both of which make this test lie if ignored:
//   * paintEntireComponent DELEGATES to cachedImage->paint() when one is attached. The
//     spy's paint is a no-op, so any render() taken while it is installed comes back
//     blank. This TU therefore never renders -- do not add a render() assertion here.
//   * invalidateAll() (the argument-less repaint(), i.e. refreshSurface's whole-window
//     invalidation) must NOT count as a pass. It is always correct and always fires
//     eventually, so a test that accepted it would be green against the bug.
//
// THE ORACLE IS INDEPENDENT ON PURPOSE. The expected rect below is computed with plain
// arithmetic in the test, NOT with design::toPhysical -- the function the fix uses. An
// oracle built from the code under test only ever proves the code agrees with itself.

#include "EngineHarness.h"   // StubPlayHead: the sequencer needs a transport that MOVES
#include "UiDriver.h"

#include "../src/ui/DesignCanvas.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace lockstep
{
namespace
{
    using test::UiDriver;

    // Records what the editor asks to have invalidated, and distinguishes a scoped
    // invalidate(rect) from the whole-component invalidateAll().
    struct RepaintSpy : juce::CachedComponentImage
    {
        std::vector<juce::Rectangle<int>> areas;
        bool all = false;

        void paint(juce::Graphics&) override {}
        bool invalidate(const juce::Rectangle<int>& r) override
        {
            areas.push_back(r);
            return true;   // true = "peer should repaint too": stay out of the way
        }
        bool invalidateAll() override
        {
            all = true;
            return true;
        }
        void releaseResources() override {}

        [[nodiscard]] bool anyContains(juce::Rectangle<int> target) const
        {
            for (const auto& a : areas)
                if (a.contains(target))
                    return true;
            return false;
        }
        [[nodiscard]] juce::String describe() const
        {
            juce::String s = all ? "invalidateAll + " : "";
            s << areas.size() << " scoped:";
            for (const auto& a : areas)
                s << " " << a.toString();
            return s;
        }
    };

    // The physical rect a logical one MUST cover, worked out from first principles:
    // round the scaled edges outward. Deliberately not design::toPhysical -- see the
    // header note.
    juce::Rectangle<int> expectedPhysical(juce::Rectangle<int> logical, double scale)
    {
        const auto l = static_cast<int>(std::floor(logical.getX() * scale));
        const auto t = static_cast<int>(std::floor(logical.getY() * scale));
        const auto r = static_cast<int>(std::ceil(logical.getRight() * scale));
        const auto b = static_cast<int>(std::ceil(logical.getBottom() * scale));
        return juce::Rectangle<int>::leftTopRightBottom(l, t, r, b);
    }

    // Make the meters actually move, through the real path: a machine that makes sound,
    // trigs under an ADVANCING playhead, and blocks run so the audio thread stores a
    // peak. Fabricating the peak through a probe would prove only that the test can
    // write to a float.
    //
    // Three things the UiDriver rig does not do for you, each of which independently
    // yields silence (and therefore a vacuous, green-looking test):
    //   * prepareToPlay -- the rig only does setRateAndBufferSizeDetails, which makes
    //     getSampleRate() sane but never prepares the MACHINES.
    //   * an advancing playhead -- ppq drives the sequencer. A static transport fires
    //     no trigs no matter how many blocks are run.
    //   * a full-width buffer -- since 11.12 a host allocates every bus (~20 channels).
    void driveAudioUntilMetersAreLive(UiDriver& d, StubPlayHead& ph, int& failed)
    {
        auto& proc = d.proc();
        proc.setPlayHead(&ph);
        proc.setRateAndBufferSizeDetails(EngineHarness::kSampleRate, EngineHarness::kBlockSize);
        proc.prepareToPlay(EngineHarness::kSampleRate, EngineHarness::kBlockSize);
        proc.clock().setInPluginPlaying(true);
        installRealMachine(d.rig());

        const int chans = juce::jmax(2, juce::jmax(proc.getTotalNumInputChannels(),
                                                   proc.getTotalNumOutputChannels()));
        juce::AudioBuffer<float> buf(chans, EngineHarness::kBlockSize);
        juce::MidiBuffer midi;

        for (int i = 0; i < 256; ++i)
        {
            buf.clear();
            midi.clear();
            proc.processBlock(buf, midi);
            ph.advance();
            if (proc.masterPeak() > 0.001f && proc.trackPeak(0) > 0.001f)
                return;
        }
        std::fprintf(stderr,
                     "FAIL [RepaintRegion] the rig never produced audio, so the meter "
                     "assertions below would be vacuous (masterPeak %g, trackPeak0 %g)\n",
                     static_cast<double>(proc.masterPeak()),
                     static_cast<double>(proc.trackPeak(0)));
        ++failed;
    }

    void testTheTimerInvalidatesWhereTheMetersArePainted(int& failed, int w, int h,
                                                         const char* label)
    {
        auto check = [&failed, label](bool ok, const juce::String& what) {
            if (!ok)
            {
                std::fprintf(stderr, "FAIL [RepaintRegion/%s] %s\n", label, what.toRawUTF8());
                ++failed;
            }
        };

        UiDriver d;
        d.editor().setSize(w, h);
        StubPlayHead ph(EngineHarness::kBpm, EngineHarness::kSampleRate,
                        EngineHarness::kBlockSize);
        driveAudioUntilMetersAreLive(d, ph, failed);

        // Now let the meters DECAY: silence the engine and settle the model.
        //
        // This is the branch that matters, and picking it is the whole design of this
        // test. timerCallback does `if (modelDirty) refreshSurface(); else { ...scoped
        // repaints... }` -- and modelDirty is set ONLY by a transport-state change, a
        // morph-fader move, or takeSurfaceDirty(). Notably NOT by the playhead moving.
        // So in ordinary playback the editor lives in the else branch, which is the
        // broken one; the startup transients (transport just started, machine just
        // installed) hide that by forcing a whole-window refresh for a tick or two.
        // The warm-up ticks below drain those, so the measured tick is the real one.
        d.proc().clock().setInPluginPlaying(false);
        {
            const int chans = juce::jmax(2, juce::jmax(d.proc().getTotalNumInputChannels(),
                                                       d.proc().getTotalNumOutputChannels()));
            juce::AudioBuffer<float> buf(chans, EngineHarness::kBlockSize);
            juce::MidiBuffer midi;
            for (int i = 0; i < 8; ++i)   // ring the machine out to silence
            {
                buf.clear();
                midi.clear();
                d.proc().processBlock(buf, midi);
            }
        }
        for (int i = 0; i < 4; ++i)
            d.editor().timerCallback();   // drain the modelDirty transients

        const double scale = DispatchProbe::uiScale(d.editor());
        const auto masterLogical = DispatchProbe::meter(d.editor());
        const auto trackLogical = DispatchProbe::trackRow(d.editor());

        check(!masterLogical.isEmpty(), "the master meter region was laid out");
        check(!trackLogical.isEmpty(), "the track/VU row region was laid out");

        // Attach the spy only for the tick: it hijacks paint, so nothing may render
        // while it is installed.
        auto spy = std::make_unique<RepaintSpy>();
        auto* spyPtr = spy.get();
        d.editor().setCachedComponentImage(spy.release());

        // setCachedComponentImage ITSELF calls repaint() (juce_Component.cpp:598), and a
        // bare repaint() is internalRepaintUnchecked(bounds, isEntireComponent=true) ->
        // invalidateAll (:1627). So attaching the spy fires the very signal the check
        // below treats as disqualifying. Reset, or the harness fails its own scenario
        // check with a record of its own installation.
        spyPtr->areas.clear();
        spyPtr->all = false;

        d.editor().timerCallback();

        const auto masterPhys = expectedPhysical(masterLogical, scale);
        const auto trackPhys = expectedPhysical(trackLogical, scale);

        // SCENARIO REACHED? Both halves must hold or the assertions below are theatre:
        // something must have been invalidated (the meters are mid-decay, so the tick
        // IS dirty), and it must NOT be a whole-window refresh (which would be correct
        // by accident and green against the bug -- exactly why turning an MZ rotary
        // "unfreezes" the master meter by hand).
        check(!spyPtr->areas.empty(), "the decaying meters invalidated something");
        check(!spyPtr->all,
              juce::String("the tick took the SCOPED repaint branch, not a whole-window "
                           "refresh -- otherwise this test proves nothing (got ")
                  + spyPtr->describe() + ")");

        check(spyPtr->anyContains(masterPhys),
              juce::String("the tick invalidates where the MASTER meter is painted"
                           " (want ")
                  + masterPhys.toString() + " covered; got " + spyPtr->describe() + ")");
        check(spyPtr->anyContains(trackPhys),
              juce::String("the tick invalidates where the TRACK VU row is painted"
                           " (want ")
                  + trackPhys.toString() + " covered; got " + spyPtr->describe() + ")");

        d.editor().setCachedComponentImage(nullptr);
    }

    // The mapping itself, pinned at the scales that ship. Cheap, and it localises a
    // failure: if this passes and the tick assertions fail, the bug is at the call
    // sites, not in the conversion.
    void testTheMappingCoversTheScaledRect(int& failed)
    {
        auto check = [&failed](bool ok, const char* what) {
            if (!ok) { std::fprintf(stderr, "FAIL [RepaintRegion/map] %s\n", what); ++failed; }
        };

        const juce::Rectangle<int> r { 950, 240, 16, 26 };

        check(design::toPhysical(r, 1.0).contains(expectedPhysical(r, 1.0)),
              "at 1.0x the mapping covers the rect (the identity case)");
        check(design::toPhysical(r, 1.2).contains(expectedPhysical(r, 1.2)),
              "at 1.2x (the shipped default) the mapping covers the scaled rect");
        check(design::toPhysical(r, 1.4141).contains(expectedPhysical(r, 1.4141)),
              "at a fractional scale the mapping covers the scaled rect");

        // The property that actually failed in production: at any scale != 1, the
        // logical rect and the physical rect must not be confused for one another.
        check(!r.contains(expectedPhysical(r, 1.2)),
              "a logical rect does NOT cover its own physical image at 1.2x "
              "(this is the whole bug: passing one where the other is meant)");

        // Round-trip: a physical point maps back to the logical point it came from.
        const juce::Point<float> phys { 600.0f, 300.0f };
        const auto back = design::toLogical(phys, 1.2);
        check(std::abs(back.x - 500) <= 1 && std::abs(back.y - 250) <= 1,
              "toLogical inverts the scale (600,300 at 1.2x -> 500,250)");
    }
}   // namespace

void runRepaintRegionTests(int& failed)
{
    testTheMappingCoversTheScaledRect(failed);
    // 1.0x is the control: the bug is invisible there because logical == physical, so
    // a green 1.0x beside a red 1.2x is the signature of a scale bug rather than a
    // broken meter.
    testTheTimerInvalidatesWhereTheMetersArePainted(failed, 990, 626, "1.0x");
    testTheTimerInvalidatesWhereTheMetersArePainted(failed, 1188, 751, "1.2x-ship");
    testTheTimerInvalidatesWhereTheMetersArePainted(failed, 1400, 900, "1.41x-rig");
}
}   // namespace lockstep
