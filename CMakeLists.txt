cmake_minimum_required(VERSION 3.16)
project(tact-fetch CXX)

# Nix provides ccache in the dev shell and sets CCACHE_DIR; use it if present
# rather than assuming it's on PATH outside that shell.
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(CASC_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# CascLib's own CMakeLists still declares cmake_minimum_required(VERSION 2.6),
# which modern CMake (>=4.0) refuses outright -- same workaround as casc-tool.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory(vendor/CascLib)

# CascLib is only ever used here for local manifest reads (FileDataID ->
# EKey/CKey/ContentSize) -- see DESIGN.md #9. The actual CDN fetch goes
# through libcurl instead of CascLib's own online-fetch path.
find_package(CURL REQUIRED)

# Placeholder only -- see CLAUDE.md ## Status, nothing beyond this stub
# exists yet. Not linked against casc_static or CURL::libcurl yet because
# src/ doesn't call into either; both are wired into the build now so
# `cmake --build` proves the vendoring/dependency shape works before any
# real logic lands on top of it.
add_executable(tact-fetch src/main.cpp)
