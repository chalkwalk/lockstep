set(plugin_formats Standalone VST3)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()

# ---------------------------------------------------------------------------
# Shared core static library: sequencer, machine, IO, state. Linked into
# the plugin target so the same code drives every format.
# ---------------------------------------------------------------------------
add_library(lockstep_core STATIC
    Parameters.cpp
    command/ButtonLayers.cpp
    command/CommandCore.cpp
    command/KeyAffordances.cpp
    command/ScopePriority.cpp
    command/SurfaceLayer.cpp
    command/KeyBindings.cpp
    command/VerbCommands.cpp
    core/Clock.cpp
    core/Metronome.cpp
    core/StateResolver.cpp
    machine/DrumSynthMachine.cpp
    machine/FMMachine.cpp
    machine/SamplePlayingMachineBase.cpp
    machine/SamplePlayer.cpp
    machine/VAMachine.cpp
    machine/MidiDevicePresets.cpp
    machine/MidiOutMachine.cpp
    machine/EffectFactory.cpp
    machine/SamplerMachine.cpp
    machine/SlicerMachine.cpp
    machine/SamplePool.cpp
    machine/MachineParamTable.cpp
    machine/VoiceChoke.cpp
    controller/XTouchMiniSurface.cpp
    controller/Push1Surface.cpp
    io/AbsoluteCC.cpp
    io/RelativeCC.cpp
    io/CCMappingTable.cpp
    io/ControllerPortManager.cpp
    io/MidiClockReceiver.cpp
    io/MidiInput.cpp
    io/EditMode.cpp
    io/QwertyOverlay.cpp
    state/Hash.cpp
    state/PluginState.cpp)

target_include_directories(lockstep_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set_target_properties(lockstep_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(lockstep_core
    PUBLIC
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_audio_devices
        juce::juce_audio_basics
        juce::juce_dsp
        juce::juce_core
        juce::juce_recommended_config_flags)

target_compile_definitions(lockstep_core
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lockstep_core PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror
        ${LOCKSTEP_SANITIZE_FLAGS})
    target_link_options(lockstep_core PUBLIC ${LOCKSTEP_SANITIZE_FLAGS})
else()
    target_link_libraries(lockstep_core PUBLIC juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Engine lib: LockstepProcessor (no UI, no plugin-client wrappers).
# Linked by the plugin target and by the headless test binary.
# ---------------------------------------------------------------------------
add_library(lockstep_engine STATIC
    PluginProcessor.cpp)

set_target_properties(lockstep_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(lockstep_engine
    PUBLIC lockstep_core
           juce::juce_audio_utils)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(lockstep_engine PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror
        ${LOCKSTEP_SANITIZE_FLAGS})
    target_link_options(lockstep_engine PUBLIC ${LOCKSTEP_SANITIZE_FLAGS})
else()
    target_link_libraries(lockstep_engine PUBLIC juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Plugin target.
# ---------------------------------------------------------------------------
juce_add_plugin(Lockstep
    COMPANY_NAME "ChalkWalkMusic"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VERSION "0.0.1"
    PRODUCT_NAME "Lockstep"
    BUNDLE_ID "com.ChalkWalkMusic.Lockstep"
    COPYRIGHT "Copyright (c) 2026 ChalkWalkMusic"
    MICROPHONE_PERMISSION_ENABLED FALSE
    FORMATS ${plugin_formats})

juce_generate_juce_header(Lockstep)

target_sources(Lockstep
    PRIVATE
        PluginEditorFactory.cpp
        PluginEditor.cpp
        ui/KeyButton.cpp
        ui/KeyboardArea.cpp
        ui/MetaBand.cpp
        ui/MetaRotary.cpp
        ui/SurfaceModel.cpp
        ui/InPluginTransport.cpp
        ui/ManipulationZone.cpp
        ui/SamplePoolOverlay.cpp
        ui/SoundBankOverlay.cpp
        ui/StandaloneFileBar.cpp
        ui/mode/ModeReducer.cpp
        ui/mode/FuncReskin.cpp
        ui/mode/LatchOps.cpp)

target_link_libraries(Lockstep
    PRIVATE
        lockstep_engine
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(Lockstep PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror)
else()
    target_link_libraries(Lockstep PUBLIC juce::juce_recommended_warning_flags)
endif()

clap_juce_extensions_plugin(TARGET Lockstep
    CLAP_ID "com.ChalkWalkMusic.Lockstep"
    CLAP_FEATURES "instrument" "sampler")
