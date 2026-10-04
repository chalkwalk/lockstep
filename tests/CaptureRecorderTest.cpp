// CaptureRecorderTest -- lifecycle coverage for lockstep::CaptureRecorder.
//
// Tests:
//  - arm/writeBlock/disarm happy path: WAV file created, readable, correct format/length/RMS
//  - isCapturing() transitions: false→true→false across the lifecycle
//  - writeBlock while disarmed is a no-op (no crash, file not created)
//  - arming to an unwritable path (parent is a regular file) returns false without crash
//  - samplesWritten() matches what was written

#include "TestHarness.h"
#include "../src/io/CaptureRecorder.h"
#include <numbers>

namespace lockstep
{
    static void testCaptureHappyPath()
    {
        constexpr double kSR = 48000.0;
        constexpr int kChannels = 2;
        constexpr int kBlockSize = 256;
        constexpr int kNumBlocks = 20;

        // Temp dir: use JUCE temp directory.
        const juce::File tempDir = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getChildFile("lockstep_capture_test");
        tempDir.createDirectory();
        const juce::File destFile = tempDir.getChildFile("test_capture.wav");
        destFile.deleteFile();

        // Build a sine source buffer.
        juce::AudioBuffer<float> sine(kChannels, kBlockSize);
        for (int ch = 0; ch < kChannels; ++ch)
            for (int i = 0; i < kBlockSize; ++i)
                sine.setSample(ch, i, std::sin(2.0f * std::numbers::pi_v<float> *
                               440.0f * static_cast<float>(i) / static_cast<float>(kSR)));

        CaptureRecorder rec;

        CHECK(!rec.isCapturing(), "CaptureRecorder: initial isCapturing must be false");
        CHECK(rec.samplesWritten() == 0, "CaptureRecorder: initial samplesWritten must be 0");

        const bool armed = rec.arm(destFile, kSR, kChannels);
        CHECK(armed, "CaptureRecorder: arm() returned false for writable path");
        CHECK(rec.isCapturing(), "CaptureRecorder: isCapturing must be true after arm");

        for (int b = 0; b < kNumBlocks; ++b)
            rec.writeBlock(sine, kBlockSize);

        // Give the ThreadedWriter a moment to flush.
        juce::Thread::sleep(200);

        const int64_t written = rec.disarm();
        CHECK(!rec.isCapturing(), "CaptureRecorder: isCapturing must be false after disarm");
        CHECK(written == kNumBlocks * kBlockSize,
              "CaptureRecorder: samplesWritten mismatch (got="
              + juce::String(written) + " expected=" + juce::String(kNumBlocks * kBlockSize) + ")");

        // Verify the WAV file.
        CHECK(destFile.existsAsFile(), "CaptureRecorder: WAV file does not exist after disarm");

        juce::WavAudioFormat wav;
        auto is = std::unique_ptr<juce::FileInputStream>(destFile.createInputStream());
        CHECK(is != nullptr, "CaptureRecorder: cannot open WAV file for reading");
        if (is)
        {
            auto* reader = wav.createReaderFor(is.get(), true);
            CHECK(reader != nullptr, "CaptureRecorder: WAV file not readable by juce::WavAudioFormat");
            if (reader)
            {
                std::unique_ptr<juce::AudioFormatReader> ownedReader(reader);
                is.release();  // reader owns stream
                CHECK(static_cast<int>(ownedReader->numChannels) == kChannels,
                      "CaptureRecorder: wrong channel count in WAV");
                CHECK(std::abs(ownedReader->sampleRate - kSR) < 1.0,
                      "CaptureRecorder: wrong sample rate in WAV");
                CHECK(ownedReader->bitsPerSample == 32,
                      "CaptureRecorder: expected 32-bit float WAV");

                const juce::int64 expectedLen =
                    static_cast<juce::int64>(kNumBlocks * kBlockSize);
                CHECK(ownedReader->lengthInSamples == expectedLen,
                      "CaptureRecorder: wrong length in WAV (got="
                      + juce::String(ownedReader->lengthInSamples)
                      + " expected=" + juce::String(expectedLen) + ")");

                // Read all samples back and check RMS > 0.1 (sine at amplitude 1 → RMS ≈ 0.707).
                juce::AudioBuffer<float> readBack(kChannels, kNumBlocks * kBlockSize);
                ownedReader->read(&readBack, 0, kNumBlocks * kBlockSize, 0, true, true);
                double sumSq = 0.0;
                for (int ch = 0; ch < kChannels; ++ch)
                    for (int i = 0; i < kNumBlocks * kBlockSize; ++i)
                    {
                        const double v = static_cast<double>(readBack.getSample(ch, i));
                        sumSq += v * v;
                    }
                const float rms = static_cast<float>(
                    std::sqrt(sumSq / static_cast<double>(kChannels * kNumBlocks * kBlockSize)));
                CHECK(rms > 0.1f,
                      "CaptureRecorder: WAV content RMS too low (got=" + juce::String(rms) + ")");
            }
        }

        // Cleanup.
        destFile.deleteFile();
        tempDir.deleteRecursively();
    }

    static void testCaptureWriteWhileDisarmed()
    {
        // writeBlock while disarmed must not crash and must not create a file.
        const juce::File dummyFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("lockstep_disarmed_test.wav");
        dummyFile.deleteFile();

        CaptureRecorder rec;
        juce::AudioBuffer<float> buf(2, 256);
        buf.clear();

        rec.writeBlock(buf, 256);  // must not crash
        rec.writeBlock(buf, 0);    // zero-length no-op

        CHECK(!rec.isCapturing(), "CaptureRecorder: still not capturing after no-op writes");
        CHECK(!dummyFile.existsAsFile(), "CaptureRecorder: file created without arm() call");
    }

    static void testCaptureUnwritablePath()
    {
        // Arm to a path whose parent is a regular file — must return false without crash.
        const juce::File tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("lockstep_not_a_dir");
        // Create the "parent" as a regular file so the sub-path cannot be created.
        tempFile.create();

        const juce::File badPath = tempFile.getChildFile("capture.wav");
        CaptureRecorder rec;
        const bool armed = rec.arm(badPath, 48000.0, 2);
        CHECK(!armed, "CaptureRecorder: arm() must return false for unwritable path");
        CHECK(!rec.isCapturing(), "CaptureRecorder: must not be capturing after failed arm");

        tempFile.deleteFile();
    }

    void runCaptureRecorderTests()
    {
        testCaptureHappyPath();
        testCaptureWriteWhileDisarmed();
        testCaptureUnwritablePath();
    }

} // namespace lockstep
