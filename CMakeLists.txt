cmake_minimum_required(VERSION 3.22)

project(OL4 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

# JUCE: use a local checkout if given (-DOL4_JUCE_DIR=...), otherwise fetch the pinned release.
set(OL4_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(OL4_JUCE_DIR)
    add_subdirectory(${OL4_JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(OL4_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND OL4_FORMATS AU)
endif()

juce_add_plugin(OL4
    COMPANY_NAME "OL Audio"
    COMPANY_WEBSITE "https://github.com/"
    BUNDLE_ID com.olaudio.ol4
    PLUGIN_MANUFACTURER_CODE OlAu
    PLUGIN_CODE Ol4a
    FORMATS ${OL4_FORMATS}
    PRODUCT_NAME "OL-4 Optical Leveler"
    DESCRIPTION "Solid-state opto compressor/limiter"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

target_sources(OL4 PRIVATE Source/PluginProcessor.cpp)

target_compile_definitions(OL4 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(OL4
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# engine-vs-reference test (no JUCE needed)
option(OL4_BUILD_TESTS "Build the engine match test" ON)
if(OL4_BUILD_TESTS)
    add_executable(ol4_engine_match tests/engine_match.cpp)
endif()
