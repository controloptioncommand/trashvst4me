cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
project(BurialVoice VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BV_BUILD_TESTS "Build the offline DSP test" OFF)

# the moduleinfo step runs the built plugin, impossible when cross-compiling
if(CMAKE_CROSSCOMPILING)
    set(BV_VST3_MANIFEST FALSE)
else()
    set(BV_VST3_MANIFEST TRUE)
endif()

# JUCE 7.0.12 is used for the Linux and MinGW (Windows) builds; for macOS with a current
# Xcode use JUCE 8 (see build-mac.sh). Point BV_JUCE_DIR at whichever checkout you have.
set(BV_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to the JUCE checkout")
add_subdirectory("${BV_JUCE_DIR}" JUCE)

# Audio Unit as well on macOS (Logic, GarageBand)
if(APPLE)
    set(BV_FORMATS VST3 AU)
else()
    set(BV_FORMATS VST3)
endif()

juce_add_plugin(BurialVoice
    COMPANY_NAME "Night Bus Audio"
    PLUGIN_MANUFACTURER_CODE Nbau
    PLUGIN_CODE Brvc
    FORMATS ${BV_FORMATS}
    AU_MAIN_TYPE kAudioUnitType_Effect
    HARDENED_RUNTIME_ENABLED FALSE
    PRODUCT_NAME "Burial Voice"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Pitch Shift Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${BV_VST3_MANIFEST})

set(BV_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_sources(BurialVoice PRIVATE ${BV_SOURCES})

set(BV_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(BurialVoice PUBLIC ${BV_DEFS})

target_link_libraries(BurialVoice
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        $<$<NOT:$<BOOL:${CMAKE_CROSSCOMPILING}>>:juce::juce_recommended_lto_flags>
        juce::juce_recommended_warning_flags)

if(BV_BUILD_TESTS)
    juce_add_console_app(BVTest PRODUCT_NAME "BVTest")
    target_sources(BVTest PRIVATE Tests/OfflineTest.cpp ${BV_SOURCES})
    target_compile_definitions(BVTest PRIVATE ${BV_DEFS}
        JucePlugin_Name="Burial Voice" JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(BVTest PRIVATE juce::juce_audio_utils juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()
