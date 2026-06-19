#include "DrumSynthMachine.h"
#include "MachineParamTable.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace lockstep
{
    DrumSynthMachine::DrumSynthMachine() = default;
    DrumSynthMachine::~DrumSynthMachine() = default;

  // ---------------------------------------------------------------------------
  // Lifecycle

    void DrumSynthMachine::prepare(double sampleRate, int /*maxBlockSize*/)
    {
        sampleRate_ = sampleRate;
        reset();
    }

    void DrumSynthMachine::reset()
    {
        voice_ = DrumVoice{};
        voice_.ampEnv.prepare(sampleRate_);
    }

  // ---------------------------------------------------------------------------
  // Schema

    namespace ds_u
    {
        static constexpr uint8_t None = 0, Ms = 1, Semi = 2, Pct = 3;
    }
    namespace ds_r
    {
        static constexpr uint8_t None = 0, Pitch = 1, Level = 3,
                                 Atk = 8, Hold = 9, Dcy = 10;
    }

    namespace
    {
        static constexpr const char* kDSTypeLabels[] = {
            "KICK", "SNARE", "HAT", "TOM", "CLAP", "COWBELL", "CYMBAL", "RIMSHOT", nullptr
        };
        static constexpr const char* kDSRetrigLabels[] = { "LEGATO", "RETRIG", nullptr };
    }

  // { id, label, min, max, def, skew, stepped, unit, role, variant, section, zcSnap, labels }
    static constexpr ParamRow kDSParams[] = {
    // --- SRC (section 1) ---
        { "drum_type", "Type", 0.f, 7.f, 0.f, 1.f, 1, ds_u::None, ds_r::None, 0, 1, 0, kDSTypeLabels }, //  0
        { "drum_tune", "Tune", -24.f, 24.f, 0.f, 1.f, 0, ds_u::Semi, ds_r::Pitch, 0, 1, 0, nullptr }, //  1
        { "drum_sweep", "Sweep", 0.f, 48.f, 24.f, 1.f, 0, ds_u::Semi, ds_r::None, 0, 1, 0, nullptr }, //  2
        { "drum_sweep_decay", "Swp Dec", 1.f, 500.f, 60.f, 0.3f, 0, ds_u::Ms, ds_r::None, 0, 1, 0, nullptr }, //  3
        { "drum_punch", "Punch", 0.f, 1.f, 0.5f, 1.f, 0, ds_u::None, ds_r::None, 0, 1, 0, nullptr }, //  4
        { "drum_tone", "Tone", 0.f, 1.f, 0.3f, 1.f, 0, ds_u::None, ds_r::None, 0, 1, 0, nullptr }, //  5
        { "drum_body", "Body", 0.f, 1.f, 0.5f, 1.f, 0, ds_u::None, ds_r::None, 0, 1, 0, nullptr }, //  6
        { "drum_snap", "Snap", 0.f, 1.f, 0.5f, 1.f, 0, ds_u::None, ds_r::None, 0, 1, 0, nullptr }, //  7
    // --- AMP (section 3) ---
        { "drum_attack", "Attack", 0.f, 50.f, 2.f, 0.3f, 0, ds_u::Ms, ds_r::Atk, 0, 3, 0, nullptr }, //  8
        { "drum_hold", "Hold", 0.f, 200.f, 0.f, 0.3f, 0, ds_u::Ms, ds_r::Hold, 0, 3, 0, nullptr }, //  9
        { "drum_decay", "Decay", 1.f, 5000.f, 500.f, 0.3f, 0, ds_u::Ms, ds_r::Dcy, 0, 3, 0, nullptr }, // 10
        { "drum_noise_decay", "Nz Dec", 1.f, 2000.f, 200.f, 0.3f, 0, ds_u::Ms, ds_r::None, 0, 3, 0, nullptr }, // 11
        { "drum_level", "Level", 0.f, 1.f, 0.5f, 1.f, 0, ds_u::Pct, ds_r::Level, 0, 3, 0, nullptr }, // 12
        { "drum_retrig", "Retrig", 0.f, 1.f, 0.f, 1.f, 1, ds_u::None, ds_r::None, 0, 3, 0, kDSRetrigLabels }, // 13
        { "drum_vel_sens", "Vel Sens", 0.f, 1.f, 0.f, 1.f, 0, ds_u::Pct, ds_r::None, 0, 3, 0, nullptr }, // 14
    };
    static_assert(std::size(kDSParams) == DrumSynthMachine::kNumSlots,
                  "kDSParams row count must equal kNumSlots");

    // §6.10 contextLabel helpers — per-slot, per-type alias tables.
    // Each function takes the full ParamFrame and reads kSlotType to choose the label.
    namespace
    {
        // Returns the drum type as an int [0..7] from a ParamFrame.
        // kSlotType = 0 (hardcoded to avoid private-member access in static function).
        int dsType(const ParamFrame& f)
        {
            if (f.empty()) return 0;
            return std::clamp(static_cast<int>(std::lround(f[0])), 0, 7);
        }

        // Slot 2 — Sweep: pitch sweep (KICK/TOM), tap count (CLAP), interval (COWBELL), spread (CYMBAL)
        juce::String sweepLabel(const ParamFrame& f)
        {
            switch (dsType(f))
            {
                case 4: return "Taps";      // CLAP: tap count
                case 5: return "Interval";  // COWBELL: two-osc interval
                case 6: return "Spread";    // CYMBAL: partial spread
                default: return "Sweep";
            }
        }

        // Slot 3 — Swp Dec: tap spacing (CLAP), otherwise sweep decay
        juce::String swpDecLabel(const ParamFrame& f)
        {
            return (dsType(f) == 4) ? juce::String("Tap Spac") : juce::String("Swp Dec");
        }

        // Slot 4 — Punch: click transient level (not used by SNARE/HAT/CLAP/CYMBAL)
        juce::String punchLabel(const ParamFrame& f)
        {
            switch (dsType(f))
            {
                case 1: return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));        // SNARE: Snap replaces punch
                case 2: return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));        // HAT: no click
                case 4: return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));        // CLAP: no click
                case 6: return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));        // CYMBAL: no click
                default: return "Punch";
            }
        }

        // Slot 5 — Tone: waveshaper (KICK/TOM), BP cutoff (SNARE), resonance (HAT),
        //                BP resonance (CLAP/RIMSHOT), ring Q (COWBELL), reso (CYMBAL)
        juce::String toneLabel(const ParamFrame& f)
        {
            switch (dsType(f))
            {
                case 0: return "Drive";     // KICK
                case 1: return "BP Freq";   // SNARE: noise bandpass cutoff (500–8kHz)
                case 2: return "Reso";      // HAT: HP filter resonance (svfK)
                case 3: return "Drive";     // TOM: same as KICK (scaled 0.25×)
                case 4: return "BP Reso";   // CLAP: bandpass resonance
                case 5: return "Ring Q";    // COWBELL: bandpass ring Q
                case 6: return "Reso";      // CYMBAL: HP resonance
                case 7: return "BP Reso";   // RIMSHOT: bandpass resonance
                default: return "Tone";
            }
        }

        // Slot 6 — Body: snare body/noise balance, hat HP cutoff,
        //                 clap tail length, cowbell BP centre, cymbal HP cutoff, rimshot tok/crack
        juce::String bodyLabel(const ParamFrame& f)
        {
            switch (dsType(f))
            {
                case 0: return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));         // KICK: unused
                case 1: return "Tone Mix";  // SNARE: body(sine)/noise balance
                case 2: return "HP Cut";    // HAT: highpass cutoff (4–18 kHz via Tune; Body unused)
                case 3: return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));         // TOM: unused
                case 4: return "Tail";      // CLAP: tail decay length (20–180 ms)
                case 5: return "BP Ctr";    // COWBELL: bandpass centre (1.5×–3.5× upper osc)
                case 6: return "HP Cut";    // CYMBAL: highpass cutoff (300–3000 Hz)
                case 7: return "Tok/Crk";   // RIMSHOT: tok vs crack balance
                default: return "Body";
            }
        }

        // Slot 7 — Snap: snap transient (SNARE), sizzle amount (CYMBAL), unused otherwise
        juce::String snapLabel(const ParamFrame& f)
        {
            switch (dsType(f))
            {
                case 1: return "Snap";      // SNARE: click/snap transient level
                case 6: return "Sizzle";    // CYMBAL: noise sizzle amount
                default: return juce::String(juce::CharPointer_UTF8("\xe2\x80\x94"));        // unused for other types
            }
        }
    }

    ParamSpec DrumSynthMachine::paramSpec(int index) const
    {
        if (index < 0 || index >= kNumSlots) return {};
        auto spec = toParamSpec(kDSParams[static_cast<std::size_t>(index)]);
        // §6.10: attach contextLabel for type-dependent SRC slots.
        switch (index)
        {
            case kSlotSweep:     spec.contextLabel = sweepLabel;  break;
            case kSlotSweepDecay:spec.contextLabel = swpDecLabel; break;
            case kSlotPunch:     spec.contextLabel = punchLabel;  break;
            case kSlotTone:      spec.contextLabel = toneLabel;   break;
            case kSlotBody:      spec.contextLabel = bodyLabel;   break;
            case kSlotSnap:      spec.contextLabel = snapLabel;   break;
            default: break;
        }
        return spec;
    }

    SectionInfo DrumSynthMachine::section(int index) const
    {
        switch (index)
        {
            case 1:  return { "SRC" };
            case 3:  return { "AMP" };
            default: return {};
        }
    }

  // ---------------------------------------------------------------------------
  // Voice active

    bool DrumSynthMachine::isVoiceActive() const
    {
        return voice_.active;
    }

  // ---------------------------------------------------------------------------
  // File-scope DSP helpers

    namespace
    {

        float paramAt(const ParamFrame& p, int slot)
        {
            return p[static_cast<std::size_t>(slot)];
        }

    // Advance noise decay envelope; returns current level.
        float advanceNoise(DrumSynthMachine::DrumVoice& v)
        {
            using P = DrumSynthMachine::NoisePhase;
            if (v.noisePhase != P::Decay) return 0.f;
            if (v.noiseDecaySamples > 0.f)
                v.noiseLevel -= 1.f / v.noiseDecaySamples;
            v.noiseLevel = std::max(0.f, v.noiseLevel);
            if (v.noiseLevel <= 0.f)
                v.noisePhase = P::Idle;
            return v.noiseLevel;
        }

    // Advance click/snap transient; returns current level before decay.
        float advanceClick(DrumSynthMachine::DrumVoice& v)
        {
            if (v.clickLevel <= 0.f) return 0.f;
            const float out = v.clickLevel;
            v.clickLevel -= 1.f / v.clickDecaySamples;
            v.clickLevel = std::max(0.f, v.clickLevel);
            return out;
        }

    // TPT SVF — returns bandpass output.
        float svfBand(DrumSynthMachine::DrumVoice& v, float x)
        {
            const float hp = (x - v.svfK * v.svfBand - v.svfLow) / (1.f + v.svfG * (v.svfG + v.svfK));
            const float bp = v.svfG * hp + v.svfBand;
            const float lp = v.svfG * bp + v.svfLow;
            v.svfBand = 2.f * bp - v.svfBand;
            v.svfLow = 2.f * lp - v.svfLow;
            return bp;
        }

    // TPT SVF — returns highpass output.
        float svfHigh(DrumSynthMachine::DrumVoice& v, float x)
        {
            const float hp = (x - v.svfK * v.svfBand - v.svfLow) / (1.f + v.svfG * (v.svfG + v.svfK));
            const float bp = v.svfG * hp + v.svfBand;
            const float lp = v.svfG * bp + v.svfLow;
            v.svfBand = 2.f * bp - v.svfBand;
            v.svfLow = 2.f * lp - v.svfLow;
            return hp;
        }

    } // namespace

  // ---------------------------------------------------------------------------
  // Note handling

    void DrumSynthMachine::noteOn(int midiNote, float vel, const ParamFrame& params)
    {
        const float sr = static_cast<float>(sampleRate_);
        auto& v = voice_;

        v.velocity = vel;
        v.active = true;
        v.gateOpen = true;

    // Reset SVF state on each new note
        v.svfLow = 0.f;
        v.svfBand = 0.f;

        const float tune = paramAt(params, kSlotTune);
        const float sweep = paramAt(params, kSlotSweep);
        const float sweepDecMs = paramAt(params, kSlotSweepDecay);
        const float punch = paramAt(params, kSlotPunch);
        const float tone = paramAt(params, kSlotTone);
        const float body = paramAt(params, kSlotBody);
        const float snap = paramAt(params, kSlotSnap);
        const float attackMs = paramAt(params, kSlotAttack);
        const float holdMs = paramAt(params, kSlotHold);
        const float decayMs = paramAt(params, kSlotDecay);
        const float noiseDecMs = paramAt(params, kSlotNoiseDecay);

    // Resolve type first — needed by amp setADSR for Hat release time.
        v.currentType = static_cast<DrumType>(
            std::clamp(static_cast<int>(std::lround(paramAt(params, kSlotType))), 0, 7));

    // Amp envelope (AHD). Hat adds 1 ms Release so note-off triggers a fast fade.
        static constexpr float kHatCloseMs = 1.f;
        const float releaseMs = (v.currentType == DrumType::Hat) ? kHatCloseMs : 0.f;
        v.ampEnv.setADSR(attackMs, decayMs, 0.f, releaseMs, holdMs, /*forceZeroSustain=*/true);
        v.ampEnv.gateOn();

    // Noise envelope
        v.noiseDecaySamples = msToSamples(noiseDecMs, sampleRate_);
        v.noiseLevel = 1.f;
        v.noisePhase = NoisePhase::Decay;

    // Click transient (~3 ms)
        static constexpr float kClickMs = 3.f;
        v.clickDecaySamples = std::max(1.f, msToSamples(kClickMs, sampleRate_));
        v.clickLevel = punch;

    // Snap transient for snare (uses the click slot with snap param level)
    // Handled per-type below.

        v.phase = 0.0;

        switch (v.currentType)
        {
            case DrumType::Kick:
            case DrumType::Tom:  {
                v.targetHz = noteHz(midiNote, tune);
                v.pitchHz = noteHz(midiNote, tune + sweep);
                v.sweepSamples = std::max(1.f, msToSamples(sweepDecMs, sampleRate_));
                v.sweepTimer = 0.f;
                v.svfG = 0.f;
                v.svfK = 0.f;
                break;
            }
            case DrumType::Snare: {
                v.targetHz = noteHz(midiNote, tune);
                v.pitchHz = v.targetHz;
                v.sweepTimer = v.sweepSamples;  // no pitch sweep
        // Bandpass SVF for noise colouring: tone maps 500–8000 Hz
                const float noiseCutHz = 500.f + tone * 7500.f;
                v.svfG = std::tan(std::numbers::pi_v<float> * noiseCutHz / sr);
                v.svfK = 1.5f;
        // Use snap param for the click transient instead of punch
                v.clickLevel = snap;
                break;
            }
            case DrumType::Hat: {
                v.pitchHz = 0.f;
        // Highpass SVF: tune (−24..+24 semitones) maps 4–18 kHz
                const float normalised = (tune + 24.f) / 48.f;
                const float cutHz = 4000.f + normalised * 14000.f;
                v.svfG = std::tan(std::numbers::pi_v<float> * std::min(cutHz / sr, 0.499f));
                v.svfK = 1.f + tone * 3.f;
        // Hat has no click transient
                v.clickLevel = 0.f;
                break;
            }
            case DrumType::Cowbell: {
        // 2 square oscillators; Sweep (0–48 semitones) / 4 = interval in semitones
        // Default Sweep=24 → 6-semitone interval (ratio ≈ 1.498), close to classic 808.
                v.pitchHz = noteHz(midiNote, tune);
                v.targetHz = v.pitchHz * std::pow(2.f, sweep / (4.f * 12.f));
                v.sqPhases[0] = v.sqPhases[1] = 0.0;
        // Bandpass *ring* (not highpass) centred above the upper oscillator gives the
        // focused metallic "bonk". Body 0→1 sweeps the centre 1.5×–3.5× the upper osc
        // (default 2.5×); Tone raises Q (lower k) for a tighter, more ringing tone.
                const float centreHz = v.targetHz * (1.5f + body * 2.f);
                v.svfG = std::tan(std::numbers::pi_v<float> * std::min(centreHz / sr, 0.499f));
                v.svfK = 0.3f + (1.f - tone) * 1.2f;
        // Click transient gives the hard attack "thock"
                v.clickLevel = punch;
                break;
            }
            case DrumType::Clap: {
                v.pitchHz = 0.f;
        // BP SVF: tune (−24..+24) maps 500–4000 Hz, tone → Q
                const float normalised = (tune + 24.f) / 48.f;
                const float cutHz = 500.f + normalised * 3500.f;
                v.svfG = std::tan(std::numbers::pi_v<float> * std::min(cutHz / sr, 0.499f));
                v.svfK = 1.f + tone * 5.f;
        // Multi-tap: sweep (0–48 semitones) → tap count (1–5),
        //            sweepDecMs / 25 → tap spacing in ms (default 60/25 = 2.4 ms)
                v.clapTapCount = std::clamp(1 + static_cast<int>(sweep * 4.f / 48.f), 1, 5);
                v.clapTapSpacing = std::max(1, static_cast<int>(sweepDecMs * 0.001f * sr / 25.f));
        // Fixed 5 ms tap decay; precompute per-sample multiply coefficient
                const float tapDecaySamples = std::max(1.f, msToSamples(5.f, sampleRate_));
                v.clapTapDecayCoef = std::exp(-1.f / tapDecaySamples);
                v.voiceSampleCount = 0;
                for (float& e : v.clapTapEnvs) e = 0.f;
        // Tail = short "room" decay: Body 0→1 maps 20–180 ms, overriding the shared
        // NzDec so the clap can't drone on (the noise env caps the tail length).
                const float tailMs = 20.f + body * 160.f;
                v.noiseDecaySamples = std::max(1.f, msToSamples(tailMs, sampleRate_));
                v.clickLevel = 0.f;
                break;
            }
            case DrumType::Cymbal: {
        // 6 inharmonic square oscillators = the metallic "clang". A decaying noise
        // sizzle (Snap amount, NzDec length) rides on top, so the hit opens as
        // noise and rings out as metal. Sweep stretches the partial spacing.
                v.pitchHz = noteHz(midiNote, tune);
                for (int j = 0; j < 6; ++j) v.sqPhases[j] = 0.0;
        // Sweep 0..48 → spread 0.5..1.5 (1.0 = canonical 808 ratios at default 24):
        // low = partials compress toward 2× (tonal), high = spread out (clangy).
                v.metalSpread = 0.5f + sweep / 48.f;
        // Body → HP cutoff (300–3000 Hz): trims low rumble, sets brightness.
                const float cutHz = 300.f + body * 2700.f;
                v.svfG = std::tan(std::numbers::pi_v<float> * std::min(cutHz / sr, 0.499f));
                v.svfK = 0.7f + tone * 2.0f;
                v.clickLevel = 0.f;
                break;
            }
            case DrumType::Rimshot: {
        // Pitched "tok" (sine at the note) blended with a bandpassed noise crack.
        // Body = tok↔crack balance; the crack BP sits well above the tok so it
        // reads as a separate sharp transient. Tone widens the crack; Punch = click.
                v.pitchHz = noteHz(midiNote, tune);
                v.phase = 0.0;
                const float centreHz = std::clamp(v.pitchHz * 4.f, 800.f, 6000.f);
                v.svfG = std::tan(std::numbers::pi_v<float> * std::min(centreHz / sr, 0.499f));
                v.svfK = 0.3f + tone * 1.7f;
                v.clickLevel = punch;
                break;
            }
        }
    }

    void DrumSynthMachine::noteOff()
    {
        voice_.gateOpen = false;
    // Hat: note-off triggers the 1 ms Release set in setADSR at noteOn.
        if (voice_.currentType == DrumType::Hat && voice_.active && voice_.ampEnv.isActive())
        {
            voice_.ampEnv.gateOff();
        }
    }

  // ---------------------------------------------------------------------------
  // Process

    void DrumSynthMachine::process(const juce::MidiBuffer& events,
                                   const ParamFrame& params,
                                   juce::AudioBuffer<float>& buffer)
    {
        struct NoteEvent
        {
            int pos;
            int note;
            float vel;
            bool on;
        };
        juce::Array<NoteEvent> noteEvents;
        noteEvents.ensureStorageAllocated(4);

        for (const auto& meta : events)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn())
                noteEvents.add({ meta.samplePosition, msg.getNoteNumber(),
                                 static_cast<float>(msg.getVelocity()) / 127.f, true });
            else if (msg.isNoteOff())
                noteEvents.add({ meta.samplePosition, msg.getNoteNumber(), 0.f, false });
        }

        if (!voice_.active && noteEvents.isEmpty())
            return;

        const int numSamples = buffer.getNumSamples();
        const int numOut = buffer.getNumChannels();
        const float level = paramAt(params, kSlotLevel);
        const float body = paramAt(params, kSlotBody);
        const float velSens = paramAt(params, kSlotVelSens);
    // velGain: lerp between full level (sens=0) and velocity-scaled level (sens=1)
        const float velGain = 1.0f - velSens + velSens * voice_.velocity;
        const double twoPi = 2.0 * std::numbers::pi;

        int evIdx = 0;

        for (int i = 0; i < numSamples; ++i)
        {
      // Dispatch note events at the correct sample position
            while (evIdx < noteEvents.size() && noteEvents[evIdx].pos <= i)
            {
                const auto& ev = noteEvents[evIdx];
                if (ev.on)
                {
                    noteOn(ev.note, ev.vel, params);
                }
                else
                {
                    noteOff();
                }
                ++evIdx;
            }

            if (!voice_.active)
                continue;

      // -----------------------------------------------------------------------
      // Per-sample synthesis

            float out = 0.f;
            auto& v = voice_;

            switch (v.currentType)
            {
        // -------------------------------------------------------------------
                case DrumType::Kick:
                case DrumType::Tom:  {
          // Exponential pitch sweep
                    float instHz;
                    if (v.sweepTimer < v.sweepSamples)
                    {
                        const float t = v.sweepTimer / v.sweepSamples;
            // Exponential interpolation: high → low
                        const float logRatio = (v.pitchHz > 0.f && v.targetHz > 0.f)
                                                   ? std::log(v.pitchHz / v.targetHz)
                                                   : 0.f;
                        instHz = v.targetHz * std::exp(logRatio * (1.f - t));
                        v.sweepTimer += 1.f;
                    }
                    else
                    {
                        instHz = v.targetHz;
                    }

                    v.phase += twoPi * static_cast<double>(instHz) / sampleRate_;
                    if (v.phase >= twoPi) v.phase -= twoPi;

                    const float osc = static_cast<float>(std::sin(v.phase));
                    const float drive = (v.currentType == DrumType::Kick)
                                            ? paramAt(params, kSlotTone)
                                            : (paramAt(params, kSlotTone) * 0.25f);
                    const float shaped = std::tanh(osc * (1.f + drive * 8.f));
                    const float click = xorNoise(v.noiseSeed) * advanceClick(v);
                    const float amp = v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    out = (shaped + click) * amp * velGain * level;
                    break;
                }

        // -------------------------------------------------------------------
                case DrumType::Snare: {
          // Body sine oscillator
                    v.phase += twoPi * static_cast<double>(v.targetHz) / sampleRate_;
                    if (v.phase >= twoPi) v.phase -= twoPi;
                    const float bodyOsc = static_cast<float>(std::sin(v.phase));

          // Noise through bandpass
                    const float noise = svfBand(v, xorNoise(v.noiseSeed)) * advanceNoise(v);

          // Snap transient
                    const float snapSig = xorNoise(v.noiseSeed) * advanceClick(v);

                    const float amp = v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    out = (bodyOsc * body + noise * (1.f - body) + snapSig) * amp * velGain * level;
                    break;
                }

        // -------------------------------------------------------------------
                case DrumType::Hat: {
                    const float filtered = svfHigh(v, xorNoise(v.noiseSeed));
                    const float amp = v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    out = filtered * amp * velGain * level;
                    break;
                }

        // -------------------------------------------------------------------
                case DrumType::Cowbell: {
                    v.sqPhases[0] += static_cast<double>(v.pitchHz) / sampleRate_;
                    if (v.sqPhases[0] >= 1.0) v.sqPhases[0] -= 1.0;
                    v.sqPhases[1] += static_cast<double>(v.targetHz) / sampleRate_;
                    if (v.sqPhases[1] >= 1.0) v.sqPhases[1] -= 1.0;
                    const float sq1 = (v.sqPhases[0] < 0.5) ? 1.0f : -1.0f;
                    const float sq2 = (v.sqPhases[1] < 0.5) ? 1.0f : -1.0f;
                    const float ring = svfBand(v, (sq1 + sq2) * 0.5f);
                    const float click = xorNoise(v.noiseSeed) * advanceClick(v);
                    const float amp = v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    out = (ring + click) * amp * velGain * level;
                    break;
                }

        // -------------------------------------------------------------------
                case DrumType::Rimshot: {
                    const float bodyBlend = paramAt(params, kSlotBody);  // 0 = tok, 1 = crack
          // Pitched tok
                    v.phase += twoPi * static_cast<double>(v.pitchHz) / sampleRate_;
                    if (v.phase >= twoPi) v.phase -= twoPi;
                    const float tok = static_cast<float>(std::sin(v.phase));
          // Bandpassed noise crack
                    const float crack = svfBand(v, xorNoise(v.noiseSeed));
                    const float click = xorNoise(v.noiseSeed) * advanceClick(v);
                    const float amp = v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    out = (tok * (1.f - bodyBlend) + crack * bodyBlend + click) * amp * velGain * level;
                    break;
                }

        // -------------------------------------------------------------------
                case DrumType::Cymbal: {
                    static constexpr double kRatios[6]{ 2.0, 3.0, 3.7, 5.3, 5.9, 6.4 };
                    const float snap = paramAt(params, kSlotSnap);  // sizzle amount
                    const double baseFreq = static_cast<double>(v.pitchHz);
                    const double spread = static_cast<double>(v.metalSpread);
                    float sum = 0.f;
                    for (int j = 0; j < 6; ++j)
                    {
            // Stretch the inharmonic spacing around 2× by metalSpread.
                        const double ratio = 2.0 + (kRatios[j] - 2.0) * spread;
                        v.sqPhases[j] += baseFreq * ratio / sampleRate_;
                        if (v.sqPhases[j] >= 1.0) v.sqPhases[j] -= 1.0;
                        sum += (v.sqPhases[j] < 0.5) ? 1.0f : -1.0f;
                    }
                    const float metal = sum * (1.f / 6.f);
                    const float sizzle = xorNoise(v.noiseSeed) * advanceNoise(v) * snap;
                    const float filtered = svfHigh(v, metal + sizzle);
                    const float amp = v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    out = filtered * amp * velGain * level;
                    break;
                }

        // -------------------------------------------------------------------
                case DrumType::Clap: {
                    const float punch = paramAt(params, kSlotPunch);
                    const float snap = paramAt(params, kSlotSnap);
          // Fire taps at their scheduled sample offsets
                    for (int j = 0; j < v.clapTapCount; ++j)
                    {
                        if (v.voiceSampleCount == j * v.clapTapSpacing)
                            v.clapTapEnvs[j] = punch * 2.f;  // ×2 for impact
                    }
          // Sum and decay active taps; add sustained noise tail via noise envelope
                    float noiseIn = 0.f;
                    for (int j = 0; j < v.clapTapCount; ++j)
                    {
                        if (v.clapTapEnvs[j] > 0.f)
                        {
                            noiseIn += xorNoise(v.noiseSeed) * v.clapTapEnvs[j];
                            v.clapTapEnvs[j] *= v.clapTapDecayCoef;
                            if (v.clapTapEnvs[j] < 1e-5f) v.clapTapEnvs[j] = 0.f;
                        }
                    }
                    noiseIn += xorNoise(v.noiseSeed) * advanceNoise(v) * snap;
                    ++v.voiceSampleCount;
                    const float filtered = svfBand(v, noiseIn);
                    const float amp = v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    out = filtered * amp * velGain * level;
                    break;
                }

                default:
                    (void)v.ampEnv.tick();
                    if (!v.ampEnv.isActive()) v.active = false;
                    break;
            }

            for (int ch = 0; ch < numOut; ++ch)
                buffer.addSample(ch, i, out);
        }
    }
}
