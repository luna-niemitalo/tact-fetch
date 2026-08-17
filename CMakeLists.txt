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

# DESIGN.md #9's fetch-pipeline resolution: not a hand-rolled fetcher
# bypassing CascLib, but a local patch to vendored CascLib's own
# HttpDownloadFile (vendor/CascLib/src/CascFiles.cpp) so the existing
# BLTE-decode/hash-verify pipeline keeps working unmodified. That patch
# is what actually calls libcurl -- casc_static is a static archive
# (just object files, no link step of its own), so the symbols it needs
# only have to be satisfied when the final executable links, which is
# why linking CURL::libcurl to tact-fetch here is enough. (Can't instead
# add it as a casc_static usage requirement: CascLib's own CMakeLists.txt
# calls target_link_libraries(casc_static ...) with the old plain
# signature, and CMake refuses to mix that with a keyword-signature call
# like PUBLIC/PRIVATE on the same target.)
find_package(CURL REQUIRED)

add_executable(tact-fetch
  src/main.cpp
  src/build_info.cpp
  src/cache_dir.cpp
  src/casc_storage.cpp
  src/content_sniff.cpp
  src/dry_run.cpp
  src/fetch.cpp
  src/log.cpp
  src/plan.cpp
  src/worklist.cpp
)
target_link_libraries(tact-fetch PRIVATE casc_static CURL::libcurl)
