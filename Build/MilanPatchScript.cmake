# Based on https://github.com/scivision/cmake-patch-file.
# >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>


# Input variables:
#   ${original} = full path to the source file
#   ${patched} = full path to the transient patched file
#   ${patch} = full path to the patch diff
#   ${cwd} = intended workind directory


# Enable execute_process(..) calls to log to stdout.
set (CMAKE_EXECUTE_PROCESS_COMMAND_ECHO STDOUT)

# Import git.exe location.
find_package (Git REQUIRED)


# Copy diff and source to the transient patched tree.
# These copies rely on current working directory being set correctly.
cmake_path (GET patch FILENAME patch_transient)
# execute_process (
#   COMMAND powershell "Get-Content '${patch}' | Out-File '${patch_transient}' -Encoding utf8"
#   WORKING_DIRECTORY ${cwd}
# )
execute_process (
  COMMAND powershell "$InPatchContents = Get-Content -Raw '${patch}'; \
    $Encoding = New-Object System.Text.UTF8Encoding $False; \
    [System.IO.File]::WriteAllLines(\"${patch_transient}\", $InPatchContents, $Encoding)"
  RESULT_VARIABLE copy_result
  ERROR_VARIABLE copy_error
  WORKING_DIRECTORY ${cwd}
)
configure_file ("${original}" "${patched}" COPYONLY)

if (NOT copy_result EQUAL 0)
  message (FATAL_ERROR "!! Failed to copy/transcode .diff: ${copy_error}")
endif ()


# Try to apply the patch.
execute_process (
  COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace --verbose "${patch_transient}"
  WORKING_DIRECTORY ${cwd}
  RESULT_VARIABLE first_result
  ERROR_VARIABLE first_error
  TIMEOUT 2
)

if (NOT first_result EQUAL 0)
  # Check if the patch was already applied.
  execute_process (
    COMMAND ${GIT_EXECUTABLE} apply --ignore-whitespace --check --reverse --verbose "${patch_transient}"
    WORKING_DIRECTORY ${cwd}
    RESULT_VARIABLE second_result
    ERROR_VARIABLE second_error
    TIMEOUT 2
  )

  if (NOT second_result EQUAL 0)
    message (FATAL_ERROR "\n
!! Failed to apply patch ${patch_transient}: \n
    git apply: result = ${first_result}, error = ${first_error} \n
    git apply --check --reverse: result = ${second_result}, error = ${second_error} \n
    Paths: \n
      Original = ${original} \n
      Patched = ${patched} \n
      Patch = ${patch} \n
")
  endif ()
endif ()
