include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("locality extension requires TOOL, CC, MODEL, HARNESS, module roots, and ROOT" VARS TOOL CC MODEL HARNESS MODULES EXTENSIONS ROOT)

joggle_workspace("${ROOT}")
set(canonical "${ROOT}/canonical.jog")
set(bounded "${ROOT}/bounded.jog")
set(bounded_source "${ROOT}/bounded.c")
set(bounded_program "${ROOT}/bounded")
set(canon "${ROOT}/canon.jog")
set(canon_stable "${ROOT}/canon-stable.jog")
set(canon_guarded "${ROOT}/canon-guarded.jog")
set(canon_source "${ROOT}/canon.c")
set(canon_program "${ROOT}/canon")
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
set(split_scalar_source "${ROOT}/split-scalar.c")
set(split_scalar_program "${ROOT}/split-scalar")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")
set(plan "${ROOT}/plan.attr")

joggle_run("canonical preparation failed"
  COMMAND "${TOOL}" run c.prepare mem.plan "${MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${canonical}"
  ERROR_VARIABLE error)
joggle_run("guarded affine index canonicalization failed"
  COMMAND "${TOOL}" run tile.canon "${canonical}" -M "${MODULES}"
  OUTPUT_FILE "${canon_guarded}"
  ERROR_VARIABLE error)
file(READ "${canon_guarded}" canon_guarded_text)
if(NOT canon_guarded_text MATCHES "var xi(_[A-Za-z0-9]+)* =")
  message(FATAL_ERROR
          "affine index canonicalization erased a guard-shared tree:\n"
          "${canon_guarded_text}")
endif()
joggle_run("locality plan query failed"
  COMMAND "${TOOL}" query locality.plan "${canonical}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${plan}"
  ERROR_VARIABLE error)
file(READ "${plan}" plan_text)
string(FIND "${plan_text}" "\"candidates\": 3" candidates_at)
string(FIND "${plan_text}"
       "\"current\": [\"n\", \"m\", \"oh\", \"ow\", \"q\", \"r\", \"s\"]"
       current_at)
string(FIND "${plan_text}"
       "\"selected\": [\"n\", \"m\", \"oh\", \"q\", \"r\", \"s\", \"ow\"]"
       selected_at)
string(FIND "${plan_text}" "\"reads\": [" reads_at)
string(FIND "${plan_text}" "\"writes\": [" writes_at)
string(FIND "${plan_text}" "\"extents\": [" extents_at)
string(FIND "${plan_text}" "\"scalar_cost\":" scalar_cost_at)
if(candidates_at EQUAL -1 OR current_at EQUAL -1 OR selected_at EQUAL -1 OR
   reads_at EQUAL -1 OR writes_at EQUAL -1 OR extents_at EQUAL -1 OR
   scalar_cost_at EQUAL -1)
  message(FATAL_ERROR "locality plan omitted its evidence:\n${plan_text}")
endif()
joggle_run("reorder contract failed"
  COMMAND "${TOOL}" run tile_pass.check_reorder "${canonical}"
          -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("scalar tail safety failed"
  COMMAND "${TOOL}" run tile_pass.check_scalarize_tail "${canonical}"
          -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("loop peel contract failed"
  COMMAND "${TOOL}" run tile_pass.check_peel "${canonical}"
          -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("Conv bounds folding failed"
  COMMAND "${TOOL}" run bounds.fold opt.fold opt.basic "${canonical}"
          -M "${MODULES}"
  OUTPUT_FILE "${bounded}"
  ERROR_VARIABLE error)
file(READ "${bounded}" bounded_text)
if(bounded_text MATCHES "if h(_[A-Za-z0-9]+)* >=" OR
   NOT bounded_text MATCHES "if value < f32\\(0\\)")
  message(FATAL_ERROR
          "Conv bounds folding changed the wrong branches:\n${bounded_text}")
endif()
joggle_run("bounded C emission failed"
  COMMAND "${TOOL}" emit c.source "${bounded}" -M "${MODULES}"
  OUTPUT_FILE "${bounded_source}"
  ERROR_VARIABLE error)
joggle_run("bounded C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${bounded_source}" "${HARNESS}" -lm -o "${bounded_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("bounded C returned the wrong result"
  COMMAND "${bounded_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("affine index canonicalization failed"
  COMMAND "${TOOL}" run tile.canon "${bounded}" -M "${MODULES}"
  OUTPUT_FILE "${canon}"
  ERROR_VARIABLE error)
joggle_run("repeated affine index canonicalization failed"
  COMMAND "${TOOL}" run tile.canon "${canon}" -M "${MODULES}"
  OUTPUT_FILE "${canon_stable}"
  ERROR_VARIABLE error)
joggle_run("affine index canonicalization is not idempotent"
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${canon}" "${canon_stable}")
file(READ "${canon}" canon_text)
if(canon_text MATCHES "var (xi|wi|yi)(_[A-Za-z0-9]+)* =" OR
   NOT canon_text MATCHES "out_1\\[int\\(n\\) \\* 4")
  message(FATAL_ERROR
          "affine index canonicalization retained a stride chain:\n${canon_text}")
endif()
joggle_run("canonical-index C emission failed"
  COMMAND "${TOOL}" emit c.source "${canon}" -M "${MODULES}"
  OUTPUT_FILE "${canon_source}"
  ERROR_VARIABLE error)
joggle_run("canonical-index C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${canon_source}" "${HARNESS}" -lm -o "${canon_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("canonical-index C returned the wrong result"
  COMMAND "${canon_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("scalar promotion contract failed"
  COMMAND "${TOOL}" run tile_pass.check_scalarize "${canonical}"
          -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("composed Conv improvement failed"
  COMMAND "${TOOL}" run tile.scalarize bounds.fold opt.fold opt.basic
          "${canonical}"
          -M "${MODULES}"
  OUTPUT_FILE "${scalarized}"
  ERROR_VARIABLE error)
joggle_run("scalar promotion structure check failed"
  COMMAND "${TOOL}" run tile_pass.check_scalarized "${scalarized}"
          -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("repeated scalar promotion failed"
  COMMAND "${TOOL}" run tile.scalarize "${scalarized}" -M "${MODULES}"
  OUTPUT_FILE "${scalarized_stable}"
  ERROR_VARIABLE error)
joggle_run("scalar promotion is not idempotent"
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${scalarized}" "${scalarized_stable}")
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
joggle_run("scalarized C emission failed"
  COMMAND "${TOOL}" emit c.source "${scalarized}" -M "${MODULES}"
  OUTPUT_FILE "${scalarized_source}"
  ERROR_VARIABLE error)
joggle_run("scalarized C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${scalarized_source}" "${HARNESS}" -lm -o "${scalarized_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("scalarized C returned the wrong result"
  COMMAND "${scalarized_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("split/reorder/scalarize composition failed"
  COMMAND "${TOOL}" run locality.block "${canonical}"
          --arg 2 -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${split_scalar_raw}"
  ERROR_VARIABLE error)
joggle_run("bounded block composition failed"
  COMMAND "${TOOL}" run locality.block "${canonical}"
          --arg 2 --arg 1000000 -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${split_scalar_bounded}"
  ERROR_VARIABLE error)
joggle_run("a loose block cost limit changed the policy"
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${split_scalar_raw}" "${split_scalar_bounded}")
joggle_run("small block cost limit failed"
  COMMAND "${TOOL}" run locality.block "${canonical}"
          --arg 2 --arg 1 -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${split_scalar_skipped}"
  ERROR_VARIABLE error)
joggle_run("a rejected block candidate changed the module"
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${canonical}" "${split_scalar_skipped}")
joggle_run("split scalar cleanup failed"
  COMMAND "${TOOL}" run bounds.fold opt.fold opt.basic
          "${split_scalar_raw}" -M "${MODULES}"
  OUTPUT_FILE "${split_scalar}"
  ERROR_VARIABLE error)
joggle_run("factor-list block composition failed"
  COMMAND "${TOOL}" run locality.block "${canonical}"
          --arg "[2, 3]" -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${split_scalar_list}"
  ERROR_VARIABLE error)
joggle_run("factor-list block cleanup failed"
  COMMAND "${TOOL}" run bounds.fold opt.fold opt.basic
          "${split_scalar_list}" -M "${MODULES}"
  OUTPUT_FILE "${split_scalar_list}.clean"
  ERROR_VARIABLE error)
joggle_run("factor-list policy disagrees with its first legal factor"
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${split_scalar}" "${split_scalar_list}.clean")
execute_process(
  COMMAND "${TOOL}" run locality.block "${canonical}"
          --arg "[2, 1]" -M "${EXTENSIONS}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_QUIET
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR
   NOT error MATCHES "locality.block requires factors greater than one")
  message(FATAL_ERROR
          "factor-list policy accepted an invalid trailing factor: ${error}")
endif()
joggle_run("split scalar structure check failed"
  COMMAND "${TOOL}" run tile_pass.check_split_scalarized "${split_scalar}"
          --arg 4 --arg 2 -M "${MODULES}"
  ERROR_VARIABLE error)
file(READ "${split_scalar}" split_scalar_text)
if(NOT split_scalar_text MATCHES "var acc_0 =" OR
   NOT split_scalar_text MATCHES "var acc_1 =" OR
   split_scalar_text MATCHES "tile_inside_")
  message(FATAL_ERROR
          "split scalar pipeline retained the wrong lane structure:\n"
          "${split_scalar_text}")
endif()
joggle_run("split scalar C emission failed"
  COMMAND "${TOOL}" emit c.source "${split_scalar}" -M "${MODULES}"
  OUTPUT_FILE "${split_scalar_source}"
  ERROR_VARIABLE error)
joggle_run("split scalar C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${split_scalar_source}" "${HARNESS}" -lm
          -o "${split_scalar_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("split scalar C returned the wrong result"
  COMMAND "${split_scalar_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
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
joggle_run("configured reorder failed"
  COMMAND "${TOOL}" run tile_pass.reorder_configured "${canonical}"
          --arg true -M "${MODULES}"
  OUTPUT_FILE "${configured}"
  ERROR_VARIABLE error)
joggle_run("disabled reorder failed"
  COMMAND "${TOOL}" run tile_pass.reorder_configured "${canonical}"
          --arg false -M "${MODULES}"
  OUTPUT_FILE "${disabled}"
  ERROR_VARIABLE error)
joggle_run("disabled reorder changed the module"
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${canonical}" "${disabled}")
joggle_run("locality scheduling failed"
  COMMAND "${TOOL}" run locality.apply "${canonical}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error)
joggle_run("repeated locality scheduling failed"
  COMMAND "${TOOL}" run locality.apply "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${stable}"
  ERROR_VARIABLE error)
joggle_run("locality scheduling is not idempotent"
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${prepared}" "${stable}")
file(READ "${prepared}" text)
if(text MATCHES "spatial.nn.conv2d" OR text MATCHES "use locality")
  message(FATAL_ERROR
          "locality scheduling retained an implementation override:\n${text}")
endif()
if(NOT text MATCHES
   "for n in [^\n]+, m in [^\n]+, oh in [^\n]+, q in [^\n]+, r in [^\n]+, s in [^\n]+, ow in")
  message(FATAL_ERROR "locality policy omitted its loop order:\n${text}")
endif()
joggle_run("spatial axis dependence check failed"
  COMMAND "${TOOL}" run tile_pass.check_spatial_axes "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("tiled scalar promotion contract failed"
  COMMAND "${TOOL}" run tile_pass.check_spatial_scalarize "${prepared}"
          --arg 2 -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("tiled scalar promotion failed"
  COMMAND "${TOOL}" run tile_pass.scalarize_budget "${prepared}"
          --arg 2 -M "${MODULES}"
  OUTPUT_FILE "${tiled_scalar_raw}"
  ERROR_VARIABLE error)
joggle_run("tiled scalar cleanup failed"
  COMMAND "${TOOL}" run opt.basic "${tiled_scalar_raw}" -M "${MODULES}"
  OUTPUT_FILE "${tiled_scalar}"
  ERROR_VARIABLE error)
joggle_run("tiled scalar structure check failed"
  COMMAND "${TOOL}" run tile_pass.check_spatial_scalarized "${tiled_scalar}"
          --arg 2 -M "${MODULES}"
  ERROR_VARIABLE error)
joggle_run("repeated tiled scalar promotion failed"
  COMMAND "${TOOL}" run tile_pass.scalarize_budget "${tiled_scalar}"
          --arg 2 -M "${MODULES}"
  OUTPUT_FILE "${tiled_scalar_stable}"
  ERROR_VARIABLE error)
joggle_run("tiled scalar promotion is not idempotent"
  COMMAND "${CMAKE_COMMAND}" -E compare_files
          "${tiled_scalar}" "${tiled_scalar_stable}")
file(READ "${tiled_scalar}" tiled_scalar_text)
if(NOT tiled_scalar_text MATCHES "var acc_0 =" OR
   NOT tiled_scalar_text MATCHES "var acc_1 =" OR
   tiled_scalar_text MATCHES "spatial\.nn\.conv2d|edge\.nn\.conv2d")
  message(FATAL_ERROR
          "tiled scalar promotion did not expose two accumulators:\n"
          "${tiled_scalar_text}")
endif()
joggle_run("tiled scalar C emission failed"
  COMMAND "${TOOL}" emit c.source "${tiled_scalar}" -M "${MODULES}"
  OUTPUT_FILE "${tiled_scalar_source}"
  ERROR_VARIABLE error)
joggle_run("tiled scalar C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${tiled_scalar_source}" "${HARNESS}" -lm
          -o "${tiled_scalar_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("tiled scalar C returned the wrong result"
  COMMAND "${tiled_scalar_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("spatial C emission failed"
  COMMAND "${TOOL}" emit c.source "${prepared}"
          -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error)
file(READ "${source}" text)
if(NOT text MATCHES
   "void model_main\\(const float\\* x, const float\\* weight, float\\* [A-Za-z_][A-Za-z0-9_]*\\)")
  message(FATAL_ERROR
          "spatial C did not preserve the public function and value names:\n${text}")
endif()
if(text MATCHES "(^|[^A-Za-z0-9_])(joggle_|jog_|v_[A-Za-z0-9])")
  message(FATAL_ERROR "spatial C introduced an opaque generated prefix:\n${text}")
endif()
joggle_run("spatial C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${source}" "${HARNESS}" -lm -o "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("spatial C returned the wrong result"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
