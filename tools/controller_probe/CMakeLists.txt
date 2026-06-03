juce_add_gui_app(controller_probe
    PRODUCT_NAME "Lockstep Controller Probe")

juce_generate_juce_header(controller_probe)

target_sources(controller_probe
    PRIVATE Main.cpp)

target_compile_definitions(controller_probe
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(controller_probe
    PRIVATE
        juce::juce_audio_devices
        juce::juce_gui_basics
        juce::juce_core
        juce::juce_recommended_config_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(controller_probe PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror)
else()
    target_link_libraries(controller_probe PUBLIC juce::juce_recommended_warning_flags)
endif()
