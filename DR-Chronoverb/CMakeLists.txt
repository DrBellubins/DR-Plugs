cmake_minimum_required(VERSION 3.22)

project(Chronoverb VERSION 0.0.1)

set(LIB_JUCE_TAG "7.0.8")

string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -s")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
option(JUCE_ENABLE_MODULE_SOURCE_GROUPS "Show all module sources in IDE projects" ON)

add_subdirectory(Libs/JUCE)

juce_add_plugin("${PROJECT_NAME}"
    FORMATS Standalone VST3
    PRODUCT_NAME "Dr Chronoverb"
    PLUGIN_NAME "Chronoverb"
    COMPANY_NAME DrBell
    PLUGIN_MANUFACTURER_CODE 42Dr
    PLUGIN_CODE drCV
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Fx Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_compile_features("${PROJECT_NAME}" PUBLIC cxx_std_17)
set_target_properties("${PROJECT_NAME}" PROPERTIES CXX_EXTENSIONS OFF)

add_subdirectory(Source)

target_compile_definitions("${PROJECT_NAME}"
    PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

juce_add_binary_data(Assets
    SOURCES
        Assets/logo.png
)

set_target_properties(Assets PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries("${PROJECT_NAME}"
    PRIVATE
        Assets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

get_target_property(ProjectSources "${PROJECT_NAME}" SOURCES)
source_group(TREE "${PROJECT_SOURCE_DIR}/Source" FILES ${ProjectSources})

if(TARGET Assets)
    get_target_property(ProjectAssets Assets SOURCES)
    source_group("" FILES ${ProjectAssets})
endif()