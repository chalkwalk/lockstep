cmake_minimum_required(VERSION 3.22)

project(Lockstep VERSION 0.0.1)

# JUCE
add_subdirectory(JUCE)

# CLAP JUCE extensions
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# Sources
add_subdirectory(src)
