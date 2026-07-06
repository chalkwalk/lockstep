# fx_audition — offline ear-test gate for the FX catalogue (9.24). Renders a
# before/after matrix of every effect (catalogue id x tier x {default,extreme}
# preset x probe signal) to WAVs so DSP-quality changes can be judged by ear and
# A/B-compared against a stashed baseline. Internally synthesised signals only —
# no input file needed.
juce_add_console_app(fx_audition
    PRODUCT_NAME "Lockstep FX Audition")

juce_generate_juce_header(fx_audition)

target_sources(fx_audition PRIVATE Main.cpp)

target_include_directories(fx_audition PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(fx_audition
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1)

target_link_libraries(fx_audition
    PRIVATE
        lockstep_core
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(fx_audition PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror)
else()
    target_link_libraries(fx_audition PUBLIC juce::juce_recommended_warning_flags)
endif()
