#pragma once

#include <string>
#include <vector>
#include "../machine/IMachine.h"

namespace lockstep
{
    // A saved per-track "sound" bundle: machine identity + parameter frame + sample ref.
    // Stored at Project scope so any track in any pattern can recall it.
    struct SoundEntry
    {
        std::string name = "Sound";
        std::string machineId = "lockstep.sample.v1";
        ParamFrame baseParams{};
        int samplePoolIndex = -1;  // -1 = no sample (or MIDI-out destination)
        std::string destinationId = "";  // for MIDI-out entries
    };

    // Project-scope library of SoundEntry bundles.
    // Independent of the audio-engine SamplePool (which stores PCM data).
    struct SoundPool
    {
        std::vector<SoundEntry> entries{};

        [[nodiscard]] int size() const { return static_cast<int>(entries.size()); }
        [[nodiscard]] bool empty() const { return entries.empty(); }

        const SoundEntry* get(int index) const
        {
            if (index < 0 || index >= size()) return nullptr;
            return &entries[static_cast<std::size_t>(index)];
        }

        // Append an entry and return its index.
        int push(SoundEntry e)
        {
            entries.push_back(std::move(e));
            return size() - 1;
        }

        void remove(int index)
        {
            if (index < 0 || index >= size()) return;
            entries.erase(entries.begin() + index);
        }
    };
}
