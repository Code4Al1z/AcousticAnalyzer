cmake_minimum_required(VERSION 3.24)

project(AcousticAnalyzer VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# JUCE is a submodule at modules/JUCE
add_subdirectory(modules/JUCE)

# -----------------------------------------------------------------------
# Codesigning — pass these in from CLI or CI secrets:
#   cmake -B build -G Xcode \
#     -DCODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#     -DDEVELOPMENT_TEAM="YOURTEAMID"
#
# For local ad-hoc (no Apple account needed):
#   cmake -B build -G Xcode -DCODESIGN_IDENTITY="-"
# -----------------------------------------------------------------------
if(NOT DEFINED CODESIGN_IDENTITY)
    set(CODESIGN_IDENTITY "-")   # ad-hoc by default; override in CI
endif()

if(NOT DEFINED DEVELOPMENT_TEAM)
    set(DEVELOPMENT_TEAM "")     # leave empty for ad-hoc
endif()

# -----------------------------------------------------------------------
# Plugin target
# -----------------------------------------------------------------------
juce_add_plugin(AcousticAnalyzer
    COMPANY_NAME                "Trailblaiz"
    COMPANY_WEBSITE             "https://alizpasztor.com"
    PLUGIN_MANUFACTURER_CODE    AL1Z
    PLUGIN_CODE                 AN01
    VERSION                     "${PROJECT_VERSION}"
    DESCRIPTION                 "Real-time analyzer for acoustic environments. Measures spectral characteristics, dynamic patterns, and temporal features that influence human wellbeing, stress, and social behavior."

    # AU + VST3 only — AAX requires the Avid SDK + paid iLok certificate
    FORMATS                     AU VST3 Standalone

    PRODUCT_NAME                "AcousticAnalyzer"

    # Required for notarization
    BUNDLE_ID                   "com.trailblaiz.acousticanalyzer"

    # Hardened runtime — required for notarization
    HARDENED_RUNTIME            TRUE

    # Codesigning
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY          "${CODESIGN_IDENTITY}"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM            "${DEVELOPMENT_TEAM}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE             "Manual"

    # Universal binary — Apple Silicon + Intel
    XCODE_ATTRIBUTE_ARCHS                       "arm64 x86_64"
    CMAKE_OSX_ARCHITECTURES                     "arm64;x86_64"

    # Minimum macOS deployment target (10.13 = broadest JUCE-supported)
    CMAKE_OSX_DEPLOYMENT_TARGET                 "10.13"

    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE

    # Set TRUE locally to auto-install to ~/Library/Audio/Plug-Ins for quick testing
    COPY_PLUGIN_AFTER_BUILD     FALSE
)

# -----------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

# Exclude the Jumake template src/CMakeLists stub sources if present
target_sources(AcousticAnalyzer PRIVATE ${SOURCES})

# -----------------------------------------------------------------------
# Entitlements (hardened runtime permissions — required for AU + notarization)
# -----------------------------------------------------------------------
set(ENTITLEMENTS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/AcousticAnalyzer.entitlements")

if(EXISTS "${ENTITLEMENTS_FILE}")
    set_target_properties(AcousticAnalyzer PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${ENTITLEMENTS_FILE}"
    )
endif()

# -----------------------------------------------------------------------
# Link libraries
# -----------------------------------------------------------------------
target_link_libraries(AcousticAnalyzer
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_gui_basics
        juce::juce_core
        juce::juce_data_structures
        juce::juce_graphics
        juce::juce_events
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------
# Compile definitions
# -----------------------------------------------------------------------
target_compile_definitions(AcousticAnalyzer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# -----------------------------------------------------------------------
# Generate JUCE header
# -----------------------------------------------------------------------
juce_generate_juce_header(AcousticAnalyzer)

# -----------------------------------------------------------------------
# Include paths
# -----------------------------------------------------------------------
target_include_directories(AcousticAnalyzer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)
