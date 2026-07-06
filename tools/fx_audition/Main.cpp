// fx_audition — offline ear-test gate for the FX catalogue (9.24).
//
// Usage: fx_audition [output-dir] [--effect <id-substr>] [--signal <name>]
//                    [--bpm-ramp]
//
// Renders a before/after matrix with NO input file: every catalogue effect
// (skipping the External send sentinel) is driven with internally synthesised
// probe signals and its output written to <out>/<badge>_<tier>_<preset>_<signal>.wav.
//
// Matrix axes:
//   * effect  — every availableEffects() entry with a real DSP instance.
//   * tier    — Track (LQ face) for non-master-only effects; Master (HQ face)
//               for master-only effects and for tier-aware effects whose two
//               faces differ (detected by param-count mismatch).
//   * preset  — "default" (all param defaults) and "extreme" (each param pushed
//               to the range end farthest from its default — maximal character,
//               the alias/click stress render).
//   * signal  — impulse (tail/IR), dualsine (440 Hz + 12 kHz alias probe),
//               sweep (20 Hz–20 kHz log), drum (synthetic burst — the main ear
//               signal).
//
// Discipline (9.24): render and stash this matrix as the BASELINE before any
// DSP-changing stage, then re-render per stage and A/B by ear. Flags narrow the
// matrix for a fast single-effect loop; --bpm-ramp sweeps the host tempo
// 120->150 BPM across the render (auditions tempo-synced delay retune).

#include "machine/EffectFactory.h"
#include "machine/IEffect.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace lockstep;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int    kBlock = 512;
    constexpr double kTailSeconds = 1.5;   // captured after the probe ends (tails)
    constexpr double kTwoPi = 6.283185307179586;

    bool writeWav(const juce::File& file, const juce::AudioBuffer<float>& buf, double rate)
    {
        file.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
        if (stream == nullptr)
            return false;
        const auto options = juce::AudioFormatWriterOptions{}
            .withSampleRate(rate)
            .withNumChannels(buf.getNumChannels())
            .withBitsPerSample(24);
        std::unique_ptr<juce::AudioFormatWriter> writer(fmt.createWriterFor(stream, options));
        if (writer == nullptr)
            return false;
        return writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    }

    // ---- Probe signals (mono) --------------------------------------------

    std::vector<float> makeImpulse()
    {
        std::vector<float> s(static_cast<std::size_t>(kRate * 1.0), 0.0f);
        if (s.size() > 100) s[100] = 1.0f;
        return s;
    }

    std::vector<float> makeDualSine()
    {
        const std::size_t n = static_cast<std::size_t>(kRate * 2.0);
        std::vector<float> s(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / kRate;
            s[i] = 0.4f * static_cast<float>(std::sin(kTwoPi * 440.0 * t))
                 + 0.4f * static_cast<float>(std::sin(kTwoPi * 12000.0 * t));
        }
        return s;
    }

    std::vector<float> makeSweep()
    {
        const double dur = 3.0;
        const std::size_t n = static_cast<std::size_t>(kRate * dur);
        std::vector<float> s(n);
        const double f0 = 20.0, f1 = 20000.0;
        const double k = std::log(f1 / f0);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double t = static_cast<double>(i) / kRate;
            // Exponential-sweep instantaneous phase.
            const double phase = kTwoPi * f0 * dur / k * (std::exp(k * t / dur) - 1.0);
            s[i] = 0.5f * static_cast<float>(std::sin(phase));
        }
        return s;
    }

    std::vector<float> makeDrum()
    {
        const std::size_t n = static_cast<std::size_t>(kRate * 1.5);
        std::vector<float> s(n, 0.0f);
        juce::Random rng(1234);
        const double hits[] = { 0.0, 0.75 };
        for (double hit : hits)
        {
            const std::size_t start = static_cast<std::size_t>(hit * kRate);
            for (std::size_t i = start; i < n; ++i)
            {
                const double t = static_cast<double>(i - start) / kRate;
                // Pitch-dropping kick body 120 -> 50 Hz over 80 ms.
                const double f = 50.0 + 70.0 * std::exp(-t / 0.02);
                const double bodyEnv = std::exp(-t / 0.18);
                const double body = std::sin(kTwoPi * f * t) * bodyEnv;
                // Short noise transient (click) decaying over ~15 ms.
                const double clickEnv = std::exp(-t / 0.005);
                const double click = (rng.nextFloat() * 2.0f - 1.0f) * clickEnv * 0.6;
                s[i] += static_cast<float>(0.9 * body + click);
            }
        }
        for (auto& v : s) v = juce::jlimit(-1.0f, 1.0f, v);
        return s;
    }

    struct Signal { const char* name; std::vector<float> mono; };

    // ---- Preset helpers ---------------------------------------------------

    // "extreme" pushes each param to the range end farthest from its default —
    // maximal departure from the default sound, so the render maximises whatever
    // aliasing / clicks / character the DSP produces.
    ParamFrame makeFrame(IEffect& fx, bool extreme)
    {
        ParamFrame f(static_cast<std::size_t>(fx.numParams()));
        for (int i = 0; i < fx.numParams(); ++i)
        {
            const ParamSpec ps = fx.paramSpec(i);
            float v = ps.defaultValue;
            if (extreme)
            {
                const float toMax = std::abs(ps.maxValue - ps.defaultValue);
                const float toMin = std::abs(ps.defaultValue - ps.minValue);
                v = (toMax >= toMin) ? ps.maxValue : ps.minValue;
            }
            f[static_cast<std::size_t>(i)] = v;
        }
        return f;
    }

    // Render a mono probe (duplicated to stereo) through the effect, appending a
    // silent tail so reverb/delay decays are captured. Peak-safety scales the
    // whole file down (logged) if it would clip, so aliasing is not masked by
    // hard digital clipping.
    juce::AudioBuffer<float> renderThrough(IEffect& fx, const std::vector<float>& mono,
                                           const ParamFrame& params, bool bpmRamp,
                                           float& appliedGain)
    {
        const int probeLen = static_cast<int>(mono.size());
        const int tail = static_cast<int>(kRate * kTailSeconds);
        const int total = probeLen + tail;

        fx.prepare(kRate, kBlock);
        fx.reset();

        juce::AudioBuffer<float> out(2, total);
        out.clear();
        for (int i = 0; i < probeLen; ++i)
        {
            out.setSample(0, i, mono[static_cast<std::size_t>(i)]);
            out.setSample(1, i, mono[static_cast<std::size_t>(i)]);
        }

        juce::AudioBuffer<float> blk(2, kBlock);
        for (int pos = 0; pos < total; pos += kBlock)
        {
            const int n = std::min(kBlock, total - pos);
            // Per-block tempo (constant, or ramped 120->150 across the render).
            const double frac = static_cast<double>(pos) / static_cast<double>(std::max(1, total));
            fx.setTimeInfo(bpmRamp ? (120.0 + 30.0 * frac) : 120.0);

            blk.clear();
            for (int ch = 0; ch < 2; ++ch)
                blk.copyFrom(ch, 0, out, ch, pos, n);
            fx.process(blk, n, params);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom(ch, pos, blk, ch, 0, n);
        }

        const float peak = out.getMagnitude(0, total);
        appliedGain = 1.0f;
        if (peak > 0.99f)
        {
            appliedGain = 0.9f / peak;
            out.applyGain(appliedGain);
        }
        return out;
    }

    // Detect a tier-aware effect: its Track and Master faces differ (param count).
    bool facesDiffer(const std::string& id)
    {
        auto t = makeEffectForId(id, EffectTier::Track);
        auto m = makeEffectForId(id, EffectTier::Master);
        if (t == nullptr || m == nullptr) return false;
        return t->numParams() != m->numParams();
    }

    juce::String shortName(const EffectInfo& e)
    {
        return juce::String(e.badge).toLowerCase();
    }
}

int main(int argc, char* argv[])
{
    juce::String outArg;
    juce::String effectFilter;
    juce::String signalFilter;   // empty = all
    bool bpmRamp = false;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a(argv[i]);
        if (a == "--effect" && i + 1 < argc)      effectFilter = juce::String(argv[++i]);
        else if (a == "--signal" && i + 1 < argc) signalFilter = juce::String(argv[++i]);
        else if (a == "--bpm-ramp")               bpmRamp = true;
        else if (!a.startsWith("--"))             outArg = a;
        else { std::cerr << "unknown flag: " << a << "\n"; return 2; }
    }

    const juce::File outDir = (outArg.isNotEmpty())
        ? juce::File::getCurrentWorkingDirectory().getChildFile(outArg)
        : juce::File::getCurrentWorkingDirectory().getChildFile("fx_audition_out");
    outDir.createDirectory();

    std::vector<Signal> signals = {
        { "impulse",  makeImpulse()  },
        { "dualsine", makeDualSine() },
        { "sweep",    makeSweep()    },
        { "drum",     makeDrum()     },
    };

    std::cout << "fx_audition  " << kRate << " Hz  -> " << outDir.getFullPathName() << "\n";
    if (bpmRamp) std::cout << "  (bpm-ramp 120->150 across each render)\n";

    int written = 0, failed = 0;
    for (const EffectInfo& e : availableEffects())
    {
        if (e.id == kExternalSendId) continue;              // routing sentinel, no DSP
        if (effectFilter.isNotEmpty()
            && !juce::String(e.id).containsIgnoreCase(effectFilter)
            && !juce::String(e.badge).containsIgnoreCase(effectFilter))
            continue;

        // Tiers to render for this effect.
        std::vector<EffectTier> tiers;
        if (e.masterOnly)                 tiers = { EffectTier::Master };
        else if (facesDiffer(e.id))       tiers = { EffectTier::Track, EffectTier::Master };
        else                              tiers = { EffectTier::Track };

        for (EffectTier tier : tiers)
        {
            const char* tierName = (tier == EffectTier::Master) ? "master" : "track";
            for (bool extreme : { false, true })
            {
                const char* preset = extreme ? "extreme" : "default";
                for (const Signal& sig : signals)
                {
                    if (signalFilter.isNotEmpty() && signalFilter != sig.name) continue;

                    auto fx = makeEffectForId(e.id, tier);
                    if (fx == nullptr) { ++failed; continue; }
                    const ParamFrame params = makeFrame(*fx, extreme);

                    float gain = 1.0f;
                    auto rendered = renderThrough(*fx, sig.mono, params, bpmRamp, gain);

                    const juce::String name = shortName(e) + "_" + tierName + "_"
                        + preset + "_" + sig.name
                        + (bpmRamp ? "_bpmramp" : "") + ".wav";
                    const bool ok = writeWav(outDir.getChildFile(name), rendered, kRate);
                    if (ok) { ++written;
                        std::cout << "  wrote " << name
                                  << (gain < 1.0f ? juce::String("  (scaled x")
                                        + juce::String(gain, 2) + ")" : juce::String())
                                  << "\n"; }
                    else    { ++failed; std::cout << "  FAILED " << name << "\n"; }
                }
            }
        }
    }

    std::cout << "done. " << written << " written, " << failed << " failed.\n";
    return failed == 0 ? 0 : 1;
}
