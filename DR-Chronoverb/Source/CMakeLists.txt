# Add sources from the current directory to the project target.
# We pass the target that needs the sources as the first argument,
# then a visibility parameter for the sources (which should normally be PRIVATE).
# Finally, we supply a list of source files that will be built into the target.

# Gather all .cpp and .h files in Utils
file(GLOB UTILS_SOURCES "Utils/*.cpp" "Utils/*.h")
file(GLOB FILTERS_SOURCES "Filters/*.cpp" "Utils/*.h")

target_sources("${PROJECT_NAME}"
    PRIVATE
        PluginEditor.h
        PluginEditor.cpp
        PluginProcessor.h
        PluginProcessor.cpp
        ${UTILS_SOURCES}
        ${FILTERS_SOURCES}
)

# If exsts, use precompiled header for JUCE to cut per translation-unit compile time
if (TARGET "${PROJECT_NAME}")
    target_precompile_headers("${PROJECT_NAME}"
        PRIVATE
            "<JuceHeader.h>"
    )
endif()

# Add subdirectories with source files, if present
# add_subdirectory(<name>)
