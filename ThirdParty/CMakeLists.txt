# File:   ThirdParty/CMakeLists.txt
# Brief:  Definitions for pulling in dependency targets
# ========================================

# We treat ImGui and ImPlot as in-source targets although they are vendored.
# Their targets need to be defined in this file because neither of them
# assume build system, and neither of them provide CMakeLists.txt


# ! ImGui target.
# ========================================

set (IMGUI_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/imgui-1.90.5")
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

target_compile_definitions (${IMGUI_TARGET} PUBLIC ${MILAN_COMPILE_DEFINITIONS})
target_compile_options (${IMGUI_TARGET} PUBLIC ${MILAN_COMPILE_OPTIONS})
target_include_directories (${IMGUI_TARGET} PUBLIC ${IMGUI_FOLDER})
target_link_options (${IMGUI_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})


# ! ImPlot target.
# ========================================

set (IMPLOT_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/implot")
set (IMPLOT_TARGET "ImPlot")

set (IMPLOT_SOURCES
  ${IMPLOT_FOLDER}/implot_demo.cpp
  ${IMPLOT_FOLDER}/implot_internal.h
  ${IMPLOT_FOLDER}/implot_items.cpp
  ${IMPLOT_FOLDER}/implot.cpp
  ${IMPLOT_FOLDER}/implot.h
)

add_library (${IMPLOT_TARGET} STATIC ${IMPLOT_SOURCES})
set_target_properties (${IMPLOT_TARGET} PROPERTIES FOLDER "ThirdParty")
source_group (TREE "${IMPLOT_FOLDER}" FILES ${IMPLOT_SOURCES})

target_compile_definitions (${IMPLOT_TARGET} PUBLIC ${MILAN_COMPILE_DEFINITIONS})
target_compile_options (${IMPLOT_TARGET} PUBLIC ${MILAN_COMPILE_OPTIONS})
target_include_directories (${IMPLOT_TARGET} PUBLIC ${IMPLOT_FOLDER})
target_link_options (${IMPLOT_TARGET} PUBLIC ${MILAN_LINK_OPTIONS})
target_link_libraries (${IMPLOT_TARGET} PUBLIC ${IMGUI_TARGET})


# ! LESDK-LE3 target.
# ========================================

set (LESDK_WITH_LE1 OFF)
set (LESDK_WITH_LE2 OFF)
set (LESDK_WITH_LE3 ON)
set (LESDK_WITH_SAMPLES OFF)
set (LESDK_WITH_TESTS OFF)

add_subdirectory ("LExSDKv2")
set_target_properties (LESDK-LE3 PROPERTIES FOLDER "ThirdParty")
