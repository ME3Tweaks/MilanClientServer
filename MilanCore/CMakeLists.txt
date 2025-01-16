# File:   MilanCore/CMakeLists.txt
# Brief:  Definition of the Milan Client Core interface target
# ========================================


set (PROJ_TARGET MilanCore)

set (FFI_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/milan_ecsm_ffi.h)
set (FFI_OUTPUT ${CMAKE_SOURCE_DIR}/Build/Cargo)
set (FFI_CRATE client_ffi)
set (FFI_BUILD cargo build -p ${FFI_CRATE} $<$<IN_LIST:$<CONFIG>,RELEASE;FINAL>:--release>)


# Ensure an empty ffi header updated by cbindgen later during build.

file (TOUCH ${FFI_HEADER})


# Import Rust crate as a CMake target and link it.

add_custom_target (${FFI_CRATE} ALL
  COMMAND cargo build -p ${FFI_CRATE}
    "$<$<IN_LIST:$<CONFIG>,RELEASE;FINAL>:--release>"
  COMMAND_EXPAND_LISTS
  USES_TERMINAL
  VERBATIM
)

add_library (${PROJ_TARGET} INTERFACE ${FFI_HEADER})

target_compile_definitions (${PROJ_TARGET} INTERFACE ${MILAN_COMPILE_DEFINITIONS})
target_include_directories (${PROJ_TARGET} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_dependencies (${PROJ_TARGET} ${FFI_CRATE})
target_link_libraries (${PROJ_TARGET} INTERFACE
  debug "${FFI_OUTPUT}/debug/${FFI_CRATE}.lib"
  optimized "${FFI_OUTPUT}/release/${FFI_CRATE}.lib"
  general ntdll ws2_32 userenv advapi32  # needed for rust stdlib
)
set_target_properties (${FFI_CRATE} PROPERTIES FOLDER "[CARGO]")
