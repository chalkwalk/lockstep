#pragma once

// AssetAudio -- real recorded audio, fed to the processor as if it were coming in a
// cable.
//
// The capture and deck journeys (Group F/G) are about what the instrument does with
// SOUND ARRIVING: a tape punch, a loop overdub, a Record machine grabbing a buffer.
// Until now the rig could only inject MIDI -- renderBlocks() cleared the buffer and
// handed the processor silence -- so every one of those journeys could assert that a
// state machine moved and nothing about the audio it moved for.
//
// Two pieces, deliberately small:
//   WavAsset   -- one file from tests/assets/, decoded once into memory.
//   feeder     -- a per-block callable that writes the next slice into the input
//                 buffer, so the processor sees a continuous stream across blocks.
//
// Procedural fixtures (a sine burst, a click train) are still the right choice for
// anything about levels or timing -- they are exact and need no file. These exist for
// the questions only real material can answer: does transient detection find the
// onsets, does key detection hear C minor, does a 40-second bed stream from disk.
// See tests/assets/LICENSES.md for what each file is and the two traps in them.

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

namespace lockstep::test
{
    struct WavAsset
    {
        juce::AudioBuffer<float> samples;
        double sampleRate = 0.0;

        [[nodiscard]] bool valid() const { return samples.getNumSamples() > 0; }
        [[nodiscard]] int length() const { return samples.getNumSamples(); }

        // Peak magnitude across every channel -- for a test that wants to compare what
        // came out against what went in, rather than against a hard-coded number (the
        // music loop peaks at about -12 dBFS; see LICENSES.md).
        [[nodiscard]] float peak() const
        {
            float p = 0.0f;
            for (int ch = 0; ch < samples.getNumChannels(); ++ch)
                p = std::max(p, samples.getMagnitude(ch, 0, samples.getNumSamples()));
            return p;
        }

        // Load by bare filename from tests/assets/. LOCKSTEP_TEST_DIR is stamped in by
        // CMake, so this works wherever the binary is run from.
        static WavAsset load(const juce::String& fileName)
        {
            WavAsset a;
            const juce::File f = juce::File(LOCKSTEP_TEST_DIR).getChildFile("assets")
                                                              .getChildFile(fileName);
            if (! f.existsAsFile())
                return a;

            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
            if (r == nullptr)
                return a;

            a.sampleRate = r->sampleRate;
            a.samples.setSize(static_cast<int>(r->numChannels),
                              static_cast<int>(r->lengthInSamples));
            r->read(&a.samples, 0, static_cast<int>(r->lengthInSamples), 0, true, true);
            return a;
        }
    };

    // A per-block feeder over an asset. Hand it to AudioRig::setInputFill and the
    // processor receives the file as continuous input, one block at a time.
    //
    // `loop` matters for the deck journeys: a tape punch or a loop overdub records for
    // longer than any of these files last, and a feeder that ran out would hand the
    // deck silence halfway through a take -- which reads as the deck stopping.
    inline std::function<void(juce::AudioBuffer<float>&)> streamOf(const WavAsset& asset,
                                                                   bool loop = true)
    {
        auto pos = std::make_shared<int>(0);
        return [&asset, pos, loop](juce::AudioBuffer<float>& buf) {
            if (! asset.valid())
                return;
            const int n = buf.getNumSamples();
            for (int i = 0; i < n; ++i)
            {
                if (*pos >= asset.length())
                {
                    if (! loop) return;
                    *pos = 0;
                }
                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                {
                    const int srcCh = ch % asset.samples.getNumChannels();
                    buf.setSample(ch, i, asset.samples.getSample(srcCh, *pos));
                }
                ++(*pos);
            }
        };
    }
}   // namespace lockstep::test
