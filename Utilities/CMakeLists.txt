# File:   Utilities/CMakeLists.txt
# Brief:  Listing of auxiliary targets
# ========================================

function (MLN_UTILITY_ASI PROJ_TARGET)
  cmake_parse_arguments (
    PROJ
    ""
    "SOURCE"
    "LINKS"
    ${ARGN}
  )

  if (NOT DEFINED PROJ_SOURCE)
    set (PROJ_SOURCE "${PROJ_TARGET}.cpp")
  endif ()

  add_library (${PROJ_TARGET} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/${PROJ_SOURCE})
  set_target_properties (${PROJ_TARGET} PROPERTIES FOLDER "Utilities")
  set_target_properties (${PROJ_TARGET} PROPERTIES PREFIX "Util" SUFFIX ".asi")

  target_compile_definitions (${PROJ_TARGET} PRIVATE ${MILAN_COMPILE_DEFINITIONS})
  target_compile_options (${PROJ_TARGET} PRIVATE ${MILAN_COMPILE_OPTIONS})
  target_link_options (${PROJ_TARGET} PRIVATE ${MILAN_LINK_OPTIONS})
  target_link_libraries (${PROJ_TARGET} PRIVATE ${PROJ_LINKS} LESDK-LE3)
endfunction ()


# ! Utility targets.
# ========================================

mln_utility_asi (CrashSandbox)
