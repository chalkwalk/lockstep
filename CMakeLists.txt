cmake_minimum_required(VERSION 3.22)

project(Lockstep VERSION 0.0.1)

# C++20 — applies to every target in the tree, including the format-specific
# plugin wrappers (Lockstep_Standalone / _VST3 / _CLAP) that juce_add_plugin
# creates. A per-target PUBLIC cxx_std_20 on lockstep_core does NOT reach those
# wrappers because JUCE compiles their sources independently with its own
# cxx_std_17 floor (see JUCEUtils.cmake). Setting it here guarantees a single
# C++ standard across the whole build so __cpp_char8_t-gated JUCE inlines
# stay consistent across translation units.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# JUCE
add_subdirectory(JUCE)

# CLAP JUCE extensions
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Sources
add_subdirectory(src)

# Headless unit tests
add_subdirectory(tests)
