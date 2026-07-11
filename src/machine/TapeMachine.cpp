#include "TapeMachine.h"

#include "ChannelPolicy.h"

#include <algorithm>
#include <cmath>

namespace lockstep
{
    ParamSpec TapeMachine::paramSpec(int index) const
    {
        ParamSpec s;
        s.sectionIndex = kSrcSecIdx;
        switch (index)
        {
            case kSlotInputSource:
                s.id = kInputSourceSlotId;
                s.label = "Source";
                s.minValue = 0.0f;
                s.maxValue = kInputSourceMaxValue;
                s.defaultValue = 1.0f;   // External — record live input by default
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kInputSourceLabels.data(),
                                                             kInputSourceLabels.size());
                return s;
            case kSlotMediumLength:
                s.id = "medium_length";
                s.label = "Reel";
                s.minValue = static_cast<float>(kMinMediumSeconds);
                s.maxValue = static_cast<float>(kMaxMediumSeconds);
                s.defaultValue = static_cast<float>(kDefaultMediumSeconds);
                s.unit = ParamSpec::Unit::Ms;  // seconds shown; a reel is a duration
                return s;
            case kSlotMonitor:
                s.id = "tape_monitor";
                s.label = "Mon";
                s.minValue = 0.0f;
                s.maxValue = static_cast<float>(kMonitorLabels.size() - 1);
                s.defaultValue = 0.0f;   // Auto
                s.isStepped = true;
                s.valueLabels = std::span<const char* const>(kMonitorLabels.data(),
                                                             kMonitorLabels.size());
                return s;
            default:
                return {};
        }
    }

    void TapeMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
        setMediumSeconds(mediumSeconds_);
        reset();
    }

    void TapeMachine::reset()
    {
        deck_.setState(dc::DeckState::Playing);  // a tape is always playing its position
        medium_.resetAllUsed();
    }

    void TapeMachine::setMediumSeconds(double seconds)
    {
        mediumSeconds_ = std::clamp(seconds, kMinMediumSeconds, kMaxMediumSeconds);
        const int cap = std::max(1, static_cast<int>(mediumSeconds_ * sampleRate_));
        // Allocate without zero-fill (§40.3 lazy commit): the reel costs address
        // space, and only recorded samples become resident.
        reel_.setSize(2, cap, false, false, false);
        undoReel_.setSize(2, cap, false, false, false);  // fence #8 punch undo (lazy)
        haveUndo_ = false;
        undoLo_ = undoHi_ = -1;
        bindReel();
    }

    void TapeMachine::bindReel() noexcept
    {
        const int cap = reel_.getNumSamples();
        if (cap <= 0) { medium_.unbind(); return; }

        dc::Medium::Config cfg;
        cfg.topology = dc::Topology::Linear;   // a reel, not a loop
        cfg.mediumRate = sampleRate_;          // 1× medium (§40.10)
        cfg.numSubTracks = 1;
        cfg.channelsPerSubTrack = 2;
        cfg.capacitySamples = cap;
        for (int ch = 0; ch < 2; ++ch)
            planes_[static_cast<std::size_t>(ch)] =
                dc::Store{ reel_.getWritePointer(ch), static_cast<std::size_t>(cap) };
        medium_.bindPlanes(cfg, planes_.data(), 2);
    }

    void TapeMachine::applyVerb(int verb)
    {
        switch (verb)
        {
            case 1:  // RecordCycle — punch in / punch out
                if (deck_.state() == dc::DeckState::Recording)
                {
                    // Punch out: the take is committed, the punched span is undoable.
                    deck_.setState(dc::DeckState::Playing);
                    haveUndo_ = (undoLo_ >= 0 && undoHi_ >= undoLo_);
                }
                else
                {
                    // Punch in: begin a fresh undo span (save-before-write fills it).
                    deck_.setState(dc::DeckState::Recording);
                    undoLo_ = undoHi_ = -1;
                    haveUndo_ = false;
                }
                break;
            case 2:  // PlayStop
                deck_.setState(deck_.state() == dc::DeckState::Stopped
                                   ? dc::DeckState::Playing : dc::DeckState::Stopped);
                break;
            case 3:  // Clear — wipe the reel
                medium_.resetAllUsed();
                haveUndo_ = false;
                undoLo_ = undoHi_ = -1;
                deck_.setState(dc::DeckState::Playing);
                break;
            case 4:  // Undo — restore the last punch's original content
                if (haveUndo_)
                {
                    for (int p = undoLo_; p <= undoHi_; ++p)
                        for (int ch = 0; ch < 2; ++ch)
                            medium_.write(0, ch, p, undoReel_.getSample(ch, p));
                    haveUndo_ = false;
                }
                break;
            default:
                break;
        }
    }

    int TapeMachine::dropMarkerHere(int labelId)
    {
        return markers_.drop(transport_.transportPhaseSamples, labelId);
    }

    int TapeMachine::dropMarkerAt(double posSamples, int labelId)
    {
        return markers_.drop(posSamples, labelId);
    }

    double TapeMachine::cueNearest() const noexcept
    {
        const int i = markers_.nearest(transport_.transportPhaseSamples);
        return i < 0 ? -1.0 : markers_.at(i).positionSamples;
    }

    double TapeMachine::cueNext() const noexcept
    {
        const int i = markers_.next(transport_.transportPhaseSamples);
        return i < 0 ? -1.0 : markers_.at(i).positionSamples;
    }

    double TapeMachine::cuePrev() const noexcept
    {
        const int i = markers_.prev(transport_.transportPhaseSamples);
        return i < 0 ? -1.0 : markers_.at(i).positionSamples;
    }

    void TapeMachine::process(const juce::MidiBuffer& /*events*/,
                              const ParamFrame& params,
                              juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int outChans = engineChannels(buffer.getNumChannels());
        if (! medium_.bound() || outChans <= 0) { buffer.clear(); return; }

        const int monMode = (params.size() > kSlotMonitor)
            ? static_cast<int>(std::lround(params[kSlotMonitor])) : 0;

        const bool recording = deck_.state() == dc::DeckState::Recording;
        const bool stopped = deck_.state() == dc::DeckState::Stopped;

        // The playhead IS the transport position (§40.2). At unity medium rate the
        // reel index equals the absolute sample position; a locate jumps the
        // transport, so the head follows for free. Stopped detaches from position.
        const auto posAtBlockStart =
            static_cast<std::int64_t>(std::llround(transport_.transportPhaseSamples));

        // Capture the live input before we overwrite the buffer with playback.
        // (Sub-track 0 only in this slice; the deck is single-sub-track for now.)
        for (int i = 0; i < numSamples; ++i)
        {
            const std::int64_t pos = posAtBlockStart + i;
            for (int ch = 0; ch < outChans; ++ch)
            {
                const float in = buffer.getSample(ch, i);
                float out = 0.0f;

                if (recording && ! stopped)
                {
                    // Fence #8: save the ORIGINAL before overwriting, once per
                    // position per punch (only when the span extends — a
                    // re-touch inside the span keeps the pre-punch value). Channel 0
                    // drives the span bookkeeping so both channels save together.
                    const int ip = static_cast<int>(pos);
                    if (ip >= 0 && ip < undoReel_.getNumSamples())
                    {
                        const bool firstTouch = (undoLo_ < 0) || ip < undoLo_ || ip > undoHi_;
                        if (firstTouch)
                        {
                            for (int c = 0; c < outChans; ++c)
                                undoReel_.setSample(c, ip, medium_.read(0, c, pos));
                            undoLo_ = (undoLo_ < 0) ? ip : std::min(undoLo_, ip);
                            undoHi_ = (undoHi_ < 0) ? ip : std::max(undoHi_, ip);
                        }
                    }
                    // Replace what is on the reel at this position with the input.
                    // Committing first turns virgin tape into silence so a write into
                    // an unrecorded region is exact, not additive.
                    medium_.ensureCommitted(0, ip + 1);
                    medium_.write(0, ch, pos, in);
                    out = in;  // monitor what we are laying down
                }
                else if (! stopped)
                {
                    out = medium_.read(0, ch, pos);              // play the reel
                    if (monMode == 1) out += in;                 // On: live-thru on top
                }
                buffer.setSample(ch, i, out);
            }
        }
    }
}
