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

# NO juce_recommended_lto_flags, and this is a correctness decision rather than
# a taste one.
#
# There is a real circular dependency between two static archives here.
# `lockstep_engine` is deliberately UI-free -- it holds LockstepProcessor and
# nothing else -- so its `createEditor()` references a `LockstepEditor` it does
# not own. That is a link seam, and it is on purpose: the headless test binary
# satisfies it with TestEditorStub.cpp, and the dispatch test binary compiles
# the real PluginEditor.cpp straight into itself. Both of those work because an
# object file is linked unconditionally.
#
# The plugin is the case that does not. PluginEditor.cpp lives in the Lockstep
# (SharedCode) target, SharedCode depends on lockstep_engine, so CMake must
# place libLockstep_SharedCode.a BEFORE liblockstep_engine.a -- and a one-pass
# linker reaching engine's undefined LockstepEditor has already walked past the
# archive that defines it. That it linked at all was luck: some earlier object
# happened to pull PluginEditor.o in.
#
# LTO removes the luck. It defers symbol resolution until after the merge, past
# the point where a skipped archive member can be reconsidered, and the
# standalone fails with an undefined reference to the editor's constructor.
# Measured: GCC links it, Clang 21 links it, Clang 18.1.3 (ubuntu-latest, and
# well above this project's stated Clang 10 floor) does not.
#
# So LTO goes. It buys some inlining across translation units; it costs a build
# that only links on some toolchains, which is not a trade worth making for a
# plugin already compiled at -O3.
#
# This is a workaround and the cycle is the actual bug. Fixing it properly means
# the engine not naming a symbol it does not own -- see ROADMAP 6.8. Until then,
# do not reintroduce these flags without fixing that first.

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
