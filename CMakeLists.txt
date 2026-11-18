cmake_minimum_required(VERSION 3.22)

project(OtokiriScope VERSION 1.1.0 LANGUAGES CXX)

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

juce_add_plugin(OtokiriScope
    COMPANY_NAME "OtokiriScope"
    BUNDLE_ID "com.github.otokiriscope.OtokiriScope"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Otks
    PLUGIN_CODE Otks
    FORMATS VST3
    PRODUCT_NAME "OtokiriScope"
)

juce_add_binary_data(OtokiriScopeResources
    SOURCES
        Resources/logo.png
)

juce_generate_juce_header(OtokiriScope)

target_sources(OtokiriScope PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_compile_definitions(OtokiriScope
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

if (MSVC)
    target_compile_options(OtokiriScope PRIVATE /utf-8)
endif()

target_link_libraries(OtokiriScope
    PRIVATE
        OtokiriScopeResources
        juce::juce_audio_utils
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
