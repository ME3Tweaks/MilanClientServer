# File:   Build/MilanBuildConfig.cmake
# Brief:  Toolchain options for different configurations
# ========================================


# This file, among other things, provides these three options for the build system:
#  * MILAN_COMPILE_DEFINITIONS - Compiler definitions applied to Milan Client code but not dependencies.
#  * MILAN_COMPILE_OPTIONS - Compiler options applied to Milan Client code but not dependencies.
#  * MILAN_LINK_OPTIONS - Linker options applied to Milan Client code but not dependencies.

if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND WIN32)

  add_compile_definitions ( "_CRT_SECURE_NO_WARNINGS" "NOMINMAX" )

  add_compile_options ( "/MP" "/utf-8" "/Zi"
    "$<$<IN_LIST:$<CONFIG>,DEBUG>:/MDd;/Od;/JMC>"
    "$<$<IN_LIST:$<CONFIG>,RELEASE;FINAL>:/MD;/O2>"
  )

  add_link_options ( "/DEBUG"
    "$<$<IN_LIST:$<CONFIG>,DEBUG>:/OPT:NOREF;/OPT:NOICF>"
    "$<$<IN_LIST:$<CONFIG>,RELEASE;FINAL>:/OPT:REF;/OPT:ICF;/INCREMENTAL:NO>"
  )

  set (MILAN_COMPILE_DEFINITIONS
    "$<$<IN_LIST:$<CONFIG>,DEBUG>:_DEBUG>"
    "$<$<IN_LIST:$<CONFIG>,FINAL>:_FINAL_RELEASE>"
  )

  set (MILAN_COMPILE_OPTIONS
    "/W4" "/WX" "/permissive"
    "/wd4121"  # -warn: alignment of member was sensitive to padding
    "/wd4127"  # -warn: conditional expression is constant
  )

  set (MILAN_LINK_OPTIONS
    # ...
  )

elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_SIMULATE_ID MATCHES "MSVC" AND WIN32)

  message (FATAL_ERROR "!! Clang-cl is not yet supported")

else ()

  # Note that compiler conditions are explicitly paired with host system checks.
  message (FATAL_ERROR "!! Unsupported compiler ${CMAKE_CXX_COMPILER_ID} on ${CMAKE_SYSTEM_NAME}")

endif ()
