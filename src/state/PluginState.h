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
        // v11: Scene::globalPhrase removed; diagonal pinned (scene N plays row N).
        //      Legacy "gp" values are materialised onto the diagonal at load time.
        // v12: per-step retrig rate (hasRetrig/retrigRate) + sound_id P-Lock.
        // v13: per-track insert chains (effectId/baseParams/bypass) in Kit node.
        // v14: per-song master FX inserts (MasterIns nodes in Song).
        // v15: P-Lock entries use string param id ("id") instead of int slot ("s").
        //      Loader accepts both; "s" int entries still load (legacy compat).
        // v16: Project::soundPool (SoundPool / SE nodes) now serialized.
        //      Missing SoundPool node on load = empty pool (trivial upgrade from v15).
        // v17: Song::masterSends (MasterSnd nodes) + TrackAmpState sendA/sendB slots 8-9.
        //      Missing MasterSnd nodes = empty sends; sendA/B P-Locks use string ids.
        // v18: TrackAmpState split into TrackChannelState (level/pan/sendA/sendB) +
        //      TrackEnvState (gateSrc/att/hld/dec/sus/rel); FLTR always present; disk
        //      ids unchanged (lockstep.amp.* / lockstep.fltr.*); trivial stamp upgrade.
        // v19: TrackKit gains densityMusicality (dMus) + densitySelection (dSel);
        //      defaults Mixed/Scrub; ephemeral density amounts not serialized.
        // v20: TrackKit::divider replaced by subdivIndex (int, 0-26, default 18 = 1/16 straight).
        //      Old kDiv values (1-16 = 0.25*d PPQ) remapped to nearest combined index.
        //      TrackKit gains velMode/velBlend/velDepth/velCenter for the live velocity overlay.
        // v21: Hierarchical time signature (DESIGN §4.8) + hierarchical tempo (DESIGN §4.9).
        //      Project::defaultTimeSig (Set-level) in NewHierarchy root.
        //      Song::hasTimeSig + Song::timeSig optional override in Song node.
        //      Scene::hasTimeSig presence flag; kCtN/kCtD only written when hasTimeSig true.
        //      Missing time-sig fields → 4/4 default; trivial stamp upgrade from v20.
        //      Song::hasTempo + Song::tempoRatio; Scene::hasTempo + Scene::tempoRatio.
        //      Missing tempo fields → ratio 1.0 (no override); backward-compat transparent.
        // v22: Hierarchical key signature (DESIGN §4.10). Project::defaultKeySig
        //      (Set-level), Song::hasKeySig+keySig, Scene::hasKeySig+coreKeySig.
        //      KeySig stored as root/brightness/mods-bitmask/symmetric. Missing
        //      fields → D Dorian, no overrides; trivial stamp upgrade from v21.
        // v23: per-track Scale stage (DESIGN §4.10) — TrackKit::scaleMode
        //      (Off/Snap/Filter) in the Kit node. Missing → Off; trivial upgrade.
        // v26: cached per-entry sample analysis (4.9) — SamplePool Entry nodes
        //      gain an/bpm/keyR/keyB/tune, written only for analysed entries and
        //      keyed by the existing sample hash. Missing props = "re-analyse on
        //      load" = exact v25 behaviour; trivial stamp upgrade from v25.
        inline constexpr int kCurrentVersion = 29;

        void writeTo(juce::MemoryBlock& dest, LockstepProcessor& proc);
        void readFrom(const void* data, int sizeInBytes, LockstepProcessor& proc);

        // File-based project I/O — shares the same serializer and upgrade chain.
        // .lockstep files are plain XML (human-readable, diffable).
        // writeToFile: first calls writeBackWorkingToActive so edits are captured.
        // readFromFile: returns false and leaves proc UNCHANGED if the file cannot
        //   be parsed (fail-safe — no partial state corruption).
        void writeToFile(const juce::File& file, LockstepProcessor& proc);
        bool readFromFile(const juce::File& file, LockstepProcessor& proc);

        // Tree-level helpers (exposed for testing and for buildStateTree / applyStateTree sharing).
        juce::ValueTree buildStateTree(LockstepProcessor& proc);
        void applyStateTree(juce::ValueTree root, LockstepProcessor& proc);

        // Exposed for testing: normalises any historical state tree to the
        // current version by applying each upgrade function in sequence.
        juce::ValueTree applyUpgrades(juce::ValueTree tree);
    }
}
