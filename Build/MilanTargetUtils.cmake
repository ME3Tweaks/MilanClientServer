# File:   Build/MilanTargetUtils.cmake
# Brief:  Auxiliary logic for CMake targets.
# ========================================


# Sorts all auto-generated files for precompiled headers into a source group.
function (MLN_ORGANIZE_PCHS PROJ_TARGET)

  set (GENERATED_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${PROJ_TARGET}.dir/)
  source_group ("[Precompiled Headers]" FILES ${GENERATED_DIRECTORY}/cmake_pch.cxx)

  #message (">> [MLN_ORGANIZE_PCHS] Generated directory = ${GENERATED_DIRECTORY}")
  #message (">> [MLN_ORGANIZE_PCHS] Configuration types = ${CMAKE_CONFIGURATION_TYPES}")

  foreach (VAR_CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
    source_group ("[Precompiled Headers]" FILES ${GENERATED_DIRECTORY}/${VAR_CONFIG_TYPE}/cmake_pch.hxx)
  endforeach()

endfunction ()
