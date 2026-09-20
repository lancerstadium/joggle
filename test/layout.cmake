# Enforce the repository boundaries documented in README.md.

if(NOT DEFINED SOURCE)
  message(FATAL_ERROR "layout.cmake requires -DSOURCE=<repository root>")
endif()

set(failures "")

# The project no longer carries the old research workspace. The separate paper
# directory currently contains only the outline manuscript.
if(EXISTS "${SOURCE}/research")
  list(APPEND failures "retired research workspace exists: research/")
endif()
if(NOT EXISTS "${SOURCE}/paper/README.md")
  list(APPEND failures "missing paper outline: paper/README.md")
endif()

execute_process(
  COMMAND git ls-files
  WORKING_DIRECTORY "${SOURCE}"
  OUTPUT_VARIABLE tracked
  ERROR_QUIET
  RESULT_VARIABLE git_result
)
if(git_result EQUAL 0)
  string(REPLACE "\n" ";" tracked_lines "${tracked}")
  foreach(path IN LISTS tracked_lines)
    if(path MATCHES "^build[^/]*/")
      list(APPEND failures "tracked generated path: ${path}")
    endif()
  endforeach()
endif()

if(failures)
  list(REMOVE_ITEM failures "")
  foreach(failure IN LISTS failures)
    message("  [FAIL] ${failure}")
  endforeach()
  list(LENGTH failures count)
  message(FATAL_ERROR "${count} repository layout violation(s)")
endif()

message("repository layout: ok")
