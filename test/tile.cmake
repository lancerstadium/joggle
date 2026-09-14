if(NOT DEFINED TOOL OR NOT DEFINED CC OR NOT DEFINED MODEL OR
   NOT DEFINED FORM_MODEL OR NOT DEFINED CUSTOM_MODEL OR
   NOT DEFINED HARNESS OR NOT DEFINED FUSE_MODEL OR
   NOT DEFINED FUSE_HARNESS OR NOT DEFINED INVALID_FUSE_MODEL OR
   NOT DEFINED EFFECT_FUSE_MODEL OR
   NOT DEFINED KEEP_MODEL OR NOT DEFINED KEEP_HARNESS OR
   NOT DEFINED RELU_MODEL OR NOT DEFINED RELU_HARNESS OR
   NOT DEFINED MULTI_MODEL OR NOT DEFINED MULTI_HARNESS OR
   NOT DEFINED MODULES OR NOT DEFINED EXAMPLES OR NOT DEFINED ROOT)
  message(FATAL_ERROR
          "tile test requires TOOL, CC, models, harnesses, module roots, and ROOT")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_custom_affine "${CUSTOM_MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE custom_result
  OUTPUT_VARIABLE custom_output
  ERROR_VARIABLE custom_error
)
if(NOT custom_result EQUAL 0)
  message(FATAL_ERROR
          "custom affine safety failed:\n${custom_output}${custom_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_axes "${MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE axes_result
  OUTPUT_VARIABLE axes_output
  ERROR_VARIABLE axes_error
)
if(NOT axes_result EQUAL 0)
  message(FATAL_ERROR
          "axis dependence query failed:\n${axes_output}${axes_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_forms "${FORM_MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE forms_result
  OUTPUT_VARIABLE forms_output
  ERROR_VARIABLE forms_error
)
if(NOT forms_result EQUAL 0)
  message(FATAL_ERROR
          "linear access-form query failed:\n${forms_output}${forms_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_multidimensional_reorder
          "${FORM_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE multidimensional_result
  OUTPUT_VARIABLE multidimensional_output
  ERROR_VARIABLE multidimensional_error
)
if(NOT multidimensional_result EQUAL 0)
  message(FATAL_ERROR
          "multidimensional reorder failed:\n"
          "${multidimensional_output}${multidimensional_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_splittable "${MODEL}"
          --arg 2 --arg 3 -M "${MODULES}"
  RESULT_VARIABLE candidate_result
  OUTPUT_VARIABLE candidate_output
  ERROR_VARIABLE candidate_error
)
if(NOT candidate_result EQUAL 0)
  message(FATAL_ERROR
          "split candidate enumeration failed:\n"
          "${candidate_output}${candidate_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_split_issue "${MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE issue_result
  OUTPUT_VARIABLE issue_output
  ERROR_VARIABLE issue_error
)
if(NOT issue_result EQUAL 0)
  message(FATAL_ERROR
          "split issue query failed:\n${issue_output}${issue_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_split_noop "${MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE split_result
  OUTPUT_VARIABLE split_output
  ERROR_VARIABLE split_error
)
if(NOT split_result EQUAL 0)
  message(FATAL_ERROR
          "factor-one split changed the model:\n"
          "${split_output}${split_error}")
endif()

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

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_unrollable "${MODEL}"
          --arg 2 --arg 1 -M "${MODULES}"
  RESULT_VARIABLE candidate_result
  OUTPUT_VARIABLE candidate_output
  ERROR_VARIABLE candidate_error
)
if(NOT candidate_result EQUAL 0)
  message(FATAL_ERROR
          "unroll candidate enumeration failed:\n"
          "${candidate_output}${candidate_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_unroll_issue "${MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE issue_result
  OUTPUT_VARIABLE issue_output
  ERROR_VARIABLE issue_error
)
if(NOT issue_result EQUAL 0)
  message(FATAL_ERROR
          "unroll issue query failed:\n${issue_output}${issue_error}")
endif()

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

execute_process(
  COMMAND "${TOOL}" run tile_pass.check_fuse_issue
          "${INVALID_FUSE_MODEL}"
          --arg "\"requires pointwise consumer reads\"" -M "${MODULES}"
  RESULT_VARIABLE issue_result
  OUTPUT_VARIABLE issue_output
  ERROR_VARIABLE issue_error
)
if(NOT issue_result EQUAL 0)
  message(FATAL_ERROR
          "fusion issue query failed:\n${issue_output}${issue_error}")
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

file(REMOVE_RECURSE "${ROOT}")
file(MAKE_DIRECTORY "${ROOT}")

set(multi_ir "${ROOT}/multi.jog")
set(multi_source "${ROOT}/multi.c")
set(multi_program "${ROOT}/multi")
execute_process(
  COMMAND "${TOOL}" run tile.scalarize tile.fuse c.prepare "${MULTI_MODEL}"
          -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${multi_ir}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "multi-axis fusion failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" check "${multi_ir}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "re-reading multi-axis fused IR failed (${result}):\n"
          "${output}${error}")
endif()
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
execute_process(
  COMMAND "${TOOL}" emit c.source "${multi_ir}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${multi_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "multi-axis C emission failed (${result}):\n${error}")
endif()
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
execute_process(
  COMMAND "${multi_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "multi-axis fused C returned the wrong result (${result}):\n"
          "${output}${error}")
endif()

set(tiled_sum "${ROOT}/tiled-sum.jog")
set(tiled "${ROOT}/tiled.jog")
set(axis_tiled "${ROOT}/axis-tiled.jog")
set(unrolled_grid "${ROOT}/unrolled-grid.jog")
set(policy_unrolled "${ROOT}/policy-unrolled.jog")
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
  COMMAND "${TOOL}" run tile_pass.named_axis "${tiled}"
          --arg "\"grid\"" --arg 0 --arg 2 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${axis_tiled}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "first-axis loop tiling failed (${result}):\n${error}")
endif()
file(READ "${axis_tiled}" axis_tiled_text)
if(NOT axis_tiled_text MATCHES
   "for row_tile in [^\n]+, row in [^\n]+, column_tile in")
  message(FATAL_ERROR
          "first-axis tiling changed or hid the original loop order:\n"
          "${axis_tiled_text}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.unroll_named "${axis_tiled}"
          --arg "\"fixed_grid\"" --arg 2 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${unrolled_grid}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "multi-axis loop unrolling failed (${result}):\n${error}")
endif()
file(READ "${unrolled_grid}" unrolled_grid_text)
if(NOT unrolled_grid_text MATCHES "unroll_blocks_[0-9]+: index = 2" OR
   NOT unrolled_grid_text MATCHES "unroll_offset_[0-9]+: index = 1")
  message(FATAL_ERROR
          "multi-axis loop unrolling omitted its points:\n${unrolled_grid_text}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.unroll_small "${unrolled_grid}"
          --arg 3 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${policy_unrolled}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "policy-selected unrolling failed (${result}):\n${error}")
endif()
file(READ "${policy_unrolled}" policy_unrolled_text)
if(NOT policy_unrolled_text MATCHES "unroll_offset_[0-9]+: index = 2")
  message(FATAL_ERROR
          "policy-selected unrolling omitted a three-point loop:\n"
          "${policy_unrolled_text}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare "${policy_unrolled}" -M "${MODULES}"
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
set(rejected_fusion "${ROOT}/rejected-fusion.jog")
set(policy_fusion "${ROOT}/policy-fusion.jog")
set(fused_ready "${ROOT}/fused-ready.jog")
set(fused_source "${ROOT}/fused.c")
set(fused_program "${ROOT}/fused")
set(unrolled "${ROOT}/unrolled.jog")
set(unrolled_source "${ROOT}/unrolled.c")
set(unrolled_program "${ROOT}/unrolled")

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
  COMMAND "${TOOL}" run tile_pass.check_fusible "${fuse_prepared}"
          --arg 2 -M "${MODULES}"
  RESULT_VARIABLE candidate_result
  OUTPUT_VARIABLE candidate_output
  ERROR_VARIABLE candidate_error
)
if(NOT candidate_result EQUAL 0)
  message(FATAL_ERROR
          "fusion candidate enumeration failed:\n"
          "${candidate_output}${candidate_error}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.unroll_first "${fuse_prepared}"
          --arg 2 -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${unrolled}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "loop unrolling failed (${result}):\n${error}")
endif()
file(READ "${unrolled}" unrolled_text)
if(NOT unrolled_text MATCHES "unroll_blocks_[0-9]+: index = 2" OR
   NOT unrolled_text MATCHES "unroll_offset_[0-9]+: index = 1" OR
   unrolled_text MATCHES "var size = 4")
  message(FATAL_ERROR
          "loop unrolling retained the old range or omitted a point:\n${unrolled_text}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${unrolled}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${unrolled_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "unrolled C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${unrolled_source}" "${FUSE_HARNESS}" -o "${unrolled_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "unrolled C did not compile (${result}):\n${output}${error}")
endif()
execute_process(
  COMMAND "${unrolled_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "unrolled C returned the wrong result (${result}):\n${output}${error}")
endif()

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

execute_process(
  COMMAND "${TOOL}" run cost.fuse "${fuse_prepared}"
          --arg 0 --arg 100 -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${rejected_fusion}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "fusion policy rejection failed (${result}):\n${error}")
endif()
file(READ "${rejected_fusion}" rejected_fusion_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" rejected_loops
       "${rejected_fusion_text}")
list(LENGTH rejected_loops rejected_loop_count)
if(NOT rejected_loop_count EQUAL 3)
  message(FATAL_ERROR
          "rejected fusion policy changed the loop chain:\n${rejected_fusion_text}")
endif()

execute_process(
  COMMAND "${TOOL}" run cost.fuse "${fuse_prepared}"
          --arg 100 --arg 100 -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${policy_fusion}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "configured fusion policy failed (${result}):\n${error}")
endif()
file(READ "${policy_fusion}" policy_fusion_text)
string(REGEX MATCHALL "for [A-Za-z_][A-Za-z0-9_]* in" policy_loops
       "${policy_fusion_text}")
list(LENGTH policy_loops policy_loop_count)
if(NOT policy_loop_count EQUAL 1)
  message(FATAL_ERROR
          "configured fusion policy did not collapse the chain:\n${policy_fusion_text}")
endif()

execute_process(
  COMMAND "${TOOL}" run tile_pass.can_first tile_pass.permitted
          "${fuse_prepared}"
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
if(NOT fused_loop_count EQUAL 1)
  message(FATAL_ERROR
          "fusion did not collapse the pointwise chain:\n${fused_text}")
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
file(READ "${fused_ready}" fused_ready_text)
if(fused_ready_text MATCHES "var first" OR
   fused_ready_text MATCHES "first\\[")
  message(FATAL_ERROR
          "fusion retained its private intermediate:\n${fused_ready_text}")
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

set(keep_ir "${ROOT}/keep.jog")
set(keep_source "${ROOT}/keep.c")
set(keep_program "${ROOT}/keep")
execute_process(
  COMMAND "${TOOL}" run c.prepare tile_pass.fuse_first c.prepare
          "${KEEP_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${keep_ir}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "retained-result fusion failed (${result}):\n${error}")
endif()
file(READ "${keep_ir}" keep_text)
if(NOT keep_text MATCHES "return first, second" OR
   NOT keep_text MATCHES "first\\[i\\] =" OR
   keep_text MATCHES "second\\[i\\] = first\\[i\\]")
  message(FATAL_ERROR
          "fusion did not retain and forward the live producer:\n${keep_text}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${keep_ir}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${keep_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "retained-result C emission failed (${result}):\n${error}")
endif()
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
execute_process(
  COMMAND "${keep_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "retained-result C was incorrect (${result}):\n${output}${error}")
endif()

set(relu_ir "${ROOT}/relu.jog")
set(relu_source "${ROOT}/relu.c")
set(relu_program "${ROOT}/relu")
execute_process(
  COMMAND "${TOOL}" run c.prepare tile_pass.all c.prepare
          "${RELU_MODEL}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${relu_ir}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "activation fusion failed (${result}):\n${error}")
endif()
file(READ "${relu_ir}" relu_text)
if(relu_text MATCHES "var sum" OR relu_text MATCHES "sum\\[" OR
   relu_text MATCHES "var normalized" OR
   relu_text MATCHES "normalized\\[" OR
   relu_text MATCHES "var size_1" OR
   NOT relu_text MATCHES "let fuse_value_")
  message(FATAL_ERROR
          "activation fusion retained its sum tensor:\n${relu_text}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${relu_ir}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${relu_source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "activation C emission failed (${result}):\n${error}")
endif()
file(READ "${relu_source}" relu_source_text)
if(relu_source_text MATCHES "sum\\[" OR
   relu_source_text MATCHES "normalized\\[" OR
   relu_source_text MATCHES "size_1" OR
   NOT relu_source_text MATCHES "float fuse_value_")
  message(FATAL_ERROR
          "activation C retained its sum tensor:\n${relu_source_text}")
endif()
execute_process(
  COMMAND "${CC}" -std=c99 -O2 -Wall -Wextra -Wstrict-prototypes -Werror
          "${relu_source}" "${RELU_HARNESS}" -lm -o "${relu_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "activation C did not compile (${result}):\n${output}${error}\n${relu_source_text}")
endif()
execute_process(
  COMMAND "${relu_program}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR
          "activation C was incorrect (${result}):\n${output}${error}")
endif()
