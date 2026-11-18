cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(Sighs VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_plugin(Sighs
    COMPANY_NAME "Independent Audio"
    PLUGIN_MANUFACTURER_CODE Ncox
    PLUGIN_CODE Sigh
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Sighs"
    BUNDLE_ID com.nickcox.sighs
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Modulation
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(Sighs PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Sighs PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Sighs
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless check of the JUCE wrapper (oversampling, state, bus layouts).
juce_add_console_app(SighsProcessorTest PRODUCT_NAME "SighsProcessorTest")
target_sources(SighsProcessorTest PRIVATE
    tests/processor_test.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(SighsProcessorTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(SighsProcessorTest PRIVATE
    juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
