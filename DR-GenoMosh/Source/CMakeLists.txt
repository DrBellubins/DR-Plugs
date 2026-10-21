# Add sources from the current directory to the project target.
#
# CONFIGURE_DEPENDS makes the glob re-run at build time, so dropping a new file
# in one of these folders does not need a manual cmake re-configure (without it
# the new .cpp is compiled nowhere and you get undefined references at link).

file(GLOB UTILS_SOURCES CONFIGURE_DEPENDS "Utils/*.cpp" "Utils/*.h")
file(GLOB GENETIC_SOURCES CONFIGURE_DEPENDS "Genetic/*.cpp" "Genetic/*.h")
file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS "Engine/*.cpp" "Engine/*.h")
file(GLOB UI_SOURCES CONFIGURE_DEPENDS "UI/*.cpp" "UI/*.h")

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
