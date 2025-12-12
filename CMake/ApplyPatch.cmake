# CMake script to apply a patch if it's not already applied
# Usage: cmake -P ApplyPatch.cmake <target_file> <patch_file> <marker_string>
#
# This script checks if the patch is already applied by looking for a marker string,
# and if not, applies it using git apply.

if(CMAKE_ARGC LESS 4)
    message(FATAL_ERROR "ApplyPatch.cmake requires 3 arguments: target_file, patch_file, marker_string")
endif()

set(TARGET_FILE "${CMAKE_ARGV3}")
set(PATCH_FILE "${CMAKE_ARGV4}")
set(MARKER_STRING "${CMAKE_ARGV5}")

# Check if files exist
if(NOT EXISTS "${TARGET_FILE}")
    message(STATUS "Target file does not exist: ${TARGET_FILE}")
    return()
endif()

if(NOT EXISTS "${PATCH_FILE}")
    message(WARNING "Patch file does not exist: ${PATCH_FILE}")
    return()
endif()

# Check if patch is already applied
file(READ "${TARGET_FILE}" FILE_CONTENT)
if(FILE_CONTENT MATCHES "${MARKER_STRING}")
    message(STATUS "Patch already applied to ${TARGET_FILE}")
    return()
endif()

# Find git
find_program(GIT_EXECUTABLE git)
if(NOT GIT_EXECUTABLE)
    message(WARNING "git not found. Cannot apply patch to ${TARGET_FILE}")
    return()
endif()

# Get the source directory (repo root)
# The patch file is at vs2022-uwp/uwp/patches/*.patch, so go up 3 levels
get_filename_component(PATCH_DIR "${PATCH_FILE}" DIRECTORY)
get_filename_component(PATCH_PARENT "${PATCH_DIR}" DIRECTORY)
get_filename_component(PATCH_PARENT2 "${PATCH_PARENT}" DIRECTORY)
get_filename_component(REPO_ROOT "${PATCH_PARENT2}" DIRECTORY)

# Apply the patch
message(STATUS "Applying patch to ${TARGET_FILE}...")
execute_process(
    COMMAND ${GIT_EXECUTABLE} apply -p1 --ignore-whitespace "${PATCH_FILE}"
    WORKING_DIRECTORY "${REPO_ROOT}"
    RESULT_VARIABLE GIT_APPLY_RESULT
    OUTPUT_VARIABLE GIT_OUTPUT
    ERROR_VARIABLE GIT_ERROR
)

if(NOT GIT_APPLY_RESULT EQUAL 0)
    message(WARNING "Failed to apply patch to ${TARGET_FILE}: ${GIT_ERROR}")
else()
    message(STATUS "Successfully applied patch to ${TARGET_FILE}")
endif()

