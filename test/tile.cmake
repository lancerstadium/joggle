if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED FUSE_MODEL OR
   NOT DEFINED FUSE_HARNESS OR NOT DEFINED INVALID_FUSE_MODEL OR
   NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "tile test requires TOOL, CC, both models and harnesses, MODULES, and ROOT")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.fuse_first "${INVALID_FUSE_MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE invalid_result
  OUTPUT_VARIABLE invalid_output
  ERROR_VARIABLE invalid_error
)
if(invalid_result EQUAL 0 OR
   NOT invalid_error MATCHES "pointwise consumer reads")
  message(FATAL_ERROR
          "unsafe shifted fusion was not rejected:\n${invalid_output}${invalid_error}")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

set(tiled_sum "${ROOT}/tiled-sum.jog")
set(tiled "${ROOT}/tiled.jog")
set(prepared "${ROOT}/prepared.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run tile_pass.first "${MODEL}"
          --arg 4 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${tiled_sum}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "loop tiling failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.named "${tiled_sum}"
          --arg "\"grid\"" --arg 3 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${tiled}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "multi-axis loop tiling failed (${result}):\n${error}")
endif()

file(READ "${tiled}" text)
if(NOT text MATCHES "for i_tile" OR
   NOT text MATCHES "column_tile" OR
   NOT text MATCHES "if tile_inside_")
  message(FATAL_ERROR "loop tiling omitted its tiled structure:\n${text}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare "${tiled}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "tiled C preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "tiled C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${source}" "${HARNESS}" -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${source}" emitted)
  message(FATAL_ERROR
          "tiled C did not compile (${result}):\n${output}${error}\n${emitted}")
endif()

execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled C returned the wrong result (${result}):\n${output}${error}")
endif()

set(fuse_prepared "${ROOT}/fuse-prepared.jog")
set(fused "${ROOT}/fused.jog")
set(fused_ready "${ROOT}/fused-ready.jog")
set(fused_source "${ROOT}/fused.c")
set(fused_program "${ROOT}/fused")

execute_process(
  COMMAND "${TOOL}" run c.prepare "${FUSE_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${fuse_prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "fusion preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.fuse_first "${fuse_prepared}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${fused}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "pointwise loop fusion failed (${result}):\n${error}")
endif()

file(READ "${fused}" fused_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" fused_loops
       "${fused_text}")
list(LENGTH fused_loops fused_loop_count)
if(NOT fused_loop_count EQUAL 2)
  message(FATAL_ERROR
          "fusion did not reduce three pointwise loops to two:\n${fused_text}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare "${fused}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${fused_ready}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "fused C preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${fused_ready}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${fused_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "fused C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${fused_source}" "${FUSE_HARNESS}" -lm -o "${fused_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${fused_source}" emitted)
  message(FATAL_ERROR
          "fused C did not compile (${result}):\n${output}${error}\n${emitted}")
endif()

execute_process(
  COMMAND "${fused_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "fused C returned the wrong result (${result}):\n${output}${error}")
endif()
