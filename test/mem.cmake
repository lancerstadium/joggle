include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("memory planning test" VARS TOOL CC MODEL HARNESS MODULES ROOT)
joggle_workspace("${ROOT}")

set(prepared "${ROOT}/prepared.jog")
set(planned "${ROOT}/planned.jog")
set(planned_again "${ROOT}/planned-again.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

joggle_run("C preparation"
  COMMAND "${TOOL}" run c.prepare "${MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}")

joggle_run("memory planning"
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${planned}")

joggle_run("buffer count query"
  COMMAND "${TOOL}" query mem.buffers "${planned}" -M "${MODULES}"
  OUTPUT_VARIABLE buffers)
joggle_expect("memory planning used the wrong buffer count"
  TEXT "${buffers}" MATCHES "^2\n$")

joggle_run("structural summary query"
  COMMAND "${TOOL}" query stat.summary "${planned}" -M "${MODULES}"
  OUTPUT_VARIABLE summary)
joggle_expect("planned summary lost its slot count"
  TEXT "${summary}" MATCHES "\"mem_slots\": 2")
joggle_expect("planned summary lost its element count"
  TEXT "${summary}" MATCHES "\"mem_elems\": 8")

joggle_run("repeated memory planning"
  COMMAND "${TOOL}" run mem.plan "${planned}" -M "${MODULES}"
  OUTPUT_FILE "${planned_again}")
joggle_expect_same("memory planning is not idempotent" "${planned}" "${planned_again}")

joggle_run("planned C emission"
  COMMAND "${TOOL}" emit c.source "${planned}" -M "${MODULES}"
  OUTPUT_FILE "${source}")

file(READ "${source}" emitted)
string(REGEX MATCHALL "float slot_f32_[0-9]+" declared "${emitted}")
list(LENGTH declared declared_count)
if(NOT declared_count EQUAL 2)
  message(FATAL_ERROR "planned C did not declare exactly two buffers:\n${emitted}")
endif()
string(REGEX MATCHALL "= \\(\\(float\\)\\(0\\)\\);" zero_fills "${emitted}")
list(LENGTH zero_fills zero_fill_count)
if(NOT zero_fill_count EQUAL 0)
  message(FATAL_ERROR "planned C retained dead tensor fills:\n${emitted}")
endif()

joggle_expect("planned C did not alias its result buffer"
  FILE "${source}" MATCHES "float\\* third = third_out;")
joggle_expect("planned C did not compute into its result"
  FILE "${source}" MATCHES "third\\[[^]]+\\] = \\(")
joggle_expect("planned C copied its final tensor instead of writing it"
  FILE "${source}" NOT_MATCHES "third_out\\[[^]]+\\] = slot_f32_")
joggle_expect("planned C removed a live tensor fill"
  FILE "${source}" MATCHES "= \\(\\(float\\)\\(7\\)\\);")

joggle_run("planned C compilation"
  COMMAND "${CC}" -std=c99 -Wall -Wextra -Wstrict-prototypes -Werror
          "${source}" "${HARNESS}" -o "${program}")

joggle_run("planned C execution" COMMAND "${program}")
