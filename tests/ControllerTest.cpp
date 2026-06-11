// ControllerTest — unit tests for SurfaceShared.h helpers.
// Delta decode functions are JUCE-free; tested here without hardware.

#include "TestHarness.h"
#include "../src/controller/SurfaceShared.h"

namespace lockstep
{
    using namespace ctrl;

    static void testSignedMagnitudeDelta()
    {
        // CW: values 1..63 map to positive delta
        CHECK(decodeSignedMagnitudeDelta(1) == 1, "SMag delta +1");
        CHECK(decodeSignedMagnitudeDelta(63) == 63, "SMag delta +63");

        // CCW: values 65..127 map to negative delta -(v-64)
        CHECK(decodeSignedMagnitudeDelta(65) == -1, "SMag delta -1");
        CHECK(decodeSignedMagnitudeDelta(127) == -63, "SMag delta -63");
        CHECK(decodeSignedMagnitudeDelta(66) == -2, "SMag delta -2");

        // Centre and extremes: 0, 64 → no movement
        CHECK(decodeSignedMagnitudeDelta(0) == 0, "SMag centre 0 → 0");
        CHECK(decodeSignedMagnitudeDelta(64) == 0, "SMag 64 → 0 (undefined band)");
        CHECK(decodeSignedMagnitudeDelta(128) == 0, "SMag 128 → 0 (out of range)");
        CHECK(decodeSignedMagnitudeDelta(-1) == 0, "SMag -1 → 0");
    }

    static void testTwosComplementDelta()
    {
        // CW: values 1..63 map to positive delta
        CHECK(decodeTwosComplementDelta(1) == 1, "2sC delta +1");
        CHECK(decodeTwosComplementDelta(63) == 63, "2sC delta +63");

        // CCW: values 64..127 map to negative delta -(128-v)
        CHECK(decodeTwosComplementDelta(127) == -1, "2sC delta -1");
        CHECK(decodeTwosComplementDelta(65) == -63, "2sC delta -63");
        CHECK(decodeTwosComplementDelta(64) == -64, "2sC delta -64 (full negative)");

        // Boundary and zero
        CHECK(decodeTwosComplementDelta(0) == 0, "2sC 0 → 0");
        CHECK(decodeTwosComplementDelta(-1) == 0, "2sC -1 → 0");
        // v=128 is out of MIDI range but evaluates as -(128-128)=0 via the >=64 branch.
        CHECK(decodeTwosComplementDelta(128) == 0, "2sC 128 → 0 (out-of-range, evaluates -(128-128))");
        CHECK(decodeTwosComplementDelta(64) == -64, "2sC 64 → -(128-64) = -64");
    }

    static void testDeltaDecodersDiffer()
    {
        // The two encodings produce different results for the same CCW byte.
        // At ccValue=65: SMag → -1, TwosC → -63.
        CHECK(decodeSignedMagnitudeDelta(65) != decodeTwosComplementDelta(65),
              "SMag and TwosC must differ for CCW values");
        // At ccValue=127: SMag → -63, TwosC → -1.
        CHECK(decodeSignedMagnitudeDelta(127) != decodeTwosComplementDelta(127),
              "SMag and TwosC must differ for CCW values (v=127)");
    }

    void runControllerTests()
    {
        testSignedMagnitudeDelta();
        testTwosComplementDelta();
        testDeltaDecodersDiffer();
    }
}
