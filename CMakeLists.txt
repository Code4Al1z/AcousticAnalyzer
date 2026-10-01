cmake_minimum_required(VERSION 3.24)

project(AcousticAnalyzer VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------------------
# JUCE
#   1. Use modules/JUCE if it exists (git submodule or manual clone).
#   2. Otherwise download a pinned JUCE release at configure time, so a fresh
#      checkout builds without any extra setup.
# ------------------------------------------------------------------------------
set(ACOUSTIC_JUCE_TAG "8.0.8" CACHE STRING "JUCE release tag used when modules/JUCE is missing")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/modules/JUCE/CMakeLists.txt")
    message(STATUS "Using local JUCE from modules/JUCE")
    add_subdirectory(modules/JUCE)
else()
    message(STATUS "modules/JUCE not found - fetching JUCE ${ACOUSTIC_JUCE_TAG}")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${ACOUSTIC_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin formats - only the ones that can build on the current platform
# ------------------------------------------------------------------------------
set(ACOUSTIC_FORMATS VST3 Standalone)

if(APPLE)
    list(APPEND ACOUSTIC_FORMATS AU)
endif()

# AAX needs the Avid SDK: configure with -DAAX_SDK_PATH=<path to SDK> to enable it
if(AAX_SDK_PATH)
    juce_set_aax_sdk_path("${AAX_SDK_PATH}")
    list(APPEND ACOUSTIC_FORMATS AAX)
endif()

# ------------------------------------------------------------------------------
# Plugin target
# ------------------------------------------------------------------------------
juce_add_plugin(AcousticAnalyzer
    COMPANY_NAME "TrailblaiZ"
    PLUGIN_MANUFACTURER_CODE AL1Z
    PLUGIN_CODE AN01
    VERSION "${PROJECT_VERSION}"
    DESCRIPTION "Real-time analyzer for acoustic environments. Measures spectral characteristics, dynamic patterns, and temporal features that influence human wellbeing, stress, and social behavior."
    FORMATS ${ACOUSTIC_FORMATS}
    PRODUCT_NAME "AcousticAnalyzer"
)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

target_sources(AcousticAnalyzer PRIVATE ${SOURCES})

target_link_libraries(AcousticAnalyzer
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_features(AcousticAnalyzer PUBLIC cxx_std_17)

target_compile_definitions(AcousticAnalyzer
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

juce_generate_juce_header(AcousticAnalyzer)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_include_directories(AcousticAnalyzer PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/resources)
endif()