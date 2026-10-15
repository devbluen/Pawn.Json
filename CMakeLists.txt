cmake_minimum_required(VERSION 3.19)
option(PAWN_JSON_BUILD_32BIT "Build the 32-bit plugin used by 32-bit SA-MP/open.mp servers (Linux only, adds -m32)" OFF)
set(PAWN_JSON_VERSION "1.0.0" CACHE STRING "Pawn.Json release version")

if(NOT PAWN_JSON_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
	message(FATAL_ERROR "PAWN_JSON_VERSION must use MAJOR.MINOR.PATCH format (got '${PAWN_JSON_VERSION}')")
endif()

# The architecture flags must be set before project() runs compiler ABI
# detection, otherwise CMake keeps believing the target is 64-bit.
if(PAWN_JSON_BUILD_32BIT AND NOT WIN32)
	set(CMAKE_C_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -m32")
	set(CMAKE_CXX_FLAGS_INIT "${CMAKE_CXX_FLAGS_INIT} -m32")
endif()

project(pawn-json LANGUAGES C CXX VERSION ${PAWN_JSON_VERSION})

# The open.mp SDK ships older third-party CMakeLists; keep them configurable
# with newer CMake releases.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(CTest)
include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
	# Static CRT: the plugin must load on servers without the VC++ redistributable.
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
elseif(UNIX)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=hidden")
endif()

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in"
	"${CMAKE_CURRENT_BINARY_DIR}/version.hpp"
	@ONLY
)

# -----------------------------------------------------------------------------
# Dependencies. Each one can point to a local checkout to build offline:
#   -DPAWN_JSON_OMP_SDK_DIR=...  -DPAWN_JSON_SAMP_SDK_DIR=...  -DPAWN_JSON_NLOHMANN_DIR=...
# -----------------------------------------------------------------------------
set(PAWN_JSON_OMP_SDK_DIR "" CACHE PATH "Local open.mp SDK checkout (with submodules)")
set(PAWN_JSON_SAMP_SDK_DIR "" CACHE PATH "Local SA-MP plugin SDK checkout (contains amx/amx.h)")
set(PAWN_JSON_NLOHMANN_DIR "" CACHE PATH "Local nlohmann/json single_include directory (contains nlohmann/json.hpp)")

if(NOT PAWN_JSON_OMP_SDK_DIR)
	FetchContent_Declare(omp_sdk
		GIT_REPOSITORY https://github.com/openmultiplayer/open.mp-sdk.git
		GIT_TAG master
		GIT_SHALLOW TRUE
		GIT_SUBMODULES_RECURSE TRUE
	)
	FetchContent_GetProperties(omp_sdk)
	if(NOT omp_sdk_POPULATED)
		FetchContent_Populate(omp_sdk)
	endif()
	set(PAWN_JSON_OMP_SDK_DIR "${omp_sdk_SOURCE_DIR}")
endif()

if(NOT PAWN_JSON_SAMP_SDK_DIR)
	FetchContent_Declare(samp_sdk
		GIT_REPOSITORY https://github.com/maddinat0r/samp-plugin-sdk.git
		GIT_TAG master
		GIT_SHALLOW TRUE
	)
	FetchContent_GetProperties(samp_sdk)
	if(NOT samp_sdk_POPULATED)
		FetchContent_Populate(samp_sdk)
	endif()
	set(PAWN_JSON_SAMP_SDK_DIR "${samp_sdk_SOURCE_DIR}")
endif()

if(NOT PAWN_JSON_NLOHMANN_DIR)
	FetchContent_Declare(nlohmann_json_src
		URL https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip
		URL_HASH SHA256=a22461d13119ac5c78f205d3df1db13403e58ce1bb1794edc9313677313f4a9d
	)
	FetchContent_GetProperties(nlohmann_json_src)
	if(NOT nlohmann_json_src_POPULATED)
		FetchContent_Populate(nlohmann_json_src)
	endif()
	set(PAWN_JSON_NLOHMANN_DIR "${nlohmann_json_src_SOURCE_DIR}/single_include")
endif()

if(NOT EXISTS "${PAWN_JSON_OMP_SDK_DIR}/CMakeLists.txt")
	message(FATAL_ERROR "open.mp SDK not found at '${PAWN_JSON_OMP_SDK_DIR}'")
endif()
if(NOT EXISTS "${PAWN_JSON_SAMP_SDK_DIR}/amx/amx.h")
	message(FATAL_ERROR "SA-MP plugin SDK not found at '${PAWN_JSON_SAMP_SDK_DIR}' (amx/amx.h missing)")
endif()
if(NOT EXISTS "${PAWN_JSON_NLOHMANN_DIR}/nlohmann/json.hpp")
	message(FATAL_ERROR "nlohmann/json not found at '${PAWN_JSON_NLOHMANN_DIR}'")
endif()

add_subdirectory("${PAWN_JSON_OMP_SDK_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/omp-sdk")

# -----------------------------------------------------------------------------
# Core library: JSON pool, encoding and natives. It does not depend on either
# server SDK beyond amx.h, so it is also linked by the unit tests.
# -----------------------------------------------------------------------------
add_library(pawn-json-core STATIC
	src/amx-api.cpp
	src/encoding.cpp
	src/json-pool.cpp
	src/log.cpp
	src/natives.cpp
)
target_include_directories(pawn-json-core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}
	${PAWN_JSON_SAMP_SDK_DIR}
	${PAWN_JSON_SAMP_SDK_DIR}/amx
	${PAWN_JSON_NLOHMANN_DIR}
)
target_compile_definitions(pawn-json-core PUBLIC HAVE_STDINT_H=1 PAWN_CELL_SIZE=32)
if(WIN32)
	target_compile_definitions(pawn-json-core PUBLIC WIN32)
else()
	target_compile_definitions(pawn-json-core PUBLIC LINUX)
endif()

# -----------------------------------------------------------------------------
# One shared library that exports both the open.mp component entry point and
# the classic SA-MP plugin entry points. The same binary goes into
# `components/` (open.mp) or `plugins/` (SA-MP / open.mp legacy loader).
# -----------------------------------------------------------------------------
add_library(pawn-json SHARED
	src/main.cpp
	src/samp-main.cpp
	src/omp-component.cpp
)
if(WIN32)
	target_sources(pawn-json PRIVATE src/plugin.def)
endif()

target_link_libraries(pawn-json PRIVATE pawn-json-core OMP-SDK)

if(MSVC)
	target_compile_options(pawn-json-core PRIVATE /W4 /permissive- /utf-8)
	target_compile_options(pawn-json PRIVATE /W3 /permissive- /utf-8)
else()
	target_compile_options(pawn-json-core PRIVATE -Wall -Wextra)
	target_compile_options(pawn-json PRIVATE -Wall)
	# The servers ship no C++ runtime. GCC needs it linked in explicitly; the
	# Zig cross-compiler (clang) already links its libc++ statically.
	if(UNIX AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
		target_link_options(pawn-json PRIVATE -static-libgcc -static-libstdc++)
	endif()
	# The statically linked C++ runtime carries megabytes of debug info.
	target_link_options(pawn-json PRIVATE $<$<CONFIG:Release,MinSizeRel>:-s>)
endif()

set_target_properties(pawn-json PROPERTIES
	OUTPUT_NAME "PawnJson"
	PREFIX ""
	LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
	PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
	string(TOUPPER ${cfg} CFG)
	set_target_properties(pawn-json PROPERTIES
		LIBRARY_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/plugins"
		RUNTIME_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/plugins"
		ARCHIVE_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/plugins"
		PDB_OUTPUT_DIRECTORY_${CFG} "${CMAKE_BINARY_DIR}/plugins"
	)
endforeach()

# Copy of the include with the version defines matching this build.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/PawnJson.inc")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/PawnJson.inc" PAWN_JSON_INC)
foreach(part MAJOR MINOR PATCH)
	string(REGEX REPLACE "#define PAWN_JSON_VERSION_${part} [0-9]+"
		"#define PAWN_JSON_VERSION_${part} ${PROJECT_VERSION_${part}}" PAWN_JSON_INC "${PAWN_JSON_INC}")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/pawno/include/PawnJson.inc" "${PAWN_JSON_INC}")

if(BUILD_TESTING)
	add_executable(pawn-json-tests tests/tests.cpp)
	target_link_libraries(pawn-json-tests PRIVATE pawn-json-core)
	if(MSVC)
		target_compile_options(pawn-json-tests PRIVATE /utf-8)
	endif()
	add_test(NAME pawn-json-tests COMMAND pawn-json-tests)
endif()
