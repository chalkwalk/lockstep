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

    // S3: four external buses. The encoding is append-only — existing values keep
    // meaning; Ext2..Ext4 sit past the Track range (19..21).
    static void testExternalBuses()
    {
        // Golden stability: the pre-S3 values decode exactly as before.
        CHECK(decodeInputSource(0.0f).kind == InputSourceKind::None, "golden: 0 = None");
        {
            const auto e1 = decodeInputSource(1.0f);
            CHECK(e1.kind == InputSourceKind::External && e1.ext == 0,
                  "golden: legacy External(1) = Ext1 (ext 0)");
        }
        CHECK(decodeInputSource(2.0f).kind == InputSourceKind::Master, "golden: 2 = Master");
        CHECK(decodeInputSource(3.0f).track == 0, "golden: 3 = Track 0");
        CHECK(decodeInputSource(18.0f).track == 15, "golden: 18 = Track 15");

        // Ext2..Ext4 at 19..21, decoding to ext 1..3.
        for (int e = 1; e < kNumExtInputs; ++e)
        {
            const float enc = static_cast<float>(18 + e);  // 19,20,21
            const auto sel = decodeInputSource(enc);
            CHECK(sel.kind == InputSourceKind::External && sel.ext == e,
                  "ExtN decodes to External with ext = N-1");
        }

        // encode/decode round-trips for all four external buses.
        for (int e = 0; e < kNumExtInputs; ++e)
        {
            const float enc = encodeInputSource(InputSourceKind::External, 0, e);
            const auto sel = decodeInputSource(enc);
            CHECK(sel.kind == InputSourceKind::External && sel.ext == e,
                  "External(ext) round-trips through encode/decode");
        }
        CHECK(feq(encodeInputSource(InputSourceKind::External, 0, 0), 1.0f),
              "Ext1 still encodes to 1 (legacy value preserved)");

        // Full-range round-trip: every representable value survives decode->encode.
        const auto reencode = [](const InputSourceSel& s) {
            return s.kind == InputSourceKind::Track
                       ? encodeInputSource(s.kind, s.track, 0)
                       : encodeInputSource(s.kind, 0, s.ext);
        };
        for (int v = 0; v <= 18 + (kNumExtInputs - 1); ++v)
        {
            const auto sel = decodeInputSource(static_cast<float>(v));
            CHECK(feq(reencode(sel), static_cast<float>(v)),
                  "value " + juce::String(v) + " round-trips decode->encode");
        }

        // Label array is indexed by encoded value, so it must be exactly long enough.
        CHECK(static_cast<int>(kInputSourceLabels.size()) == 18 + kNumExtInputs,
              "label array size == max encoded value + 1");
        CHECK(juce::String(kInputSourceLabels[1]) == "Ext1", "label[1] = Ext1");
        CHECK(juce::String(kInputSourceLabels[19]) == "Ext2", "label[19] = Ext2");
        CHECK(juce::String(kInputSourceLabels[21]) == "Ext4", "label[21] = Ext4");
    }

    void runInputSourceTests()
    {
        testInputSourceDecode();
        testExternalBuses();
    }
}
