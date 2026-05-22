#pragma once

#include <string>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>

namespace lockstep
{
    inline constexpr int kParamsPerPage = 4;   // MZ display width — UI constant, not a machine limit

    // Variable-length parameter frame, sized to the machine's numParams() at attachment.
    using ParamFrame = std::vector<float>;

    // Per-slot descriptor declared by each machine.
    // id is stable across releases (used as the serialisation key).
    // index is the machine's dense runtime position (0..numParams()-1).
    struct ParamSpec
    {
        enum class Unit { None, Ms, Semitones, Percent };

        // Closed set of semantic roles. Used by Control-All as an id-fallback
        // (broadcast to every track whose schema has the same role when no
        // exact id match exists) and by 16-levels mode to enumerate eligible
        // target parameters. Most slots are Role::None and don't participate.
        enum class Role
        {
            None,
            // Pitch / dynamics
            Pitch,      // fine-tune or coarse pitch offset
            Velocity,   // output velocity / gain
            Level,      // amplitude / volume
            Pan,        // stereo pan
            // Filter
            Cutoff,
            Resonance,
            Drive,
            // Envelope
            Attack,
            Hold,
            Decay,
            Sustain,
            Release,
            // Modulation
            LfoDepth,
            LfoRate,
            LfoShape,
        };

        juce::String id;            // stable serialisation key
        juce::String label;         // shown in the Manipulation Zone
        float minValue     = 0.0f;
        float maxValue     = 1.0f;
        float defaultValue = 0.0f;
        bool  isStepped    = false;
        Unit  unit         = Unit::None;
        int   sectionIndex = 0;     // which section this slot belongs to (0-based)
        Role  role         = Role::None;
    };

    // Returned by LockstepProcessor::section() after augmenting the machine's
    // raw label with computed layout fields (firstSlot, pageCount).
    // IMachine::section() only fills `label`; the processor fills the rest.
    struct SectionInfo
    {
        juce::String label;
        int firstSlot  = 0;   // dense index of the first slot in this section
        int pageCount  = 1;   // ceil(slotCount / kParamsPerPage)
    };

    class IMachine
    {
    public:
        virtual ~IMachine() = default;

        virtual void prepare(double sampleRate, int maxBlockSize) = 0;
        virtual void reset() = 0;

        // The sequencer resolves Override-ELSE-Base into a single ParamFrame
        // per block and hands it across the boundary. The machine writes
        // additively into `buffer`. `events` carries note-on/off from the
        // sequencer and from external MIDI (already routed to this track).
        virtual void process(const juce::MidiBuffer& events,
                             const ParamFrame& params,
                             juce::AudioBuffer<float>& buffer) = 0;

        // Schema — immutable after construction. Queried on the UI thread.
        virtual int       numParams()          const = 0;
        virtual ParamSpec paramSpec(int index) const = 0;

        // Section taxonomy. Up to kMaxSections sections; fewer is fine.
        // Section membership is declared per-slot via ParamSpec::sectionIndex.
        static constexpr int kMaxSections = 6;
        virtual int         numSections()        const { return 0; }
        virtual SectionInfo section(int /*index*/) const { return {}; }

        // Stable string ID used for serialization and factory dispatch.
        // Format: "lockstep.<engine>.<version>", e.g. "lockstep.sampler.v1".
        [[nodiscard]] virtual const char* machineId() const = 0;

        // Voice topology hint. 1 = monophonic with sequencer-managed choke,
        // n>1 = self-managed polyphony, 0 = unbounded / MIDI-out.
        virtual int maxVoices() const { return 1; }

        // Returns true if this machine is currently producing audio (voice active,
        // fading, or pending). Used by the sequencer to decide whether to apply
        // a choke fade before re-triggering a monophonic machine.
        virtual bool isVoiceActive() const { return false; }

        // -----------------------------------------------------------------------
        // Slot identity bridge for serialization.
        // Runtime P-Lock storage uses integer indices; on-disk representation
        // uses stable string ids so slot reordering between releases doesn't break
        // saved patches. Unknown ids on load → drop with a log warning.
        // Both methods are non-virtual and resolve via the virtual schema accessors.

        [[nodiscard]] juce::String idForSlot(int index) const
        {
            if (index < 0 || index >= numParams()) return {};
            return paramSpec(index).id;
        }

        // Returns -1 if no slot with the given id is found.
        [[nodiscard]] int slotForId(const juce::String& id) const
        {
            const int n = numParams();
            for (int i = 0; i < n; ++i)
                if (paramSpec(i).id == id) return i;
            return -1;
        }
    };
}
