// InputSourceTest — pure decode of the input_source slot encoding (DESIGN §27).

#include "TestHarness.h"
#include "../src/machine/InputSource.h"

namespace lockstep
{
    static void testInputSourceDecode()
    {
        // Slot encoding: 0=None, 1=External, 2=Master, 3+N = Track N (0-based).
        CHECK(decodeInputSource(0.0f).kind == InputSourceKind::None, "0 = None");
        CHECK(decodeInputSource(0.0f).track == -1, "None carries no track");
        CHECK(decodeInputSource(1.0f).kind == InputSourceKind::External, "1 = External");
        CHECK(decodeInputSource(2.0f).kind == InputSourceKind::Master, "2 = Master");

        const auto t0 = decodeInputSource(3.0f);
        CHECK(t0.kind == InputSourceKind::Track && t0.track == 0, "3 = Track 0");
        const auto t15 = decodeInputSource(18.0f);
        CHECK(t15.kind == InputSourceKind::Track && t15.track == 15, "18 = Track 15");

        // Rounds to the nearest detent (stepped-slot resolution).
        CHECK(decodeInputSource(1.4f).kind == InputSourceKind::External, "1.4 rounds to External");
        CHECK(decodeInputSource(1.6f).kind == InputSourceKind::Master, "1.6 rounds to Master");

        // Negative / clamp guards resolve to None rather than a phantom track.
        CHECK(decodeInputSource(-1.0f).kind == InputSourceKind::None, "negative = None");
    }

    void runInputSourceTests()
    {
        testInputSourceDecode();
    }
}
