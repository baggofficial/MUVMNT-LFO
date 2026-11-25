cmake_minimum_required(VERSION 3.22)
project(FreeLfoTool VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(FreeLfoTool
    COMPANY_NAME "FreeLfo"
    PRODUCT_NAME "Free LFO Tool"
    PLUGIN_MANUFACTURER_CODE Frlf
    PLUGIN_CODE Flt1
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# Bundled UI font (Inter, SIL Open Font License; see Assets/Fonts/Inter-LICENSE.txt)
juce_add_binary_data(FreeLfoFonts HEADER_NAME "FreeLfoFontData.h" NAMESPACE FreeLfoFontData
    SOURCES Assets/Fonts/Inter-Regular.ttf Assets/Fonts/Inter-SemiBold.ttf)
set_target_properties(FreeLfoFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(FreeLfoTool PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EnvelopeEditor.cpp
    Source/MultibandPanel.cpp
    Source/PresetMenu.cpp)

target_compile_definitions(FreeLfoTool PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FreeLfoTool
    PRIVATE FreeLfoFonts juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional CPU benchmark (not built by default):  cmake -B build -DFREELFO_BENCH=ON
option(FREELFO_BENCH "Build the audio-thread CPU benchmark" OFF)
if(FREELFO_BENCH)
    juce_add_console_app(LfoBench PRODUCT_NAME "LfoBench")
    target_sources(LfoBench PRIVATE
        tests/bench.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/EnvelopeEditor.cpp
        Source/MultibandPanel.cpp
    Source/PresetMenu.cpp)
    target_link_libraries(LfoBench PRIVATE
        FreeLfoFonts
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
    target_compile_definitions(LfoBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

    juce_add_console_app(LfoAudioTest PRODUCT_NAME "LfoAudioTest")
    target_sources(LfoAudioTest PRIVATE
        tests/audio_test.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/EnvelopeEditor.cpp
        Source/MultibandPanel.cpp
    Source/PresetMenu.cpp)
    target_link_libraries(LfoAudioTest PRIVATE
        FreeLfoFonts
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
    target_compile_definitions(LfoAudioTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

    juce_add_console_app(LfoMonkey PRODUCT_NAME "LfoMonkey")
    target_sources(LfoMonkey PRIVATE
        tests/monkey.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/EnvelopeEditor.cpp
        Source/MultibandPanel.cpp
        Source/PresetMenu.cpp)
    target_link_libraries(LfoMonkey PRIVATE
        FreeLfoFonts
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
    target_compile_definitions(LfoMonkey PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)

    juce_add_console_app(LfoSnap PRODUCT_NAME "LfoSnap")
    target_sources(LfoSnap PRIVATE
        tests/snapshot.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/EnvelopeEditor.cpp
        Source/MultibandPanel.cpp
    Source/PresetMenu.cpp)
    target_link_libraries(LfoSnap PRIVATE
        FreeLfoFonts
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
    target_compile_definitions(LfoSnap PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
