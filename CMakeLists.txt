cmake_minimum_required(VERSION 3.22)

project(OtokirisVST3 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(OtokirisVST3
    COMPANY_NAME "Otokiris"
    BUNDLE_ID "jp.otokiris.OtokirisVST3"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Otk1
    PLUGIN_CODE Otk3
    FORMATS VST3
    PRODUCT_NAME "Otokiris"
)

juce_generate_juce_header(OtokirisVST3)

target_sources(OtokirisVST3 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_compile_definitions(OtokirisVST3
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

if (MSVC)
    target_compile_options(OtokirisVST3 PRIVATE /utf-8)
endif()

target_link_libraries(OtokirisVST3
    PRIVATE
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
