# File:   ThirdParty/CMakeLists.txt
# Brief:  Definitions for pulling in dependency targets
# ========================================

# We treat ImGui and ImPlot as in-source targets although they are vendored.
# Their targets need to be defined in this file because neither of them
# assume build system, and neither of them provide CMakeLists.txt


# ! fpng target.
# ========================================

set (FPNG_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/fpng")
set (FPNG_TARGET "fpng")

set (FPNG_SOURCES
  ${FPNG_FOLDER}/fpng.cpp
  ${FPNG_FOLDER}/fpng.h
)

add_library (${FPNG_TARGET} STATIC ${FPNG_SOURCES})
set_target_properties (${FPNG_TARGET} PROPERTIES FOLDER "ThirdParty")
source_group (TREE "${FPNG_FOLDER}" FILES ${FPNG_SOURCES})

target_compile_definitions (${FPNG_TARGET} PRIVATE ${MILAN_COMPILE_DEFINITIONS})
target_compile_options (${FPNG_TARGET} PRIVATE ${MILAN_COMPILE_OPTIONS})
target_include_directories (${FPNG_TARGET} PUBLIC ${FPNG_FOLDER})
target_link_options (${FPNG_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})


# ! ImGui target.
# ========================================

set (IMGUI_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/imgui-1.93.0")
set (IMGUI_TARGET "ImGui")

set (IMGUI_SOURCES
  ${IMGUI_FOLDER}/backends/imgui_impl_dx11.cpp
  ${IMGUI_FOLDER}/backends/imgui_impl_dx11.h
  ${IMGUI_FOLDER}/backends/imgui_impl_win32.cpp
  ${IMGUI_FOLDER}/backends/imgui_impl_win32.h
  ${IMGUI_FOLDER}/misc/debuggers/imgui.natvis
  ${IMGUI_FOLDER}/imconfig.h
  ${IMGUI_FOLDER}/imgui_demo.cpp
  ${IMGUI_FOLDER}/imgui_draw.cpp
  ${IMGUI_FOLDER}/imgui_internal.h
  ${IMGUI_FOLDER}/imgui_tables.cpp
  ${IMGUI_FOLDER}/imgui_widgets.cpp
  ${IMGUI_FOLDER}/imgui.cpp
  ${IMGUI_FOLDER}/imgui.h
  ${IMGUI_FOLDER}/imstb_rectpack.h
  ${IMGUI_FOLDER}/imstb_textedit.h
  ${IMGUI_FOLDER}/imstb_truetype.h
)

add_library (${IMGUI_TARGET} STATIC ${IMGUI_SOURCES})
set_target_properties (${IMGUI_TARGET} PROPERTIES FOLDER "ThirdParty")
source_group (TREE "${IMGUI_FOLDER}" FILES ${IMGUI_SOURCES})

target_compile_definitions (${IMGUI_TARGET} PRIVATE ${MILAN_COMPILE_DEFINITIONS})
target_compile_options (${IMGUI_TARGET} PRIVATE ${MILAN_COMPILE_OPTIONS})
target_include_directories (${IMGUI_TARGET} PUBLIC ${IMGUI_FOLDER})
target_link_options (${IMGUI_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})


# ! LESDK-LE3 target.
# ========================================

set (LESDK_WITH_LE1 OFF)
set (LESDK_WITH_LE2 OFF)
set (LESDK_WITH_LE3 ON)
set (LESDK_WITH_SAMPLES OFF)
set (LESDK_WITH_TESTS OFF)

add_subdirectory ("LExSDKv2")
set_target_properties (LESDK-LE3 PROPERTIES FOLDER "ThirdParty")


# ! nlohmann/json interface.
# ========================================

set (NLOHMANN_JSON_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann")
set (NLOHMANN_JSON_SOURCE "${NLOHMANN_JSON_FOLDER}/json.hpp")
set (NLOHMANN_JSON_TARGET "nlohmann-json")

add_library (${NLOHMANN_JSON_TARGET} INTERFACE ${NLOHMANN_JSON_SOURCE})
set_target_properties (${NLOHMANN_JSON_TARGET} PROPERTIES FOLDER "ThirdParty")
source_group (TREE "${NLOHMANN_JSON_FOLDER}" FILES ${NLOHMANN_JSON_SOURCE})
target_include_directories (${NLOHMANN_JSON_TARGET} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})


# ! RenderDoc API interface.
# ========================================

set (RENDERDOC_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/renderdoc")
set (RENDERDOC_SOURCE "${RENDERDOC_FOLDER}/renderdoc_app.h")
set (RENDERDOC_TARGET "renderdoc-api")

add_library (${RENDERDOC_TARGET} INTERFACE ${RENDERDOC_SOURCE})
set_target_properties (${RENDERDOC_TARGET} PROPERTIES FOLDER "ThirdParty")
source_group (TREE "${RENDERDOC_FOLDER}" FILES ${RENDERDOC_SOURCE})
target_include_directories (${RENDERDOC_TARGET} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})


# ! toml++ interface.
# ========================================

set (TOMLPLUSPLUS_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus")
set (TOMLPLUSPLUS_SOURCE "${TOMLPLUSPLUS_FOLDER}/toml.hpp")
set (TOMLPLUSPLUS_TARGET "tomlplusplus")

add_library (${TOMLPLUSPLUS_TARGET} INTERFACE ${TOMLPLUSPLUS_SOURCE})
set_target_properties (${TOMLPLUSPLUS_TARGET} PROPERTIES FOLDER "ThirdParty")
source_group (TREE "${TOMLPLUSPLUS_FOLDER}" FILES ${TOMLPLUSPLUS_SOURCE})
target_include_directories (${TOMLPLUSPLUS_TARGET} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})


# ! Zydis (dis)assembler.
# ========================================

set (ZYDIS_BUILD_SHARED_LIB OFF)

add_subdirectory ("zydis")

set_target_properties (Zycore PROPERTIES FOLDER "ThirdParty")
set_target_properties (Zydis PROPERTIES FOLDER "ThirdParty")
