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
#include "../src/machine/VAMachine.h"
#include "../src/machine/FMMachine.h"
#include "../src/machine/DrumSynthMachine.h"
#include "../src/machine/SamplerMachine.h"
#include "../src/machine/SlicerMachine.h"
#include "../src/machine/MidiOutMachine.h"
#include "../src/machine/SamplePool.h"
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

    static void testVAMachineParams()
    {
        VAMachine m;
        const std::vector<const char*> golden = {
            "va_osc1_coarse",   //  0
            "va_osc1_fine",     //  1
            "va_osc1_wave",     //  2
            "va_osc1_pw",       //  3
            "va_osc2_coarse",   //  4
            "va_osc2_fine",     //  5
            "va_osc2_wave",     //  6
            "va_osc2_pw",       //  7
            "va_sub",           //  8
            "va_noise",         //  9
            "va_porta",         // 10
            "va_voice_mode",    // 11
            "va_cutoff",        // 12
            "va_res",           // 13
            "va_filter_type",   // 14
            "va_drive",         // 15
            "va_fenv_depth",    // 16
            "va_fenv_a",        // 17
            "va_fenv_d",        // 18
            "va_fenv_s",        // 19
            "va_fenv_r",        // 20
            "va_amp_a",         // 21
            "va_amp_d",         // 22
            "va_amp_s",         // 23
            "va_amp_r",         // 24
            "va_level",         // 25
            "va_pan",           // 26
            "va_retrig",        // 27
            "va_vel_sens",      // 28
            "va_lfo_rate",      // 29
            "va_lfo_depth",     // 30
            "va_lfo_shape",     // 31
            "va_lfo_target",    // 32
            "va_lfo_sync",      // 33
            "va_osc_mix",       // 34
        };
        checkGoldenIds(m, "VAMachine", golden);
        checkInvariants(m, "VAMachine");
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

    static void testDrumSynthMachineParams()
    {
        DrumSynthMachine m;
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
        checkGoldenIds(m, "DrumSynthMachine", golden);
        checkInvariants(m, "DrumSynthMachine");
    }

    static void testSamplerMachineParams()
    {
        SamplePool pool;
        SamplerMachine m{ pool };
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
        };
        checkGoldenIds(m, "SamplerMachine", golden);
        checkInvariants(m, "SamplerMachine");
    }

    static void testSlicerMachineParams()
    {
        SamplePool pool;
        SlicerMachine m{ pool };
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
        checkGoldenIds(m, "SlicerMachine", golden);
        checkInvariants(m, "SlicerMachine");
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

    void runParamSpecTests()
    {
        testVAMachineParams();
        testFMMachineParams();
        testDrumSynthMachineParams();
        testSamplerMachineParams();
        testSlicerMachineParams();
        testMidiOutMachineParams();
    }

} // namespace lockstep
