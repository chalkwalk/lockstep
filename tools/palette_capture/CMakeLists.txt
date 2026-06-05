juce_add_gui_app(palette_capture
    PRODUCT_NAME "Lockstep Palette Capture")

juce_generate_juce_header(palette_capture)

target_sources(palette_capture
    PRIVATE Main.cpp)

# Oklab.h lives beside the Push surface; share it with the runtime matcher.
target_include_directories(palette_capture
    PRIVATE ${CMAKE_SOURCE_DIR}/src)

target_compile_definitions(palette_capture
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)

target_link_libraries(palette_capture
    PRIVATE
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_core
        juce::juce_recommended_config_flags)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(palette_capture PRIVATE
        -Wall -Wextra -Wpedantic
        -Wsign-conversion -Wfloat-conversion -Wshadow
        -Werror)
else()
    target_link_libraries(palette_capture PUBLIC juce::juce_recommended_warning_flags)
endif()
