# Toolchain-x86_64-w64-mingw32.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

set(triple x86_64-w64-mingw32)

set(CMAKE_C_COMPILER   ${triple}-gcc)
set(CMAKE_CXX_COMPILER ${triple}-g++)
set(CMAKE_RC_COMPILER  ${triple}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${triple})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Recommended for JUCE plugins
add_compile_options(-static-libgcc -static-libstdc++)
add_link_options(
    -static
    -static-libgcc
    -static-libstdc++
    -Wl,--subsystem,windows   # removes console window for Standalone
)
