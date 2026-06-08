#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lockstep
{
    class LockstepProcessor;

    // Save/load seam. M8 expands this to carry the full sequence, P-Locks,
    // sample-pool refs, and CC mappings alongside the APVTS parameters.
    namespace PluginState
    {
        // Bump when the on-disk format changes in a breaking way.
        // v1: flat Sequence + SamplePool + CCMappings + Misc
        // v2: full Project/Bank/Pattern/Part hierarchy; Sequence/BaseParams
        //     moved into Project node; Misc gains activeBankIdx/activePatternIdx
        // v3: TrigOverride gate stored as MusicalGate enum; per-note velocity added
        // v4: Pattern/Part nodes gain explicit "init=1" attribute; uninitialised
        //     slots are empty by default; PartTrack may hold "lockstep.stub" machineId
        // v5: Phase 7 musical hierarchy — old Project/Bank/Pattern/Part nodes dropped
        //     (clean break); new NewHierarchy node with Song/SongTrack/Kit/Phrase/Section
        // v6: scope-respecting Checkpoints; floor seeded on Song load/switch
        // v7: legacy Bank/Pattern/Part structs deleted; serializer emits new-hierarchy only
        // v8: Scene::phraseIdx[] removed; floor routing is globalPhrase only
        // v9: Step::microOffset added; per-track + global swing APVTS params
        // v10: Swing moved from APVTS into Song/SongTrack/Scene musical state;
        //      Song::swing, Song::SongTrack::swing, Scene::swing added.
        //      v9 APVTS swing values migrated into Song[0] on first load.
        inline constexpr int kCurrentVersion = 10;

        void writeTo(juce::MemoryBlock& dest, LockstepProcessor& proc);
        void readFrom(const void* data, int sizeInBytes, LockstepProcessor& proc);

        // Exposed for testing: normalises any historical state tree to the
        // current version by applying each upgrade function in sequence.
        juce::ValueTree applyUpgrades(juce::ValueTree tree);
    }
}
