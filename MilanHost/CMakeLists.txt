# File:   MilanHost/CMakeLists.txt
# Brief:  Definition of the Milan Client Host target
# ========================================

set (PROJ_TARGET MilanHost)
set (PROJ_FOLDER ${CMAKE_CURRENT_SOURCE_DIR})

set (PROJ_SOURCES
  ${PROJ_FOLDER}/Base/Base.cpp
  ${PROJ_FOLDER}/Base/Base.hpp
  ${PROJ_FOLDER}/Base/Buffers.cpp
  ${PROJ_FOLDER}/Base/Buffers.hpp
  ${PROJ_FOLDER}/Base/IOUtils.cpp
  ${PROJ_FOLDER}/Base/IOUtils.hpp
  ${PROJ_FOLDER}/Base/Logger.cpp
  ${PROJ_FOLDER}/Base/Logger.hpp
  ${PROJ_FOLDER}/Base/LoggingCategories.inl
  ${PROJ_FOLDER}/Base/Streams.cpp
  ${PROJ_FOLDER}/Base/Streams.hpp
  ${PROJ_FOLDER}/Components/CCommands.cpp
  ${PROJ_FOLDER}/Components/CCommands.hpp
  ${PROJ_FOLDER}/Components/CGlobals.cpp
  ${PROJ_FOLDER}/Components/CGlobals.hpp
  #${PROJ_FOLDER}/Components/CImgui.cpp
  #${PROJ_FOLDER}/Components/CImgui.hpp
  ${PROJ_FOLDER}/Components/CNetworkProfiler.cpp
  ${PROJ_FOLDER}/Components/CNetworkProfiler.hpp
  ${PROJ_FOLDER}/Components/COutput.cpp
  ${PROJ_FOLDER}/Components/COutput.hpp
  ${PROJ_FOLDER}/Components/CProfilers.cpp
  ${PROJ_FOLDER}/Components/CProfilers.hpp
  ${PROJ_FOLDER}/Components/CRenderdoc.cpp
  ${PROJ_FOLDER}/Components/CRenderdoc.hpp
  ${PROJ_FOLDER}/Components/CState.cpp
  ${PROJ_FOLDER}/Components/CState.hpp
  ${PROJ_FOLDER}/Components/CDirectories.cpp
  ${PROJ_FOLDER}/Components/CDirectories.hpp
  ${PROJ_FOLDER}/Containers/string.hpp
  ${PROJ_FOLDER}/Containers/unordered_map.hpp
  ${PROJ_FOLDER}/Containers/vector.hpp
  ${PROJ_FOLDER}/Engine/Canvas.cpp
  ${PROJ_FOLDER}/Engine/Canvas.hpp
  ${PROJ_FOLDER}/Engine/ExtObject.cpp
  ${PROJ_FOLDER}/Engine/ExtObject.hpp
  ${PROJ_FOLDER}/Engine/Hooks.cpp
  ${PROJ_FOLDER}/Engine/Hooks.hpp
  ${PROJ_FOLDER}/Engine/Intercept.cpp
  ${PROJ_FOLDER}/Engine/Intercept.hpp
  ${PROJ_FOLDER}/Misc/GuidCacheUtils.cpp
  ${PROJ_FOLDER}/Misc/GuidCacheUtils.hpp
  ${PROJ_FOLDER}/NetProf/NetProfDecls.hpp
  ${PROJ_FOLDER}/NetProf/NetProfExtractors.asm
  ${PROJ_FOLDER}/NetProf/NetProfHandlers.cpp
  ${PROJ_FOLDER}/NetProf/NetProfStream.cpp
  ${PROJ_FOLDER}/NetProf/NetProfStream.hpp
  ${PROJ_FOLDER}/Online/MPProfile.cpp
  ${PROJ_FOLDER}/Online/MPProfile.hpp
  ${PROJ_FOLDER}/Online/SFXOnlineComponentBlazeGame.cpp
  ${PROJ_FOLDER}/Online/SFXOnlineComponentBlazeLogin.cpp
  ${PROJ_FOLDER}/Online/SFXOnlineHijacks.cpp
  ${PROJ_FOLDER}/Online/SFXOnlineHijacks.hpp
  ${PROJ_FOLDER}/Shell/ClientConfig.cpp
  ${PROJ_FOLDER}/Shell/ClientConfig.hpp
  ${PROJ_FOLDER}/Shell/CmdArgs.cpp
  ${PROJ_FOLDER}/Shell/CmdArgs.hpp
  ${PROJ_FOLDER}/Shell/Entry.cpp
  ${PROJ_FOLDER}/Shell/Entry.hpp
  ${PROJ_FOLDER}/Version/MilanHost.rc
  ${PROJ_FOLDER}/Version/SharedVersion.h
  ${PROJ_FOLDER}/Assets/icon.ico
  ${PROJ_FOLDER}/Milan.natvis
)

set (PROJ_LINKS
  LESDK-LE3
  ImGui renderdoc-api tomlplusplus Zydis
  d3d11.lib dxgi.lib dxguid.lib
  Ws2_32.lib
)

add_library (${PROJ_TARGET} SHARED ${PROJ_SOURCES})
source_group (TREE "${PROJ_FOLDER}" FILES ${PROJ_SOURCES})

set_target_properties (${PROJ_TARGET} PROPERTIES SUFFIX ".asi")
set_property (DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJ_TARGET})

target_compile_definitions (${PROJ_TARGET} PUBLIC ${MILAN_COMPILE_DEFINITIONS})
target_compile_options (${PROJ_TARGET} PUBLIC $<$<COMPILE_LANGUAGE:CXX>:${MILAN_COMPILE_OPTIONS}>)
target_include_directories (${PROJ_TARGET} PUBLIC ${PROJ_FOLDER})
target_link_options (${PROJ_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})
target_link_libraries (${PROJ_TARGET} PUBLIC ${PROJ_LINKS})

mln_organize_pchs (${PROJ_TARGET})


# If there's an appropriate environment variable set auto-copy
# the built .asi to game directory and update default debug options.

if (DEFINED ENV{MILAN_GAME_WIN64})

  set (MILAN_GAME_WIN64 "$ENV{MILAN_GAME_WIN64}")

  message ("-- User-provided Mass Effect LMP Win64 folder:")
  message ("-- ${MILAN_GAME_WIN64}")

  set (COPY_SOURCE $<TARGET_FILE:${PROJ_TARGET}>)
  set (COPY_DEST ${MILAN_GAME_WIN64}/ASI/$<TARGET_FILE_NAME:${PROJ_TARGET}>)

  add_custom_command (
    TARGET ${PROJ_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${COPY_SOURCE} ${COPY_DEST}
  )

  set_target_properties (${PROJ_TARGET} PROPERTIES
    VS_DEBUGGER_COMMAND "${MILAN_GAME_WIN64}/MassEffectMultiplayer.exe"
    VS_DEBUGGER_COMMAND_ARGUMENTS "-enableminidumps -nosplash -nomovie"
    VS_DEBUGGER_WORKING_DIRECTORY "${MILAN_GAME_WIN64}"
  )

endif ()
