# File:   Build/MilanPatchHooks.cmake
# Brief:  Some key logic for sdk patcher.
# See:    Based on https://github.com/scivision/cmake-patch-file.
# >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


find_package (Git REQUIRED)
set (CMAKE_EXECUTE_PROCESS_COMMAND_ECHO STDOUT)


# Applies a single patch to a single submodule.
function (mln_patch_single__ original patch cwd module)
  set (target_directory ${cwd}\\ThirdParty\\${module})

  # Try to apply the patch.
  execute_process (
    COMMAND ${GIT_EXECUTABLE}
      apply --ignore-whitespace --unsafe-paths --verbose
        --directory=${target_directory} ${patch}
    WORKING_DIRECTORY ${cwd}
    RESULT_VARIABLE first_result
    ERROR_VARIABLE first_error
    TIMEOUT 2
  )

  if (NOT first_result EQUAL 0)
    # Check if the patch was already applied.
    execute_process (
      COMMAND ${GIT_EXECUTABLE}
        apply --ignore-whitespace --unsafe-paths --verbose --check --reverse
          --directory=${target_directory} ${patch}
      WORKING_DIRECTORY ${cwd}
      RESULT_VARIABLE second_result
      ERROR_VARIABLE second_error
      TIMEOUT 2
    )

    if (NOT second_result EQUAL 0)
      message (FATAL_ERROR "\n
!! Failed to apply patch ${patch}: \n
    git apply: result = ${first_result}, error = ${first_error} \n
    git apply --check --reverse: result = ${second_result}, error = ${second_error} \n
    Paths: \n
      Original = ${original} \n
      Patch = ${patch} \n
      Cwd = ${cwd} \n
")
    endif ()
  endif ()

endfunction ()


# Applies patches from the manifest to their submodules.
function (mln_patching_apply)

  set (PATCHING_INDEX_PATH "${CMAKE_SOURCE_DIR}/Patching/_index.list")
  file (STRINGS ${PATCHING_INDEX_PATH} PATCHING_INDEX_LIST)

  message ("-- Parsing patch index list (${PATCHING_INDEX_PATH}):")

  # Parse patch manifest and apply every entry.
  foreach (PATCHING_LINE IN LISTS PATCHING_INDEX_LIST)
    if (PATCHING_LINE MATCHES "^([^:]+):([^:]+):([^:]+):([^:]+)$")
      set (PATCHING_SUBMODULE_PATH "${CMAKE_SOURCE_DIR}/ThirdParty/${CMAKE_MATCH_1}")
      set (PATCHING_ORIGINAL_FILE "${CMAKE_SOURCE_DIR}/ThirdParty/${CMAKE_MATCH_1}/${CMAKE_MATCH_3}")
      set (PATCHING_PATCH_FILE "${CMAKE_SOURCE_DIR}/Patching/${CMAKE_MATCH_4}")

      message ("-- Applying patch: submodule = ${CMAKE_MATCH_1}, library = ${CMAKE_MATCH_2}, file = ${CMAKE_MATCH_3}, diff = ${CMAKE_MATCH_4}")
      message ("--   Target file path: ${PATCHING_ORIGINAL_FILE}")

      mln_patch_single__ (
        ${PATCHING_ORIGINAL_FILE}   # original
        ${PATCHING_PATCH_FILE}      # patch
        ${CMAKE_SOURCE_DIR}         # cwd
        ${CMAKE_MATCH_1}            # module
      )

    else ()
      message (FATAL_ERROR "!! Invalid patch index line: ${PATCHING_LINE}")
    endif ()
  endforeach ()

endfunction ()

# Runs "git restore ." for every submodule in the manifest.
function (mln_patching_cleanup)

  set (PATCHING_INDEX_PATH "${CMAKE_SOURCE_DIR}/Patching/_index.list")
  file (STRINGS ${PATCHING_INDEX_PATH} PATCHING_INDEX_LIST)

  message ("-- Parsing patch index list (${PATCHING_INDEX_PATH}):")
  foreach (PATCHING_LINE IN LISTS PATCHING_INDEX_LIST)
    if (PATCHING_LINE MATCHES "^([^:]+):([^:]+):([^:]+):([^:]+)$")

      set (PATCHING_SUBMODULE_PATH "${CMAKE_SOURCE_DIR}/ThirdParty/${CMAKE_MATCH_1}")
      execute_process (
        COMMAND ${GIT_EXECUTABLE} restore .
        WORKING_DIRECTORY ${PATCHING_SUBMODULE_PATH}
        RESULT_VARIABLE RESTORE_RESULT
        ERROR_VARIABLE RESTORE_ERROR
        TIMEOUT 2
      )
      if (NOT RESTORE_RESULT EQUAL 0)
        message (FATAL_ERROR "!! Failed to restore submodule ${CMAKE_MATCH_1} (${RESTORE_ERROR}): ${RESTORE_RESULT}")
      endif ()

    else ()
      message (FATAL_ERROR "!! Invalid patch index line: ${PATCHING_LINE}")
    endif ()
  endforeach ()

endfunction ()


# Sets up tasks to control patches en masse.
function (mln_patching_setup_tasks)

  add_custom_target (APPLY_PATCHES
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/Build/MilanPatchApply.cmake"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  add_custom_target (CLEANUP_PATCHES
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_SOURCE_DIR}/Build/MilanPatchCleanup.cmake"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

  set_target_properties (APPLY_PATCHES PROPERTIES FOLDER "[PATCHING]")
  set_target_properties (CLEANUP_PATCHES PROPERTIES FOLDER "[PATCHING]")

endfunction ()
