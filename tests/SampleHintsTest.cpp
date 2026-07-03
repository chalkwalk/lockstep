// SampleHintsTest.cpp — 4.9 filename/ACID hint parsing + detection fusion
// (dsp/SampleHints.h). Pure string/number logic; no audio.

#include "TestHarness.h"
#include "../src/dsp/SampleHints.h"

namespace lockstep
{
    void runSampleHintsTests()
    {
        // ---- Filename BPM ------------------------------------------------
        CHECK(feq((float)parseFilenameHints("drums_120bpm").bpm, 120.0f), "drums_120bpm -> 120");
        CHECK(feq((float)parseFilenameHints("bpm95_loop").bpm, 95.0f),    "bpm95_loop -> 95");
        CHECK(feq((float)parseFilenameHints("kick 128 bpm").bpm, 128.0f), "128 bpm (spaced) -> 128");
        CHECK(feq((float)parseFilenameHints("loop-95").bpm, 95.0f),       "loop-95 -> 95 (fallback)");
        CHECK(feq((float)parseFilenameHints("_174_").bpm, 174.0f),        "_174_ -> 174 (fallback)");
        CHECK(feq((float)parseFilenameHints("take3").bpm, 0.0f),          "take3 -> no bpm");
        CHECK(feq((float)parseFilenameHints("808").bpm, 0.0f),            "808 out of range -> no bpm");
        CHECK(feq((float)parseFilenameHints("120bpm_140").bpm, 120.0f),   "120bpm beats trailing 140");

        // ---- Filename key ------------------------------------------------
        {
            auto k = parseFilenameHints("Amin");
            CHECK(k.keyRoot == 9 && k.keyBrightness == kAeolian, "Amin -> 9/Aeolian");
        }
        {
            auto k = parseFilenameHints("lead_F#maj");
            CHECK(k.keyRoot == 6 && k.keyBrightness == kIonian, "F#maj -> 6/Ionian");
        }
        {
            auto k = parseFilenameHints("Cm");
            CHECK(k.keyRoot == 0 && k.keyBrightness == kAeolian, "Cm -> 0/Aeolian");
        }
        {
            auto k = parseFilenameHints("pad_Gbm");
            CHECK(k.keyRoot == 6 && k.keyBrightness == kAeolian, "Gbm -> 6/Aeolian");
        }
        {
            auto k = parseFilenameHints("bass_A");
            CHECK(k.keyRoot == -1, "bare A -> no key hint");
        }
        {
            auto k = parseFilenameHints("bmaj");
            CHECK(k.keyRoot == 11 && k.keyBrightness == kIonian, "bmaj -> 11/Ionian (B major)");
        }

        // ---- ACID metadata ----------------------------------------------
        {
            juce::StringPairArray md;
            md.set(juce::WavAudioFormat::acidTempo, "128.0");
            md.set(juce::WavAudioFormat::acidRootSet, "1");
            md.set(juce::WavAudioFormat::acidRootNote, "57");   // 57 % 12 = 9 (A)
            const auto h = parseMetadataHints(md);
            CHECK(feq((float)h.bpm, 128.0f), "ACID tempo -> 128");
            CHECK(h.keyRoot == 9, "ACID root note 57 -> pc 9");
            CHECK(!h.oneShot, "no one-shot flag -> false");
        }
        {
            juce::StringPairArray md;
            md.set(juce::WavAudioFormat::acidTempo, "170.0");
            md.set(juce::WavAudioFormat::acidOneShot, "1");
            const auto h = parseMetadataHints(md);
            CHECK(h.oneShot, "ACID one-shot flag -> true");
            // Fusion must suppress the tempo hint on a one-shot.
            const auto f = fuseAnalysis(0.0, KeyEstimate{}, h);
            CHECK(feq((float)f.bpm, 0.0f), "one-shot suppresses hint bpm");
        }

        // ---- merge: metadata beats filename ------------------------------
        {
            SampleHints meta; meta.bpm = 100.0; meta.keyRoot = 2; meta.keyBrightness = kIonian;
            SampleHints fname; fname.bpm = 90.0; fname.keyRoot = 7; fname.keyBrightness = kAeolian;
            const auto m = mergeHints(meta, fname);
            CHECK(feq((float)m.bpm, 100.0f), "merge bpm: metadata wins");
            CHECK(m.keyRoot == 2 && m.keyBrightness == kIonian, "merge key: metadata wins");
        }
        {
            SampleHints meta;   // empty
            SampleHints fname; fname.bpm = 90.0; fname.keyRoot = 7;
            const auto m = mergeHints(meta, fname);
            CHECK(feq((float)m.bpm, 90.0f), "merge bpm: filename fills gap");
            CHECK(m.keyRoot == 7, "merge key: filename fills gap");
        }

        // ---- fusion: bpm --------------------------------------------------
        {
            // Detection octave-folded to 87; hint 174 resolves it.
            SampleHints h; h.bpm = 174.0;
            const auto f = fuseAnalysis(87.0, KeyEstimate{}, h);
            CHECK(feq((float)f.bpm, 174.0f), "octave-fold: detected 87 + hint 174 -> 174");
        }
        {
            // Detection and hint disagree, not an octave relation -> keep detected.
            SampleHints h; h.bpm = 95.0;
            const auto f = fuseAnalysis(120.0, KeyEstimate{}, h);
            CHECK(feq((float)f.bpm, 120.0f), "disagreement: detection wins (120)");
        }
        {
            // Detection unknown -> hint fills the gap.
            SampleHints h; h.bpm = 95.0;
            const auto f = fuseAnalysis(0.0, KeyEstimate{}, h);
            CHECK(feq((float)f.bpm, 95.0f), "gap-fill: detected 0 + hint 95 -> 95");
        }

        // ---- fusion: key + tuning ----------------------------------------
        {
            KeyEstimate ke; ke.root = 5; ke.brightness = kDorian; ke.tuningCents = 12.5;
            SampleHints h; h.keyRoot = 0; h.keyBrightness = kIonian;   // should lose
            const auto f = fuseAnalysis(120.0, ke, h);
            CHECK(f.keyRoot == 5 && f.keyBrightness == kDorian, "detected key beats hint key");
            CHECK(feq((float)f.tuningCents, 12.5f), "tuning passes through from detection");
        }
        {
            KeyEstimate ke;   // unknown key
            SampleHints h; h.keyRoot = 3; h.keyBrightness = kAeolian;
            const auto f = fuseAnalysis(120.0, ke, h);
            CHECK(f.keyRoot == 3, "no detected key -> hint key fills in");
        }
    }
}
