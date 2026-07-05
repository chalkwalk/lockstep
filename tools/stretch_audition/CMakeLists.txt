# stretch_audition — offline ear-test gate for the Bungee stretch engine (9.23).
# Renders a speed×pitch matrix (plus one looped render) of an input WAV so the
# engine's real-time quality can be judged by ear before it ships.
juce_add_console_app(stretch_audition
    PRODUCT_NAME "Lockstep Stretch Audition")

juce_generate_juce_header(stretch_audition)

target_sources(stretch_audition PRIVATE Main.cpp)

target_include_directories(stretch_audition PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(stretch_audition
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_FLAC=1
        JUCE_USE_OGGVORBIS=1)

target_link_libraries(stretch_audition
    PRIVATE
        lockstep_core
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(stretch_audition PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror)
else()
    target_link_libraries(stretch_audition PUBLIC juce::juce_recommended_warning_flags)
endif()
