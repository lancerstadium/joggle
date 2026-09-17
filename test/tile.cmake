include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("tile test requires TOOL, CC, models, harnesses, module roots, and ROOT" VARS TOOL CC MODEL FORM_MODEL CUSTOM_MODEL HARNESS FUSE_MODEL FUSE_HARNESS INVALID_FUSE_MODEL EFFECT_FUSE_MODEL KEEP_MODEL KEEP_HARNESS RELU_MODEL RELU_HARNESS MULTI_MODEL MULTI_HARNESS MERGE_MODEL MERGE_HARNESS MODULES EXTENSIONS ROOT)

joggle_run("custom affine safety failed"
  COMMAND "${TOOL}" run tile_pass.check_custom_affine "${CUSTOM_MODEL}"
          -M "${MODULES}"
  OUTPUT_VARIABLE custom_output
  ERROR_VARIABLE custom_error)

joggle_run("axis dependence query failed"
  COMMAND "${TOOL}" run tile_pass.check_axes "${MODEL}"
          -M "${MODULES}"
  OUTPUT_VARIABLE axes_output
  ERROR_VARIABLE axes_error)

joggle_run("linear access-form query failed"
  COMMAND "${TOOL}" run tile_pass.check_forms "${FORM_MODEL}"
          -M "${MODULES}"
  OUTPUT_VARIABLE forms_output
  ERROR_VARIABLE forms_error)

joggle_run("multidimensional reorder failed"
  COMMAND "${TOOL}" run tile_pass.check_multidimensional_reorder
          "${FORM_MODEL}" -M "${MODULES}"
  OUTPUT_VARIABLE multidimensional_output
  ERROR_VARIABLE multidimensional_error)

joggle_run("split candidate enumeration failed"
  COMMAND "${TOOL}" run tile_pass.check_splittable "${MODEL}"
          --arg 2 --arg 3 -M "${MODULES}"
  OUTPUT_VARIABLE candidate_output
  ERROR_VARIABLE candidate_error)

joggle_run("split issue query failed"
  COMMAND "${TOOL}" run tile_pass.check_split_issue "${MODEL}"
          -M "${MODULES}"
  OUTPUT_VARIABLE issue_output
  ERROR_VARIABLE issue_error)

joggle_run("factor-one split changed the model"
  COMMAND "${TOOL}" run tile_pass.check_split_noop "${MODEL}"
          -M "${MODULES}"
  OUTPUT_VARIABLE split_output
  ERROR_VARIABLE split_error)

execute_process(
  COMMAND "${TOOL}" run tile_pass.first "${MODEL}"
          --arg 0 -M "${MODULES}"
  RESULT_VARIABLE split_result
  OUTPUT_VARIABLE split_output
  ERROR_VARIABLE split_error
)
if(split_result EQUAL 0 OR
   NOT split_error MATCHES "requires a positive factor")
  message(FATAL_ERROR
          "invalid split was not rejected:\n${split_output}${split_error}")
endif()

joggle_run("unroll candidate enumeration failed"
  COMMAND "${TOOL}" run tile_pass.check_unrollable "${MODEL}"
          --arg 2 --arg 1 -M "${MODULES}"
  OUTPUT_VARIABLE candidate_output
  ERROR_VARIABLE candidate_error)

joggle_run("unroll issue query failed"
  COMMAND "${TOOL}" run tile_pass.check_unroll_issue "${MODEL}"
          -M "${MODULES}"
  OUTPUT_VARIABLE issue_output
  ERROR_VARIABLE issue_error)

execute_process(
  COMMAND "${TOOL}" run tile_pass.fuse_first "${EFFECT_FUSE_MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE effect_result
  OUTPUT_VARIABLE effect_output
  ERROR_VARIABLE effect_error
)
if(effect_result EQUAL 0 OR
   NOT effect_error MATCHES "non-setup operation")
  message(FATAL_ERROR
          "fusion crossed an observable call:\n${effect_output}${effect_error}")
endif()

joggle_run("fusion issue query failed"
  COMMAND "${TOOL}" run tile_pass.check_fuse_issue
          "${INVALID_FUSE_MODEL}"
          --arg "\"requires pointwise consumer reads\"" -M "${MODULES}"
  OUTPUT_VARIABLE issue_output
  ERROR_VARIABLE issue_error)

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

execute_process(
  COMMAND "${TOOL}" run tile_pass.reject_mutating_unroll_policy "${MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE mutating_unroll_result
  OUTPUT_VARIABLE mutating_unroll_output
  ERROR_VARIABLE mutating_unroll_error
)
if(mutating_unroll_result EQUAL 0 OR
   NOT mutating_unroll_error MATCHES "policy changed the module")
  message(FATAL_ERROR
          "mutating unroll policy was not rejected:\n"
          "${mutating_unroll_output}${mutating_unroll_error}")
endif()

joggle_workspace("${ROOT}")

set(merged_raw "${ROOT}/merged-raw.jog")
set(merged_ir "${ROOT}/merged.jog")
set(merged_source "${ROOT}/merged.c")
set(merged_program "${ROOT}/merged")
joggle_run("adjacent-axis merge failed"
  COMMAND "${TOOL}" run tile_pass.check_mergeable tile_pass.merge_first
          "${MERGE_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${merged_raw}"
  ERROR_VARIABLE error)
joggle_run("merged C preparation failed"
  COMMAND "${TOOL}" run c.prepare "${merged_raw}" -M "${MODULES}"
  OUTPUT_FILE "${merged_ir}"
  ERROR_VARIABLE error)
joggle_run("re-reading merged IR failed"
  COMMAND "${TOOL}" check "${merged_ir}" -M "${MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
file(READ "${merged_ir}" merged_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" merged_loops
       "${merged_text}")
list(LENGTH merged_loops merged_loop_count)
if(NOT merged_loop_count EQUAL 2 OR
   NOT merged_text MATCHES "for merge_[0-9]+ in" OR
   NOT merged_text MATCHES "for i in [^\n]+, j in" OR
   NOT merged_text MATCHES "merge_inner_offset_[0-9]+" OR
   NOT merged_text MATCHES "x\[merge_[0-9]+\]" OR
   NOT merged_text MATCHES "%")
  message(FATAL_ERROR
          "axis merge did not expose one reconstructed loop:\n${merged_text}")
endif()
joggle_run("merged C emission failed"
  COMMAND "${TOOL}" emit c.source "${merged_ir}" -M "${MODULES}"
  OUTPUT_FILE "${merged_source}"
  ERROR_VARIABLE error)
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${merged_source}" "${MERGE_HARNESS}" -o "${merged_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${merged_source}" emitted)
  message(FATAL_ERROR
          "merged C did not compile (${result}):\n"
          "${output}${error}\n${emitted}")
endif()
joggle_run("merged C returned the wrong result"
  COMMAND "${merged_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

set(multi_ir "${ROOT}/multi.jog")
set(multi_source "${ROOT}/multi.c")
set(multi_program "${ROOT}/multi")
joggle_run("multi-axis fusion failed"
  COMMAND "${TOOL}" run tile.scalarize tile.fuse c.prepare "${MULTI_MODEL}"
          -M "${MODULES}"
  OUTPUT_FILE "${multi_ir}"
  ERROR_VARIABLE error)
joggle_run("re-reading multi-axis fused IR failed"
  COMMAND "${TOOL}" check "${multi_ir}" -M "${MODULES}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
file(READ "${multi_ir}" multi_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" multi_loops
       "${multi_text}")
list(LENGTH multi_loops multi_loop_count)
if(NOT multi_loop_count EQUAL 3 OR multi_text MATCHES "var first" OR
   multi_text MATCHES "first\\[" OR multi_text MATCHES "var sum" OR
   multi_text MATCHES "sum\\[")
  message(FATAL_ERROR
          "multi-axis fusion retained its intermediate:\n${multi_text}")
endif()
joggle_run("multi-axis C emission failed"
  COMMAND "${TOOL}" emit c.source "${multi_ir}" -M "${MODULES}"
  OUTPUT_FILE "${multi_source}"
  ERROR_VARIABLE error)
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${multi_source}" "${MULTI_HARNESS}" -lm -o "${multi_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${multi_source}" emitted)
  message(FATAL_ERROR
          "multi-axis fused C did not compile (${result}):\n"
          "${output}${error}\n${emitted}")
endif()
joggle_run("multi-axis fused C returned the wrong result"
  COMMAND "${multi_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

set(tiled_sum "${ROOT}/tiled-sum.jog")
set(tiled "${ROOT}/tiled.jog")
set(axis_tiled "${ROOT}/axis-tiled.jog")
set(unrolled_grid "${ROOT}/unrolled-grid.jog")
set(policy_unrolled "${ROOT}/policy-unrolled.jog")
set(prepared "${ROOT}/prepared.jog")
set(source "${ROOT}/model.c")
set(program "${ROOT}/model")

joggle_run("loop tiling failed"
  COMMAND "${TOOL}" run tile_pass.first "${MODEL}"
          --arg 4 -M "${MODULES}"
  OUTPUT_FILE "${tiled_sum}"
  ERROR_VARIABLE error)

joggle_run("multi-axis loop tiling failed"
  COMMAND "${TOOL}" run tile_pass.named "${tiled_sum}"
          --arg "\"grid\"" --arg 3 -M "${MODULES}"
  OUTPUT_FILE "${tiled}"
  ERROR_VARIABLE error)

file(READ "${tiled}" text)
if(NOT text MATCHES "for i_tile" OR
   NOT text MATCHES "column_tile" OR
   NOT text MATCHES "if tile_inside_")
  message(FATAL_ERROR "loop tiling omitted its tiled structure:\n${text}")
endif()

joggle_run("first-axis loop tiling failed"
  COMMAND "${TOOL}" run tile_pass.named_axis "${tiled}"
          --arg "\"grid\"" --arg 0 --arg 2 -M "${MODULES}"
  OUTPUT_FILE "${axis_tiled}"
  ERROR_VARIABLE error)
file(READ "${axis_tiled}" axis_tiled_text)
if(NOT axis_tiled_text MATCHES
   "for row_tile in [^\n]+, row in [^\n]+, column_tile in")
  message(FATAL_ERROR
          "first-axis tiling changed or hid the original loop order:\n"
          "${axis_tiled_text}")
endif()

joggle_run("multi-axis loop unrolling failed"
  COMMAND "${TOOL}" run tile_pass.unroll_named "${axis_tiled}"
          --arg "\"fixed_grid\"" --arg 2 -M "${MODULES}"
  OUTPUT_FILE "${unrolled_grid}"
  ERROR_VARIABLE error)
file(READ "${unrolled_grid}" unrolled_grid_text)
if(NOT unrolled_grid_text MATCHES "unroll_blocks_[0-9]+: index = 2" OR
   NOT unrolled_grid_text MATCHES "unroll_offset_[0-9]+: index = 1")
  message(FATAL_ERROR
          "multi-axis loop unrolling omitted its points:\n${unrolled_grid_text}")
endif()

joggle_run("policy-selected unrolling failed"
  COMMAND "${TOOL}" run tile_pass.unroll_small "${unrolled_grid}"
          --arg 3 -M "${MODULES}"
  OUTPUT_FILE "${policy_unrolled}"
  ERROR_VARIABLE error)
file(READ "${policy_unrolled}" policy_unrolled_text)
if(NOT policy_unrolled_text MATCHES "unroll_offset_[0-9]+: index = 2")
  message(FATAL_ERROR
          "policy-selected unrolling omitted a three-point loop:\n"
          "${policy_unrolled_text}")
endif()

joggle_run("tiled C preparation failed"
  COMMAND "${TOOL}" run c.prepare "${policy_unrolled}" -M "${MODULES}"
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error)

joggle_run("tiled C emission failed"
  COMMAND "${TOOL}" emit c.source "${prepared}" -M "${MODULES}"
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error)

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

joggle_run("tiled C returned the wrong result"
  COMMAND "${program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

set(fuse_prepared "${ROOT}/fuse-prepared.jog")
set(fused "${ROOT}/fused.jog")
set(rejected_fusion "${ROOT}/rejected-fusion.jog")
set(policy_fusion "${ROOT}/policy-fusion.jog")
set(fused_ready "${ROOT}/fused-ready.jog")
set(fused_source "${ROOT}/fused.c")
set(fused_program "${ROOT}/fused")
set(unrolled "${ROOT}/unrolled.jog")
set(unrolled_source "${ROOT}/unrolled.c")
set(unrolled_program "${ROOT}/unrolled")

joggle_run("fusion preparation failed"
  COMMAND "${TOOL}" run c.prepare "${FUSE_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${fuse_prepared}"
  ERROR_VARIABLE error)

joggle_run("fusion candidate enumeration failed"
  COMMAND "${TOOL}" run tile_pass.check_fusible "${fuse_prepared}"
          --arg 2 -M "${MODULES}"
  OUTPUT_VARIABLE candidate_output
  ERROR_VARIABLE candidate_error)

joggle_run("loop unrolling failed"
  COMMAND "${TOOL}" run tile_pass.unroll_first "${fuse_prepared}"
          --arg 2 -M "${MODULES}"
  OUTPUT_FILE "${unrolled}"
  ERROR_VARIABLE error)
file(READ "${unrolled}" unrolled_text)
if(NOT unrolled_text MATCHES "unroll_blocks_[0-9]+: index = 2" OR
   NOT unrolled_text MATCHES "unroll_offset_[0-9]+: index = 1" OR
   unrolled_text MATCHES "var size = 4")
  message(FATAL_ERROR
          "loop unrolling retained the old range or omitted a point:\n${unrolled_text}")
endif()

joggle_run("unrolled C emission failed"
  COMMAND "${TOOL}" emit c.source "${unrolled}" -M "${MODULES}"
  OUTPUT_FILE "${unrolled_source}"
  ERROR_VARIABLE error)
joggle_run("unrolled C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${unrolled_source}" "${FUSE_HARNESS}" -o "${unrolled_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("unrolled C returned the wrong result"
  COMMAND "${unrolled_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

execute_process(
  COMMAND "${TOOL}" run tile_pass.unroll_first "${fuse_prepared}"
          --arg 3 -M "${MODULES}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "divisible by the factor")
  message(FATAL_ERROR
          "non-divisible unrolling was not rejected (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.reject_mutating_policy "${fuse_prepared}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error MATCHES "policy must not mutate the module")
  message(FATAL_ERROR
          "mutating fusion policy was not rejected (${result}):\n${error}")
endif()

joggle_run("fusion policy rejection failed"
  COMMAND "${TOOL}" run cost.fuse "${fuse_prepared}"
          --arg 0 --arg 100 -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${rejected_fusion}"
  ERROR_VARIABLE error)
file(READ "${rejected_fusion}" rejected_fusion_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" rejected_loops
       "${rejected_fusion_text}")
list(LENGTH rejected_loops rejected_loop_count)
if(NOT rejected_loop_count EQUAL 3)
  message(FATAL_ERROR
          "rejected fusion policy changed the loop chain:\n${rejected_fusion_text}")
endif()

joggle_run("configured fusion policy failed"
  COMMAND "${TOOL}" run cost.fuse "${fuse_prepared}"
          --arg 100 --arg 100 -M "${EXTENSIONS}" -M "${MODULES}"
  OUTPUT_FILE "${policy_fusion}"
  ERROR_VARIABLE error)
file(READ "${policy_fusion}" policy_fusion_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" policy_loops
       "${policy_fusion_text}")
list(LENGTH policy_loops policy_loop_count)
if(NOT policy_loop_count EQUAL 1)
  message(FATAL_ERROR
          "configured fusion policy did not collapse the chain:\n${policy_fusion_text}")
endif()

joggle_run("pointwise loop fusion failed"
  COMMAND "${TOOL}" run tile_pass.can_first tile_pass.permitted
          "${fuse_prepared}"
          -M "${MODULES}"
  OUTPUT_FILE "${fused}"
  ERROR_VARIABLE error)

file(READ "${fused}" fused_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" fused_loops
       "${fused_text}")
list(LENGTH fused_loops fused_loop_count)
if(NOT fused_loop_count EQUAL 1)
  message(FATAL_ERROR
          "fusion did not collapse the pointwise chain:\n${fused_text}")
endif()

joggle_run("fused C preparation failed"
  COMMAND "${TOOL}" run c.prepare "${fused}" -M "${MODULES}"
  OUTPUT_FILE "${fused_ready}"
  ERROR_VARIABLE error)
file(READ "${fused_ready}" fused_ready_text)
if(fused_ready_text MATCHES "var first" OR
   fused_ready_text MATCHES "first\\[")
  message(FATAL_ERROR
          "fusion retained its private intermediate:\n${fused_ready_text}")
endif()

joggle_run("fused C emission failed"
  COMMAND "${TOOL}" emit c.source "${fused_ready}" -M "${MODULES}"
  OUTPUT_FILE "${fused_source}"
  ERROR_VARIABLE error)
file(READ "${fused_source}" fused_source_text)
if(fused_source_text MATCHES "first\\[")
  message(FATAL_ERROR
          "fused C retained its private intermediate:\n${fused_source_text}")
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

joggle_run("fused C returned the wrong result"
  COMMAND "${fused_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

set(keep_ir "${ROOT}/keep.jog")
set(keep_source "${ROOT}/keep.c")
set(keep_program "${ROOT}/keep")
joggle_run("retained-result fusion failed"
  COMMAND "${TOOL}" run c.prepare tile_pass.fuse_first c.prepare
          "${KEEP_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${keep_ir}"
  ERROR_VARIABLE error)
file(READ "${keep_ir}" keep_text)
if(NOT keep_text MATCHES "return first, second" OR
   NOT keep_text MATCHES "first\\[i\\] =" OR
   keep_text MATCHES "second\\[i\\] = first\\[i\\]")
  message(FATAL_ERROR
          "fusion did not retain and forward the live producer:\n${keep_text}")
endif()
joggle_run("retained-result C emission failed"
  COMMAND "${TOOL}" emit c.source "${keep_ir}" -M "${MODULES}"
  OUTPUT_FILE "${keep_source}"
  ERROR_VARIABLE error)
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${keep_source}" "${KEEP_HARNESS}" -lm -o "${keep_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  file(READ "${keep_source}" emitted)
  message(FATAL_ERROR
          "retained-result C did not compile (${result}):\n${output}${error}\n${emitted}")
endif()
joggle_run("retained-result C was incorrect"
  COMMAND "${keep_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)

set(relu_ir "${ROOT}/relu.jog")
set(relu_source "${ROOT}/relu.c")
set(relu_program "${ROOT}/relu")
joggle_run("activation fusion failed"
  COMMAND "${TOOL}" run c.prepare tile_pass.all c.prepare
          "${RELU_MODEL}" -M "${MODULES}"
  OUTPUT_FILE "${relu_ir}"
  ERROR_VARIABLE error)
file(READ "${relu_ir}" relu_text)
if(relu_text MATCHES "var sum" OR relu_text MATCHES "sum\\[" OR
   relu_text MATCHES "var normalized" OR
   relu_text MATCHES "normalized\\[" OR
   relu_text MATCHES "var size_1" OR
   NOT relu_text MATCHES "let fuse_value_")
  message(FATAL_ERROR
          "activation fusion retained its sum tensor:\n${relu_text}")
endif()
joggle_run("activation C emission failed"
  COMMAND "${TOOL}" emit c.source "${relu_ir}" -M "${MODULES}"
  OUTPUT_FILE "${relu_source}"
  ERROR_VARIABLE error)
file(READ "${relu_source}" relu_source_text)
if(relu_source_text MATCHES "sum\\[" OR
   relu_source_text MATCHES "normalized\\[" OR
   relu_source_text MATCHES "size_1" OR
   NOT relu_source_text MATCHES "float fuse_value_")
  message(FATAL_ERROR
          "activation C retained its sum tensor:\n${relu_source_text}")
endif()
joggle_run("activation C did not compile"
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${relu_source}" "${RELU_HARNESS}" -lm -o "${relu_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
joggle_run("activation C was incorrect"
  COMMAND "${relu_program}"
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
