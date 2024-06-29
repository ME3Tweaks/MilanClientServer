# File:   MilanHost/CMakeLists.txt
# Brief:  Definition of the Milan Client Host target
# ========================================

set (PROJ_TARGET MilanHost)
set (PROJ_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})

set (PROJ_SOURCES
  ${PROJ_FOLDER}/Containers/string.hpp
  ${PROJ_FOLDER}/Containers/unordered_map.hpp
  ${PROJ_FOLDER}/Containers/vector.hpp
  ${PROJ_FOLDER}/Milan/Base.cpp
  ${PROJ_FOLDER}/Milan/Base.hpp
  ${PROJ_FOLDER}/Milan/Buffers.cpp
  ${PROJ_FOLDER}/Milan/Buffers.hpp
  ${PROJ_FOLDER}/Milan/Canvas.cpp
  ${PROJ_FOLDER}/Milan/Canvas.hpp
  ${PROJ_FOLDER}/Milan/GuidCacheUtils.cpp
  ${PROJ_FOLDER}/Milan/GuidCacheUtils.hpp
  ${PROJ_FOLDER}/Milan/Hooks.cpp
  ${PROJ_FOLDER}/Milan/Hooks.hpp
  ${PROJ_FOLDER}/Milan/Intercept.cpp
  ${PROJ_FOLDER}/Milan/Intercept.hpp
  ${PROJ_FOLDER}/Milan/Logger.cpp
  ${PROJ_FOLDER}/Milan/Logger.hpp
  ${PROJ_FOLDER}/Milan/Misc.cpp
  ${PROJ_FOLDER}/Milan/Misc.hpp
  ${PROJ_FOLDER}/Milan/MPProfile.cpp
  ${PROJ_FOLDER}/Milan/MPProfile.hpp
  ${PROJ_FOLDER}/Milan/NetProfDecls.hpp
  ${PROJ_FOLDER}/Milan/NetProfExtractors.asm
  ${PROJ_FOLDER}/Milan/NetProfHandlers.cpp
  ${PROJ_FOLDER}/Milan/NetProfStream.cpp
  ${PROJ_FOLDER}/Milan/NetProfStream.hpp
  ${PROJ_FOLDER}/Milan/Streams.cpp
  ${PROJ_FOLDER}/Milan/Streams.hpp
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
  ${PROJ_FOLDER}/Entry.cpp
  ${PROJ_FOLDER}/Entry.hpp
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
target_compile_options (${PROJ_TARGET} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${MILAN_COMPILE_OPTIONS}>)
target_include_directories (${PROJ_TARGET} PUBLIC ${PROJ_FOLDER})
target_link_options (${PROJ_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})
target_link_libraries (${PROJ_TARGET} PUBLIC ${PROJ_LINKS})


# Override unsupported options .asm files.

# foreach (ASM_FILE IN LISTS PROJ_SOURCES)
#   if (ASM_FILE MATCHES ".asm$")
#     message (">> Fixing assembly compile flags for: ${ASM_FILE}")
#     set_source_files_properties (${ASM_FILE} PROPERTIES COMPILE_FLAGS "")
#   endif ()
# endforeach ()



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

  set_property (DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJ_TARGET})

endif ()
