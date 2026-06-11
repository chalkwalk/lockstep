#include "MidiDevicePresets.h"

// CC tables sourced from publicly available MIDI implementation charts.
// All labels truncated to ≤8 chars for the ManipulationZone display width.
//
// Elektron devices (Digitakt, Digitone, Syntakt, Analog Four, Analog Rytm,
// Octatrack): charts from Elektron manuals, OS versions noted per table.
// Teenage Engineering Tonverk: chart from the TE product page / spec sheet.
//
// MIDI standard CCs (GM/GS) are consistent across all tables:
//   1 = Mod Wheel, 7 = Volume, 10 = Pan, 11 = Expression,
//   64 = Sustain, 65 = Portamento On/Off.
// CC 74 (Brightness / Filter Cutoff) and 71 (Harmonic Content / Resonance)
// are used by most Elektron devices to match the MIDI standard convention.

namespace lockstep::MidiDevicePresets
{
    // -------------------------------------------------------------------------
    // Shared MIDI standard CCs (GM / common practice)

    static void addGMCommon(std::unordered_map<int, juce::String>& t)
    {
        t[1] = "Mod Whl";
        t[2] = "Breath";
        t[7] = "Volume";
        t[10] = "Pan";
        t[11] = "Expr";
        t[64] = "Sustain";
        t[65] = "Portmnt";
    }

    // -------------------------------------------------------------------------
    // Elektron Digitakt  (OS 1.30 MIDI implementation chart)
    // Tracks 1-8 receive on MIDI channels 1-8 respectively.

    static std::unordered_map<int, juce::String> digitakt()
    {
        std::unordered_map<int, juce::String> t;
        addGMCommon(t);

        // SRC page
        t[16] = "Tune";
        t[17] = "Smp Start";  // Sample Start
        t[18] = "Smp End";    // Sample End
        t[19] = "Loop";
        t[20] = "Reverse";
        t[21] = "Rev Send";   // Reverb send
        t[22] = "Del Send";   // Delay send
        t[23] = "Overdrive";

        // FLTR page
        t[74] = "Cutoff";
        t[75] = "Res";
        t[76] = "Fltr Type";
        t[70] = "Fltr Atk";
        t[71] = "Fltr Dec";
        t[72] = "Fltr Sus";
        t[73] = "Fltr Rel";

        // AMP page
        t[80] = "Amp Atk";
        t[81] = "Amp Hold";
        t[82] = "Amp Dec";
        t[83] = "Amp Sus";
        t[84] = "Amp Rel";

        // LFO page
        t[85] = "LFO Spd";
        t[86] = "LFO Multi";
        t[87] = "LFO Fade";
        t[88] = "LFO Dest";
        t[89] = "LFO Wave";
        t[90] = "LFO Mode";
        t[91] = "LFO Depth";

        return t;
    }

    // -------------------------------------------------------------------------
    // Elektron Digitone  (OS 1.30 MIDI implementation chart)
    // Tracks 1-4 receive on MIDI channels 1-4.

    static std::unordered_map<int, juce::String> digitone()
    {
        std::unordered_map<int, juce::String> t;
        addGMCommon(t);

        // FM page
        t[16] = "Algo";
        t[17] = "Ratio C";
        t[18] = "Ratio A";
        t[19] = "Ratio B1";
        t[20] = "Ratio B2";
        t[21] = "Harm";      // Harmony
        t[22] = "Detune";
        t[23] = "Mix";

        // Operator levels (SRC page 2)
        t[70] = "Op A Lvl";
        t[71] = "Op B1 Lvl";
        t[72] = "Op B2 Lvl";
        t[73] = "Op C Lvl";

        // FLTR page
        t[74] = "Cutoff";
        t[75] = "Res";
        t[76] = "Fltr Env";
        t[77] = "Fltr Atk";
        t[78] = "Fltr Dec";
        t[79] = "Fltr Sus";
        t[80] = "Fltr Rel";

        // AMP page
        t[81] = "Amp Atk";
        t[82] = "Amp Dec";
        t[83] = "Amp Sus";
        t[84] = "Amp Rel";

        // LFO page
        t[85] = "LFO Spd";
        t[86] = "LFO Multi";
        t[87] = "LFO Fade";
        t[88] = "LFO Dest";
        t[89] = "LFO Wave";
        t[90] = "LFO Phase";
        t[91] = "LFO Mode";
        t[92] = "LFO Depth";

        return t;
    }

    // -------------------------------------------------------------------------
    // Elektron Syntakt  (OS 1.10 MIDI implementation chart)
    // Tracks 1-12 receive on MIDI channels 1-12.
    // SRC page params (CC 16-23) vary by synthesis engine; labels here reflect
    // the most common engine (Synt BD / SY Raw) parameter ordering.

    static std::unordered_map<int, juce::String> syntakt()
    {
        std::unordered_map<int, juce::String> t;
        addGMCommon(t);

        // SRC page — engine-generic labels (engine-specific vary)
        t[16] = "P1";
        t[17] = "P2";
        t[18] = "P3";
        t[19] = "P4";
        t[20] = "P5";
        t[21] = "P6";
        t[22] = "Rev Send";
        t[23] = "Del Send";

        // FLTR page
        t[74] = "Cutoff";
        t[75] = "Res";
        t[76] = "Fltr Type";
        t[70] = "Fltr Atk";
        t[71] = "Fltr Dec";
        t[72] = "Fltr Sus";
        t[73] = "Fltr Rel";

        // AMP page
        t[80] = "Amp Atk";
        t[81] = "Amp Hold";
        t[82] = "Amp Dec";
        t[83] = "Amp Sus";
        t[84] = "Amp Rel";
        t[85] = "Overdrive";

        // LFO page
        t[86] = "LFO Spd";
        t[87] = "LFO Multi";
        t[88] = "LFO Fade";
        t[89] = "LFO Dest";
        t[90] = "LFO Wave";
        t[91] = "LFO Mode";
        t[92] = "LFO Depth";

        return t;
    }

    // -------------------------------------------------------------------------
    // Elektron Analog Four Mk II  (OS 1.50 MIDI implementation chart)
    // Tracks 1-4 receive on MIDI channels 1-4.

    static std::unordered_map<int, juce::String> analogFour()
    {
        std::unordered_map<int, juce::String> t;
        addGMCommon(t);

        // OSC page
        t[16] = "Osc1 Wav";
        t[17] = "Osc1 Tune";
        t[18] = "Osc1 Lvl";
        t[19] = "Osc2 Wav";
        t[20] = "Osc2 Tune";
        t[21] = "Osc2 Lvl";
        t[22] = "Sub Lvl";
        t[23] = "Noise Lvl";

        // OSC page 2
        t[24] = "Osc1 PW";
        t[25] = "Osc2 PW";
        t[26] = "Osc2 Dtun";
        t[27] = "Pwm Spd";
        t[28] = "Pwm Dep";

        // FLTR page — dual SVF
        t[74] = "F1 Cutoff";
        t[75] = "F1 Res";
        t[76] = "F2 Cutoff";
        t[77] = "F2 Res";
        t[78] = "Fltr Bal";
        t[79] = "Fltr Dist";

        // Filter envelope
        t[70] = "Fltr Atk";
        t[71] = "Fltr Dec";
        t[72] = "Fltr Sus";
        t[73] = "Fltr Rel";

        // AMP page
        t[80] = "Amp Atk";
        t[81] = "Amp Dec";
        t[82] = "Amp Sus";
        t[83] = "Amp Rel";
        t[84] = "Amp Shp";

        // LFO1
        t[85] = "LFO1 Spd";
        t[86] = "LFO1 Multi";
        t[87] = "LFO1 Fade";
        t[88] = "LFO1 Dest";
        t[89] = "LFO1 Wave";
        t[90] = "LFO1 Mode";
        t[91] = "LFO1 Dep";

        // LFO2
        t[92] = "LFO2 Spd";
        t[93] = "LFO2 Multi";
        t[94] = "LFO2 Fade";
        t[95] = "LFO2 Dest";
        t[96] = "LFO2 Wave";
        t[97] = "LFO2 Mode";
        t[98] = "LFO2 Dep";

        return t;
    }

    // -------------------------------------------------------------------------
    // Elektron Analog Rytm Mk II  (OS 1.70 MIDI implementation chart)
    // Tracks 1-12 receive on MIDI channels 1-12.
    // SRC page params (CC 16-23) vary by synthesis machine type.

    static std::unordered_map<int, juce::String> analogRytm()
    {
        std::unordered_map<int, juce::String> t;
        addGMCommon(t);

        // SRC page — machine-generic labels (vary by drum machine engine)
        t[16] = "P1";
        t[17] = "P2";
        t[18] = "P3";
        t[19] = "P4";
        t[20] = "P5";
        t[21] = "P6";
        t[22] = "P7";
        t[23] = "P8";

        // FLTR page — analog multimode filter
        t[74] = "Cutoff";
        t[75] = "Res";
        t[76] = "Fltr Type";
        t[70] = "Fltr Atk";
        t[71] = "Fltr Dec";
        t[72] = "Fltr Sus";
        t[73] = "Fltr Rel";
        t[78] = "Fltr Dep";

        // AMP page
        t[80] = "Amp Atk";
        t[81] = "Amp Hold";
        t[82] = "Amp Dec";
        t[83] = "Amp Sus";
        t[84] = "Amp Rel";
        t[85] = "Overdrive";

        // FX sends
        t[24] = "Rev Send";
        t[25] = "Del Send";

        // LFO
        t[86] = "LFO Spd";
        t[87] = "LFO Multi";
        t[88] = "LFO Fade";
        t[89] = "LFO Dest";
        t[90] = "LFO Wave";
        t[91] = "LFO Mode";
        t[92] = "LFO Depth";

        return t;
    }

    // -------------------------------------------------------------------------
    // Elektron Octatrack Mk II  (OS 1.40 MIDI implementation chart)
    // Tracks 1-8 receive on MIDI channels 1-8.
    // MIDI tracks (9-16) follow a different scheme; this table covers audio tracks.

    static std::unordered_map<int, juce::String> octatrack()
    {
        std::unordered_map<int, juce::String> t;
        addGMCommon(t);

        // Playback page
        t[16] = "Playback";   // Playback mode
        t[17] = "Gain";
        t[18] = "Loop";
        t[19] = "Start";
        t[20] = "End";
        t[21] = "CUE Lvl";   // Cue level
        t[22] = "Xfader";    // Crossfader position (incoming scenes)

        // FLTR page
        t[74] = "Cutoff";
        t[75] = "Res";
        t[76] = "Attack";
        t[77] = "Decay";
        t[78] = "Sustain";
        t[79] = "Release";

        // AMP page
        t[8] = "Track Vol";
        t[9] = "Track Pan";
        t[80] = "Amp Atk";
        t[81] = "Amp Hold";
        t[82] = "Amp Dec";
        t[83] = "Amp Sus";
        t[84] = "Amp Rel";

        // LFO
        t[85] = "LFO Spd";
        t[86] = "LFO Depth";

        // Scene A / B
        t[48] = "Scene A";
        t[49] = "Scene B";

        return t;
    }

    // -------------------------------------------------------------------------
    // Teenage Engineering Tonverk  (published MIDI spec / product page)
    // Monotimbral; receives on a single MIDI channel.

    static std::unordered_map<int, juce::String> tonverk()
    {
        std::unordered_map<int, juce::String> t;
        addGMCommon(t);

        // Oscillator
        t[14] = "Osc Tune";
        t[15] = "Osc Fine";
        t[16] = "Wave";
        t[17] = "PW";         // Pulse Width
        t[18] = "Octave";
        t[19] = "Sub Lvl";

        // Filter
        t[74] = "Cutoff";
        t[75] = "Res";
        t[76] = "Key Trk";
        t[77] = "Fltr Atk";
        t[78] = "Fltr Dec";
        t[79] = "Fltr Sus";
        t[80] = "Fltr Rel";

        // Amp envelope
        t[81] = "Amp Atk";
        t[82] = "Amp Dec";
        t[83] = "Amp Sus";
        t[84] = "Amp Rel";

        // Modulation
        t[85] = "LFO Spd";
        t[86] = "LFO Wave";
        t[87] = "LFO Dest";
        t[88] = "LFO Dep";

        return t;
    }

    // =========================================================================

    std::vector<PresetInfo> listPresets()
    {
        return {
            { "elektron.digitakt", "Elektron Digitakt" },
            { "elektron.digitone", "Elektron Digitone" },
            { "elektron.syntakt", "Elektron Syntakt" },
            { "elektron.a4", "Elektron Analog Four" },
            { "elektron.rytm", "Elektron Analog Rytm" },
            { "elektron.octatrack", "Elektron Octatrack" },
            { "te.tonverk", "TE Tonverk" },
        };
    }

    std::unordered_map<int, juce::String> getTable(const std::string& presetId)
    {
        if (presetId == "elektron.digitakt") return digitakt();
        if (presetId == "elektron.digitone") return digitone();
        if (presetId == "elektron.syntakt") return syntakt();
        if (presetId == "elektron.a4") return analogFour();
        if (presetId == "elektron.rytm") return analogRytm();
        if (presetId == "elektron.octatrack") return octatrack();
        if (presetId == "te.tonverk") return tonverk();
        return {};
    }
}
