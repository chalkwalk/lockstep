#pragma once

#include <span>
#include <string>
#include <vector>
#include <juce_audio_basics/juce_audio_basics.h>
#include "ConsoleMode.h"

namespace lockstep
{
    inline constexpr int kParamsPerPage = 8;   // MZ display width (MHX 4x2 grid) — UI constant, not a machine limit

    // Variable-length parameter frame, sized to the machine's numParams() at attachment.
    using ParamFrame = std::vector<float>;

    // Per-slot descriptor declared by each machine.
    // id is stable across releases (used as the serialisation key).
    // index is the machine's dense runtime position (0..numParams()-1).
    struct ParamSpec
    {
        enum class Unit
        {
            None,
            Ms,
            Semitones,
            Percent
        };

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

        // Variant declares which Func tier this slot belongs to (MHY).
        // Primary slots populate the section's no-modifier pages.
        // Secondary slots populate the Func+section pages — used for machine
        // deep-dives (FM modulation matrix, VA voice-mode block) that we want
        // available in two presses without crowding the primary section.
        enum class Variant
        {
            Primary,
            Secondary
        };

        juce::String id;            // stable serialisation key
        juce::String label;         // shown in the Manipulation Zone
        float minValue = 0.0f;
        float maxValue = 1.0f;
        float defaultValue = 0.0f;
        bool isStepped = false;
        Unit unit = Unit::None;
        int sectionIndex = 0;     // which canonical section (0..kMaxSections-1)
        Role role = Role::None;
        Variant variant = Variant::Primary;  // MHY: primary vs Func-secondary page (appended last to preserve aggregate-init)

        // Optional textual labels for stepped/enum slots (MHZ.2.5).
        // Non-empty → MZ renders valueLabels[round(value)] instead of a numeric string.
        // Must point to static-lifetime data; the span is non-owning.
        std::span<const char* const> valueLabels = {};

        // When true, writeParam() snaps this slot's value to the nearest
        // zero-crossing in the currently-loaded sample before storing it.
        // Applies to normalised [0..1] position slots (start, length, loop_*).
        bool zeroCrossingSnap = false;

        // Encoder curve following JUCE NormalisableRange::skew semantics.
        // 1.0 = linear (default). < 1 biases resolution toward the low end of
        // the range (exponential feel for time parameters). Applied by the MZ
        // rotary only — P-Lock and serializer always work with actual values.
        float skew = 1.0f;

        // Optional contextual label hook (§6.10).  When non-null, ManipulationZone
        // calls contextLabel(frame) instead of using the static `label` field.
        // The frame is the track's current base ParamFrame (not the resolved per-step
        // frame) so the label reflects the persistent machine state.
        // Use a function pointer so the struct stays POD-friendly for the future
        // Machine ABI (§36) — the hook's identity is baked into the function itself.
        juce::String (*contextLabel)(const ParamFrame&) = nullptr;

        // Optional labels rendered (by formatParamValue) when the value rests at the
        // floor / ceiling of the range. Presentation only — the stored/processed
        // value stays the canonical float; these just name the extremes (e.g. "Auto"
        // at min for a 0=auto attack/release, "Off"/"Full"/"Inf" at either end).
        // nullptr = numeric. Each uses a small epsilon so values resting a hair from
        // the extreme still read as the label.
        const char* minLabel = nullptr;
        const char* maxLabel = nullptr;
    };

    // Returned by LockstepProcessor::section() after augmenting the machine's
    // raw label with computed layout fields (firstSlot, pageCount).
    // IMachine::section() only fills `label` and `parentCanonical`; the processor
    // fills firstSlot and pageCount.
    struct SectionInfo
    {
        juce::String label;
        int firstSlot = -1;  // dense index of the first slot; -1 = empty section
        int pageCount = 0;   // ceil(slotCount / kParamsPerPage); 0 = empty section
        // -1: this is a canonical section (sectionIndex 0..kMaxSections-1).
        // >=0: this is an extension of canonical section [parentCanonical], reached
        //      by extra presses of the same section key after cycling canonical pages.
        int parentCanonical = -1;
    };

    class IMachine
    {
    public:
        virtual ~IMachine() = default;

        virtual void prepare(double sampleRate, int maxBlockSize) = 0;
        virtual void reset() = 0;

        // Gently release every currently-sounding internal voice: move amp/voice
        // envelopes into their RELEASE stage so a transport stop lets held notes
        // ring out and die naturally instead of freezing mid-note. This is NOT the
        // hard reset() — it preserves tails. Default no-op for machines with no
        // sustained internal voices (Stub/Route/MIDI-out/capture). Voiced synth
        // machines override it. Called on the transport-stop edge; the sequencer's
        // own note-off dispatch handles gated trigs, this catches whatever it misses
        // (open-ended / held voices the pending-note-off bookkeeping doesn't track).
        virtual void releaseAllVoices() {}

        // The sequencer resolves Override-ELSE-Base into a single ParamFrame
        // per block and hands it across the boundary. The machine writes
        // additively into `buffer`. `events` carries note-on/off from the
        // sequencer and from external MIDI (already routed to this track).
        virtual void process(const juce::MidiBuffer& events,
                             const ParamFrame& params,
                             juce::AudioBuffer<float>& buffer) = 0;

        // Schema — immutable after construction. Queried on the UI thread.
        virtual int numParams() const = 0;
        virtual ParamSpec paramSpec(int index) const = 0;

        // Section taxonomy. Up to kMaxSections sections; fewer is fine.
        // Section membership is declared per-slot via ParamSpec::sectionIndex.
        static constexpr int kMaxSections = 6;

        // Canonical label for each of the kMaxSections section-bar keys.
        // A machine section is "available" when at least one slot has a
        // matching sectionIndex; the rendering always shows these labels.
        // Canonical-six labels for the section bar (MHY rename: LFO -> MOD).
        // The MOD section is broader than just LFO — it hosts modulation
        // matrices, per-operator envelopes, voice-mode, and portamento for
        // machines that have them. See DESIGN §6.1.1.
        static constexpr std::array<const char*, kMaxSections> kCanonicalSectionNames = {
            "TRIG", "SRC", "FILTER", "AMP", "MOD", "FX"
        };

        // Canonical section indices into kCanonicalSectionNames. SRC is the
        // note domain: it owns the per-step trig note/velocity/gate payload, so
        // section-scoped clears on SRC also wipe the trig override (DESIGN §13.2).
        static constexpr int kTrigSecIdx = 0;
        static constexpr int kSrcSecIdx  = 1;
        virtual int numSections() const { return 0; }
        virtual SectionInfo section(int /*index*/) const { return {}; }

        // Stable string ID used for serialization and factory dispatch.
        // Format: "lockstep.<engine>.<version>", e.g. "lockstep.sample.v1".
        [[nodiscard]] virtual const char* machineId() const = 0;

        // Short display badge (1-4 chars) shown in the track VU area.
        // Return "" to suppress the badge (StubMachine only).
        // May reflect machine state (e.g. a multi-variant machine returning
        // different labels per variant), so it is called per-repaint.
        [[nodiscard]] virtual const char* badge() const noexcept = 0;

        // Bounded polyphony enum (0..4). The step-side chord ceiling is
        // kMaxNotesPerStep (4), so the type itself forbids requesting more.
        enum class Polyphony : int
        {
            V0 = 0,
            V1 = 1,
            V2 = 2,
            V3 = 3,
            V4 = 4
        };

        // Current live voice count. Pulled by the sequencer per trig, so the
        // machine can switch modes at runtime by returning a different value
        // (e.g. VA Mono ↔ Para, FM Mono ↔ Poly). V0 = doesn't accept notes /
        // MIDI-out passthrough (sequencer governs note count from the step).
        // `baseParams` is the track's base ParamFrame (not the per-step resolved
        // frame, since the clamp runs before frame resolution); machines that
        // route their voice-mode slot via base-only writes can read it here.
        virtual Polyphony currentVoices(const ParamFrame& baseParams) const
        {
            (void)baseParams;
            return Polyphony::V1;
        }

        // sampleClass() (9.18): which SamplePool origins this machine's sample
        // picker offers, so the two picker families stay disjoint — a Stream entry
        // never lands in a PCM player (which would decode nothing and play silence)
        // and a resident-PCM entry never lands in a StreamMachine. Pcm players
        // (Sample / Slice / Stretch) resolve File + volatile captures; a
        // StreamMachine references a disk file streamed on the fly (Stream). None =
        // not a sample machine (no picker); the default.
        enum class SampleClass { None = 0, Pcm, Stream };
        [[nodiscard]] virtual SampleClass sampleClass() const { return SampleClass::None; }

        // sequencesTrigs(): false for control-only machines (Route, and future
        // pure-control engines) that produce no note voices and derive nothing
        // from note-on. When false, a step press places a *lock-only* anchor
        // (Step.lockOnly) instead of a note trig: the grid becomes a bank of
        // per-step P-Lock carriers, never emitting notes. P-Locks still ride.
        // Default true — ordinary note-driven machines are unaffected.
        virtual bool sequencesTrigs() const { return true; }

        // Machine console (7b). A console repurposes the step grid as a
        // machine-specific control surface (rendered on SurfaceLayer::MachineConsole,
        // generalising the hard-coded looper console). Default None.
        //   AlwaysOn  — shown whenever the track is focused (like the looper).
        //   OnDemand  — opened/closed by long-pressing consoleSectionIndex()'s key.
        virtual ConsoleMode consoleMode() const { return ConsoleMode::None; }

        // For an OnDemand console, the canonical section key (0..kMaxSections-1)
        // whose long-press opens/closes it. Ignored when consoleMode() != OnDemand.
        // Defaults to SRC — the note/source domain most consoles belong to.
        virtual int consoleSectionIndex() const { return kSrcSecIdx; }

        // hasInternalAmp(): return true if the machine shapes its own amplitude
        // envelope. When true, the ENVELOPE block (AHDSR + gate source) is omitted
        // from the track's slot space — the CHANNEL block (level/pan/sendA/sendB)
        // is always present regardless of this flag (DESIGN §14).
        // hasInternalFilter() was deleted in 8.28: the track filter is now universal
        // (with an OFF mode for machines that don't need it).
        virtual bool hasInternalAmp() const { return false; }

        // MIDI-out machines override both of these. isMidiOut() lets the processor
        // skip the audio path without a dynamic_cast. processMidi() is called instead
        // of process(); midiOut receives channel-remapped notes and CC messages that
        // the processor routes to the host MIDI output (plugin) or to the machine's
        // open juce::MidiOutput device (standalone, handled internally).
        virtual bool isMidiOut() const { return false; }
        virtual void processMidi(const juce::MidiBuffer& /*events*/,
                                 const ParamFrame& /*params*/,
                                 juce::MidiBuffer& /*midiOut*/) {}

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
