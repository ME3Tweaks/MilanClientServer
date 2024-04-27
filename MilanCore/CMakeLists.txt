# File:   MilanCore/CMakeLists.txt
# Brief:  Definition of the Milan Client Core interface target
# ========================================


corrosion_import_crate (
  MANIFEST_PATH "${CMAKE_SOURCE_DIR}/Cargo.toml"
  CRATE_TYPES "staticlib"
  CRATES "client_ffi"
)

add_library (MilanCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/milan_ecsm_ffi.h)

target_compile_definitions (MilanCore INTERFACE ${MILAN_COMPILE_DEFINITIONS})
target_include_directories (MilanCore INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries (MilanCore INTERFACE client_ffi)


# Group auto-generated cargo targets.
# We should keep Solution Explorer clean :)

get_property (CARGO_TARGETS
  DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  PROPERTY BUILDSYSTEM_TARGETS
)

foreach (MY_CARGO_TARGET ${CARGO_TARGETS})
  if (MY_CARGO_TARGET MATCHES "cargo-")
    message ("!! Grouping auto-generated '${MY_CARGO_TARGET}' target")
    set_target_properties (${MY_CARGO_TARGET} PROPERTIES FOLDER "Cargo")
  endif ()
endforeach ()
