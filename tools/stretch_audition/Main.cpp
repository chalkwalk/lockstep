// stretch_audition — offline ear-test gate for the Bungee stretch engine (9.23).
//
// Usage: stretch_audition <input.wav> [output-dir]
//
// Loads a WAV and renders the matrix {speed 0.7,0.85,1.15,1.3} x
// {pitch -7,-3,0,+3,+7 semitones}, plus one looped render (loop=On, unity),
// to WAVs in the output directory. Listen on a drum loop, a vocal phrase and a
// full mix. PASS the engine iff: drum transients stay singular (no doubled or
// fluttered hits) at +/-30 %, vocals at +/-7 semitones are free of gross
// metallic ringing, the loop seam is inaudible, and there are no clicks/NaNs.
// FAIL => vendor signalsmith-stretch behind the identical IStretchEngine seam.

#include "dsp/BungeeStretchEngine.h"
#include "dsp/PcmStretchSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

using namespace lockstep;

namespace
{
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

    // Render `source` through a fresh engine at the given ratios. When `loop` is
    // set, renders `loopSeconds` of looped output; otherwise renders until the
    // engine drains.
    juce::AudioBuffer<float> render(PcmStretchSource& source, double rate,
                                    double timeRatio, double pitchRatio,
                                    bool loop, double loopSeconds)
    {
        constexpr int kBlock = 512;
        BungeeStretchEngine engine;
        engine.prepare(rate, rate, 2, kBlock);
        engine.start(&source, 0.0, timeRatio, pitchRatio);
        if (loop)
            engine.setLoop(0, source.length());

        // For the loop render, cap at an EXACT integer number of loop cycles so the
        // written file itself loops seamlessly (period = source length at unity). Any
        // click heard on `play loop_unity.wav repeat -` is then the engine's internal
        // seam, not a file-boundary wrap. Render at least a few seconds / >= 3 cycles.
        int cap;
        if (loop)
        {
            const juce::int64 period = juce::jmax<juce::int64>(1, source.length());
            const int cycles = juce::jmax(2, static_cast<int>(
                std::ceil(loopSeconds * rate / static_cast<double>(period))));
            cap = static_cast<int>(cycles * period);
        }
        else
        {
            cap = static_cast<int>(source.length() * timeRatio * 1.5 + rate);
        }

        std::vector<float> ch0, ch1;
        ch0.reserve(static_cast<std::size_t>(cap));
        ch1.reserve(static_cast<std::size_t>(cap));

        juce::AudioBuffer<float> blk(2, kBlock);
        int produced = 0, guard = 0;
        while (produced < cap && guard++ < 2000000 && (loop || engine.isActive()))
        {
            blk.clear();
            engine.process(blk, 0, kBlock);
            for (int i = 0; i < kBlock && produced < cap; ++i, ++produced)
            {
                ch0.push_back(blk.getSample(0, i));
                ch1.push_back(blk.getSample(1, i));
            }
        }

        juce::AudioBuffer<float> out(2, produced);
        for (int i = 0; i < produced; ++i)
        {
            out.setSample(0, i, ch0[static_cast<std::size_t>(i)]);
            out.setSample(1, i, ch1[static_cast<std::size_t>(i)]);
        }
        return out;
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: stretch_audition <input.wav> [output-dir]\n";
        return 2;
    }

    const juce::File input(juce::File::getCurrentWorkingDirectory()
                               .getChildFile(juce::String(argv[1])));
    if (!input.existsAsFile())
    {
        std::cerr << "input not found: " << input.getFullPathName() << "\n";
        return 2;
    }

    const juce::File outDir = (argc >= 3)
        ? juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[2]))
        : input.getParentDirectory().getChildFile(input.getFileNameWithoutExtension() + "_audition");
    outDir.createDirectory();

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(input));
    if (reader == nullptr)
    {
        std::cerr << "could not decode: " << input.getFullPathName() << "\n";
        return 2;
    }

    const double rate = reader->sampleRate;
    const int len = static_cast<int>(reader->lengthInSamples);
    juce::AudioBuffer<float> pcm(static_cast<int>(reader->numChannels), len);
    reader->read(&pcm, 0, len, 0, true, true);
    PcmStretchSource source(&pcm, rate);

    std::cout << "loaded " << input.getFileName() << "  "
              << rate << " Hz  " << pcm.getNumChannels() << " ch  "
              << (len / rate) << " s\n";
    std::cout << "engine: Bungee " << "(rendering to " << outDir.getFullPathName() << ")\n";

    const double speeds[] = { 0.7, 0.85, 1.15, 1.3 };
    const int    semis[]  = { -7, -3, 0, 3, 7 };

    for (double speed : speeds)
        for (int semi : semis)
        {
            const double timeRatio = 1.0 / speed;               // slower = longer
            const double pitchRatio = std::pow(2.0, semi / 12.0);
            auto out = render(source, rate, timeRatio, pitchRatio, false, 0.0);

            juce::String name = "s"
                + juce::String(juce::roundToInt(speed * 100)).paddedLeft('0', 3)
                + "_p" + (semi >= 0 ? "+" : "-")
                + juce::String(std::abs(semi)).paddedLeft('0', 2) + ".wav";
            const juce::File f = outDir.getChildFile(name);
            const bool ok = writeWav(f, out, rate);
            std::cout << (ok ? "  wrote " : "  FAILED ") << name
                      << "  (" << out.getNumSamples() << " frames)\n";
        }

    // One looped render at unity (seam audit).
    {
        auto out = render(source, rate, 1.0, 1.0, true, std::min(8.0, (len / rate) * 3.0 + 1.0));
        const juce::File f = outDir.getChildFile("loop_unity.wav");
        const bool ok = writeWav(f, out, rate);
        std::cout << (ok ? "  wrote " : "  FAILED ") << "loop_unity.wav"
                  << "  (" << out.getNumSamples() << " frames)\n";
    }

    std::cout << "done.\n";
    return 0;
}
