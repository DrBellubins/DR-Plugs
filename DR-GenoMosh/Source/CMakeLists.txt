# Add sources from the current directory to the project target.

file(GLOB UTILS_SOURCES "Utils/*.cpp" "Utils/*.h")
file(GLOB GENETIC_SOURCES "Genetic/*.cpp" "Genetic/*.h")
file(GLOB ENGINE_SOURCES "Engine/*.cpp" "Engine/*.h")
file(GLOB UI_SOURCES "UI/*.cpp" "UI/*.h")

target_sources("${PROJECT_NAME}"
    PRIVATE
        PluginEditor.h
        PluginEditor.cpp
        PluginProcessor.h
        PluginProcessor.cpp
        ${UTILS_SOURCES}
        ${GENETIC_SOURCES}
        ${ENGINE_SOURCES}
        ${UI_SOURCES})

# Add subdirectories with source files, if present
# add_subdirectory(<name>)
