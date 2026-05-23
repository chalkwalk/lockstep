#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <juce_core/juce_core.h>

namespace lockstep
{
    // MF.8: factory CC name tables for common hardware MIDI destinations.
    //
    // Each table maps MIDI CC number → short display name (≤8 chars fits the MZ).
    // Tables are sourced from published MIDI implementation charts; verify against
    // the device's current manual if a CC behaves unexpectedly.
    //
    // Usage:
    //   auto table = MidiDevicePresets::getTable("elektron.digitone");
    //   machine->setCCNameTable(std::move(table));
    //
    // The preset id is persisted in PartTrack::midiPresetName so the table
    // is reloaded automatically on session restore.
    namespace MidiDevicePresets
    {
        struct PresetInfo
        {
            std::string id;    // stable identifier stored in PartTrack::midiPresetName
            std::string name;  // display name shown in UI preset picker
        };

        // Returns all available presets in display order.
        std::vector<PresetInfo> listPresets();

        // Returns the CC name table for the given preset id.
        // Empty map if the id is unknown or empty.
        std::unordered_map<int, juce::String> getTable(const std::string& presetId);
    }
}
