if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED MODULES OR NOT DEFINED EXAMPLES OR
   NOT DEFINED ROOT)
  message(FATAL_ERROR
          "spatial example requires TOOL, CC, MODEL, HARNESS, module roots, ROOT")
endif()

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")
set(canonical "${ROOT}/canonical.jog")
set(bounded "${ROOT}/bounded.jog")
set(bounded_source "${ROOT}/bounded.c")
set(bounded_program "${ROOT}/bounded")
set(prepared "${ROOT}/prepared.jog")
set(configured "${ROOT}/configured.jog")
set(disabled "${ROOT}/disabled.jog")
set(stable "${ROOT}/stable.jog")
set(scalarized "${ROOT}/scalarized.jog")
set(scalarized_stable "${ROOT}/scalarized-stable.jog")
set(scalarized_source "${ROOT}/scalarized.c")
set(scalarized_program "${ROOT}/scalarized")
set(tiled_scalar_raw "${ROOT}/tiled-scalar-raw.jog")
set(tiled_scalar "${ROOT}/tiled-scalar.jog")
set(tiled_scalar_stable "${ROOT}/tiled-scalar-stable.jog")
set(tiled_scalar_source "${ROOT}/tiled-scalar.c")
set(tiled_scalar_program "${ROOT}/tiled-scalar")
set(split_scalar_raw "${ROOT}/split-scalar-raw.jog")
set(split_scalar "${ROOT}/split-scalar.jog")
set(split_scalar_list "${ROOT}/split-scalar-list.jog")
set(split_scalar_bounded "${ROOT}/split-scalar-bounded.jog")
set(split_scalar_skipped "${ROOT}/split-scalar-skipped.jog")
set(split_scalar_ranked "${ROOT}/split-scalar-ranked.jog")
set(split_scalar_source "${ROOT}/split-scalar.c")
set(split_scalar_program "${ROOT}/split-scalar")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

execute_process(
  COMMAND "${TOOL}" run c.prepare mem.plan "${MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${canonical}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "canonical preparation failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_reorder "${canonical}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "reorder contract failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_scalarize_tail "${canonical}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "scalar tail safety failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_peel "${canonical}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "loop peel contract failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run bounds.fold opt.fold opt.basic "${canonical}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${bounded}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "Conv bounds folding failed (${result}):\n${error}")
endif()
file(READ "${bounded}" bounded_text)
if(bounded_text MATCHES "if h(_[A-Za-z0-9]+)* >=" OR
   NOT bounded_text MATCHES "if value < f32\\(0\\)")
  message(FATAL_ERROR
          "Conv bounds folding changed the wrong branches:\n${bounded_text}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${bounded}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${bounded_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "bounded C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${bounded_source}" "${HARNESS}" -lm -o "${bounded_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "bounded C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${bounded_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "bounded C returned the wrong result (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_scalarize "${canonical}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "scalar promotion contract failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile.scalarize bounds.fold opt.fold opt.basic
          "${canonical}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${scalarized}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "composed Conv improvement failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_scalarized "${scalarized}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "scalar promotion structure check failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile.scalarize "${scalarized}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${scalarized_stable}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "repeated scalar promotion failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${scalarized}" "${scalarized_stable}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "scalar promotion is not idempotent")
endif()
file(READ "${scalarized}" scalarized_text)
if(NOT scalarized_text MATCHES "var acc =" OR
   scalarized_text MATCHES "spatial\.nn\.conv2d|edge\.nn\.conv2d")
  message(FATAL_ERROR
          "scalar promotion did not remain a structural pass:\n${scalarized_text}")
endif()
if(scalarized_text MATCHES "var (xi|wi)(_[A-Za-z0-9]+)* =")
  message(FATAL_ERROR
          "scalar promotion retained expanded affine address state:\n${scalarized_text}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${scalarized}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${scalarized_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "scalarized C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${scalarized_source}" "${HARNESS}" -lm -o "${scalarized_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "scalarized C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${scalarized_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "scalarized C returned the wrong result (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.block "${canonical}"
          --arg 2 -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar_raw}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "split/reorder/scalarize composition failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.block "${canonical}"
          --arg 2 --arg 1000000 -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar_bounded}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "bounded block composition failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${split_scalar_raw}" "${split_scalar_bounded}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "a loose block cost limit changed the policy")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.block "${canonical}"
          --arg 2 --arg 1 -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar_skipped}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "small block cost limit failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${canonical}" "${split_scalar_skipped}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "a rejected block candidate changed the module")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.block "${canonical}"
          --arg 2 --arg 80 -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar_ranked}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ranked block composition failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_ranked "${split_scalar_ranked}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "ranked block selection failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run bounds.fold opt.fold opt.basic
          "${split_scalar_raw}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "split scalar cleanup failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.block "${canonical}"
          --arg "[2, 3]" -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar_list}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "factor-list block composition failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run bounds.fold opt.fold opt.basic
          "${split_scalar_list}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar_list}.clean"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "factor-list block cleanup failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${split_scalar}" "${split_scalar_list}.clean"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "factor-list policy disagrees with its first legal factor")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.block "${canonical}"
          --arg "[2, 1]" -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR
   NOT error MATCHES "spatial.block requires factors greater than one")
  message(FATAL_ERROR
          "factor-list policy accepted an invalid trailing factor: ${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_split_scalarized "${split_scalar}"
          --arg 4 --arg 2 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "split scalar structure check failed (${result}):\n${error}")
endif()
file(READ "${split_scalar}" split_scalar_text)
if(NOT split_scalar_text MATCHES "var acc_0 =" OR
   NOT split_scalar_text MATCHES "var acc_1 =" OR
   split_scalar_text MATCHES "tile_inside_")
  message(FATAL_ERROR
          "split scalar pipeline retained the wrong lane structure:\n"
          "${split_scalar_text}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${split_scalar}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${split_scalar_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "split scalar C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${split_scalar_source}" "${HARNESS}" -lm
          -o "${split_scalar_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "split scalar C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${split_scalar_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "split scalar C returned the wrong result (${result}):\n"
          "${output}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.reject_mutating_reorder_policy
          "${canonical}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "policy changed the module")
  message(FATAL_ERROR
          "mutating reorder policy was not rejected (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.reorder_configured "${canonical}"
          --arg true -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${configured}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "configured reorder failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.reorder_configured "${canonical}"
          --arg false -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${disabled}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "disabled reorder failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${canonical}" "${disabled}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "disabled reorder changed the module")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.apply "${canonical}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spatial scheduling failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run spatial.apply "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${stable}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "repeated spatial scheduling failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${prepared}" "${stable}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spatial scheduling is not idempotent")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${configured}" "${prepared}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "configured and source-only reorder policies diverged")
endif()
file(READ "${prepared}" text)
if(text MATCHES "spatial.nn.conv2d" OR text MATCHES "use spatial")
  message(FATAL_ERROR
          "spatial scheduling retained an implementation override:\n${text}")
endif()
if(NOT text MATCHES
   "for n in [^\n]+, m in [^\n]+, q in [^\n]+, r in [^\n]+, s in [^\n]+, oh in [^\n]+, ow in")
  message(FATAL_ERROR "spatial pass omitted its loop order:\n${text}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_spatial_axes "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "spatial axis dependence check failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_spatial_scalarize "${prepared}"
          --arg 4 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled scalar promotion contract failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.scalarize_budget "${prepared}"
          --arg 4 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${tiled_scalar_raw}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled scalar promotion failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run opt.basic "${tiled_scalar_raw}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${tiled_scalar}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled scalar cleanup failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.check_spatial_scalarized "${tiled_scalar}"
          --arg 4 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled scalar structure check failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" run tile_pass.scalarize_budget "${tiled_scalar}"
          --arg 4 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${tiled_scalar_stable}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "repeated tiled scalar promotion failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${tiled_scalar}" "${tiled_scalar_stable}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "tiled scalar promotion is not idempotent")
endif()
file(READ "${tiled_scalar}" tiled_scalar_text)
if(NOT tiled_scalar_text MATCHES "var acc_0 =" OR
   NOT tiled_scalar_text MATCHES "var acc_3 =" OR
   tiled_scalar_text MATCHES "spatial\.nn\.conv2d|edge\.nn\.conv2d")
  message(FATAL_ERROR
          "tiled scalar promotion did not expose four accumulators:\n"
          "${tiled_scalar_text}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${tiled_scalar}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${tiled_scalar_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled scalar C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${tiled_scalar_source}" "${HARNESS}" -lm
          -o "${tiled_scalar_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled scalar C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${tiled_scalar_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "tiled scalar C returned the wrong result (${result}):\n"
          "${output}${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "spatial C emission failed (${result}):\n${error}")
endif()
file(READ "${source}" text)
if(NOT text MATCHES
   "void model_main\\(const float\\* x, const float\\* weight, float\\* [A-Za-z_][A-Za-z0-9_]*\\)")
  message(FATAL_ERROR
          "spatial C did not preserve the public function and value names:\n${text}")
endif()
if(text MATCHES "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])")
  message(FATAL_ERROR "spatial C introduced an opaque generated prefix:\n${text}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${source}" "${HARNESS}" -lm -o "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "spatial C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "spatial C returned the wrong result (${result}):\n${output}${error}")
endif()
