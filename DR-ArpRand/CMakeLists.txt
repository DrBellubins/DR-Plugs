cmake_minimum_required(VERSION 3.22)

project(DR-ArpRand VERSION 0.0.1)

set(LIB_JUCE_TAG "8.0.13")

string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -s")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Show all module sources in IDE projects" ON)

add_subdirectory(Libs/JUCE)

juce_add_plugin("${PROJECT_NAME}"
    FORMATS Standalone AU VST3
    PRODUCT_NAME "Dr Arp Rand"
    PLUGIN_NAME "Arp Rand"
    COMPANY_NAME DrBell
    PLUGIN_MANUFACTURER_CODE 42Dr
    PLUGIN_CODE drAR
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_compile_features("${PROJECT_NAME}" PUBLIC cxx_std_23)
set_target_properties("${PROJECT_NAME}" PROPERTIES CXX_EXTENSIONS OFF)

add_subdirectory(Source)

target_compile_definitions("${PROJECT_NAME}"
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries("${PROJECT_NAME}"
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

get_target_property(ProjectSources "${PROJECT_NAME}" SOURCES)
source_group(TREE "${PROJECT_SOURCE_DIR}/Source" FILES ${ProjectSources})