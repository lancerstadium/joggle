if(NOT DEFINED JOGGLE_CLI OR NOT DEFINED JOGGLE_SOURCE OR
   NOT DEFINED JOGGLE_OUTPUT OR NOT DEFINED JOGGLE_BEHAVIOR_SOURCE OR
   NOT DEFINED JOGGLE_BEHAVIOR OR NOT DEFINED JOGGLE_TARGET OR
   NOT DEFINED JOGGLE_IR_MODULE OR NOT DEFINED JOGGLE_IR_TRANSFORM OR
   NOT DEFINED JOGGLE_IR_TRANSFORM_BEHAVIOR)
  message(FATAL_ERROR "function run test arguments are incomplete")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" main --unknown
  RESULT_VARIABLE unknown_option_result
  ERROR_VARIABLE unknown_option_error
)
string(FIND "${unknown_option_error}" "unknown option '--unknown'"
  unknown_option_position)
if(unknown_option_result EQUAL 0 OR unknown_option_position EQUAL -1)
  message(FATAL_ERROR
    "run treated an unknown option as a pass name:\n${unknown_option_error}")
endif()

set(target_output "${JOGGLE_OUTPUT}.target")
execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" function_cli.main
          function_target.convert --with "${JOGGLE_TARGET}" -o "${target_output}"
  RESULT_VARIABLE target_result
  ERROR_VARIABLE target_error
)
if(NOT target_result EQUAL 0)
  message(FATAL_ERROR "cross-Module pipeline failed:\n${target_error}")
endif()
file(READ "${target_output}" target_function)
string(FIND "${target_function}" "module function_cli_main_compiled@1.0.0"
  target_module_position)
string(FIND "${target_function}" "function_cli.identity" target_identity_position)
string(FIND "${target_function}" "import function_target" target_import_position)
string(FIND "${target_function}" "function_cli.source()" target_source_position)
if(target_module_position EQUAL -1 OR target_identity_position EQUAL -1 OR
   NOT target_import_position EQUAL -1 OR target_source_position EQUAL -1)
  message(FATAL_ERROR
    "cross-Module pipeline retained converted or unused target state:\n${target_function}")
endif()

set(module_output "${JOGGLE_OUTPUT}.module")
execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" main
          ir_transform.add_helper
          --with "${JOGGLE_IR_MODULE}"
          --with "${JOGGLE_IR_TRANSFORM}"
          --load-behavior
          "ir_transform=${JOGGLE_IR_TRANSFORM_BEHAVIOR}"
          -o "${module_output}"
  RESULT_VARIABLE module_result
  ERROR_VARIABLE module_error
)
if(NOT module_result EQUAL 0)
  message(FATAL_ERROR "ir.module pipeline failed:\n${module_error}")
endif()
file(READ "${module_output}" module_program)
string(FIND "${module_program}" "fn helper()" module_helper_position)
string(FIND "${module_program}" "fn main()" module_main_position)
string(FIND "${module_program}" "import function_cli@1.0.0"
  module_import_position)
if(module_helper_position EQUAL -1 OR module_main_position EQUAL -1 OR
   module_import_position EQUAL -1 OR
   NOT module_helper_position LESS module_main_position)
  message(FATAL_ERROR
    "ir.module transform did not publish a canonical multi-function artifact:\n"
    "${module_program}")
endif()

set(behavior_output "${JOGGLE_OUTPUT}.behavior")
execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_BEHAVIOR_SOURCE}" main noop
          --behavior "${JOGGLE_BEHAVIOR}" -o "${behavior_output}"
  RESULT_VARIABLE behavior_result
  ERROR_VARIABLE behavior_error
)
if(NOT behavior_result EQUAL 0)
  message(FATAL_ERROR "C++ behavior pipeline failed:\n${behavior_error}")
endif()
file(READ "${behavior_output}" behavior_function)
string(FIND "${behavior_function}" "module behavior_plugin_main_compiled@1.0.0"
  behavior_module_position)
string(FIND "${behavior_function}" "behavior_plugin.source()"
  behavior_source_position)
if(behavior_module_position EQUAL -1 OR behavior_source_position EQUAL -1)
  message(FATAL_ERROR
    "C++ behavior pipeline did not publish its Function:\n${behavior_function}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" main simplify
          -o "${JOGGLE_OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE standard_output
  ERROR_VARIABLE standard_error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "joggle run failed:\n${standard_error}")
endif()
if(NOT standard_output STREQUAL "")
  message(FATAL_ERROR "joggle run wrote stdout despite -o")
endif()

file(READ "${JOGGLE_OUTPUT}" function)
string(FIND "${function}" "fn main()" function_position)
string(FIND "${function}" "module function_cli_main_compiled@1.0.0" module_position)
string(FIND "${function}" "import function_cli@1.0.0" import_position)
string(FIND "${function}" "function_cli.source()" source_position)
string(FIND "${function}" "function_cli.identity" identity_position)
if(module_position EQUAL -1 OR import_position EQUAL -1 OR
   function_position EQUAL -1 OR source_position EQUAL -1 OR
   identity_position EQUAL -1)
  message(FATAL_ERROR "joggle run did not publish the transformed Function:\n${function}")
endif()

set(replayed "${JOGGLE_OUTPUT}.replayed")
execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_OUTPUT}" main
          --with "${JOGGLE_SOURCE}" -o "${replayed}"
  RESULT_VARIABLE replay_result
  ERROR_VARIABLE replay_error
)
if(NOT replay_result EQUAL 0)
  message(FATAL_ERROR "compiled Module is not a reusable input:\n${replay_error}")
endif()
file(READ "${replayed}" replayed_function)
string(FIND "${replayed_function}" "module function_cli_main_compiled@1.0.0"
  replayed_module_position)
string(FIND "${replayed_function}" "import function_cli_main_compiled"
  redundant_import_position)
if(replayed_module_position EQUAL -1 OR NOT redundant_import_position EQUAL -1)
  message(FATAL_ERROR
    "replayed pipeline changed identity or retained an unused Module:\n${replayed_function}")
endif()
