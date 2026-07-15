#include "Parameters.h"
#include "ParameterIDs.h"
#include "core/Sequence.h"
#include "core/Track.h"

namespace lockstep
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{ ParamIDs::outputGain, 1 },
            "Output Gain",
            juce::NormalisableRange<float>(-60.0f, 6.0f, 0.01f),
            0.0f));

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ ParamIDs::syncMode, 1 },
            "Sync Mode",
            juce::StringArray{ "Locked", "Auto" },
            0));

        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{ ParamIDs::channelMode, 1 },
            "Channel Mode",
            juce::StringArray{ "Omni", "Per-Track" },
            0));

        // Swing (DESIGN §19.2) lives in Song/Scene musical state, not APVTS.

        for (int t = 0; t < kNumTracks; ++t)
        {
            layout.add(std::make_unique<juce::AudioParameterInt>(
                juce::ParameterID{ ParamIDs::trackLength(t), 1 },
                "Track " + juce::String(t + 1) + " Length",
                1, kMaxStepsPerTrack, 16));

            // Subdivision: combined index 0-26 (see Subdivision.h); default 18 = 1/16 straight.
            layout.add(std::make_unique<juce::AudioParameterInt>(
                juce::ParameterID{ ParamIDs::trackDivider(t), 1 },
                "Track " + juce::String(t + 1) + " Subdivision",
                0, 26, 18));

            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{ ParamIDs::trackMute(t), 1 },
                "Track " + juce::String(t + 1) + " Mute",
                false));

            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{ ParamIDs::trackSolo(t), 1 },
                "Track " + juce::String(t + 1) + " Solo",
                false));

            // 6.4 cue balance: 0 = main only, 1 = cue only (DESIGN §31).
            layout.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{ ParamIDs::cueBalance(t), 1 },
                "Track " + juce::String(t + 1) + " Cue",
                juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
        }

        return layout;
    }
}
