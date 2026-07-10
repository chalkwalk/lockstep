#pragma once

namespace dc
{
    // How audio crosses the deck_core boundary (DESIGN §40.11): a non-owning view
    // over channel pointers the caller owns. deck_core allocates nothing on the
    // process path and never learns what a juce::AudioBuffer is; a host with any
    // buffer type at all can hand it one of these.
    //
    // `chans` points at `numChans` channel pointers, each with `numSamples` frames.
    // The view does not outlive the caller's buffer, and nothing in the library
    // stores one across a call.
    struct ChannelView
    {
        float* const* chans = nullptr;
        int numChans = 0;
        int numSamples = 0;

        [[nodiscard]] bool empty() const noexcept
        {
            return chans == nullptr || numChans <= 0 || numSamples <= 0;
        }

        [[nodiscard]] float* channel(int c) const noexcept { return chans[c]; }
    };

    struct ConstChannelView
    {
        const float* const* chans = nullptr;
        int numChans = 0;
        int numSamples = 0;

        ConstChannelView() = default;
        ConstChannelView(const float* const* c, int n, int s) noexcept
            : chans(c), numChans(n), numSamples(s) {}

        // A writable view reads as a const one.
        ConstChannelView(const ChannelView& v) noexcept  // NOLINT(google-explicit-constructor)
            : chans(v.chans), numChans(v.numChans), numSamples(v.numSamples) {}

        [[nodiscard]] bool empty() const noexcept
        {
            return chans == nullptr || numChans <= 0 || numSamples <= 0;
        }

        [[nodiscard]] const float* channel(int c) const noexcept { return chans[c]; }
    };
}
