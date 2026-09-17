include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("memory safety test requires TOOL, CC, MODEL, HARNESS, MODULES, ROOT" VARS TOOL CC MODEL HARNESS MODULES ROOT)

if(NOT DEFINED PLANNER)
  set(PLANNER mem.plan)
endif()
set(module_args)
foreach(directory IN LISTS MODULES)
  list(APPEND module_args -M "${directory}")
endforeach()
file(MAKE_DIRECTORY "${ROOT}")
set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
execute_process(
  COMMAND "${TOOL}" run c.prepare "${MODEL}" ${module_args}
  RESULT_VARIABLE result OUTPUT_FILE "${prepared}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "memory safety preparation failed: ${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run "${PLANNER}" "${prepared}" ${module_args}
  RESULT_VARIABLE result OUTPUT_FILE "${planned}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "memory safety planning failed: ${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run "${PLANNER}" "${planned}" ${module_args}
  RESULT_VARIABLE result OUTPUT_VARIABLE repeated ERROR_VARIABLE error
)
file(READ "${planned}" expected)
if(NOT result EQUAL 0 OR NOT repeated STREQUAL expected)
  message(FATAL_ERROR "memory safety plan is not idempotent: ${error}")
endif()

# Check the same oracle before and after planning.  An IR verifier cannot
# detect storage aliasing introduced in generated C.
foreach(mode prepared planned)
  set(source "${ROOT}/${mode}.c")
  set(program "${ROOT}/${mode}")
  execute_process(
    COMMAND "${TOOL}" emit c.source "${${mode}}" ${module_args}
    RESULT_VARIABLE result OUTPUT_FILE "${source}" ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${mode} C emission failed: ${error}")
  endif()
  execute_process(
    COMMAND "${CC}" -std=c99 -Wall -Wextra -Werror -Wstrict-prototypes
            "${source}" "${HARNESS}" -o "${program}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${mode} C compilation failed: ${output}${error}")
  endif()
  execute_process(
    COMMAND "${program}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${mode} memory safety oracle failed: ${output}${error}")
  endif()
endforeach()
