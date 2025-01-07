# File:   Build/MilanPatchHooks.cmake
# Brief:  Some key logic for sdk patcher.
# >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


# Property to preserve configuration time on initialization.
set_property (GLOBAL PROPERTY MLN_CONFIGURE_TIMESTAMP)


# ! Creates files which will be patched at build-time.
# ========================================

function (mln_patching_init)

  # Fetch current timestamp.
  string (TIMESTAMP PATCHING_TIMESTAMP "%Y%m%d%H%M%S")
  set (GLOBAL PROPERTY MLN_CONFIGURE_TIMESTAMP "${PATCHING_TIMESTAMP}")
  message ("-- Patching timestamp: ${PATCHING_TIMESTAMP}")

  set (PATCHING_INDEX_PATH "${CMAKE_SOURCE_DIR}/Patching/_index.list")

  message ("-- Parsing patch index list (${PATCHING_INDEX_PATH}):")
  file (STRINGS ${PATCHING_INDEX_PATH} PATCHING_INDEX_LIST)

  # Parse patch manifest.
  foreach (PATCHING_LINE IN LISTS PATCHING_INDEX_LIST)
    if (PATCHING_LINE MATCHES "^([^:]+):([^:]+):([^:]+):([^:]+)$")
      set (PATCHING_PATCHED_MODULE "${CMAKE_SOURCE_DIR}/Build/Patched/${CMAKE_MATCH_2}/_submodule.name")
      set (PATCHING_PATCHED_FILE "${CMAKE_SOURCE_DIR}/Build/Patched/${CMAKE_MATCH_2}/${CMAKE_MATCH_3}")

      message ("-- Registering patch: submodule = ${CMAKE_MATCH_1}, library = ${CMAKE_MATCH_2}, file = ${CMAKE_MATCH_3}, diff = ${CMAKE_MATCH_4}")
      message ("--   Ensuring file for build-time patching: ${PATCHING_PATCHED_FILE}")

      if (NOT EXISTS ${PATCHING_PATCHED_FILE})
        file (WRITE ${PATCHING_PATCHED_FILE} "")
      endif ()

      if (NOT EXISTS ${PATCHING_PATCHED_MODULE})
        file (WRITE ${PATCHING_PATCHED_MODULE} "${CMAKE_MATCH_1}")
      endif ()

    else ()
      message (FATAL_ERROR "!! Invalid patch index line: ${PATCHING_LINE}")
    endif ()
  endforeach ()

endfunction ()


# ! Overrides `add_library` function to inject patched paths.
# ========================================

function (add_library target)

  # We need this to populate passed source files, which are left unparsed.
  cmake_parse_arguments (MLN_AL
    "INTERFACE;MODULE;OBJECT;SHARED;STATIC;EXCLUDE_FROM_ALL;GLOBAL;IMPORTED"
    ""
    ""
    ${ARGN}
  )

  set (IS_PATCH_CANDIDATE OFF)
  set (MLN_AL_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/ThirdParty")

  # Only consider the simplest types, without any imported targets, ...
  if ((MLN_AL_SHARED OR MLN_AL_STATIC) AND NOT MLN_AL_GLOBAL AND NOT MLN_AL_IMPORTED)
    # ... for targets within third party dependency folder ...
    string (FIND ${CMAKE_CURRENT_SOURCE_DIR} ${MLN_AL_THIRDPARTY_DIR} MLN_AL_THIRDPARTY_POS)
    if (NOT MLN_AL_THIRDPARTY_POS EQUAL "-1")
      # ... and only if the target declares sources.
      if (MLN_AL_UNPARSED_ARGUMENTS)
        set (IS_PATCH_CANDIDATE ON)
      endif ()
    endif ()
  endif ()

  if (IS_PATCH_CANDIDATE)

    message (">> ADDING LIBRARY ${ARGV0} IN ${CMAKE_CURRENT_SOURCE_DIR}:")

    set (MLN_AL_ORIGINAL_SOURCES)
    set (MLN_AL_PATCHED_SOURCES)

    foreach (MLN_AL_SOURCE IN LISTS MLN_AL_UNPARSED_ARGUMENTS)
      string (LENGTH ${CMAKE_CURRENT_SOURCE_DIR} MLN_AL_DIRECTORY_LENGTH)
      string (SUBSTRING ${MLN_AL_SOURCE} ${MLN_AL_DIRECTORY_LENGTH} -1 MLN_AL_SOURCE_LOCAL)
      set (MLN_AL_PATCHED_SOURCE "${CMAKE_SOURCE_DIR}/Build/Patched/${ARGV0}/${MLN_AL_SOURCE_LOCAL}")

      # This path will exist only if current source file needs to be patched.
      if (EXISTS ${MLN_AL_PATCHED_SOURCE})

        # If the patched file is empty, fill it in with original source.
        file (SIZE ${MLN_AL_PATCHED_SOURCE} MLN_AL_PATCHED_SIZE)
        if (MLN_AL_PATCHED_SIZE EQUAL 0)
          message (">>   Writing initial contents for ${MLN_AL_SOURCE_LOCAL} (${ARGV0}).")
          file (COPY_FILE ${MLN_AL_SOURCE} ${MLN_AL_PATCHED_SOURCE})
        else ()
          message ("--   Skipped writing initial contents for ${MLN_AL_SOURCE_LOCAL} (${ARGV0}): file not empty.")
        endif ()

        list (APPEND MLN_AL_PATCHED_SOURCES ${MLN_AL_PATCHED_SOURCE})

      else ()

        list (APPEND MLN_AL_ORIGINAL_SOURCES ${MLN_AL_SOURCE})

      endif ()
    endforeach ()

    # We're assuming two things here: that first argument is always the target name,
    # and that the second one is always the library type. Works for Milan's use case, but it's nowhere
    # good enough for a general-purpose solution.

    _add_library (${ARGV0} ${ARGV1} ${MLN_AL_ORIGINAL_SOURCES} ${MLN_AL_PATCHED_SOURCES})
    source_group ("[Patched files]" FILES ${MLN_AL_PATCHED_SOURCES})

  else ()

    # Pass all arguments unmodified.
    _add_library (${ARGV})

  endif ()

endfunction ()


# ! Overrides `target_include_directories` function to inject patched paths.
# ========================================

function (target_include_directories)

  if (EXISTS "${CMAKE_SOURCE_DIR}/Build/Patched/${ARGV0}")
    _target_include_directories (${ARGV0} ${ARGV1} "${CMAKE_SOURCE_DIR}/Build/Patched/${ARGV0}")
  endif ()

  _target_include_directories (${ARGV})

endfunction ()
