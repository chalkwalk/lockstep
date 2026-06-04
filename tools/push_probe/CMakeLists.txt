juce_add_gui_app(push_probe
    PRODUCT_NAME "Lockstep Push Probe")

juce_generate_juce_header(push_probe)

target_sources(push_probe
    PRIVATE Main.cpp)

target_compile_definitions(push_probe
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(push_probe
    PRIVATE
        juce::juce_audio_devices
        juce::juce_gui_basics
        juce::juce_core
        juce::juce_recommended_config_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(push_probe PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror)
else()
    target_link_libraries(push_probe PUBLIC juce::juce_recommended_warning_flags)
endif()
