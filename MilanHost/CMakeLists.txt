# File:   MilanHost/CMakeLists.txt
# Brief:  Definition of the Milan Client Host target
# ========================================

set (PROJ_TARGET MilanHost)
set (PROJ_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})

set (PROJ_SOURCES
  ${PROJ_FOLDER}/Containers/string.hpp
  ${PROJ_FOLDER}/Containers/unordered_map.hpp
  ${PROJ_FOLDER}/Containers/vector.hpp
  ${PROJ_FOLDER}/Proxies/Core.hpp
  ${PROJ_FOLDER}/Proxies/Engine.hpp
  ${PROJ_FOLDER}/Proxies/SFXGame.hpp
  ${PROJ_FOLDER}/Systems/Console.cpp
  ${PROJ_FOLDER}/Systems/Console.hpp
  ${PROJ_FOLDER}/Systems/Globals.cpp
  ${PROJ_FOLDER}/Systems/Globals.hpp
  ${PROJ_FOLDER}/Systems/Output.cpp
  ${PROJ_FOLDER}/Systems/Output.hpp
  ${PROJ_FOLDER}/Systems/Imgui.cpp
  ${PROJ_FOLDER}/Systems/Imgui.hpp
  ${PROJ_FOLDER}/Systems/Profilers.cpp
  ${PROJ_FOLDER}/Systems/Profilers.hpp
  ${PROJ_FOLDER}/Systems/State.cpp
  ${PROJ_FOLDER}/Systems/State.hpp
  ${PROJ_FOLDER}/Base.cpp
  ${PROJ_FOLDER}/Base.hpp
  ${PROJ_FOLDER}/Buffers.cpp
  ${PROJ_FOLDER}/Buffers.hpp
  ${PROJ_FOLDER}/Canvas.cpp
  ${PROJ_FOLDER}/Canvas.hpp
  ${PROJ_FOLDER}/Entry.cpp
  ${PROJ_FOLDER}/Entry.hpp
  ${PROJ_FOLDER}/Hooks.cpp
  ${PROJ_FOLDER}/Hooks.hpp
  ${PROJ_FOLDER}/Intercept.cpp
  ${PROJ_FOLDER}/Intercept.hpp
  ${PROJ_FOLDER}/Logger.cpp
  ${PROJ_FOLDER}/Logger.hpp
  ${PROJ_FOLDER}/Misc.cpp
  ${PROJ_FOLDER}/Misc.hpp
  ${PROJ_FOLDER}/MPProfile.cpp
  ${PROJ_FOLDER}/MPProfile.hpp
  ${PROJ_FOLDER}/Streams.cpp
  ${PROJ_FOLDER}/Streams.hpp
)

set (PROJ_LINKS
  MilanCore  # ECSM FFI
  ImGui ImPlot LESDK-LE3 Zydis  # External libraries.
  d3d11.lib dxgi.lib dxguid.lib  # DirectX 11 SDK for ImGui.
)

add_library (${PROJ_TARGET} SHARED ${PROJ_SOURCES})
source_group (TREE "${PROJ_FOLDER}" FILES ${PROJ_SOURCES})

set_target_properties (${PROJ_TARGET} PROPERTIES SUFFIX ".asi")

target_compile_definitions (${PROJ_TARGET} PUBLIC ${MILAN_COMPILE_DEFINITIONS})
target_compile_options (${PROJ_TARGET} PUBLIC ${MILAN_COMPILE_OPTIONS})
target_include_directories (${PROJ_TARGET} PUBLIC ${PROJ_FOLDER})
target_link_options (${PROJ_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})
target_link_libraries (${PROJ_TARGET} PUBLIC ${PROJ_LINKS})


# If there's an appropriate environment variable set auto-copy
# the built .asi to game directory and update default debug options.

if (DEFINED ENV{MILAN_GAME_WIN64})
  set (MILAN_GAME_WIN64 "$ENV{MILAN_GAME_WIN64}")

  message ("-- User-provided Mass Effect 3 (LE) Win64 folder:")
  message ("-- ${MILAN_GAME_WIN64}")

  set (COPY_SOURCE $<TARGET_FILE:${PROJ_TARGET}>)
  set (COPY_DEST ${MILAN_GAME_WIN64}/ASI/$<TARGET_FILE_NAME:${PROJ_TARGET}>)

  add_custom_command (TARGET ${PROJ_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${COPY_SOURCE} ${COPY_DEST}
  )

  set_target_properties (${PROJ_TARGET} PROPERTIES
    VS_DEBUGGER_COMMAND "${MILAN_GAME_WIN64}/MassEffect3.exe"
    VS_DEBUGGER_COMMAND_ARGUMENTS "-enableminidumps -nosplash -nomovie"
    VS_DEBUGGER_WORKING_DIRECTORY "${MILAN_GAME_WIN64}"
  )
endif ()
