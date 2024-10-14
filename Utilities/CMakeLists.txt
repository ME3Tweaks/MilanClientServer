# File:   Utilities/CMakeLists.txt
# Brief:  Listing of auxiliary targets
# ========================================

function (MLN_UTILITY_SOURCE_GROUP PROJ_TARGET)
  get_target_property (PROJ_SOURCES ${PROJ_TARGET} SOURCES)
  source_group (TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${PROJ_SOURCES})
endfunction ()

function (MLN_UTILITY_ASI PROJ_TARGET)
  cmake_parse_arguments (
    PROJ
    ""
    "SOURCE"
    "DEFINES;LINKS"
    ${ARGN}
  )

  if (NOT DEFINED PROJ_SOURCE)
    set (PROJ_SOURCE "${PROJ_TARGET}.cpp")
  endif ()

  add_library (${PROJ_TARGET} SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/${PROJ_SOURCE}
    ${CMAKE_CURRENT_SOURCE_DIR}/UtilsShared.hpp
  )

  set_target_properties (${PROJ_TARGET} PROPERTIES FOLDER "Utilities")
  set_target_properties (${PROJ_TARGET} PROPERTIES PREFIX "Util" SUFFIX ".asi")

  target_compile_definitions (${PROJ_TARGET} PRIVATE ${MILAN_COMPILE_DEFINITIONS} ${PROJ_DEFINES})
  target_compile_options (${PROJ_TARGET} PRIVATE ${MILAN_COMPILE_OPTIONS})
  target_include_directories(${PROJ_TARGET} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_options (${PROJ_TARGET} PRIVATE ${MILAN_LINK_OPTIONS})
  target_link_libraries (${PROJ_TARGET} PRIVATE ${PROJ_LINKS} LESDK-LE3)

  mln_utility_source_group (${PROJ_TARGET})
  mln_organize_pchs (${PROJ_TARGET})
endfunction ()


# ! Utility targets.
# ========================================

mln_utility_asi (CrashSandbox)
