// MachineDspTest -- smoke tests for every IMachine and IEffect concrete type.
//
// For synthesis machines (VA, FM, DrumSynth): construct, prepare, fire a
// note-on, render N blocks, assert non-silence + no NaN/Inf; then note-off
// with a short release time set, render release tail, assert silence.
//
// For sample-playing machines (Sampler, Slicer): no sample is loaded so audio
// is silent -- we just assert no crash and no NaN.
//
// For IEffect: drive with a non-zero input, assert no NaN/Inf.

#include "TestHarness.h"
#include "../src/machine/VAMachine.h"
#include "../src/machine/FMMachine.h"
#include "../src/machine/DrumSynthMachine.h"
#include "../src/machine/SamplerMachine.h"
#include "../src/machine/SlicerMachine.h"
#include "../src/machine/SamplePool.h"
#include "../src/machine/IEffect.h"
#include "../src/machine/EffectFactory.h"
#include "../src/dsp/Oversampler2x.h"
#include <cmath>

namespace lockstep
{
    // -----------------------------------------------------------------------
    // Helpers

    static bool hasNaNOrInf(const juce::AudioBuffer<float>& buf)
    {
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                if (!std::isfinite(buf.getSample(ch, i)))
                    return true;
            }
        }
        return false;
    }

    static float blockRms(const juce::AudioBuffer<float>& buf)
    {
        double sum = 0.0;
        int n = 0;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = buf.getSample(ch, i);
                sum += static_cast<double>(v) * static_cast<double>(v);
                ++n;
            }
        }
        return (n > 0) ? static_cast<float>(std::sqrt(sum / static_cast<double>(n))) : 0.0f;
    }

    // Build a default ParamFrame (all defaults) for a machine.
    static ParamFrame defaultFrame(IMachine& m)
    {
        ParamFrame f(static_cast<size_t>(m.numParams()));
        for (int i = 0; i < m.numParams(); ++i)
            f[static_cast<size_t>(i)] = m.paramSpec(i).defaultValue;
        return f;
    }

    // Set a slot by id if found, otherwise return false.
    static bool setSlot(IMachine& m, ParamFrame& frame, const char* id, float val)
    {
        const int slot = m.slotForId(id);
        if (slot < 0) return false;
        frame[static_cast<size_t>(slot)] = val;
        return true;
    }

    static void renderBlock(IMachine& m, juce::MidiBuffer& midi,
                            const ParamFrame& frame,
                            juce::AudioBuffer<float>& buf)
    {
        buf.clear();
        m.process(midi, frame, buf);
        midi.clear();
    }

    // -----------------------------------------------------------------------
    // Synthesis machine smoke test.
    // releaseSlotId: param id whose minimum value gives a short release (e.g.
    // "va_amp_r"). If not found the release-tail silence check is skipped.
    // For AHD drums pass the decay id; the check verifies the decay clears.
    static void smokeTestSynth(IMachine& m, int noteNum,
                               int activeBlocks, int releaseBlocks,
                               float silenceThreshold,
                               const char* releaseSlotId,
                               const char* name)
    {
        constexpr int kBlockSize = 256;
        constexpr double kSR = 48000.0;

        m.prepare(kSR, kBlockSize);
        m.reset();

        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        ParamFrame frame = defaultFrame(m);

        // Force short release so we don't need thousands of blocks.
        const int releaseSlot = m.slotForId(releaseSlotId);
        const bool hasRelease = (releaseSlot >= 0);
        if (hasRelease)
            frame[static_cast<size_t>(releaseSlot)] = m.paramSpec(releaseSlot).minValue;

        // Note-on -- first block.
        midi.addEvent(juce::MidiMessage::noteOn(1, noteNum, static_cast<juce::uint8>(100)), 0);
        renderBlock(m, midi, frame, buf);
        CHECK(!hasNaNOrInf(buf),
              juce::String(name) + ": NaN/Inf in first block after note-on");

        // Active phase.
        float maxRms = blockRms(buf);
        for (int b = 1; b < activeBlocks; ++b)
        {
            renderBlock(m, midi, frame, buf);
            CHECK(!hasNaNOrInf(buf),
                  juce::String(name) + ": NaN/Inf in active block " + juce::String(b));
            maxRms = std::max(maxRms, blockRms(buf));
        }
        CHECK(maxRms > 1e-4f,
              juce::String(name) + ": no audio after note-on (RMS=" + juce::String(maxRms) + ")");

        // Note-off.
        midi.addEvent(juce::MidiMessage::noteOff(1, noteNum), 0);
        renderBlock(m, midi, frame, buf);
        CHECK(!hasNaNOrInf(buf),
              juce::String(name) + ": NaN/Inf in first block after note-off");

        // Release tail.
        for (int b = 1; b < releaseBlocks; ++b)
        {
            renderBlock(m, midi, frame, buf);
            CHECK(!hasNaNOrInf(buf),
                  juce::String(name) + ": NaN/Inf in release block " + juce::String(b));
        }

        if (hasRelease)
        {
            const float finalRms = blockRms(buf);
            CHECK(finalRms < silenceThreshold,
                  juce::String(name) + ": envelope did not reach silence after release tail (RMS=" + juce::String(finalRms) + ", threshold=" + juce::String(silenceThreshold) + ")");
        }
    }

    // Smoke test for sample-playing machines: no crash, no NaN (silent without a loaded sample).
    static void smokeTestSampleMachine(IMachine& m, const char* name)
    {
        constexpr int kBlockSize = 256;
        constexpr double kSR = 48000.0;

        m.prepare(kSR, kBlockSize);
        m.reset();

        juce::AudioBuffer<float> buf(2, kBlockSize);
        juce::MidiBuffer midi;
        ParamFrame frame = defaultFrame(m);

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        renderBlock(m, midi, frame, buf);
        CHECK(!hasNaNOrInf(buf),
              juce::String(name) + ": NaN/Inf after note-on (no sample loaded)");

        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        for (int b = 0; b < 10; ++b)
            renderBlock(m, midi, frame, buf);
        CHECK(!hasNaNOrInf(buf),
              juce::String(name) + ": NaN/Inf after note-off tail (no sample loaded)");
    }

    // -----------------------------------------------------------------------
    // Block-size invariance: RMS with 64 vs 512 sample blocks must agree
    // within 20 dB (~10x ratio) over the same number of rendered samples.
    static void blockSizeInvariance(IMachine& m, int noteNum,
                                    int activeBlocks64, const char* name)
    {
        auto renderRms = [&](int blockSize) -> float {
            m.prepare(48000.0, blockSize);
            m.reset();
            juce::AudioBuffer<float> buf(2, blockSize);
            juce::MidiBuffer midi;
            ParamFrame frame = defaultFrame(m);

            midi.addEvent(juce::MidiMessage::noteOn(1, noteNum,
                                                    static_cast<juce::uint8>(100)),
                          0);
            double sumSq = 0.0;
            int total = 0;
            for (int b = 0; b < activeBlocks64; ++b)
            {
                buf.clear();
                m.process(midi, frame, buf);
                midi.clear();
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                {
                    for (int i = 0; i < blockSize; ++i)
                    {
                        const float v = buf.getSample(ch, i);
                        sumSq += static_cast<double>(v) * static_cast<double>(v);
                    }
                }
                total += blockSize * buf.getNumChannels();
            }
            return (total > 0)
                       ? static_cast<float>(std::sqrt(sumSq / static_cast<double>(total)))
                       : 0.0f;
        };

        const float rms64 = renderRms(64);
        const float rms512 = renderRms(512);

        CHECK(rms64 > 1e-5f,
              juce::String(name) + ": block-size 64 is silent");
        CHECK(rms512 > 1e-5f,
              juce::String(name) + ": block-size 512 is silent");

        const float ratio = (rms512 > 0.0f) ? (rms64 / rms512) : 0.0f;
        CHECK(ratio > 0.1f && ratio < 10.0f,
              juce::String(name) + ": block-size RMS ratio " + juce::String(ratio) + " out of tolerance (>10x difference between 64 and 512 samples)");
    }

    // -----------------------------------------------------------------------
    // IEffect smoke test: fill buffer with a sine, process, assert no NaN.
    // Retained as a named helper for ad-hoc use; the catalogue loop in
    // runMachineDspTests() does the same via inline code for all 13 effects.
    [[maybe_unused]] static void smokeTestEffect(IEffect& fx, const char* name)
    {
        constexpr int kBlockSize = 256;
        constexpr double kSR = 48000.0;

        fx.prepare(kSR, kBlockSize);
        fx.reset();

        juce::AudioBuffer<float> buf(2, kBlockSize);
        ParamFrame frame(static_cast<size_t>(fx.numParams()));
        for (int i = 0; i < fx.numParams(); ++i)
            frame[static_cast<size_t>(i)] = fx.paramSpec(i).defaultValue;

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < kBlockSize; ++i)
                buf.setSample(ch, i, std::sin(static_cast<float>(i) * 0.1f) * 0.5f);
        }

        fx.process(buf, kBlockSize, frame);
        CHECK(!hasNaNOrInf(buf),
              juce::String(name) + ": NaN/Inf after processing a sine signal");
    }

    // -----------------------------------------------------------------------
    // VAMachine envelope golden.
    // Sets A=10ms, D=50ms, S=0.5, R=20ms at 48 kHz and verifies:
    //   - after attack (>=10ms): peak is non-trivial
    //   - after release tail (>=60ms): near silence
    static void vaEnvelopeGolden()
    {
        VAMachine va;
        va.prepare(48000.0, 64);

        ParamFrame frame = defaultFrame(va);

        const int slotA = va.slotForId("va_amp_a");
        const int slotD = va.slotForId("va_amp_d");
        const int slotS = va.slotForId("va_amp_s");
        const int slotR = va.slotForId("va_amp_r");
        const int slotLv = va.slotForId("va_level");

        if (slotA < 0 || slotD < 0 || slotS < 0 || slotR < 0 || slotLv < 0)
        {
            juce::Logger::writeToLog("vaEnvelopeGolden: amp slot ids not found - skipping");
            return;
        }

        // A=10ms, D=50ms, S=0.5, R=20ms (short release so test finishes quickly)
        auto norm = [](float val, const ParamSpec& ps) -> float {
            const float range = ps.maxValue - ps.minValue;
            return (range > 0.0f) ? (val - ps.minValue) / range : 0.0f;
        };

        frame[static_cast<size_t>(slotA)] = norm(10.0f, va.paramSpec(slotA));
        frame[static_cast<size_t>(slotD)] = norm(50.0f, va.paramSpec(slotD));
        frame[static_cast<size_t>(slotS)] = 0.5f;
        frame[static_cast<size_t>(slotR)] = norm(20.0f, va.paramSpec(slotR));
        frame[static_cast<size_t>(slotLv)] = 1.0f;

        juce::AudioBuffer<float> buf(2, 64);
        juce::MidiBuffer midi;

        // Note-on; render ~15ms (11 blocks) to clear the attack.
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        for (int b = 0; b < 11; ++b)
        {
            buf.clear();
            va.process(midi, frame, buf);
            midi.clear();
        }
        CHECK(!hasNaNOrInf(buf), "VA golden: NaN after attack");

        float peakAfterAttack = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
                peakAfterAttack = std::max(peakAfterAttack, std::abs(buf.getSample(ch, i)));
        }
        // Threshold tracks the shared machine loudness reference (147ca30): the
        // VA was deliberately calibrated down to sit alongside drum/FM, so a
        // level=1.0 / vel=100 note now peaks ~0.15, not the pre-calibration ~0.3.
        // This asserts the envelope is clearly firing (matches the FM golden's
        // 0.1 floor below), not the old absolute level.
        CHECK(peakAfterAttack > 0.1f,
              "VA golden: level too low after attack -- envelope may not be firing");

        // Render ~60ms more (45 blocks) to clear decay and settle at sustain.
        for (int b = 0; b < 45; ++b)
        {
            buf.clear();
            va.process(midi, frame, buf);
            midi.clear();
        }
        CHECK(!hasNaNOrInf(buf), "VA golden: NaN after decay");

        // Note-off; render 50ms (38 blocks) release tail -- 20ms release should clear.
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        for (int b = 0; b < 38; ++b)
        {
            buf.clear();
            va.process(midi, frame, buf);
            midi.clear();
        }
        CHECK(!hasNaNOrInf(buf), "VA golden: NaN after release tail");

        const float finalRms = blockRms(buf);
        CHECK(finalRms < 1e-2f,
              "VA golden: did not reach near-silence after 50ms release tail (RMS=" + juce::String(finalRms) + ")");
    }

    // -----------------------------------------------------------------------
    // FM envelope golden.
    // fm_macro_atk / fm_macro_rel range 0..3 (normalized: 0=fastest, 3=slowest).
    // Set both to 0 for near-instant attack and release.
    static void fmEnvelopeGolden()
    {
        FMMachine fm;
        fm.prepare(48000.0, 64);

        ParamFrame frame = defaultFrame(fm);

        const int slotA = fm.slotForId("fm_macro_atk");
        const int slotR = fm.slotForId("fm_macro_rel");

        if (slotA < 0 || slotR < 0)
        {
            juce::Logger::writeToLog("fmEnvelopeGolden: macro slot ids not found - skipping");
            return;
        }

        // 0 = minimum (fastest attack/release).
        frame[static_cast<size_t>(slotA)] = fm.paramSpec(slotA).minValue;
        frame[static_cast<size_t>(slotR)] = fm.paramSpec(slotR).minValue;

        // Set op1 release to minimum so the voice silences quickly after note-off.
        setSlot(fm, frame, "fm_rel_1", fm.paramSpec(fm.slotForId("fm_rel_1")).minValue);
        setSlot(fm, frame, "fm_rel_2", fm.paramSpec(fm.slotForId("fm_rel_2")).minValue);
        setSlot(fm, frame, "fm_rel_3", fm.paramSpec(fm.slotForId("fm_rel_3")).minValue);
        setSlot(fm, frame, "fm_rel_4", fm.paramSpec(fm.slotForId("fm_rel_4")).minValue);

        juce::AudioBuffer<float> buf(2, 64);
        juce::MidiBuffer midi;

        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        // Render ~15ms.
        for (int b = 0; b < 11; ++b)
        {
            buf.clear();
            fm.process(midi, frame, buf);
            midi.clear();
        }
        CHECK(!hasNaNOrInf(buf), "FM golden: NaN after attack");

        float peakAfterAttack = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            for (int i = 0; i < buf.getNumSamples(); ++i)
                peakAfterAttack = std::max(peakAfterAttack, std::abs(buf.getSample(ch, i)));
        }
        CHECK(peakAfterAttack > 0.1f,
              "FM golden: level too low after attack -- envelope may not be firing");

        // Note-off; render 50ms (38 blocks) release tail.
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        for (int b = 0; b < 38; ++b)
        {
            buf.clear();
            fm.process(midi, frame, buf);
            midi.clear();
        }
        CHECK(!hasNaNOrInf(buf), "FM golden: NaN after release tail");

        const float finalRms = blockRms(buf);
        CHECK(finalRms < 1e-2f,
              "FM golden: did not reach near-silence after 50ms release tail (RMS=" + juce::String(finalRms) + ")");
    }

    // -----------------------------------------------------------------------
    // Retrig click metric: fires note-on, renders ~50 ms to reach a stable
    // amplitude, then fires note-on again on the same pitch and measures the
    // RMS amplitude ratio across the retrig boundary.
    //
    // The ghost-fade mechanism preserves amplitude continuity at retrig: the
    // block-before and block-after RMS should be within a 3× window.  A hard
    // cut-to-silence or a sudden amplitude spike would blow this ratio.
    //
    // This metric avoids per-sample delta comparisons which are confounded by
    // the polyBlep oscillator's natural wrap-around discontinuity (max |Δ| of
    // a 2-osc saw can legitimately exceed ±4 on the cycle boundary — that is
    // NOT a retrig artifact).
    static void testRetrigClickMetrics()
    {
        constexpr int kBlockSize = 64;
        constexpr double kSR = 48000.0;

        auto runRetrigRatioTest = [&](IMachine& m, const ParamFrame& frame,
                                      const char* name) {
            m.prepare(kSR, kBlockSize);
            m.reset();

            juce::AudioBuffer<float> buf(2, kBlockSize);
            juce::MidiBuffer midi;

            // Note-on; render ~50 ms (37 blocks).
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0);
            for (int b = 0; b < 36; ++b)
                renderBlock(m, midi, frame, buf);

            // Block 37: capture RMS just before retrig.
            renderBlock(m, midi, frame, buf);
            const float rmsBefore = blockRms(buf);

            CHECK(!hasNaNOrInf(buf),
                  juce::String(name) + " retrig: NaN/Inf before retrig event");

            // Retrig: same note.
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)), 0);
            renderBlock(m, midi, frame, buf);
            const float rmsRetrig = blockRms(buf);

            CHECK(!hasNaNOrInf(buf),
                  juce::String(name) + " retrig: NaN/Inf in retrig block");

            juce::Logger::writeToLog(juce::String(name) + " retrig RMS before=" +
                                     juce::String(rmsBefore, 6) + " retrig=" +
                                     juce::String(rmsRetrig, 6));

            // Both blocks should be non-silent (the voice is still sounding).
            CHECK(rmsBefore > 1e-4f,
                  juce::String(name) + " retrig: no audio before retrig (precondition)");
            CHECK(rmsRetrig > 1e-4f,
                  juce::String(name) + " retrig: audio silenced at retrig boundary "
                  "-- ghost-fade should maintain amplitude continuity");

            // Ratio: retrig block vs before block within a 3× window.
            // A ghost-fade keeps the ratio near 1.0; a hard cut would give ~0.
            const float ratio = rmsRetrig / rmsBefore;
            CHECK(ratio > 1.0f / 3.0f && ratio < 3.0f,
                  juce::String(name) + " retrig: amplitude discontinuity at retrig boundary "
                  "(ratio=" + juce::String(ratio, 3) + ") -- ghost-fade not working");
        };

        // VA in RETRIG mode: 1.5 ms ghost-fade (VAMachine.cpp ~:695).
        {
            VAMachine va;
            ParamFrame frame = defaultFrame(va);
            setSlot(va, frame, "va_voice_mode", 0.0f);  // MONO
            setSlot(va, frame, "va_retrig", 1.0f);       // RETRIG
            setSlot(va, frame, "va_level", 1.0f);
            runRetrigRatioTest(va, frame, "VA");
        }

        // FM in RETRIG mode: 1.5 ms ghost-fade (FMMachine.cpp ~:399).
        {
            FMMachine fm;
            ParamFrame frame = defaultFrame(fm);
            setSlot(fm, frame, "fm_voice_mode", 0.0f);  // MONO
            setSlot(fm, frame, "fm_retrig", 1.0f);       // RETRIG
            setSlot(fm, frame, "fm_level", 1.0f);
            runRetrigRatioTest(fm, frame, "FM");
        }

        // DrumSynth: AHD always-retrig.
        {
            DrumSynthMachine ds;
            ParamFrame frame = defaultFrame(ds);
            setSlot(ds, frame, "drum_level", 1.0f);
            runRetrigRatioTest(ds, frame, "DrumSynth");
        }

        // Sample-playing machines have no sample loaded → silence; skip.
        juce::Logger::writeToLog("retrig click: Sampler/Slicer skipped (no sample loaded)");
    }

    // -----------------------------------------------------------------------
    // VA legato golden.
    // Verifies: (a) a second note pressed while first is held produces audio
    // without hard-clicking, (b) releasing the first note does NOT kill the
    // second (MonoGate.Ignore path), and (c) the voice silences after both
    // notes are released.
    static void vaLegatoGolden()
    {
        VAMachine va;
        va.prepare(48000.0, 64);

        ParamFrame frame = defaultFrame(va);

        // Explicit mono+legato (defaults, but pin them so the test is self-describing).
        if (!setSlot(va, frame, "va_voice_mode", 0.0f) ||
            !setSlot(va, frame, "va_retrig", 0.0f))
        {
            juce::Logger::writeToLog("vaLegatoGolden: voice_mode/retrig ids not found - skipping");
            return;
        }

        const int slotA  = va.slotForId("va_amp_a");
        const int slotD  = va.slotForId("va_amp_d");
        const int slotS  = va.slotForId("va_amp_s");
        const int slotR  = va.slotForId("va_amp_r");
        const int slotLv = va.slotForId("va_level");
        if (slotA < 0 || slotD < 0 || slotS < 0 || slotR < 0 || slotLv < 0)
        {
            juce::Logger::writeToLog("vaLegatoGolden: amp slot ids not found - skipping");
            return;
        }

        auto norm = [](float val, const ParamSpec& ps) -> float {
            const float range = ps.maxValue - ps.minValue;
            return (range > 0.0f) ? (val - ps.minValue) / range : 0.0f;
        };
        frame[static_cast<size_t>(slotA)]  = norm(10.0f, va.paramSpec(slotA));
        frame[static_cast<size_t>(slotD)]  = norm(50.0f, va.paramSpec(slotD));
        frame[static_cast<size_t>(slotS)]  = 0.7f;
        frame[static_cast<size_t>(slotR)]  = norm(20.0f, va.paramSpec(slotR));
        frame[static_cast<size_t>(slotLv)] = 1.0f;

        juce::AudioBuffer<float> buf(2, 64);
        juce::MidiBuffer midi;

        // Note A (C4=60) on; render ~100 ms (75 blocks).
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        float lastSampleBeforeTransition = 0.0f;
        for (int b = 0; b < 75; ++b)
        {
            renderBlock(va, midi, frame, buf);
            if (b == 74)
                lastSampleBeforeTransition = buf.getSample(0, 63);
        }
        CHECK(!hasNaNOrInf(buf), "VA legato: NaN after note A sustain");

        // Note B (E4=64) on while A is still held — legato slide, no note A off.
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
        renderBlock(va, midi, frame, buf);  // transition block
        CHECK(!hasNaNOrInf(buf), "VA legato: NaN at A->B transition block");

        const float firstSampleAfterTransition = buf.getSample(0, 0);
        const float transitionDelta = std::abs(firstSampleAfterTransition - lastSampleBeforeTransition);
        CHECK(transitionDelta < 0.5f,
              "VA legato: hard click at A->B transition (delta=" + juce::String(transitionDelta, 6) + ")");

        // Render ~100 ms more; note B should be sounding throughout.
        float maxRmsHeld = 0.0f;
        for (int b = 0; b < 74; ++b)
        {
            renderBlock(va, midi, frame, buf);
            maxRmsHeld = std::max(maxRmsHeld, blockRms(buf));
        }
        CHECK(maxRmsHeld > 1e-3f,
              "VA legato: note B not sounding after legato transition (RMS=" + juce::String(maxRmsHeld) + ")");

        // Note A off — B is the active note, so MonoGate returns Ignore.
        // Voice must keep playing (the bug this pins: note A off incorrectly killing B).
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        float maxRmsAfterAOff = 0.0f;
        for (int b = 0; b < 15; ++b)
        {
            renderBlock(va, midi, frame, buf);
            maxRmsAfterAOff = std::max(maxRmsAfterAOff, blockRms(buf));
        }
        CHECK(maxRmsAfterAOff > 1e-3f,
              "VA legato: note B killed by note A off (RMS=" + juce::String(maxRmsAfterAOff) + ") "
              "-- MonoGate Ignore path not respected");

        // Note B off — envelope releases; silence expected after ~30 ms.
        midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
        for (int b = 0; b < 38; ++b)
            renderBlock(va, midi, frame, buf);
        CHECK(!hasNaNOrInf(buf), "VA legato: NaN after release tail");
        const float finalRms = blockRms(buf);
        CHECK(finalRms < 1e-2f,
              "VA legato: not silent after release tail (RMS=" + juce::String(finalRms) + ")");
    }

    // -----------------------------------------------------------------------
    // FM legato golden — mirrors vaLegatoGolden() for FMMachine.
    static void fmLegatoGolden()
    {
        FMMachine fm;
        fm.prepare(48000.0, 64);

        ParamFrame frame = defaultFrame(fm);

        if (!setSlot(fm, frame, "fm_voice_mode", 0.0f) ||
            !setSlot(fm, frame, "fm_retrig", 0.0f))
        {
            juce::Logger::writeToLog("fmLegatoGolden: voice_mode/retrig ids not found - skipping");
            return;
        }

        // Fastest attack+release macro; minimum per-op release so the tail is short.
        const int slotAtk = fm.slotForId("fm_macro_atk");
        const int slotRel = fm.slotForId("fm_macro_rel");
        if (slotAtk < 0 || slotRel < 0)
        {
            juce::Logger::writeToLog("fmLegatoGolden: macro slot ids not found - skipping");
            return;
        }
        frame[static_cast<size_t>(slotAtk)] = fm.paramSpec(slotAtk).minValue;
        frame[static_cast<size_t>(slotRel)] = fm.paramSpec(slotRel).minValue;

        for (const char* relId : { "fm_rel_1", "fm_rel_2", "fm_rel_3", "fm_rel_4" })
        {
            const int s = fm.slotForId(relId);
            if (s >= 0)
                frame[static_cast<size_t>(s)] = fm.paramSpec(s).minValue;
        }

        juce::AudioBuffer<float> buf(2, 64);
        juce::MidiBuffer midi;

        // Note A (C4=60) on; render ~100 ms.
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
        float lastSampleBeforeTransition = 0.0f;
        for (int b = 0; b < 75; ++b)
        {
            renderBlock(fm, midi, frame, buf);
            if (b == 74)
                lastSampleBeforeTransition = buf.getSample(0, 63);
        }
        CHECK(!hasNaNOrInf(buf), "FM legato: NaN after note A sustain");

        // Note B (E4=64) on while A is still held.
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(100)), 0);
        renderBlock(fm, midi, frame, buf);
        CHECK(!hasNaNOrInf(buf), "FM legato: NaN at A->B transition block");

        const float firstSampleAfterTransition = buf.getSample(0, 0);
        const float transitionDelta = std::abs(firstSampleAfterTransition - lastSampleBeforeTransition);
        CHECK(transitionDelta < 0.5f,
              "FM legato: hard click at A->B transition (delta=" + juce::String(transitionDelta, 6) + ")");

        float maxRmsHeld = 0.0f;
        for (int b = 0; b < 74; ++b)
        {
            renderBlock(fm, midi, frame, buf);
            maxRmsHeld = std::max(maxRmsHeld, blockRms(buf));
        }
        CHECK(maxRmsHeld > 1e-3f,
              "FM legato: note B not sounding after legato transition (RMS=" + juce::String(maxRmsHeld) + ")");

        // Note A off — B is active, MonoGate returns Ignore; voice must keep playing.
        midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
        float maxRmsAfterAOff = 0.0f;
        for (int b = 0; b < 15; ++b)
        {
            renderBlock(fm, midi, frame, buf);
            maxRmsAfterAOff = std::max(maxRmsAfterAOff, blockRms(buf));
        }
        CHECK(maxRmsAfterAOff > 1e-3f,
              "FM legato: note B killed by note A off (RMS=" + juce::String(maxRmsAfterAOff) + ") "
              "-- MonoGate Ignore path not respected");

        // Note B off — release tail.
        midi.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
        for (int b = 0; b < 38; ++b)
            renderBlock(fm, midi, frame, buf);
        CHECK(!hasNaNOrInf(buf), "FM legato: NaN after release tail");
        const float finalRms = blockRms(buf);
        CHECK(finalRms < 1e-2f,
              "FM legato: not silent after release tail (RMS=" + juce::String(finalRms) + ")");
    }

    // -----------------------------------------------------------------------

    void runMachineDspTests()
    {
        // --- VAMachine ---
        {
            VAMachine va;
            smokeTestSynth(va, 60, /*activeBlocks=*/20, /*releaseBlocks=*/20,
                           /*silenceThreshold=*/1e-3f, "va_amp_r", "VAMachine");
        }
        {
            VAMachine va;
            blockSizeInvariance(va, 60, /*activeBlocks=*/10, "VAMachine");
        }
        vaEnvelopeGolden();
        vaLegatoGolden();
        testRetrigClickMetrics();

        // --- FMMachine ---
        {
            FMMachine fm;
            smokeTestSynth(fm, 60, 20, 20, 1e-3f, "fm_rel_1", "FMMachine");
        }
        {
            FMMachine fm;
            blockSizeInvariance(fm, 60, 10, "FMMachine");
        }
        fmEnvelopeGolden();
        fmLegatoGolden();

        // --- DrumSynthMachine ---
        // Drums are AHD; after note-off they just complete the decay naturally.
        // Use the decay id so smokeTestSynth sets a short decay.
        {
            DrumSynthMachine ds;
            smokeTestSynth(ds, 60, 20, 20, 1e-3f, "drum_decay", "DrumSynthMachine");
        }
        {
            DrumSynthMachine ds;
            blockSizeInvariance(ds, 60, 10, "DrumSynthMachine");
        }

        // --- Sample-playing machines (no sample loaded -- smoke only) ---
        {
            SamplePool pool;
            SamplerMachine sampler(pool);
            smokeTestSampleMachine(sampler, "SamplerMachine");
        }
        {
            SamplePool pool;
            SlicerMachine slicer(pool);
            smokeTestSampleMachine(slicer, "SlicerMachine");
        }

        // --- IEffect catalogue — all 13 effects ---
        // Phase 1: for every catalogued effect, smoke-test at default params then
        // at per-param min and max.
        {
            constexpr int kBlockSize = 256;
            constexpr double kSR = 48000.0;

            auto makeSine = [](juce::AudioBuffer<float>& buf) {
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                        buf.setSample(ch, i, std::sin(static_cast<float>(i) * 0.05f) * 0.35f);
            };

            const auto catalogue = availableEffects();
            for (const auto& info : catalogue)
            {
                const std::string& id = info.id;

                // Reusable source — fresh sine for every process() call so wet-only
                // effects (mix=1) don't recirculate their own output as the next input.
                juce::AudioBuffer<float> sineSource(2, kBlockSize);
                makeSine(sineSource);

                // -- Smoke at default params --
                {
                    auto fx = makeEffectForId(id);
                    CHECK(fx != nullptr, "makeEffectForId returned nullptr for id=" + juce::String(id));
                    if (!fx) continue;

                    fx->prepare(kSR, kBlockSize);
                    fx->reset();

                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;

                    juce::AudioBuffer<float> buf(2, kBlockSize);

                    // 10 blocks with fresh sine each iteration (let smoothers settle).
                    for (int b = 0; b < 10; ++b)
                    {
                        buf.makeCopyOf(sineSource);
                        fx->process(buf, kBlockSize, frame);
                    }

                    CHECK(!hasNaNOrInf(buf), "effect smoke (default): NaN/Inf for " + juce::String(id));
                    // delayhq default delay is 1/4 note (24000 samples, ~94 blocks at 256) —
                    // silence on the first 10 blocks is expected; Phase 2 covers it explicitly.
                    if (id != "lockstep.delayhq.v1")
                        CHECK(blockRms(buf) > 1e-6f, "effect smoke (default): silent output for " + juce::String(id));
                }

                // -- Stress at min params --
                {
                    auto fx = makeEffectForId(id);
                    if (!fx) continue;
                    fx->prepare(kSR, kBlockSize);
                    fx->reset();

                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).minValue;

                    juce::AudioBuffer<float> buf(2, kBlockSize);
                    for (int b = 0; b < 20; ++b)
                    {
                        buf.makeCopyOf(sineSource);
                        fx->process(buf, kBlockSize, frame);
                    }

                    CHECK(!hasNaNOrInf(buf), "effect stress (min): NaN/Inf for " + juce::String(id));
                }

                // -- Stress at max params --
                {
                    auto fx = makeEffectForId(id);
                    if (!fx) continue;
                    fx->prepare(kSR, kBlockSize);
                    fx->reset();

                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).maxValue;

                    juce::AudioBuffer<float> buf(2, kBlockSize);
                    for (int b = 0; b < 20; ++b)
                    {
                        buf.makeCopyOf(sineSource);
                        fx->process(buf, kBlockSize, frame);
                    }

                    CHECK(!hasNaNOrInf(buf), "effect stress (max): NaN/Inf for " + juce::String(id));
                }
            }
        }

        // Phase 2: effect-specific assertions for the 9 new effects.
        {
            constexpr int kBlockSize = 256;
            constexpr double kSR = 48000.0;

            // Helper: approx-passthrough check (max abs diff < 1e-2).
            auto checkPassthrough = [&](IEffect& fx, const ParamFrame& frame, const char* label) {
                fx.prepare(kSR, kBlockSize);
                fx.reset();
                juce::AudioBuffer<float> dry(2, kBlockSize), wet(2, kBlockSize);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kBlockSize; ++i)
                    {
                        const float v = std::sin(static_cast<float>(i) * 0.05f) * 0.35f;
                        dry.setSample(ch, i, v);
                        wet.setSample(ch, i, v);
                    }
                // Settle smoothers first.
                for (int b = 0; b < 10; ++b)
                    fx.process(wet, kBlockSize, frame);
                // Re-load dry and measure diff.
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kBlockSize; ++i)
                        wet.setSample(ch, i, dry.getSample(ch, i));
                fx.process(wet, kBlockSize, frame);
                float maxDiff = 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kBlockSize; ++i)
                        maxDiff = std::max(maxDiff, std::abs(wet.getSample(ch, i) - dry.getSample(ch, i)));
                CHECK(maxDiff < 1e-2f,
                      juce::String(label) + " passthrough: max diff=" + juce::String(maxDiff, 6) +
                      " (expected < 0.01 at neutral settings)");
            };

            // tilt EQ: tilt=0, gain=0 → passthrough.
            {
                auto fx = makeEffectForId("lockstep.tilteq.v1");
                if (fx)
                {
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p) frame[static_cast<size_t>(p)] = 0.0f;
                    checkPassthrough(*fx, frame, "tilteq tilt=0/gain=0");
                }
            }

            // tilt EQ direction: positive tilt must BRIGHTEN (boost highs, cut lows).
            // Regression guard for the inverted-tilt bug.
            {
                auto fx = makeEffectForId("lockstep.tilteq.v1");
                if (fx)
                {
                    constexpr double sr = 44100.0;
                    constexpr int N = 4096;
                    // out/in RMS ratio for a pure tone at `freq` with tilt=+1.
                    auto ratio = [&](double freq) {
                        fx->prepare(sr, N);  // reset filter + smoothing state
                        juce::AudioBuffer<float> buf(2, N);
                        for (int n = 0; n < N; ++n)
                        {
                            const float s = static_cast<float>(
                                std::sin(2.0 * juce::MathConstants<double>::pi * freq
                                         * static_cast<double>(n) / sr));
                            buf.setSample(0, n, s);
                            buf.setSample(1, n, s);
                        }
                        ParamFrame frame = { 1.0f, 0.0f };  // tilt=+1, gain=0 dB
                        fx->process(buf, N, frame);
                        // Measure over the second half (after the 5 ms smoothing settles).
                        double outSq = 0.0, inSq = 0.0;
                        for (int n = N / 2; n < N; ++n)
                        {
                            const double in = std::sin(2.0 * juce::MathConstants<double>::pi * freq
                                                       * static_cast<double>(n) / sr);
                            outSq += static_cast<double>(buf.getSample(0, n))
                                     * static_cast<double>(buf.getSample(0, n));
                            inSq += in * in;
                        }
                        return std::sqrt(outSq / inSq);
                    };
                    const double lowRatio  = ratio(100.0);
                    const double highRatio = ratio(6000.0);
                    CHECK(highRatio > 1.05, "tilt=+1 boosts highs (ratio "
                          + juce::String(highRatio, 3) + " > 1)");
                    CHECK(lowRatio < 0.95, "tilt=+1 cuts lows (ratio "
                          + juce::String(lowRatio, 3) + " < 1)");
                    CHECK(highRatio > lowRatio, "tilt=+1: highs louder than lows");
                }
            }

            // Bus comp: gain reduction is wired (attack/release envelope functional).
            // A loud tone above threshold must come out quieter; a quiet tone passes.
            // Guards against the envelope-ignored-its-params regression.
            {
                auto fx = makeEffectForId("lockstep.buscomp.v1");
                if (fx)
                {
                    constexpr double sr = 44100.0;
                    constexpr int N = 8192;
                    // thresh,ratio,atk,rel,schpf,makeup,mix
                    auto outRms = [&](float amp) {
                        fx->prepare(sr, N);
                        juce::AudioBuffer<float> buf(2, N);
                        for (int n = 0; n < N; ++n)
                        {
                            const float s = amp * static_cast<float>(
                                std::sin(2.0 * juce::MathConstants<double>::pi * 220.0
                                         * static_cast<double>(n) / sr));
                            buf.setSample(0, n, s);
                            buf.setSample(1, n, s);
                        }
                        ParamFrame frame = { -30.0f, 8.0f, 1.0f, 80.0f, 20.0f, 0.0f, 1.0f };
                        fx->process(buf, N, frame);
                        double sq = 0.0;
                        for (int n = N / 2; n < N; ++n)
                            sq += static_cast<double>(buf.getSample(0, n))
                                  * static_cast<double>(buf.getSample(0, n));
                        return std::sqrt(sq / (N / 2));
                    };
                    const double loudIn  = 0.5 / std::sqrt(2.0);   // ~-9 dBFS RMS, above -30 thresh
                    const double loudOut = outRms(0.5f);
                    CHECK(loudOut < loudIn * 0.9, "bus comp reduces gain on loud input ("
                          + juce::String(loudOut, 4) + " < " + juce::String(loudIn, 4) + ")");
                }
            }

            // Compressor: thresh=0 dBFS → compressor never fires (our sine is ~-9 dBFS);
            // makeup=0 → unity gain; effectively passthrough.
            // Note: kRatios[] = {2:1, 4:1, 8:1, 20:1} — no 1:1 option; use thresh to
            // suppress compression instead.
            {
                auto fx = makeEffectForId("lockstep.comp.v1");
                if (fx)
                {
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;
                    frame[0] = fx->paramSpec(0).maxValue;  // thresh=0 dBFS (never triggers)
                    frame[4] = 0.0f;                        // makeup=0 dB
                    checkPassthrough(*fx, frame, "comp thresh=0dBFS/makeup=0");
                }
            }

            // Bitcrush: bits=max, rate=max → minimal crushing; mix=0 → passthrough.
            {
                auto fx = makeEffectForId("lockstep.bitcrush.v1");
                if (fx)
                {
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;
                    frame[2] = 0.0f;  // mix=0 → dry passthrough
                    checkPassthrough(*fx, frame, "bitcrush mix=0");
                }
            }

            // Flanger: mix=0 → passthrough.
            {
                auto fx = makeEffectForId("lockstep.flanger.v1");
                if (fx)
                {
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;
                    frame[3] = 0.0f;  // mix=0 → dry passthrough
                    checkPassthrough(*fx, frame, "flanger mix=0");
                }
            }

            // Phaser: mix=0 → passthrough.
            {
                auto fx = makeEffectForId("lockstep.phaser.v1");
                if (fx)
                {
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;
                    frame[4] = 0.0f;  // mix=0 → dry passthrough
                    checkPassthrough(*fx, frame, "phaser mix=0");
                }
            }

            // Master Utility: tilt=0, width=1, trim=0 → passthrough.
            {
                auto fx = makeEffectForId("lockstep.mutility.v1");
                if (fx)
                {
                    ParamFrame frame = { 0.0f, 1.0f, 0.0f };  // tilt, width, trim
                    checkPassthrough(*fx, frame, "mutility tilt=0/width=1/trim=0");
                }
            }

            // HQ Reverb: produces a tail (non-silence) after input stops.
            {
                auto fx = makeEffectForId("lockstep.verbhq.v1");
                if (fx)
                {
                    fx->prepare(kSR, kBlockSize);
                    fx->reset();
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;

                    // Feed 10 blocks of fresh sine to fill the FDN delay lines.
                    // Must copy fresh sine each iteration — mix=1 means fx->process()
                    // replaces buf with wet output; reprocessing that would feed silence.
                    // Shortest FDN line is ~30 ms (~5.5 blocks), so 10 blocks is enough.
                    juce::AudioBuffer<float> sineRef(2, kBlockSize);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < kBlockSize; ++i)
                            sineRef.setSample(ch, i, std::sin(static_cast<float>(i) * 0.05f) * 0.35f);

                    juce::AudioBuffer<float> buf(2, kBlockSize);
                    for (int b = 0; b < 10; ++b)
                    {
                        buf.makeCopyOf(sineRef);
                        fx->process(buf, kBlockSize, frame);
                    }

                    // Then feed silence; the reverb tail should continue.
                    buf.clear();
                    float maxTailRms = 0.0f;
                    for (int b = 0; b < 5; ++b)
                    {
                        buf.clear();
                        fx->process(buf, kBlockSize, frame);
                        maxTailRms = std::max(maxTailRms, blockRms(buf));
                    }
                    CHECK(maxTailRms > 1e-4f,
                          "verbhq: no reverb tail after input stops (max tail RMS=" +
                          juce::String(maxTailRms, 6) + ")");
                }
            }

            // HQ Delay: a delayed copy appears after the dry block (mix=1, div=1/16).
            {
                auto fx = makeEffectForId("lockstep.delayhq.v1");
                if (fx)
                {
                    fx->prepare(kSR, kBlockSize);
                    fx->reset();
                    fx->setTimeInfo(120.0);  // 120 BPM

                    // time=0 (div index 0 = 1/16 note), feedback=0, mix=1.
                    // 1/16 note at 120 BPM = 0.25 beats * (60/120 s/beat) * 48000 = 6000 samples.
                    // 6000 / 256 = 23.4 blocks → delayed copy starts appearing mid-block 23
                    // (silence blocks numbered from 0 after the initial sine block).
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;
                    frame[0] = 0.0f;  // time=1/16 note (kDivBeats[0] = 0.25 beats)
                    frame[1] = 0.0f;  // feedback=0
                    frame[4] = 1.0f;  // mix=1 (full wet)

                    // Feed one block of sine.
                    juce::AudioBuffer<float> buf(2, kBlockSize);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < kBlockSize; ++i)
                            buf.setSample(ch, i, std::sin(static_cast<float>(i) * 0.05f) * 0.35f);
                    fx->process(buf, kBlockSize, frame);

                    // Feed silence; check blocks 22-26 where the delayed copy appears.
                    float maxDelayRms = 0.0f;
                    for (int b = 0; b < 27; ++b)
                    {
                        buf.clear();
                        fx->process(buf, kBlockSize, frame);
                        if (b >= 22)
                            maxDelayRms = std::max(maxDelayRms, blockRms(buf));
                    }
                    CHECK(maxDelayRms > 1e-4f,
                          "delayhq: no delayed copy in expected window (blocks 22-26 RMS=" +
                          juce::String(maxDelayRms, 6) + ")");
                }
            }

            // Bus Compressor: thresh=0 (max, never fires), mix=1, makeup=0 → passthrough.
            {
                auto fx = makeEffectForId("lockstep.buscomp.v1");
                if (fx)
                {
                    ParamFrame frame(static_cast<size_t>(fx->numParams()));
                    for (int p = 0; p < fx->numParams(); ++p)
                        frame[static_cast<size_t>(p)] = fx->paramSpec(p).defaultValue;
                    frame[0] = fx->paramSpec(0).maxValue;  // thresh=0 dB (never triggers)
                    frame[5] = 0.0f;                        // makeup=0 dB
                    frame[6] = 1.0f;                        // mix=1
                    checkPassthrough(*fx, frame, "buscomp thresh=0dB/makeup=0");
                }
            }
        }

        // --- A0 regression: master insert processes (output differs from dry) ---
        // processMasterChain is not directly callable from here (it's an instance
        // method on LockstepProcessor), so we test the IEffect path directly: a
        // DistortionEffect at full drive should produce output that differs from the
        // unprocessed input. This guarantees the effect actually runs and that the
        // helper code path changes audio.
        {
            constexpr int kBlockSize = 256;
            auto fx = makeEffectForId("lockstep.distortion.v1");
            CHECK(fx != nullptr, "A0: distortion effect not found");
            if (fx)
            {
                fx->prepare(48000.0, kBlockSize);
                fx->reset();
                juce::AudioBuffer<float> dry(2, kBlockSize);
                juce::AudioBuffer<float> wet(2, kBlockSize);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kBlockSize; ++i)
                    {
                        const float v = std::sin(static_cast<float>(i) * 0.05f) * 0.5f;
                        dry.setSample(ch, i, v);
                        wet.setSample(ch, i, v);
                    }
                ParamFrame frame(static_cast<std::size_t>(fx->numParams()));
                for (int p = 0; p < fx->numParams(); ++p)
                    frame[static_cast<std::size_t>(p)] = fx->paramSpec(p).defaultValue;
                // Set drive to max to guarantee measurable difference.
                const int driveSlot = fx->numParams() > 0 ? 0 : -1;
                if (driveSlot >= 0)
                    frame[static_cast<std::size_t>(driveSlot)] = fx->paramSpec(driveSlot).maxValue;
                fx->process(wet, kBlockSize, frame);
                CHECK(!hasNaNOrInf(wet), "A0: master insert produced NaN/Inf");
                bool differs = false;
                for (int i = 0; i < kBlockSize && !differs; ++i)
                    if (std::abs(wet.getSample(0, i) - dry.getSample(0, i)) > 1e-6f)
                        differs = true;
                CHECK(differs, "A0: master insert had no effect on output -- processMasterChain may be bypassed");
            }
        }

        // --- Placement-aware quality tiers ---
        // Reverb/Delay/Saturation present a lean LQ face on track inserts and a
        // richer HQ face on master slots; the tier is chosen at construction.
        {
            for (const char* id : { "lockstep.reverb.v1", "lockstep.delay.v1",
                                    "lockstep.saturation.v1" })
            {
                auto lq = makeEffectForId(id, EffectTier::Track);
                auto hqx = makeEffectForId(id, EffectTier::Master);
                CHECK(lq != nullptr && hqx != nullptr,
                      "tier: both faces resolve for " + juce::String(id));
                if (lq && hqx)
                    CHECK(hqx->numParams() >= lq->numParams(),
                          "tier: HQ face exposes >= LQ params for " + juce::String(id));
            }

            // Saturation: explicit LQ=4 / HQ=8 contract, and the HQ oversampled
            // path stays finite on a hot input.
            auto satLQ = makeEffectForId("lockstep.saturation.v1", EffectTier::Track);
            auto satHQ = makeEffectForId("lockstep.saturation.v1", EffectTier::Master);
            CHECK(satLQ && satLQ->numParams() == 4, "saturation: LQ has 4 params");
            CHECK(satHQ && satHQ->numParams() == 8, "saturation: HQ has 8 params");
            if (satHQ)
            {
                constexpr int kBlk = 256;
                satHQ->prepare(48000.0, kBlk);
                satHQ->reset();
                ParamFrame f(static_cast<std::size_t>(satHQ->numParams()));
                for (int p = 0; p < satHQ->numParams(); ++p)
                    f[static_cast<std::size_t>(p)] = satHQ->paramSpec(p).defaultValue;
                f[0] = 1.0f;  // drive hot to exercise the saturator + oversampler
                juce::AudioBuffer<float> buf(2, kBlk);
                for (int b = 0; b < 16; ++b)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < kBlk; ++i)
                            buf.setSample(ch, i,
                                1.3f * std::sin(static_cast<float>(b * kBlk + i) * 0.06f));
                    satHQ->process(buf, kBlk, f);
                }
                CHECK(!hasNaNOrInf(buf), "saturation HQ: NaN/Inf through oversampled path");
                CHECK(blockRms(buf) > 1e-4f, "saturation HQ: produced output");
            }

            // Legacy HQ ids still resolve (backward-compat) and canonicalise.
            CHECK(makeEffectForId("lockstep.verbhq.v1", EffectTier::Master) != nullptr,
                  "tier: legacy verbhq id still resolves");
            CHECK(canonicalEffectId("lockstep.verbhq.v1") == "lockstep.reverb.v1",
                  "tier: verbhq canonicalises to reverb");
            CHECK(canonicalEffectId("lockstep.delayhq.v1") == "lockstep.delay.v1",
                  "tier: delayhq canonicalises to delay");
        }

        // --- Oversampler2x: DC gain, passband, and alias rejection ---
        {
            dsp::Oversampler2x os;
            double dc = 0.0;
            for (int n = 0; n < 2000; ++n)
            {
                float a, b;
                os.upsample(1.0f, a, b);
                const float y = os.decimate(a, b);
                if (n > 300) dc += static_cast<double>(y);
            }
            CHECK(std::abs(dc / 1700.0 - 1.0) < 0.02, "oversampler: unity DC gain through up->down");

            // A tone above base-Nyquist (would fold) must be strongly rejected by
            // the decimator. f2=0.35 cyc/sample @2x is well into the stopband.
            dsp::Oversampler2x os2;
            double pk = 0.0;
            for (int m = 0; m < 4000; ++m)
            {
                const float a = std::sin(2.0f * 3.14159265f * 0.35f * static_cast<float>(2 * m));
                const float b = std::sin(2.0f * 3.14159265f * 0.35f * static_cast<float>(2 * m + 1));
                const float y = os2.decimate(a, b);
                if (m > 400) pk = std::max(pk, static_cast<double>(std::abs(y)));
            }
            CHECK(pk < 0.05, "oversampler: rejects above-Nyquist content (alias guard)");
        }
    }
}
