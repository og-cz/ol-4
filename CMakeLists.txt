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
if(UNIX AND NOT APPLE)
    list(APPEND OL4_FORMATS LV2)
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
    LV2URI "https://github.com/olaudio/ol4"
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

juce_add_binary_data(OL4Art SOURCES
    Resources/plate.jpg
    Resources/knob.png
    Resources/layout.json
    Resources/crop_skirt_thr_0.png Resources/crop_skirt_thr_1.png Resources/crop_skirt_thr_2.png
    Resources/crop_skirt_thr_3.png Resources/crop_skirt_thr_4.png
    Resources/crop_skirt_out_0.png Resources/crop_skirt_out_1.png Resources/crop_skirt_out_2.png
    Resources/crop_tog_pwr_0.png Resources/crop_tog_pwr_1.png
    Resources/crop_tog_st_0.png Resources/crop_tog_st_1.png
    Resources/crop_led_pwr_0.png Resources/crop_led_pwr_1.png
    Resources/crop_led_ovl_0.png Resources/crop_led_ovl_1.png)
set_target_properties(OL4Art PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(OL4 PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)

target_compile_definitions(OL4 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(OL4
    PRIVATE
        OL4Art
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

# test host: loads the built VST3 like a DAW and compares its audio to the reference engine
option(OL4_BUILD_HOSTTEST "Build the VST3 host test" OFF)
if(OL4_BUILD_HOSTTEST)
    juce_add_console_app(ol4_hosttest PRODUCT_NAME "ol4_hosttest")
    target_sources(ol4_hosttest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(ol4_hosttest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_LV2=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ol4_hosttest PRIVATE juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_basics juce::juce_recommended_config_flags)
endif()

# editor snapshot tool (renders the real editor code off-screen)
if(OL4_BUILD_HOSTTEST)
    juce_add_console_app(ol4_guishot PRODUCT_NAME "ol4_guishot")
    target_sources(ol4_guishot PRIVATE tests/GuiShot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(ol4_guishot PRIVATE JucePlugin_VersionString="1.0.0" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ol4_guishot PRIVATE OL4Art juce::juce_audio_processors juce::juce_audio_utils juce::juce_gui_basics juce::juce_recommended_config_flags)
endif()
