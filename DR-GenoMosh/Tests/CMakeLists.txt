# Offline test / render harness for the genetic + synth core.
# The core is deliberately free of JUCE types, so this target links only the
# Genetic/ and Engine/ sources (design doc section 10: unit testable core).

file(GLOB TEST_CORE_SOURCES
        "${CMAKE_SOURCE_DIR}/Source/Genetic/*.cpp"
        "${CMAKE_SOURCE_DIR}/Source/Engine/*.cpp")

add_executable(GenoMosh_Tests EngineTests.cpp ${TEST_CORE_SOURCES})

target_include_directories(GenoMosh_Tests PRIVATE "${CMAKE_SOURCE_DIR}/Source")
target_compile_features(GenoMosh_Tests PRIVATE cxx_std_17)
set_target_properties(GenoMosh_Tests PROPERTIES CXX_EXTENSIONS OFF)

add_test(NAME genoMosh_engine COMMAND GenoMosh_Tests)
