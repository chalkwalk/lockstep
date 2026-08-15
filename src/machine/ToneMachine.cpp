#include "ToneMachine.h"

#include <array>
#include <cmath>

namespace lockstep
{
namespace
{
    // General MIDI's 128 program names, in GM order: sixteen families of eight.
    // That structure is not decoration -- it is exactly two pages of 64 cells
    // with a family per row, which is why the program picker needs no scrolling
    // and no popup (DESIGN §29.3).
    constexpr std::array<const char*, 128> kGmNames = { {
        // Piano
        "Grand", "Bright", "E.Grand", "Honky", "E.Piano1", "E.Piano2", "Harpsi", "Clav",
        // Chromatic percussion
        "Celesta", "Glocken", "MusicBox", "Vibes", "Marimba", "Xylophon", "TubeBell", "Dulcimer",
        // Organ
        "DrawOrg", "PercOrg", "RockOrg", "ChurOrg", "ReedOrg", "Acordion", "Harmonca", "TangoAcc",
        // Guitar
        "NylonGtr", "SteelGtr", "JazzGtr", "CleanGtr", "MuteGtr", "OvrdGtr", "DistGtr", "GtrHarm",
        // Bass
        "AcBass", "FngrBass", "PickBass", "Fretless", "SlapBas1", "SlapBas2", "SynBass1", "SynBass2",
        // Strings
        "Violin", "Viola", "Cello", "Contra", "TremStr", "PizzStr", "Harp", "Timpani",
        // Ensemble
        "String1", "String2", "SynStr1", "SynStr2", "ChoirAah", "VoiceOoh", "SynVoice", "OrchHit",
        // Brass
        "Trumpet", "Trombone", "Tuba", "MuteTrmp", "FrenchHn", "BrassSec", "SynBras1", "SynBras2",
        // Reed
        "SprnoSax", "AltoSax", "TenorSax", "BariSax", "Oboe", "EngHorn", "Bassoon", "Clarinet",
        // Pipe
        "Piccolo", "Flute", "Recorder", "PanFlute", "Bottle", "Shakhchi", "Whistle", "Ocarina",
        // Synth lead
        "Square", "Sawtooth", "CaliopeL", "ChiffLd", "CharngLd", "VoiceLd", "FifthLd", "BassLead",
        // Synth pad
        "NewAge", "WarmPad", "PolySyn", "ChoirPad", "BowedPad", "MetalPad", "HaloPad", "SweepPad",
        // Synth effects
        "Rain", "Sndtrack", "Crystal", "Atmosphr", "Bright", "Goblins", "Echoes", "SciFi",
        // Ethnic
        "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "BagPipe", "Fiddle", "Shanai",
        // Percussive
        "TnklBell", "Agogo", "SteelDrm", "WoodBlok", "TaikoDrm", "MeloTom", "SynthDrm", "RevCymbl",
        // Sound effects
        "GtrFret", "Breath", "Seashore", "BirdTwit", "Phone", "Helicptr", "Applause", "Gunshot"
    } };

    // GM's sixteen families, in order. Short enough for a grid cell.
    constexpr std::array<const char*, 16> kGmFamilies = { {
        "Piano", "Chrom", "Organ", "Guitar",
        "Bass", "Strings", "Ensmbl", "Brass",
        "Reed", "Pipe", "Lead", "Pad",
        "SynFX", "Ethnic", "Perc", "SFX"
    } };

    // 0..1 -> 0..127, the MIDI CC domain.
    int toCc(float v) noexcept
    {
        return std::clamp(static_cast<int>(std::lround(v * 127.0f)), 0, 127);
    }
}   // namespace

std::span<const char* const> ToneMachine::programNames() noexcept
{
    return std::span<const char* const>(kGmNames.data(), kGmNames.size());
}

std::span<const char* const> ToneMachine::familyNames() noexcept
{
    return std::span<const char* const>(kGmFamilies.data(), kGmFamilies.size());
}

void ToneMachine::reset()
{
    primed_ = false;
    if (engine_ != nullptr && channel_ >= 0)
        engine_->allNotesOff(channel_);
}

ParamSpec ToneMachine::paramSpec(int index) const
{
    ParamSpec s;
    switch (index)
    {
        case kProgram:
            s.id = "tone.program";
            s.label = "Program";
            s.minValue = 0.0f;
            s.maxValue = 127.0f;
            s.defaultValue = 0.0f;
            s.isStepped = true;
            s.sectionIndex = kSrcSecIdx;
            s.valueLabels = programNames();
            return s;

        case kDrumKit:
            // 0 = melodic (bank 0). Non-zero picks the Nth kit the bank carries
            // (bank 128). A stepped slot rather than a separate machine, because
            // "which instrument" is one question and GM answers it with a bank.
            s.id = "tone.drumkit";
            s.label = "Kit";
            s.minValue = 0.0f;
            s.maxValue = 16.0f;
            s.defaultValue = 0.0f;
            s.isStepped = true;
            s.sectionIndex = kSrcSecIdx;
            return s;

        case kBrightness:
            s.id = "tone.brightness";  s.label = "Brite";
            s.defaultValue = 0.5f;     s.sectionIndex = kFltrSecIdx;  return s;
        case kResonance:
            s.id = "tone.resonance";   s.label = "Reso";
            s.defaultValue = 0.5f;     s.sectionIndex = kFltrSecIdx;  return s;
        case kAttack:
            s.id = "tone.attack";      s.label = "Attack";
            s.defaultValue = 0.5f;     s.sectionIndex = kAmpSecIdx;   return s;
        case kRelease:
            s.id = "tone.release";     s.label = "Release";
            s.defaultValue = 0.5f;     s.sectionIndex = kAmpSecIdx;   return s;
        case kModWheel:
            s.id = "tone.mod";         s.label = "Mod";
            s.defaultValue = 0.0f;     s.sectionIndex = kModSecIdx;   return s;
        case kPortamento:
            s.id = "tone.porta";       s.label = "Porta";
            s.defaultValue = 0.0f;     s.sectionIndex = kModSecIdx;   return s;

        default:
            return {};
    }
}

SectionInfo ToneMachine::section(int index) const
{
    switch (index)
    {
        case kSrcSecIdx:  return { "SRC" };
        case kFltrSecIdx: return { "FILTER" };
        case kAmpSecIdx:  return { "AMP" };
        case kModSecIdx:  return { "MOD" };
        default:          return {};
    }
}

void ToneMachine::feedEngine(const juce::MidiBuffer& events, const ParamFrame& params)
{
    if (engine_ == nullptr || channel_ < 0 || !engine_->ready())
        return;

    const auto at = [&params](int slot) -> float {
        return slot < static_cast<int>(params.size())
                   ? params[static_cast<std::size_t>(slot)] : 0.0f;
    };

    // ── Program / kit ─────────────────────────────────────────────────────
    // Sent only on change. selectProgram() is allocation-free (the preset cache
    // is the whole reason tone_preset_swap.c exists), but a program change also
    // re-selects the channel's bank, and doing that every block would be noise.
    const float prog = at(kProgram);
    const float kit = at(kDrumKit);
    if (!primed_ || prog != lastSent_[kProgram] || kit != lastSent_[kDrumKit])
    {
        const int kitIdx = static_cast<int>(std::lround(kit));
        if (kitIdx > 0 && !engine_->drumKits().empty())
        {
            const auto& kits = engine_->drumKits();
            const auto n = static_cast<int>(kits.size());
            const auto& k = kits[static_cast<std::size_t>(std::min(kitIdx, n) - 1)];
            engine_->selectProgram(channel_, k.bank, k.program);
        }
        else
        {
            engine_->selectProgram(channel_, tone::ToneEngine::kMelodicBank,
                                   std::clamp(static_cast<int>(std::lround(prog)), 0, 127));
        }
        lastSent_[kProgram] = prog;
        lastSent_[kDrumKit] = kit;
    }

    // ── Continuous controllers ────────────────────────────────────────────
    // Level and pan are pointedly NOT here (CC 7 / CC 10): they belong to
    // Lockstep's own channel strip, and sending them from the machine too would
    // give one fact two owners.
    struct CcMap { int slot; int cc; };
    static constexpr std::array<CcMap, 5> kCcs = { {
        { kBrightness, 74 }, { kResonance, 71 },
        { kAttack, 73 },     { kRelease, 72 },
        { kModWheel, 1 },
    } };

    for (const auto& m : kCcs)
    {
        const auto si = static_cast<std::size_t>(m.slot);
        const float v = at(m.slot);
        if (primed_ && v == lastSent_[si]) continue;
        engine_->controlChange(channel_, m.cc, toCc(v));
        lastSent_[si] = v;
    }

    // Portamento: time on CC 5, and the on/off switch (CC 65) rides with it so
    // one knob means one thing -- zero is off.
    {
        const float v = at(kPortamento);
        if (!primed_ || v != lastSent_[kPortamento])
        {
            engine_->controlChange(channel_, 5, toCc(v));
            engine_->controlChange(channel_, 65, v > 0.0f ? 127 : 0);
            lastSent_[kPortamento] = v;
        }
    }

    primed_ = true;

    // ── Notes ─────────────────────────────────────────────────────────────
    // FluidLite has no internal event queue: note-on/off are direct calls that
    // take effect at the next rendered sample, so sample offsets within the
    // block are not honoured. That is the same granularity every other machine
    // gets from a per-block ParamFrame.
    for (const auto meta : events)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            engine_->noteOn(channel_, msg.getNoteNumber(), msg.getVelocity());
        else if (msg.isNoteOff())
            engine_->noteOff(channel_, msg.getNoteNumber());
        else if (msg.isPitchWheel())
            engine_->pitchBend(channel_, msg.getPitchWheelValue());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine_->allNotesOff(channel_);
    }
}

void ToneMachine::process(const juce::MidiBuffer&, const ParamFrame&,
                          juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
    if (engine_ == nullptr || channel_ < 0 || !engine_->ready())
        return;

    const float* l = engine_->groupLeft(channel_);
    const float* r = engine_->groupRight(channel_);
    if (l == nullptr || r == nullptr) return;

    const int n = std::min(buffer.getNumSamples(), engine_->maxBlockSize());
    if (buffer.getNumChannels() > 0) buffer.copyFrom(0, 0, l, n);
    if (buffer.getNumChannels() > 1) buffer.copyFrom(1, 0, r, n);
}
}   // namespace lockstep
