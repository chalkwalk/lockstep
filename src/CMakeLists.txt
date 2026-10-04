set(plugin_formats Standalone VST3)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()

# The JUCE-free deck engine sits BELOW lockstep_core and knows nothing of it.
add_subdirectory(deckcore)
add_subdirectory(tonecore)

# ---------------------------------------------------------------------------
# Embedded product typeface (Inter, OFL-1.1 — see assets/fonts/OFL.txt).
#
# The surface renders with the SAME glyphs everywhere: a platform-default sans
# varies by distro and by machine, which is both a look the product never chose
# and the reason a rendered frame could not be compared against a golden. Only
# the two static cuts the product actually asks for are embedded (every Font in
# src/ is either plain or .boldened()); the variable font is not, because a
# typeface JUCE has to instance per size is exactly the nondeterminism this
# removes.
# ---------------------------------------------------------------------------
juce_add_binary_data(lockstep_bank
    NAMESPACE LockstepBank
    HEADER_NAME LockstepBank.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/bank/GeneralUser-GS.sf3)

juce_add_binary_data(lockstep_fonts
    NAMESPACE LockstepFonts
    HEADER_NAME LockstepFonts.h
    SOURCES
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Bold.ttf)

# ---------------------------------------------------------------------------
# Shared core static library: sequencer, machine, IO, state. Linked into
# the plugin target so the same code drives every format.
# ---------------------------------------------------------------------------
add_library(lockstep_core STATIC
    Parameters.cpp
    command/ButtonLayers.cpp
    command/CommandCore.cpp
    command/ScopePriority.cpp
    command/SurfaceLayer.cpp
    command/KeyBindings.cpp
    command/VerbCommands.cpp
    core/Clock.cpp
    core/Metronome.cpp
    core/StateResolver.cpp
    machine/DrumMachine.cpp
    machine/FMMachine.cpp
    machine/SamplePlayingMachineBase.cpp
    machine/SamplePlayer.cpp
    machine/AnalogMachine.cpp
    machine/MidiDevicePresets.cpp
    machine/MidiOutMachine.cpp
    machine/EffectFactory.cpp
    machine/SampleMachine.cpp
    machine/SliceMachine.cpp
    machine/RecordMachine.cpp
    machine/LoopMachine.cpp
    machine/TapeMachine.cpp
    machine/StreamMachine.cpp
    machine/StretchMachine.cpp
    machine/ToneMachine.cpp
    dsp/BungeeStretchEngine.cpp
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

# Bungee headers (and the Eigen they pull in) are SYSTEM includes so their
# warnings never trip lockstep_core's -Werror. The include is confined to
# lockstep_core because only dsp/BungeeStretchEngine.cpp includes <bungee/...>.
target_include_directories(lockstep_core SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/bungee)

# Signalsmith DSP (MIT, header-only) — fractional-delay interpolators, filters,
# envelopes, spectral primitives. SYSTEM include so the vendored headers' warnings
# never trip -Werror. Rooted at modules/ so headers namespace as
# <signalsmith-dsp/...> (their internal includes are relative, and the bare file
# names — fft.h, common.h, mix.h — would otherwise collide). PUBLIC because dsp
# effect headers (src/dsp/*Effect.h) are header-only and included by the plugin,
# tests, and tools targets alike.
target_include_directories(lockstep_core SYSTEM PUBLIC
    ${CMAKE_SOURCE_DIR}/modules)

set_target_properties(lockstep_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(lockstep_core
    PUBLIC
        deck_core
        tone_core
        # MelodyGen.h turns a seed context into an RNG seed with this, and it is
        # PUBLIC because that header is included from the editor as well.
        chalkwalk::seed
        lockstep_bank
        bungee_library
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
        -Wsign-conversion -Wfloat-conversion -Wshadow -Wimplicit-fallthrough
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
        -Wsign-conversion -Wfloat-conversion -Wshadow -Wimplicit-fallthrough
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
        ui/ChromeLookAndFeel.cpp
        ui/KeyButton.cpp
        ui/KeyboardArea.cpp
        ui/MetaBand.cpp
        ui/MetaRotary.cpp
        ui/SurfaceModel.cpp
        ui/NameGen.cpp
        ui/SectionResolve.cpp
        ui/InspectorModel.cpp
        ui/TimelineModel.cpp
        ui/InspectorBar.cpp
        ui/TimelineStrip.cpp
        ui/InPluginTransport.cpp
        ui/ManipulationZone.cpp
        ui/SamplePoolOverlay.cpp
        ui/SoundBankOverlay.cpp
        ui/StandaloneFileBar.cpp
        ui/mode/ModeReducer.cpp
        ui/mode/FuncReskin.cpp
        ui/mode/ModalState.cpp
        ui/mode/LatchOps.cpp)

target_link_libraries(Lockstep
    PRIVATE
        lockstep_engine
        lockstep_fonts
        juce::juce_audio_plugin_client
    PUBLIC
        juce::juce_recommended_config_flags)

# NO juce_recommended_lto_flags. The reason is not the one first recorded here,
# and the correction matters because it points somewhere else entirely.
#
# The original diagnosis was a circular dependency between two static archives:
# lockstep_engine named a LockstepEditor it did not own, and SharedCode.a has to
# precede liblockstep_engine.a, so a one-pass linker met the undefined symbol
# after walking past the archive defining it. That cycle was real and is now
# fixed -- the engine owns createEditor() and asks a registered factory, so it
# names nothing it does not own.
#
# IT DID NOT FIX THE LINK. With LTO back on, Clang 18.1.3 still fails, and now
# the undefined reference is INSIDE ONE ARCHIVE: createPluginFilter()'s lambda
# needs LockstepEditor's constructor, and PluginEditorFactory.cpp and
# PluginEditor.cpp are both members of libLockstep_SharedCode.a. A linker is
# expected to re-scan an archive for symbols newly required by a member it just
# pulled in; under this LTO plugin that does not happen.
#
# So this is archive-member resolution in Clang 18's LTO, not our dependency
# direction, and no amount of restructuring our targets will move it. Measured:
# GCC links it, Clang 21 links it, Clang 18.1.3 does not -- and 18 is far above
# the Clang 10 floor this project claims.
#
# The cost of leaving LTO off is cross-TU inlining and about 4 MB of binary.
# The cost of turning it on is a build that links only on some toolchains.
#
# If this is revisited: the lever is the link line, not the source. Either make
# the plugin's own objects link unconditionally rather than through an archive,
# or wrap the static libraries in a rescan group. Both are linker-specific,
# which is why neither was done for a nicety.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(Lockstep PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow -Wimplicit-fallthrough
        -Werror)
else()
    target_link_libraries(Lockstep PUBLIC juce::juce_recommended_warning_flags)
endif()

clap_juce_extensions_plugin(TARGET Lockstep
    CLAP_ID "com.ChalkWalkMusic.Lockstep"
    CLAP_FEATURES "instrument" "sampler")
