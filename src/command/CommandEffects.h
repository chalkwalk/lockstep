#pragma once

#include <juce_core/juce_core.h>
#include <cstdint>
#include "../io/ControllerEvent.h"

namespace lockstep
{
  // Pure-virtual intent interface returned to the editor after command dispatch.
  // Implementations: EditorEffects (live editor), RecordingEffects (gesture tests).
  // Keeps CommandCore JUCE-free except for juce::String (same bar EditMode meets).
  class CommandEffects
  {
  public:
    enum class TransportAction : std::uint8_t
    {
      Play, Pause, StopReset, Panic, RecArm, Metronome, TapTempo
    };

    enum class OverlayId : std::uint8_t
    {
      SamplePool,
      SoundBank,
      MachinePicker,  // step-grid re-skin, not a floating overlay
    };

    virtual ~CommandEffects() = default;

    virtual void status      (const juce::String& msg)        = 0;
    virtual void requestRepaint()                              = 0;
    virtual void transport   (TransportAction action)          = 0;
    virtual void machineAssign(int track, const char* id)      = 0;
    virtual void openOverlay  (OverlayId id, int param = 0)   = 0;
    virtual void crossfader   (float value)                   = 0;
    // Auto-release a transient modifier latch after its terminal action.
    // No-op when the modifier is not latched (physically held is unaffected).
    virtual void releaseLatch (ControllerButton cb)           = 0;
  };
}
