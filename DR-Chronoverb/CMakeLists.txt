cmake_minimum_required(VERSION 3.22)

# Project
project(Chronoverb VERSION 0.0.1 LANGUAGES C CXX)

# --- Modern CMake defaults ----------------------------------------------------
# Require a modern C++ standard and produce compile_commands.json for tools.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(ENABLE_CCACHE "Enable ccache compiler launcher if present" ON)
option(ENABLE_UNITY_BUILDS "Enable JUCE unity builds for faster builds" ON)

# --- Compiler capability checks ----------------------------------------------
include(CheckCXXCompilerFlag)

check_cxx_compiler_flag("-gline-tables-only" COMPILER_SUPPORTS_GLINE)
check_cxx_compiler_flag("-gsplit-dwarf" COMPILER_SUPPORTS_GSPLIT)

# --- Optional compiler launchers (ccache) ------------------------------------
if (ENABLE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if (CCACHE_PROGRAM)
        message(STATUS "Using ccache at ${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
    endif()
endif()

# --- Recommended per-config debug flags (applied per-target later) -----------
if (COMPILER_SUPPORTS_GLINE)
    message(STATUS "Compiler supports -gline-tables-only")
else()
    message(STATUS "Compiler does NOT support -gline-tables-only")
endif()

if (COMPILER_SUPPORTS_GSPLIT)
    message(STATUS "Compiler supports -gsplit-dwarf")
else()
    message(STATUS "Compiler does NOT support -gsplit-dwarf")
endif()

# --- JUCE dependency (FetchContent) ------------------------------------------
set(LIB_JUCE_TAG "7.0.8")

include(FetchContent)

# Keep dependencies outside build dir so clean build doesn't re-fetch
set(FETCHCONTENT_BASE_DIR "${PROJECT_SOURCE_DIR}/Libs" CACHE PATH "External dependencies path." FORCE)

FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${LIB_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_CONFIG     advice.detachedHead=false
    SOURCE_DIR     "${FETCHCONTENT_BASE_DIR}/JUCE"
    SUBBUILD_DIR   "${FETCHCONTENT_BASE_DIR}/JUCE-Subbuild"
    BINARY_DIR     "${FETCHCONTENT_BASE_DIR}/JUCE-Build"
)

FetchContent_MakeAvailable(juce)

# --- JUCE plugin target ------------------------------------------------------
juce_add_plugin("${PROJECT_NAME}"
    FORMATS Standalone AU VST3
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
    VST3_CATEGORIES Fx,Reverb
    COPY_PLUGIN_AFTER_BUILD FALSE
)

# Make sure the shared code target uses c++17
target_compile_features("${PROJECT_NAME}" PUBLIC cxx_std_17)

# Set recommended target properties for all project targets (disable extensions)
get_property(project_targets DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
set_target_properties(${project_targets} PROPERTIES CXX_EXTENSIONS OFF)

# --- Generate JuceHeader and ensure ordering for PCH/generation ----------------
juce_generate_juce_header("${PROJECT_NAME}")

# Retrieve the generated sources directory and the JuceHeader path so we can
# create an explicit dependency to avoid races (PCH vs generated header).
get_target_property(JUCE_GENERATED_DIR "${PROJECT_NAME}" JUCE_GENERATED_SOURCES_DIRECTORY)
set(JUCE_HEADER_PATH "${JUCE_GENERATED_DIR}/JuceHeader.h")

# Create a prepare target that depends on the rule file produced by juce_generate_juce_header
# This target is used to guarantee ordering for PCH / compilation of module .cpp files.
add_custom_target(prepare_juce_header ALL DEPENDS "${JUCE_HEADER_PATH}.rule")
add_dependencies("${PROJECT_NAME}" prepare_juce_header)

# Ensure the generated directory is added to the plugin target includes explicitly
target_include_directories("${PROJECT_NAME}" PRIVATE "${JUCE_GENERATED_DIR}")

# --- Source layout / subdirectory --------------------------------------------
# Add sources from Source/ (this subdir contains Source/CMakeLists.txt)
add_subdirectory(Source)

# --- Apply per-target compile options and defs (robust & modern) -------------
# POSIX / GNU feature macros only on Linux-like systems (not on Apple).
if (UNIX AND NOT APPLE)
    target_compile_definitions("${PROJECT_NAME}"
        PRIVATE
            _DEFAULT_SOURCE=1
            _XOPEN_SOURCE=700
            _POSIX_C_SOURCE=200809L
    )
endif()

# Apply debug-only flags using generator expressions (per-target)
if (TARGET "${PROJECT_NAME}")
    if (COMPILER_SUPPORTS_GLINE)
        target_compile_options("${PROJECT_NAME}" PRIVATE $<$<CONFIG:Debug>:-gline-tables-only>)
    endif()

    if (COMPILER_SUPPORTS_GSPLIT)
        target_compile_options("${PROJECT_NAME}" PRIVATE $<$<CONFIG:Debug>:-gsplit-dwarf>)
    endif()

    # Recommended warning groups (target-local)
    target_compile_options("${PROJECT_NAME}" PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )

    # Platform LTO / recommended flags are provided by JUCE via these imported targets
    target_link_libraries("${PROJECT_NAME}"
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
    )
endif()

# --- Unity builds (optional) --------------------------------------------------
if (ENABLE_UNITY_BUILDS AND TARGET "${PROJECT_NAME}")
    set_target_properties("${PROJECT_NAME}" PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 16
    )
endif()

# --- Precompiled headers (guarded) ------------------------------------------
# Only enable PCH after the generated JuceHeader rule is available (we used prepare_juce_header for ordering).
# Source/CMakeLists.txt calls target_precompile_headers on the same target; ensure dependency ordering is set.
if (TARGET "${PROJECT_NAME}")
    # It's safe to enable PCH here because prepare_juce_header will ensure JuceHeader is generated first.
    target_precompile_headers("${PROJECT_NAME}"
        PRIVATE
            "<JuceHeader.h>"
    )
endif()

# --- Binary data target (assets) ---------------------------------------------
juce_add_binary_data(Assets
    SOURCES Assets/logo.png
)

set_target_properties(Assets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Link small helper JUCE modules and binary data
target_link_libraries("${PROJECT_NAME}"
    PRIVATE
        Assets
        juce::juce_audio_utils
        juce::juce_dsp
)

# --- IDE source grouping (nice-to-have) -------------------------------------
# Move target(s) into a "Targets" folder and organise source groups
set_target_properties(${project_targets} PROPERTIES FOLDER "Targets" XCODE_GENERATE_SCHEME ON)
set_target_properties("${PROJECT_NAME}" PROPERTIES FOLDER "")

get_target_property(project_sources "${PROJECT_NAME}" SOURCES)
get_target_property(juce_library_code "${PROJECT_NAME}" JUCE_GENERATED_SOURCES_DIRECTORY)
set(juce_header "${juce_library_code}/JuceHeader.h")

# Remove JuceHeader from source list to avoid duplicate handling
list(REMOVE_ITEM project_sources "${juce_header}")

source_group("JUCE Library Code" FILES "${juce_header}")
source_group("JUCE Library Code/CMake Rules" FILES "${juce_header}.rule")
source_group(TREE "${PROJECT_SOURCE_DIR}/Source" FILES ${project_sources})

# If Assets target exists, put its sources at the root for convenience
if (TARGET Assets)
    get_target_property(project_assets Assets SOURCES)
    source_group("" FILES ${project_assets})
endif()

# --- MSVC runtime modern setting (CMake 3.15+) --------------------------------
if (MSVC AND TARGET "${PROJECT_NAME}")
    # Use MultiThreaded for release and MultiThreadedDebug for debug
    set_property(TARGET "${PROJECT_NAME}" PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- Final messages -----------------------------------------------------------
message(STATUS "Project: ${PROJECT_NAME} (${PROJECT_VERSION})")
message(STATUS "JUCE: ${LIB_JUCE_TAG}")
