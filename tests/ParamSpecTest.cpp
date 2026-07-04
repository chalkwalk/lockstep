// ParamSpecTest — golden param-id list + invariant checks per machine.
//
// Purpose: an id is a serialization key; an accidental rename is silent data
// loss. These tests pin the full ordered id list for every machine so any
// rename fails the build immediately.
//
// Invariants checked per slot:
//   - id is non-empty
//   - id is unique within the machine
//   - default is in [min, max]
//   - stepped slots with valueLabels have label count == max - min + 1
//   - sectionIndex >= 0
//   - skew > 0.0f (positive, never zero or negative)

#include "TestHarness.h"
#include "../src/machine/AnalogMachine.h"
#include "../src/machine/FMMachine.h"
#include "../src/machine/DrumMachine.h"
#include "../src/machine/SampleMachine.h"
#include "../src/machine/SliceMachine.h"
#include "../src/machine/MidiOutMachine.h"
#include "../src/machine/SamplePool.h"
#include "../src/ui/ParamFormat.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace lockstep
{
    static void checkInvariants(IMachine& m, const char* machineName)
    {
        const int n = m.numParams();
        std::unordered_set<std::string> seenIds;
        for (int i = 0; i < n; ++i)
        {
            const auto ps = m.paramSpec(i);
            const std::string id(ps.id.isEmpty() ? "" : ps.id.toStdString());

            CHECK(!ps.id.isEmpty(),
                  juce::String(machineName) + " slot " + juce::String(i) + ": id must be non-empty");

            CHECK(seenIds.insert(id).second,
                  juce::String(machineName) + " slot " + juce::String(i) + ": duplicate id \"" + ps.id + "\"");

            CHECK(ps.defaultValue >= ps.minValue && ps.defaultValue <= ps.maxValue,
                  juce::String(machineName) + " slot " + juce::String(i) + " (" + ps.id + "): default " + juce::String(ps.defaultValue) + " not in [" + juce::String(ps.minValue) + ", " + juce::String(ps.maxValue) + "]");

            if (ps.isStepped && !ps.valueLabels.empty())
            {
                const int expectedLabels = static_cast<int>(ps.maxValue - ps.minValue) + 1;
                CHECK(static_cast<int>(ps.valueLabels.size()) == expectedLabels,
                      juce::String(machineName) + " slot " + juce::String(i) + " (" + ps.id + "): stepped with " + juce::String(static_cast<int>(ps.valueLabels.size())) + " labels but max-min+1=" + juce::String(expectedLabels));
            }

            CHECK(ps.sectionIndex >= 0,
                  juce::String(machineName) + " slot " + juce::String(i) + " (" + ps.id + "): sectionIndex must be >= 0");

            CHECK(ps.skew > 0.0f,
                  juce::String(machineName) + " slot " + juce::String(i) + " (" + ps.id + "): skew must be > 0");
        }
    }

    // Check that the id at each slot matches the golden string exactly.
    static void checkGoldenIds(IMachine& m, const char* machineName,
                               const std::vector<const char*>& golden)
    {
        CHECK(m.numParams() == static_cast<int>(golden.size()),
              juce::String(machineName) + ": numParams() " + juce::String(m.numParams()) + " != golden count " + juce::String(static_cast<int>(golden.size())));

        const int n = std::min(m.numParams(), static_cast<int>(golden.size()));
        for (int i = 0; i < n; ++i)
        {
            const auto ps = m.paramSpec(i);
            CHECK(ps.id == juce::String(golden[static_cast<std::size_t>(i)]),
                  juce::String(machineName) + " slot " + juce::String(i) + ": id=\"" + ps.id + "\" expected=\"" + golden[static_cast<std::size_t>(i)] + "\"");
        }
    }

    // -------------------------------------------------------------------------

    static void testAnalogMachineParams()
    {
        AnalogMachine m;
        const std::vector<const char*> golden = {
            "va_osc1_coarse",   //  0
            "va_osc1_fine",     //  1
            "va_osc1_wave",     //  2
            "va_osc1_pw",       //  3
            "va_osc2_coarse",   //  4
            "va_osc2_fine",     //  5
            "va_osc2_wave",     //  6
            "va_osc2_pw",       //  7
            "va_osc1_level",    //  8
            "va_osc2_level",    //  9
            "va_sub",           // 10
            "va_noise",         // 11
            "va_mixer_drive",   // 12
            "va_porta",         // 13
            "va_voice_mode",    // 14
            "va_cutoff",        // 15
            "va_res",           // 16
            "va_filter_type",   // 17
            "va_drive",         // 18
            "va_fenv_depth",    // 19
            "va_fenv_a",        // 20
            "va_fenv_d",        // 21
            "va_fenv_s",        // 22
            "va_fenv_r",        // 23
            "va_keytrack",      // 24
            "va_amp_a",         // 25
            "va_amp_d",         // 26
            "va_amp_s",         // 27
            "va_amp_r",         // 28
            "va_level",         // 29
            "va_pan",           // 30
            "va_retrig",        // 31
            "va_vel_sens",      // 32
            "va_lfo_rate",      // 33
            "va_lfo_depth",     // 34
            "va_lfo_shape",     // 35
            "va_lfo_target",    // 36
            "va_lfo_sync",      // 37
            "va_age",           // 38
        };
        checkGoldenIds(m, "AnalogMachine", golden);
        checkInvariants(m, "AnalogMachine");

        // LFO rate must stay exponential (log-ish) so the slow end is reachable —
        // guards against a silent regression back to the linear skew that made
        // almost the whole encoder throw fast.
        CHECK(m.paramSpec(33).id == "va_lfo_rate",
              "AnalogMachine slot 33 should be va_lfo_rate");
        CHECK(m.paramSpec(33).skew < 1.0f,
              "AnalogMachine va_lfo_rate: skew must be < 1 (exponential/log rate)");
    }

    static void testFMMachineParams()
    {
        FMMachine m;
        const std::vector<const char*> golden = {
            "fm_ratio_1",    //  0
            "fm_ratio_2",    //  1
            "fm_ratio_3",    //  2
            "fm_ratio_4",    //  3
            "fm_fine_1",     //  4
            "fm_fine_2",     //  5
            "fm_fine_3",     //  6
            "fm_fine_4",     //  7
            "fm_mix_1",      //  8
            "fm_mix_2",      //  9
            "fm_mix_3",      // 10
            "fm_mix_4",      // 11
            "fm_macro_atk",  // 12
            "fm_macro_rel",  // 13
            "fm_macro_sus",  // 14
            "fm_level",      // 15
            "fm_atk_1",      // 16
            "fm_dec_1",      // 17
            "fm_sus_1",      // 18
            "fm_rel_1",      // 19
            "fm_atk_2",      // 20
            "fm_dec_2",      // 21
            "fm_sus_2",      // 22
            "fm_rel_2",      // 23
            "fm_atk_3",      // 24
            "fm_dec_3",      // 25
            "fm_sus_3",      // 26
            "fm_rel_3",      // 27
            "fm_atk_4",      // 28
            "fm_dec_4",      // 29
            "fm_sus_4",      // 30
            "fm_rel_4",      // 31
            "fm_vs_1",       // 32
            "fm_vs_2",       // 33
            "fm_vs_3",       // 34
            "fm_vs_4",       // 35
            "fm_retrig",     // 36
            // Mod matrix: kSlotModBase=37, formula = kSlotModBase + dst*4 + src
            "fm_mod_s1_d1",  // 37  dst=0, src=0
            "fm_mod_s2_d1",  // 38  dst=0, src=1
            "fm_mod_s3_d1",  // 39  dst=0, src=2
            "fm_mod_s4_d1",  // 40  dst=0, src=3
            "fm_mod_s1_d2",  // 41  dst=1, src=0
            "fm_mod_s2_d2",  // 42  dst=1, src=1
            "fm_mod_s3_d2",  // 43  dst=1, src=2
            "fm_mod_s4_d2",  // 44  dst=1, src=3
            "fm_mod_s1_d3",  // 45  dst=2, src=0
            "fm_mod_s2_d3",  // 46  dst=2, src=1
            "fm_mod_s3_d3",  // 47  dst=2, src=2
            "fm_mod_s4_d3",  // 48  dst=2, src=3
            "fm_mod_s1_d4",  // 49  dst=3, src=0
            "fm_mod_s2_d4",  // 50  dst=3, src=1
            "fm_mod_s3_d4",  // 51  dst=3, src=2
            "fm_mod_s4_d4",  // 52  dst=3, src=3
            "fm_voice_mode", // 53
        };
        checkGoldenIds(m, "FMMachine", golden);
        checkInvariants(m, "FMMachine");
    }

    static void testDrumMachineParams()
    {
        DrumMachine m;
        const std::vector<const char*> golden = {
            "drum_type",        //  0
            "drum_tune",        //  1
            "drum_sweep",       //  2
            "drum_sweep_decay", //  3
            "drum_punch",       //  4
            "drum_tone",        //  5
            "drum_body",        //  6
            "drum_snap",        //  7
            "drum_attack",      //  8
            "drum_hold",        //  9
            "drum_decay",       // 10
            "drum_noise_decay", // 11
            "drum_level",       // 12
            "drum_retrig",      // 13
            "drum_vel_sens",    // 14
        };
        checkGoldenIds(m, "DrumMachine", golden);
        checkInvariants(m, "DrumMachine");
    }

    static void testSampleMachineParams()
    {
        SamplePool pool;
        SampleMachine m{ pool };
        const std::vector<const char*> golden = {
            "sample_id",       //  0
            "pitch",           //  1
            "samp_start",      //  2
            "samp_length",     //  3
            "samp_loop_mode",  //  4
            "samp_loop_start", //  5
            "samp_loop_len",   //  6
            "level",           //  7
            "attack",          //  8
            "hold",            //  9
            "decay",           // 10
            "sustain",         // 11
            "release",         // 12
            "samp_retrig",     // 13
            "samp_velsens",    // 14
            "samp_loop_xfade", // 15 (SRC — appended past the AMP block)
        };
        checkGoldenIds(m, "SampleMachine", golden);
        checkInvariants(m, "SampleMachine");
    }

    static void testSliceMachineParams()
    {
        SamplePool pool;
        SliceMachine m{ pool };
        const std::vector<const char*> golden = {
            "slicer_sample_id",   //  0
            "slicer_mode",        //  1
            "slicer_slice_src",   //  2
            "slicer_slice_count", //  3
            "slicer_rate",        //  4
            "slicer_start",       //  5
            "slicer_length",      //  6
            "slicer_loop_mode",   //  7
            "slicer_loop_start",  //  8
            "slicer_loop_len",    //  9
            "slicer_pitch",       // 10
            "slicer_voice_mode",  // 11
            "slicer_fade",        // 12
        };
        checkGoldenIds(m, "SliceMachine", golden);
        checkInvariants(m, "SliceMachine");
    }

    static void testMidiOutMachineParams()
    {
        MidiOutMachine m;
        // First 3 fixed slots; then cc0..cc15 (16 CCs).
        std::vector<const char*> golden;
        golden.push_back("dest");
        golden.push_back("channel");
        golden.push_back("program");
        for (int i = 0; i < 16; ++i)
        {
            // The id is built dynamically: "cc" + String(i), so we check
            // only the fixed prefix plus the count.
            (void)i;
        }

        // Check count and fixed-slot ids.
        CHECK(m.numParams() == 19, "MidiOutMachine: numParams() should be 19");
        CHECK(m.paramSpec(0).id == "dest", "MidiOutMachine slot 0: id == dest");
        CHECK(m.paramSpec(1).id == "channel", "MidiOutMachine slot 1: id == channel");
        CHECK(m.paramSpec(2).id == "program", "MidiOutMachine slot 2: id == program");
        for (int i = 0; i < 16; ++i)
        {
            const juce::String expected = juce::String("cc") + juce::String(i);
            CHECK(m.paramSpec(3 + i).id == expected,
                  "MidiOutMachine slot " + juce::String(3 + i) + ": id == " + expected);
        }
        // Invariants for the fixed slots only (CC slots have dynamic ranges).
        checkInvariants(m, "MidiOutMachine");
    }

    // -------------------------------------------------------------------------
    // 9.6 D2 — contextLabel hook: representative type/mode values per machine.

    static void testContextLabels()
    {
        // ── Drum ─────────────────────────────────────────────────────────
        // contextLabel is set on SRC slots 2-7 (Sweep/SwpDec/Punch/Tone/Body/Snap).
        // Slot 1 (Tune) carries no contextLabel.  Slot indices are private; using
        // literals with comments matching the header.
        {
            DrumMachine m;
            const int n = m.numParams();

            // Frame helper: set TYPE at index 0 (kSlotType), rest 0.0f.
            auto frame = [&](float typeVal) -> ParamFrame {
                ParamFrame f(static_cast<std::size_t>(n), 0.0f);
                f[0] = typeVal;
                return f;
            };

            // Tune (slot 1): no contextLabel.
            CHECK(m.paramSpec(1).contextLabel == nullptr,
                  "Drum slot 1 (Tune): contextLabel must be nullptr");

            // Sweep (slot 2 = kSlotSweep): KICK/TOM/etc → "Sweep";
            //   CLAP(4) → "Taps"; COWBELL(5) → "Interval"; CYMBAL(6) → "Spread".
            {
                const auto spec = m.paramSpec(2);
                CHECK(spec.contextLabel != nullptr,
                      "Drum slot 2 (Sweep): contextLabel must be set");
                CHECK(spec.contextLabel(frame(0.0f)) == "Sweep",
                      "Drum Sweep: TYPE=KICK(0) → Sweep");
                CHECK(spec.contextLabel(frame(4.0f)) == "Taps",
                      "Drum Sweep: TYPE=CLAP(4) → Taps");
                CHECK(spec.contextLabel(frame(5.0f)) == "Interval",
                      "Drum Sweep: TYPE=COWBELL(5) → Interval");
                CHECK(spec.contextLabel(frame(6.0f)) == "Spread",
                      "Drum Sweep: TYPE=CYMBAL(6) → Spread");
            }

            // SwpDec (slot 3 = kSlotSweepDecay): CLAP(4) → "Tap Spac"; others → "Swp Dec".
            {
                const auto spec = m.paramSpec(3);
                CHECK(spec.contextLabel != nullptr,
                      "Drum slot 3 (SwpDec): contextLabel must be set");
                CHECK(spec.contextLabel(frame(0.0f)) == "Swp Dec",
                      "Drum SwpDec: TYPE=KICK(0) → Swp Dec");
                CHECK(spec.contextLabel(frame(4.0f)) == "Tap Spac",
                      "Drum SwpDec: TYPE=CLAP(4) → Tap Spac");
            }

            // Punch (slot 4 = kSlotPunch): KICK(0) → "Punch"; SNARE(1) → "—".
            {
                const auto spec = m.paramSpec(4);
                CHECK(spec.contextLabel != nullptr,
                      "Drum slot 4 (Punch): contextLabel must be set");
                CHECK(spec.contextLabel(frame(0.0f)) == "Punch",
                      "Drum Punch: TYPE=KICK(0) → Punch");
                CHECK(spec.contextLabel(frame(1.0f)) == juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")),
                      "Drum Punch: TYPE=SNARE(1) → em-dash (unused slot)");
            }

            // Tone (slot 5 = kSlotTone): KICK(0) → "Drive"; SNARE(1) → "BP Freq"; HAT(2) → "Reso".
            {
                const auto spec = m.paramSpec(5);
                CHECK(spec.contextLabel != nullptr,
                      "Drum slot 5 (Tone): contextLabel must be set");
                CHECK(spec.contextLabel(frame(0.0f)) == "Drive",
                      "Drum Tone: TYPE=KICK(0) → Drive");
                CHECK(spec.contextLabel(frame(1.0f)) == "BP Freq",
                      "Drum Tone: TYPE=SNARE(1) → BP Freq");
                CHECK(spec.contextLabel(frame(2.0f)) == "Reso",
                      "Drum Tone: TYPE=HAT(2) → Reso");
            }

            // Snap (slot 7 = kSlotSnap): SNARE(1) → "Snap"; CYMBAL(6) → "Sizzle"; KICK(0) → "—".
            {
                const auto spec = m.paramSpec(7);
                CHECK(spec.contextLabel != nullptr,
                      "Drum slot 7 (Snap): contextLabel must be set");
                CHECK(spec.contextLabel(frame(1.0f)) == "Snap",
                      "Drum Snap: TYPE=SNARE(1) → Snap");
                CHECK(spec.contextLabel(frame(6.0f)) == "Sizzle",
                      "Drum Snap: TYPE=CYMBAL(6) → Sizzle");
                CHECK(spec.contextLabel(frame(0.0f)) == juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")),
                      "Drum Snap: TYPE=KICK(0) → em-dash (unused slot)");
            }
        }

        // ── SampleMachine ────────────────────────────────────────────────────
        // LpStart (slot 5) and LpLen (slot 6) have contextLabel.
        // kSlotLoopMode = 4 (private); use literal with comment.
        {
            SamplePool pool;
            SampleMachine m{ pool };
            const int n = m.numParams();

            // Frame helper: set loop mode at index 4 (kSlotLoopMode).
            auto frame = [&](float loopMode) -> ParamFrame {
                ParamFrame f(static_cast<std::size_t>(n), 0.0f);
                f[4] = loopMode;
                return f;
            };

            const auto startSpec = m.paramSpec(5);  // kSlotLoopStart
            const auto lenSpec   = m.paramSpec(6);  // kSlotLoopLen
            CHECK(startSpec.contextLabel != nullptr,
                  "SampleMachine slot 5 (LpStart): contextLabel must be set");
            CHECK(lenSpec.contextLabel != nullptr,
                  "SampleMachine slot 6 (LpLen): contextLabel must be set");

            // Mode 0 (Off) and 1 (Sust): both slots use plain labels.
            CHECK(startSpec.contextLabel(frame(0.0f)) == "LpStart",
                  "Sample LpStart: mode=Off(0) → LpStart");
            CHECK(startSpec.contextLabel(frame(1.0f)) == "LpStart",
                  "Sample LpStart: mode=Sust(1) → LpStart");
            CHECK(lenSpec.contextLabel(frame(0.0f)) == "LpLen",
                  "Sample LpLen: mode=Off(0) → LpLen");
            CHECK(lenSpec.contextLabel(frame(1.0f)) == "LpLen",
                  "Sample LpLen: mode=Sust(1) → LpLen");

            // Mode 2 (SustRel): len auto; start still plain.
            CHECK(startSpec.contextLabel(frame(2.0f)) == "LpStart",
                  "Sample LpStart: mode=SustRel(2) → LpStart");
            CHECK(lenSpec.contextLabel(frame(2.0f)) == "LpLen (auto)",
                  "Sample LpLen: mode=SustRel(2) → LpLen (auto)");

            // Mode 3 (All): both auto.
            CHECK(startSpec.contextLabel(frame(3.0f)) == "LpStart (auto)",
                  "Sample LpStart: mode=All(3) → LpStart (auto)");
            CHECK(lenSpec.contextLabel(frame(3.0f)) == "LpLen (auto)",
                  "Sample LpLen: mode=All(3) → LpLen (auto)");
        }

        // ── SliceMachine ─────────────────────────────────────────────────────
        // LpStart (slot 8) and LpLen (slot 9); kSlotLoopMode = 7 (private).
        {
            SamplePool pool;
            SliceMachine m{ pool };
            const int n = m.numParams();

            // Frame helper: set loop mode at index 7 (kSlotLoopMode).
            auto frame = [&](float loopMode) -> ParamFrame {
                ParamFrame f(static_cast<std::size_t>(n), 0.0f);
                f[7] = loopMode;
                return f;
            };

            const auto startSpec = m.paramSpec(8);  // kSlotLoopStart
            const auto lenSpec   = m.paramSpec(9);  // kSlotLoopLen
            CHECK(startSpec.contextLabel != nullptr,
                  "SliceMachine slot 8 (LpStart): contextLabel must be set");
            CHECK(lenSpec.contextLabel != nullptr,
                  "SliceMachine slot 9 (LpLen): contextLabel must be set");

            // Mode 0-2: start = plain; mode 3 (All): auto.
            CHECK(startSpec.contextLabel(frame(0.0f)) == "LpStart",
                  "Slice LpStart: mode=Off(0) → LpStart");
            CHECK(startSpec.contextLabel(frame(3.0f)) == "LpStart (auto)",
                  "Slice LpStart: mode=All(3) → LpStart (auto)");

            // Mode 0-1: len = plain; mode 2-3: auto.
            CHECK(lenSpec.contextLabel(frame(1.0f)) == "LpLen",
                  "Slice LpLen: mode=Sust(1) → LpLen");
            CHECK(lenSpec.contextLabel(frame(2.0f)) == "LpLen (auto)",
                  "Slice LpLen: mode=SustRel(2) → LpLen (auto)");
            CHECK(lenSpec.contextLabel(frame(3.0f)) == "LpLen (auto)",
                  "Slice LpLen: mode=All(3) → LpLen (auto)");
        }
    }

    // -------------------------------------------------------------------------

    // formatParamValue: min/maxLabel render at the extremes, numeric between.
    static void testEdgeLabelFormatting()
    {
        ParamSpec p;
        p.minValue = 0.0f; p.maxValue = 30.0f; p.unit = ParamSpec::Unit::Ms;
        p.minLabel = "Auto";
        CHECK(formatParamValue(0.0f, p) == "Auto", "value at floor -> minLabel");
        CHECK(formatParamValue(0.0005f, p) == "Auto", "near-floor (float dust) -> minLabel");
        CHECK(formatParamValue(5.0f, p) == "5.0 ms", "value above floor -> numeric ms");
        CHECK(formatParamValue(0.5f, p) == "0.5 ms", "0.5 ms is a real value, not Auto");

        // maxLabel at the ceiling.
        ParamSpec m;
        m.minValue = 0.0f; m.maxValue = 1.0f; m.unit = ParamSpec::Unit::Percent;
        m.maxLabel = "Full";
        CHECK(formatParamValue(1.0f, m) == "Full", "value at ceiling -> maxLabel");
        CHECK(formatParamValue(0.9999f, m) == "Full", "near-ceiling -> maxLabel");
        CHECK(formatParamValue(0.5f, m) == "50%", "mid value -> numeric percent");

        // Without labels, both extremes are numeric (no behaviour change for others).
        ParamSpec q;
        q.minValue = 0.0f; q.maxValue = 30.0f; q.unit = ParamSpec::Unit::Ms;
        CHECK(formatParamValue(0.0f, q) == "0.0 ms", "no minLabel -> numeric floor");
    }

    void runParamSpecTests()
    {
        testAnalogMachineParams();
        testFMMachineParams();
        testDrumMachineParams();
        testSampleMachineParams();
        testMidiOutMachineParams();
        testSliceMachineParams();
        testContextLabels();
        testEdgeLabelFormatting();
    }

} // namespace lockstep
