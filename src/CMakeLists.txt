# Intentionally empty.
#
# The plugin target is defined in the top-level CMakeLists.txt, which globs
# src/*.cpp and src/*.h. This directory is NOT added with add_subdirectory().
#
# This file used to hold the unused jumake "AudioPlugin" template, with a second
# juce_add_plugin() call and different manufacturer/plugin codes. Keeping it as
# real CMake would have caused a duplicate-target error if it were ever included.
