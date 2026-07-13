// DispatchSpike -- feasibility spike for ROADMAP 9.12 Stage 5.
//
// Question: can LockstepEditor::dispatchDown/dispatchUp be driven HEADLESSLY, so
// a golden net can pin today's dispatch behaviour BEFORE the table migration
// rewrites it? 9.13's stage 7/8 note claims an editor-dispatch harness "proved
// unviable headless (component-teardown segfault)". This spike tries to
// reproduce that, and if it reproduces, to find the actual cause.
//
// Not a test. A throwaway probe with a main(), kept only until it answers.
// Reaches dispatch through the one `friend struct DispatchProbe` seam in
// PluginEditor.h (`#define private public` is not an option: it redeclares
// libstdc++'s own private members and breaks <sstream>).

#include "../src/PluginEditor.h"
#include "../src/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <cstdio>

namespace lockstep
{
    struct DispatchProbe
    {
        static bool down(LockstepEditor& ed, ControllerEvent ev)
        {
            return ed.dispatchDown(ev, 0);
        }
        static void up(LockstepEditor& ed, ControllerEvent ev) { ed.dispatchUp(ev, 0); }
    };
}

namespace
{
    // write(2): unbuffered, survives a crash that kills us before any flush.
    void say(const char* stage)
    {
        std::fprintf(stderr, "[spike] %s\n", stage);
        std::fflush(stderr);
    }

    // Did we even reach main()? If this prints and "start" does not, the crash is
    // in static initialisation, not in anything the spike does.
    struct StaticInitProbe
    {
        StaticInitProbe() { say("static init of the spike TU ran"); }
    };
    StaticInitProbe staticInitProbe;
}

int main()
{
    say("start");

    // A MessageManager must exist before any Component is constructed.
    juce::ScopedJuceInitialiser_GUI juceInit;
    say("gui initialised");

    {
        // HEAP, never the stack: LockstepProcessor embeds Arrangement (~47 MB), so a
        // stack-local processor overflows the stack in main()'s prologue -- the crash
        // lands *before* the first statement, which is what makes it look like a
        // mysterious "headless doesn't work" rather than the known gotcha it is.
        auto proc = std::make_unique<lockstep::LockstepProcessor>();
        proc->setRateAndBufferSizeDetails(44100.0, 512);   // EngineTest gotcha
        say("processor constructed");

        // Heap too, and destroyed before the processor it references.
        auto editor = std::make_unique<lockstep::LockstepEditor>(*proc);
        say("editor constructed");

        editor->setSize(1400, 900);   // provoke resized() without a native peer
        say("editor resized");

        using CE = lockstep::ControllerEvent;
        using CB = lockstep::ControllerButton;
        using P = lockstep::DispatchProbe;

        // A modifier hold: down then up. Touches latch, scope, and surface refresh.
        (void)P::down(*editor, CE{ CE::Type::ButtonDown, CB::TrackScope, 0, 0 });
        say("dispatchDown(TrackScope) survived");

        P::up(*editor, CE{ CE::Type::ButtonUp, CB::TrackScope, 0, 0 });
        say("dispatchUp(TrackScope) survived");

        // A step press: the busiest family.
        (void)P::down(*editor, CE{ CE::Type::ButtonDown, CB::Step, 0, 0 });
        P::up(*editor, CE{ CE::Type::ButtonUp, CB::Step, 0, 0 });
        say("dispatch(Step 0) survived");

        // Let any AsyncUpdater (SurfaceDispatcher) actually deliver: the coalesced
        // frame is where a headless repaint path would blow up, if it does.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
        say("message loop pumped (async frame delivered)");

        editor.reset();
        say("editor destroyed");
    }
    say("processor destroyed");

    say("SURVIVED -- dispatch is drivable headlessly");
    return 0;
}
