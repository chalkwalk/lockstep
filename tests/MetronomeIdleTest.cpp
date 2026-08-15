// MetronomeIdleTest -- the click must not sound while the transport is parked.
//
// Reported as: hold Rec while playing, and the playback stops (that part is the
// gesture doing its job -- hold(Rec) is the transport reset) but the click turns
// into a loud buzz.
//
// The buzz is a stopped clock still publishing a MOVING window. Clock.cpp
// computes
//
//     ppqBlockEnd_ = ppqBlockStart_ + oneBlockOfPpq
//
// unconditionally, while `localPpq_` only advances when the transport is
// playing. So a parked transport hands the metronome the identical
// [ppq, ppq + block) range on every single block. If `ppq` happens to sit
// exactly on a beat, that beat is inside the window every time and the click
// re-triggers at block rate -- ~86 Hz on a 1 kHz strong click, which is a buzz,
// not a click.
//
// `transportStopReset()` parks the transport on PPQ 0, a beat by definition,
// which is why hold(Rec) reproduced it every time while an ordinary stop
// mid-pattern usually did not: it depends on where you happened to stop.
//
// Asserted as the property, not the gesture: parked transport + click enabled =
// silence, wherever it is parked. The two cases below are the two halves of the
// bug -- parked ON a beat is what actually fired, parked OFF one is the case
// that accidentally passed before and must keep passing.

#include "TestHarness.h"
#include "EngineHarness.h"
#include "../src/ParameterIDs.h"

namespace lockstep
{
namespace
{
    // Put the rig in the regime the bug actually lives in: Lockstep owning the
    // transport. The rig defaults to hosted+Locked, where transportStopReset only
    // PARKS THE ARM (phase is host-derived, so a reset there cannot move it) and
    // the clock reads a host playhead -- so nothing ever sits on a frozen local
    // ppq, and the bug is unreachable. Auto sync + no playhead is standalone.
    void makeTransportOwner(EngineHarness& h)
    {
        if (auto* sync = h.processor().apvts().getParameter(ParamIDs::syncMode))
        {
            sync->beginChangeGesture();
            sync->setValueNotifyingHost(sync->convertTo0to1(1.0f));   // Auto
            sync->endChangeGesture();
        }
        h.processor().setPlayHead(nullptr);
    }

    float peakOver(EngineHarness& h, int blocks)
    {
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            h.renderBlocks(1);
            const auto& buf = h.buffer();
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                peak = std::max(peak, buf.getMagnitude(ch, 0, buf.getNumSamples()));
        }
        return peak;
    }
}   // namespace

void runMetronomeIdleTests()
{
    // ── Parked exactly on the downbeat (what hold(Rec) does) ─────────────
    {
        EngineHarness h;
        auto& proc = h.processor();
        proc.clock().setMetronomeEnabled(true);

        makeTransportOwner(h);
        proc.clock().setInPluginPlaying(true);
        h.renderBlocks(8);
        proc.transportStopReset();    // what hold(Rec) does: stop and rewind to PPQ 0
        h.renderBlocks(4);            // let the last real click decay out

        // Long enough to contain several beats had the transport been rolling --
        // a window too short to hold a beat passes whatever the code does.
        const float parked = peakOver(h, 200);
        CHECK(parked < 1.0e-4f,
              juce::String("a transport parked on the downbeat with CLICK on is silent "
                           "-- it does not re-trigger the beat every block (peak ")
                  + juce::String(parked, 6) + ")");
    }

    // ── Parked somewhere that is NOT a beat ──────────────────────────────
    // This case passed even with the bug, because no beat fell inside the frozen
    // window. It is here so a fix that only special-cases PPQ 0 does not look
    // like a fix.
    {
        EngineHarness h;
        auto& proc = h.processor();
        proc.clock().setMetronomeEnabled(true);

        makeTransportOwner(h);
        proc.clock().setInPluginPlaying(true);
        h.renderBlocks(11);           // stop mid-beat: ppq is not a whole beat here
        proc.clock().setInPluginPlaying(false);
        h.renderBlocks(4);

        const float parked = peakOver(h, 200);
        CHECK(parked < 1.0e-4f,
              juce::String("...and so is one parked between beats (peak ")
                  + juce::String(parked, 6) + ")");
    }

    // ── The click still works when it is supposed to ─────────────────────
    // Without this the two assertions above are satisfied by a metronome that
    // never sounds at all, which would be a worse bug delivered as a fix.
    {
        EngineHarness h;
        auto& proc = h.processor();
        proc.clock().setMetronomeEnabled(true);
        makeTransportOwner(h);
        proc.clock().setInPluginPlaying(true);

        const float rolling = peakOver(h, 200);
        CHECK(rolling > 1.0e-3f,
              juce::String("a ROLLING transport still clicks (peak ")
                  + juce::String(rolling, 6) + ")");
    }
}
}   // namespace lockstep
