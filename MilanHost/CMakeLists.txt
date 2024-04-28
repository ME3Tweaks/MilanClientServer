# File:   MilanHost/CMakeLists.txt
# Brief:  Definition of the Milan Client Host target
# ========================================

set (PROJ_TARGET MilanHost)
set (PROJ_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})

set (PROJ_SOURCES
  ${PROJ_FOLDER}/Containers/string.hpp
  ${PROJ_FOLDER}/Containers/unordered_map.hpp
  ${PROJ_FOLDER}/Containers/vector.hpp
  ${PROJ_FOLDER}/Systems/Builtins.cpp
  ${PROJ_FOLDER}/Systems/Builtins.hpp
  ${PROJ_FOLDER}/Systems/Console.cpp
  ${PROJ_FOLDER}/Systems/Console.hpp
  ${PROJ_FOLDER}/Systems/Output.cpp
  ${PROJ_FOLDER}/Systems/Output.hpp
  ${PROJ_FOLDER}/Systems/Panels.cpp
  ${PROJ_FOLDER}/Systems/Panels.hpp
  ${PROJ_FOLDER}/Base.cpp
  ${PROJ_FOLDER}/Base.hpp
  ${PROJ_FOLDER}/Entry.cpp
  ${PROJ_FOLDER}/Entry.hpp
  ${PROJ_FOLDER}/Logger.cpp
  ${PROJ_FOLDER}/Logger.hpp
  ${PROJ_FOLDER}/Misc.cpp
  ${PROJ_FOLDER}/Misc.hpp
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
