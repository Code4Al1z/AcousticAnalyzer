cmake_minimum_required(VERSION 3.24)

project(ProsocialAudioAnalyzer VERSION 0.0.1)

# JUCE module paths (ensure JUCE is correctly included)
add_subdirectory(modules/JUCE)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define the plugin target
juce_add_plugin(ProsocialAudioAnalyzer
    COMPANY_NAME "Trailblaiz"
    PLUGIN_MANUFACTURER_CODE AL1Z
    PLUGIN_CODE AN01
    VERSION "${PROJECT_VERSION}"
    DESCRIPTION "Real-time analyzer for prosocial acoustic environments. Measures spectral characteristics, dynamic patterns, and temporal features that influence human wellbeing, stress, and social behavior."
    FORMATS AU VST3 AAX Standalone
    PRODUCT_NAME "ProsocialAudioAnalyzer"
)

# Globbing files
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.h"
)

target_sources(ProsocialAudioAnalyzer PRIVATE ${SOURCES})

# Include JUCE modules needed for the plugin
target_link_libraries(ProsocialAudioAnalyzer PRIVATE
    juce::juce_audio_processors    # Core audio processing
    juce::juce_dsp                 # FFT and DSP functions
    juce::juce_gui_extra           # Extended GUI components
    juce::juce_audio_basics        # Basic audio utilities
    juce::juce_audio_formats       # Audio file I/O (for future data export)
    juce::juce_audio_plugin_client # Plugin wrapper
    juce::juce_gui_basics          # Basic GUI components
    juce::juce_core                # Core utilities
    juce::juce_data_structures     # Data structures
    juce::juce_graphics            # Graphics rendering
    juce::juce_events              # Event handling
    juce::juce_audio_utils
    juce::juce_audio_devices
)

# Set the standard C++ version
target_compile_features(ProsocialAudioAnalyzer PUBLIC cxx_std_17)

# Generate JUCE header
juce_generate_juce_header(ProsocialAudioAnalyzer)

# Specify the location of the plugin's resources (icons, etc.)
target_include_directories(ProsocialAudioAnalyzer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/resources
)

# Optional: Add compile definitions for debugging
target_compile_definitions(ProsocialAudioAnalyzer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)