// LooperMachineTest -- overdub looper state machine (6.3, DESIGN §29.2).
//
// Drives the machine through Idle → Record → Play → Overdub → Undo → Clear via the
// command mailbox (one command drained per block) and checks the loop audio.

#include "TestHarness.h"
#include "../src/machine/LooperMachine.h"
#include <cmath>

namespace lockstep
{
    namespace
    {
        using Cmd = LooperMachine::Cmd;
        using State = LooperMachine::State;

        // Run one block with a constant input value; returns the output buffer.
        juce::AudioBuffer<float> runBlock(LooperMachine& m, int n, float inValue,
                                          Cmd cmd = Cmd::None)
        {
            if (cmd != Cmd::None) m.postCommand(cmd);
            juce::AudioBuffer<float> buf(2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    buf.setSample(ch, i, inValue);
            juce::MidiBuffer midi;
            ParamFrame params{ 1.0f };  // input_source = External (unused here)
            m.process(midi, params, buf);
            return buf;
        }

    }

    void runLooperMachineTests()
    {
        constexpr double kSr = 48000.0;
        constexpr int n = 512;

        LooperMachine loop;
        loop.prepare(kSr, n);
        CHECK(loop.state() == State::Idle, "starts Idle");

        // Idle: silent output regardless of input.
        {
            auto out = runBlock(loop, n, 0.5f);
            CHECK(feq(out.getSample(0, 0), 0.0f) && feq(out.getSample(0, 256), 0.0f),
                  "Idle output is silent");
        }

        // Record first layer (Idle → Recording): monitors input while capturing.
        {
            auto out = runBlock(loop, n, 0.5f, Cmd::RecordCycle);
            CHECK(loop.state() == State::Recording, "Record enters Recording");
            CHECK(feq(out.getSample(0, 0), 0.5f), "monitors input while recording");
        }

        // Close loop (Recording → Playing): plays back the recorded layer.
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::RecordCycle);
            CHECK(loop.state() == State::Playing, "second Record closes loop → Playing");
            CHECK(feq(out.getSample(0, 0), 0.5f) && feq(out.getSample(1, 256), 0.5f),
                  "plays back the recorded layer (0.5)");
        }

        // Overdub (Playing → Overdubbing): sums new input onto the loop.
        {
            auto out = runBlock(loop, n, 0.25f, Cmd::RecordCycle);
            CHECK(loop.state() == State::Overdubbing, "third Record → Overdubbing");
            CHECK(feq(out.getSample(0, 0), 0.75f), "overdub sums input onto loop (0.5+0.25)");
        }

        // Undo: reverts the overdub layer back to the pre-overdub loop (0.5).
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::Undo);
            CHECK(loop.state() == State::Playing, "Undo returns to Playing");
            CHECK(feq(out.getSample(0, 0), 0.5f), "Undo reverts to pre-overdub loop");
        }

        // Play/Stop toggles playback.
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::PlayStop);
            CHECK(loop.state() == State::Stopped, "PlayStop stops");
            CHECK(feq(out.getSample(0, 0), 0.0f), "stopped output is silent");
            auto out2 = runBlock(loop, n, 0.0f, Cmd::PlayStop);
            CHECK(loop.state() == State::Playing, "PlayStop resumes");
            CHECK(feq(out2.getSample(0, 0), 0.5f), "resumed playback (0.5)");
        }

        // Clear: empties the loop, back to Idle/silent.
        {
            auto out = runBlock(loop, n, 0.0f, Cmd::Clear);
            CHECK(loop.state() == State::Idle, "Clear returns to Idle");
            CHECK(feq(out.getSample(0, 0), 0.0f), "cleared output is silent");
        }
    }
}
