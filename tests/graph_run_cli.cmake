if(NOT DEFINED JOGGLE_CLI OR NOT DEFINED JOGGLE_SOURCE OR
   NOT DEFINED JOGGLE_OUTPUT OR NOT DEFINED JOGGLE_BEHAVIOR_SOURCE OR
   NOT DEFINED JOGGLE_BEHAVIOR OR NOT DEFINED JOGGLE_TARGET)
  message(FATAL_ERROR "graph run test arguments are incomplete")
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
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" graph_cli.main
          graph_target.lower --with "${JOGGLE_TARGET}" -o "${target_output}"
  RESULT_VARIABLE target_result
  ERROR_VARIABLE target_error
)
if(NOT target_result EQUAL 0)
  message(FATAL_ERROR "cross-Module pipeline failed:\n${target_error}")
endif()
file(READ "${target_output}" target_graph)
string(FIND "${target_graph}" "module graph_cli_main_compiled@1.0.0"
  target_module_position)
string(FIND "${target_graph}" "graph_cli.identity" target_identity_position)
string(FIND "${target_graph}" "import graph_target" target_import_position)
string(FIND "${target_graph}" "graph_cli.source()" target_source_position)
if(target_module_position EQUAL -1 OR NOT target_identity_position EQUAL -1 OR
   NOT target_import_position EQUAL -1 OR target_source_position EQUAL -1)
  message(FATAL_ERROR
    "cross-Module pipeline retained lowered or unused target state:\n${target_graph}")
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
file(READ "${behavior_output}" behavior_graph)
string(FIND "${behavior_graph}" "module behavior_plugin_main_compiled@1.0.0"
  behavior_module_position)
string(FIND "${behavior_graph}" "behavior_plugin.source()"
  behavior_source_position)
if(behavior_module_position EQUAL -1 OR behavior_source_position EQUAL -1)
  message(FATAL_ERROR
    "C++ behavior pipeline did not publish its Graph:\n${behavior_graph}")
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

file(READ "${JOGGLE_OUTPUT}" graph)
string(FIND "${graph}" "graph main()" graph_position)
string(FIND "${graph}" "module graph_cli_main_compiled@1.0.0" module_position)
string(FIND "${graph}" "import graph_cli@1.0.0" import_position)
string(FIND "${graph}" "graph_cli.source()" source_position)
string(FIND "${graph}" "graph_cli.identity" identity_position)
if(module_position EQUAL -1 OR import_position EQUAL -1 OR
   graph_position EQUAL -1 OR source_position EQUAL -1 OR
   NOT identity_position EQUAL -1)
  message(FATAL_ERROR "joggle run did not publish the transformed Graph:\n${graph}")
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
file(READ "${replayed}" replayed_graph)
string(FIND "${replayed_graph}" "module graph_cli_main_compiled@1.0.0"
  replayed_module_position)
string(FIND "${replayed_graph}" "import graph_cli_main_compiled"
  redundant_import_position)
if(replayed_module_position EQUAL -1 OR NOT redundant_import_position EQUAL -1)
  message(FATAL_ERROR
    "replayed pipeline changed identity or retained an unused Module:\n${replayed_graph}")
endif()
