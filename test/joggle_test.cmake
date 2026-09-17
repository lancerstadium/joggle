# Shared helpers for the script-driven tests.
#
# Every test script previously repeated the same three idioms by hand: a guard
# listing its required -D variables, an execute_process followed by a result
# check, and an ad-hoc regex assertion on an emitted file. Those spellings
# drifted apart, so failures reported inconsistent context. These helpers give
# one spelling with one diagnostic format.
#
# Include with:
#   include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

include_guard(GLOBAL)

# joggle_require(<label> VARS <name>...)
#
# Fails unless every named variable is defined, naming the absent ones rather
# than restating the whole contract.
function(joggle_require label)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "VARS")
  if(NOT arg_VARS)
    message(FATAL_ERROR "joggle_require(${label}) needs VARS")
  endif()
  set(missing "")
  foreach(name IN LISTS arg_VARS)
    if(NOT DEFINED ${name})
      list(APPEND missing "${name}")
    endif()
  endforeach()
  if(missing)
    string(REPLACE ";" ", " required "${arg_VARS}")
    string(REPLACE ";" ", " absent "${missing}")
    message(FATAL_ERROR "${label} requires ${required}\nmissing: ${absent}")
  endif()
endfunction()

# joggle_workspace(<directory>)
#
# Replaces a stale working tree with an empty one.
function(joggle_workspace directory)
  if(NOT directory)
    message(FATAL_ERROR "joggle_workspace needs a directory")
  endif()
  file(REMOVE_RECURSE "${directory}")
  file(MAKE_DIRECTORY "${directory}")
endfunction()

# joggle_run(<label>
#            COMMAND <argv>...
#            [OUTPUT_FILE <path>] [OUTPUT_VARIABLE <name>]
#            [ERROR_VARIABLE <name>] [WORKING_DIRECTORY <dir>]
#            [EXPECT_FAIL] [INPUT_FILE <path>])
#
# Runs one command and turns a non-zero status into a labelled failure that
# carries the child's stderr. With EXPECT_FAIL the polarity is inverted, so a
# rejection test states what it expects instead of hand-writing the inverse
# comparison.
function(joggle_run label)
  # PARSE_ARGV, not ${ARGN}: assertion patterns and emitted C contain
  # semicolons, and ${ARGN} would re-split them into separate arguments.
  cmake_parse_arguments(
    PARSE_ARGV 1 arg "EXPECT_FAIL"
    "OUTPUT_FILE;OUTPUT_VARIABLE;ERROR_VARIABLE;WORKING_DIRECTORY;INPUT_FILE"
    "COMMAND")
  if(NOT arg_COMMAND)
    message(FATAL_ERROR "joggle_run(${label}) needs COMMAND")
  endif()

  set(redirect "")
  if(arg_OUTPUT_FILE)
    list(APPEND redirect OUTPUT_FILE "${arg_OUTPUT_FILE}")
  else()
    list(APPEND redirect OUTPUT_VARIABLE captured)
  endif()
  if(arg_WORKING_DIRECTORY)
    list(APPEND redirect WORKING_DIRECTORY "${arg_WORKING_DIRECTORY}")
  endif()
  if(arg_INPUT_FILE)
    list(APPEND redirect INPUT_FILE "${arg_INPUT_FILE}")
  endif()

  execute_process(
    COMMAND ${arg_COMMAND}
    RESULT_VARIABLE result
    ERROR_VARIABLE failure
    ${redirect}
  )

  string(REPLACE ";" " " shown "${arg_COMMAND}")
  if(arg_EXPECT_FAIL)
    if(result EQUAL 0)
      message(FATAL_ERROR "${label} unexpectedly succeeded\ncommand: ${shown}")
    endif()
  elseif(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} failed (${result})\ncommand: ${shown}\n${failure}")
  endif()

  if(arg_OUTPUT_VARIABLE)
    set(${arg_OUTPUT_VARIABLE} "${captured}" PARENT_SCOPE)
  endif()
  if(arg_ERROR_VARIABLE)
    set(${arg_ERROR_VARIABLE} "${failure}" PARENT_SCOPE)
  endif()
endfunction()

# joggle_expect(<label>
#               {FILE <path> | TEXT <string>}
#               [MATCHES <regex>] [NOT_MATCHES <regex>])
#
# Asserts on emitted text. On failure the offending regex is named and the
# subject is echoed, which the hand-written assertions did inconsistently.
#
# One pattern per call, deliberately. A multi-value keyword would hold the
# patterns as a CMake list, and a list cannot carry a regex containing a
# bracket expression such as `[^]]+`: CMake merges it with the following
# element and the assertion then tests a regex nobody wrote. One pattern per
# call also gives every assertion its own label in the failure message.
function(joggle_expect label)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "FILE;TEXT;MATCHES;NOT_MATCHES" "")
  if(DEFINED arg_FILE AND DEFINED arg_TEXT)
    message(FATAL_ERROR "joggle_expect(${label}) takes FILE or TEXT, not both")
  endif()
  if(DEFINED arg_FILE)
    if(NOT EXISTS "${arg_FILE}")
      message(FATAL_ERROR "${label}: ${arg_FILE} does not exist")
    endif()
    file(READ "${arg_FILE}" subject)
    set(origin "${arg_FILE}")
  elseif(DEFINED arg_TEXT)
    set(subject "${arg_TEXT}")
    set(origin "text")
  else()
    message(FATAL_ERROR "joggle_expect(${label}) needs FILE or TEXT")
  endif()

  if(NOT DEFINED arg_MATCHES AND NOT DEFINED arg_NOT_MATCHES)
    message(FATAL_ERROR "joggle_expect(${label}) needs MATCHES or NOT_MATCHES")
  endif()
  if(DEFINED arg_MATCHES AND NOT subject MATCHES "${arg_MATCHES}")
    message(FATAL_ERROR
            "${label}: ${origin} does not match\n  ${arg_MATCHES}\n${subject}")
  endif()
  if(DEFINED arg_NOT_MATCHES AND subject MATCHES "${arg_NOT_MATCHES}")
    message(FATAL_ERROR
            "${label}: ${origin} unexpectedly matches\n  ${arg_NOT_MATCHES}\n${subject}")
  endif()
endfunction()

# joggle_expect_same(<label> <left> <right>)
#
# Asserts byte equality, the common check for idempotence and for a rebuild
# reproducing a recorded artifact.
function(joggle_expect_same label left right)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${left}" "${right}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label}: ${left} and ${right} differ")
  endif()
endfunction()

# joggle_expect_different(<label> <left> <right>)
function(joggle_expect_different label left right)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${left}" "${right}"
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_QUIET
  )
  if(result EQUAL 0)
    message(FATAL_ERROR "${label}: ${left} and ${right} are identical")
  endif()
endfunction()
