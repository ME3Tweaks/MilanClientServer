# File:   MilanHost/CMakeLists.txt
# Brief:  Definition of the Milan Client Host target
# ========================================

set (PROJ_TARGET MilanHost)
set (PROJ_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})

set (PROJ_SOURCES
  ${PROJ_FOLDER}/Console.cpp
  ${PROJ_FOLDER}/Console.hpp
  ${PROJ_FOLDER}/Entry.cpp
  ${PROJ_FOLDER}/Entry.hpp
  ${PROJ_FOLDER}/Logs.cpp
  ${PROJ_FOLDER}/Logs.hpp
  ${PROJ_FOLDER}/Profilers.cpp
  ${PROJ_FOLDER}/Profilers.hpp
)

set (PROJ_LINKS
  MilanCore  # ECSM FFI
  ImGui ImPlot LESDK-LE3  # External libraries.
  d3d11.lib dxgi.lib dxguid.lib  # DirectX 11 SDK.
)

add_library (${PROJ_TARGET} SHARED ${PROJ_SOURCES})
source_group (TREE "${PROJ_FOLDER}" FILES ${PROJ_SOURCES})

set_target_properties (${PROJ_TARGET} PROPERTIES SUFFIX ".asi")

target_compile_definitions (${PROJ_TARGET} PUBLIC ${MILAN_COMPILE_DEFINITIONS})
target_compile_options (${PROJ_TARGET} PUBLIC ${MILAN_COMPILE_OPTIONS})
target_include_directories (${PROJ_TARGET} PUBLIC ${PROJ_FOLDER})
target_link_options (${PROJ_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})
target_link_libraries (${PROJ_TARGET} PUBLIC ${PROJ_LINKS})
