# Cross-compiles the 32-bit Linux plugin from Windows with Zig (clang plus a
# bundled glibc/libc++), so no Linux machine, WSL or Docker is needed.
#
# build.ps1 creates small wrapper scripts around zig.exe and passes their
# folder in the PAWN_JSON_ZIG_WRAPPERS environment variable. The glibc version
# baked into the wrappers is the oldest one the binary may load on.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(NOT DEFINED ENV{PAWN_JSON_ZIG_WRAPPERS})
	message(FATAL_ERROR "PAWN_JSON_ZIG_WRAPPERS is not set; build Linux through build.ps1")
endif()
file(TO_CMAKE_PATH "$ENV{PAWN_JSON_ZIG_WRAPPERS}" PAWN_JSON_ZIG_WRAPPERS)

set(CMAKE_C_COMPILER "${PAWN_JSON_ZIG_WRAPPERS}/zig-cc.cmd")
set(CMAKE_CXX_COMPILER "${PAWN_JSON_ZIG_WRAPPERS}/zig-c++.cmd")
set(CMAKE_AR "${PAWN_JSON_ZIG_WRAPPERS}/zig-ar.cmd" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${PAWN_JSON_ZIG_WRAPPERS}/zig-ranlib.cmd" CACHE FILEPATH "" FORCE)

# Zig's linker rejects --dependency-file, which CMake passes with Ninja.
set(CMAKE_LINK_DEPENDS_USE_LINKER OFF)

# Nothing is searched on the Windows host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
