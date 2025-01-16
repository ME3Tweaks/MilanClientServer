cmake_minimum_required (VERSION 3.27)
project (MilanClient VERSION 0.1.0 LANGUAGES CXX ASM_MASM)


# ! Global configuration.
# ========================================

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_policy (SET CMP0057 NEW)
cmake_policy (SET CMP0077 NEW)

set_property (GLOBAL PROPERTY DEBUG_CONFIGURATIONS "DEBUG")
set_property (GLOBAL PROPERTY USER_FOLDERS ON)


# ! Build options.
# ========================================

option (MILAN_BUILD_HOST "Build the client host target" ON)

if (DEFINED ENV{MILAN_CMAKE_PRESET})
  set (MILAN_CMAKE_PRESET $ENV{MILAN_CMAKE_PRESET})
  message ("-- Environment selected preset: ${MILAN_CMAKE_PRESET}")
else ()
  message (FATAL_ERROR "!! Environment does not select preset via MILAN_CMAKE_PRESET")
endif ()


# ! Build configuration.
# ========================================

list (APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})

include (MilanBuildConfig)
include (MilanPatchHooks)
include (MilanTargetUtils)


# ! Output paths.
# ========================================

if (CMAKE_GENERATOR MATCHES "Visual Studio 17 2022")
  message ("-- Detected supported generator: ${CMAKE_GENERATOR}")
  set (MILAN_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/Build/${MILAN_CMAKE_PRESET}/$<CONFIG>)
else ()
  message (FATAL_ERROR "!! Detected unsupported generator: ${CMAKE_GENERATOR}")
endif ()

set (MILAN_OUTPUT_DIRECTORY_CLIENT      ${MILAN_OUTPUT_DIRECTORY})
set (MILAN_OUTPUT_DIRECTORY_EXTERNAL    ${MILAN_OUTPUT_DIRECTORY}/ThirdParty)

message ("-- Configured output path (client):     ${MILAN_OUTPUT_DIRECTORY_CLIENT}")
message ("-- Configured output path (external):   ${MILAN_OUTPUT_DIRECTORY_EXTERNAL}")


# ! Project subtrees.
# ========================================

mln_patching_apply ()
mln_patching_setup_tasks ()

set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${MILAN_OUTPUT_DIRECTORY_EXTERNAL})
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${MILAN_OUTPUT_DIRECTORY_EXTERNAL})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${MILAN_OUTPUT_DIRECTORY_EXTERNAL})

add_subdirectory ("ThirdParty")

if (MILAN_BUILD_HOST)

  set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${MILAN_OUTPUT_DIRECTORY_CLIENT})
  set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${MILAN_OUTPUT_DIRECTORY_CLIENT})
  set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${MILAN_OUTPUT_DIRECTORY_CLIENT})

  set_property (GLOBAL PROPERTY VS_STARTUP_PROJECT MilanHost)

  add_subdirectory ("MilanCore")
  add_subdirectory ("MilanHost")

endif ()


# ! Bogus target for freestanding files.
# ========================================

set (REPO_SOURCES
  .editorconfig
  #.gitattributes
  .gitignore
  .gitmodules
  CMakeLists.txt
  CMakePresets.json
  MilanBuildConfig.cmake
  MilanPatchApply.cmake
  MilanPatchCleanup.cmake
  MilanPatchHooks.cmake
  MilanTargetUtils.cmake
)

add_custom_target (Repo SOURCES ${REPO_SOURCES})
source_group (TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${REPO_SOURCES})
