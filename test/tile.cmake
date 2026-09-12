if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "tile test requires TOOL, CC, MODEL, HARNESS, MODULES, and ROOT")
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
