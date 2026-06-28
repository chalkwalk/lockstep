#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace lockstep
{
    // N-stream performance capture to 32-bit float WAV via JUCE ThreadedWriter.
    // Today exactly one stream ("master") is used; the shape allows per-track
    // streams to be added without redesign (write multi-channel or additional files).
    //
    // Threading contract:
    //   - arm() / disarm() are called from the message thread only.
    //   - writeBlock() is called from the audio thread; it is lock-free (ring buffer
    //     write only) when capturing_ is true.
    //   - The atomic capturing_ flag is the sole audio-thread-visible gate; all
    //     file/writer allocation happens on the message thread before/after flip.
    class CaptureRecorder
    {
    public:
        CaptureRecorder() = default;
        ~CaptureRecorder() { disarm(); }

        // Arm recording to `destFile` (32-bit float WAV, given sample rate / channels).
        // Returns false if the file could not be opened.
        bool arm(const juce::File& destFile, double sampleRate, int numChannels)
        {
            disarm();   // stop any existing capture first

            if (!destFile.getParentDirectory().createDirectory())
                return false;

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> os(destFile.createOutputStream());
            if (!os) return false;

            // 32-bit float WAV, no metadata. createWriterFor moves ownership of the
            // stream into the writer on success.
            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate(sampleRate)
                                     .withNumChannels(numChannels)
                                     .withBitsPerSample(32)
                                     .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
            auto writer = wav.createWriterFor(os, options);
            if (!writer)
                return false;

            destFile_ = destFile;
            samplesWritten_ = 0;

            if (!thread_)
            {
                thread_ = std::make_unique<juce::TimeSliceThread>("capture writer");
                thread_->startThread(juce::Thread::Priority::low);
            }

            // Buffer: ~4 seconds of audio at the given rate.
            const int bufferSamples = static_cast<int>(sampleRate * 4.0);
            auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
                writer.release(), *thread_, bufferSamples);
            threadedWriter_.reset(tw.release());
            capturing_.store(true, std::memory_order_release);
            return true;
        }

        // Disarm and flush — blocks until the writer has flushed.
        // Returns the number of samples written (0 if never armed).
        int64_t disarm()
        {
            capturing_.store(false, std::memory_order_release);
            const int64_t n = samplesWritten_.load(std::memory_order_relaxed);
            threadedWriter_.reset();   // destructor flushes remaining buffer
            return n;
        }

        [[nodiscard]] bool isCapturing() const noexcept
        {
            return capturing_.load(std::memory_order_relaxed);
        }

        [[nodiscard]] juce::File captureFile() const { return destFile_; }

        [[nodiscard]] int64_t samplesWritten() const noexcept
        {
            return samplesWritten_.load(std::memory_order_relaxed);
        }

        // Audio-thread entry point. No-op when !capturing_.
        void writeBlock(const juce::AudioBuffer<float>& buf, int numSamples)
        {
            if (!capturing_.load(std::memory_order_acquire)) return;
            auto* tw = threadedWriter_.get();
            if (!tw) return;
            tw->write(buf.getArrayOfReadPointers(), numSamples);
            samplesWritten_.fetch_add(numSamples, std::memory_order_relaxed);
        }

    private:
        std::unique_ptr<juce::TimeSliceThread> thread_;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
        std::atomic<bool> capturing_ { false };
        std::atomic<int64_t> samplesWritten_ { 0 };
        juce::File destFile_;
    };

    // Generate a timestamped capture filename.
    inline juce::File chooseCaptureFile(const juce::File& projectFile)
    {
        juce::File capturesDir;
        if (projectFile.existsAsFile())
        {
            capturesDir = projectFile.getParentDirectory().getChildFile("Captures");
        }
        else
        {
            capturesDir = juce::File::getSpecialLocation(juce::File::userMusicDirectory)
                              .getChildFile("Lockstep")
                              .getChildFile("Captures");
        }

        const juce::Time now = juce::Time::getCurrentTime();
        const juce::String stamp = "capture-"
            + juce::String(now.getYear())
            + juce::String(now.getMonth() + 1).paddedLeft('0', 2)
            + juce::String(now.getDayOfMonth()).paddedLeft('0', 2)
            + "-"
            + juce::String(now.getHours()).paddedLeft('0', 2)
            + juce::String(now.getMinutes()).paddedLeft('0', 2)
            + juce::String(now.getSeconds()).paddedLeft('0', 2);

        // D: every take is its own directory holding master.wav + any stems, so
        // the master and its stems stay grouped on disk.
        return capturesDir.getChildFile(stamp).getChildFile("master.wav");
    }

    // D (stems): per-track WAV path inside the take directory (next to master.wav).
    // track is 0-based; the file is 1-based ("track-01.wav") to match the surface.
    inline juce::File stemFileFor(const juce::File& masterFile, int track)
    {
        const juce::String name =
            "track-" + juce::String(track + 1).paddedLeft('0', 2) + ".wav";
        return masterFile.getParentDirectory().getChildFile(name);
    }
}
