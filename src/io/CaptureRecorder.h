#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lockstep
{
    // N-stream performance capture to 32-bit float WAV via JUCE ThreadedWriter.
    // One instance per stream: the master bus plus one per track (stems).
    //
    // Threading contract:
    //   - arm() / disarm() / discardFile() are called from the message thread only.
    //   - writeBlock() / padTo() are called from the audio thread; both are lock-free
    //     (ring buffer write only) when capturing_ is true.
    //   - The atomic capturing_ flag is the sole audio-thread-visible gate; all
    //     file/writer allocation happens on the message thread before/after flip.
    //
    // 11.11: the caller supplies the writer thread, so N recorders share one
    // TimeSliceThread rather than spawning N of them (a take arms 1 master + 16
    // stems). Passing none keeps the old self-owned-thread behaviour.
    class CaptureRecorder
    {
    public:
        CaptureRecorder() = default;
        ~CaptureRecorder() { disarm(); }

        // Arm recording to `destFile` (32-bit float WAV, given sample rate / channels).
        // `sharedThread`, when non-null, is used instead of a private writer thread;
        // it must outlive the capture. Returns false if the file could not be opened.
        bool arm(const juce::File& destFile, double sampleRate, int numChannels,
                 juce::TimeSliceThread* sharedThread = nullptr)
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
            numChannels_ = numChannels;
            prepareSilence(numChannels);   // padTo() must not allocate

            juce::TimeSliceThread* writeThread = sharedThread;
            if (writeThread == nullptr)
            {
                if (!thread_)
                {
                    thread_ = std::make_unique<juce::TimeSliceThread>("capture writer");
                    thread_->startThread(juce::Thread::Priority::low);
                }
                writeThread = thread_.get();
            }

            // Buffer: ~4 seconds of audio at the given rate.
            const int bufferSamples = static_cast<int>(sampleRate * 4.0);
            auto tw = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
                writer.release(), *writeThread, bufferSamples);
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

        // 11.11 prune: disarm and delete the file. Used at take close for a stem
        // whose track was never stemmable (a feeder folded into a bus, an
        // Off-routed or never-assigned track) — see DESIGN §41.3.
        void discardFile()
        {
            disarm();
            if (destFile_ != juce::File{} && destFile_.existsAsFile())
                destFile_.deleteFile();
            destFile_ = juce::File{};
            samplesWritten_.store(0, std::memory_order_relaxed);
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

        // 11.11, the top-up sweep (audio thread). Write silence until this stream
        // holds `target` samples, so every stem stays sample-aligned with the
        // master however the caller's per-track work was skipped (a floored mute
        // ramp, a MIDI-out track, a zero-length track). Allocation-free: the
        // silence buffer is sized at arm(). No-op when already at/past target.
        void padTo(int64_t target)
        {
            if (!capturing_.load(std::memory_order_acquire)) return;
            auto* tw = threadedWriter_.get();
            if (tw == nullptr) return;

            int64_t deficit = target - samplesWritten_.load(std::memory_order_relaxed);
            if (deficit <= 0) return;

            const int chunk = silence_.getNumSamples();
            if (chunk <= 0) return;
            while (deficit > 0)
            {
                const int n = static_cast<int>(std::min<int64_t>(deficit, chunk));
                tw->write(silence_.getArrayOfReadPointers(), n);
                samplesWritten_.fetch_add(n, std::memory_order_relaxed);
                deficit -= n;
            }
        }

    private:
        // Pre-allocated silence for padTo (message-thread allocation, in arm()).
        // kSilenceChunk covers any plausible block size; a larger deficit loops.
        static constexpr int kSilenceChunk = 8192;

        void prepareSilence(int numChannels)
        {
            silence_.setSize(std::max(1, numChannels), kSilenceChunk, false, true, false);
            silence_.clear();
        }

        std::unique_ptr<juce::TimeSliceThread> thread_;
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter_;
        std::atomic<bool> capturing_ { false };
        std::atomic<int64_t> samplesWritten_ { 0 };
        juce::File destFile_;
        int numChannels_ = 0;
        juce::AudioBuffer<float> silence_;
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
