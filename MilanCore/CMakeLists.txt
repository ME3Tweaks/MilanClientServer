# File:   MilanCore/CMakeLists.txt
# Brief:  Definition of the Milan Client Core interface target
# ========================================


set (PROJ_TARGET MilanCore)

set (FFI_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/milan_ecsm_ffi.h)
set (FFI_CRATE client_ffi)


# Ensure an empty ffi header updated by cbindgen later during build.

file (TOUCH ${FFI_HEADER})


# Import Rust crate as a CMake target and link it.

corrosion_import_crate (
  MANIFEST_PATH "${CMAKE_SOURCE_DIR}/Cargo.toml"
  CRATE_TYPES "staticlib"
  CRATES "${FFI_CRATE}"
)

corrosion_add_target_local_rustflags ("${FFI_CRATE}" "-Cpanic=abort")
corrosion_set_cargo_flags ("${FFI_CRATE}" "--verbose")

add_library (${PROJ_TARGET} INTERFACE ${FFI_HEADER})

target_compile_definitions (${PROJ_TARGET} INTERFACE ${MILAN_COMPILE_DEFINITIONS})
target_include_directories (${PROJ_TARGET} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (${PROJ_TARGET} INTERFACE ${FFI_CRATE})


# Group auto-generated cargo targets.
# We should keep Solution Explorer clean :)

get_property (CARGO_TARGETS
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  PROPERTY BUILDSYSTEM_TARGETS
)

foreach (MY_CARGO_TARGET ${CARGO_TARGETS})
  if (MY_CARGO_TARGET MATCHES "cargo-")
    message ("-- Grouping auto-generated '${MY_CARGO_TARGET}' target")
    set_target_properties (${MY_CARGO_TARGET} PROPERTIES FOLDER "Cargo")
  endif ()
endforeach ()
