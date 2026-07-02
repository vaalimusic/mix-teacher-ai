cmake_minimum_required(VERSION 3.22)

project(MixTeacher VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(MIX_TEACHER_JUCE_TAG "8.0.14" CACHE STRING "JUCE git tag used to build Mix Teacher")
option(MIX_TEACHER_COPY_PLUGIN_AFTER_BUILD "Copy the VST3 to the default plugin folder after build" OFF)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${MIX_TEACHER_JUCE_TAG}
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MixTeacher
    COMPANY_NAME "Mix Teacher"
    BUNDLE_ID "com.mixteacher.plugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${MIX_TEACHER_COPY_PLUGIN_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE MxTc
    PLUGIN_CODE MxT1
    FORMATS VST3
    PRODUCT_NAME "Mix Teacher")

juce_generate_juce_header(MixTeacher)

target_sources(MixTeacher
    PRIVATE
        Source/Analysis.cpp
        Source/Analysis.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h)

target_compile_definitions(MixTeacher
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

if (MSVC)
    target_compile_options(MixTeacher PRIVATE /utf-8)
endif()

target_link_libraries(MixTeacher
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
