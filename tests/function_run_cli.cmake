foreach(required JOGGLE_CLI JOGGLE_SOURCE JOGGLE_DEPENDENCY JOGGLE_BEHAVIOR
                 JOGGLE_OUTPUT)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} was not provided")
  endif()
endforeach()

set(input "${JOGGLE_OUTPUT}.input")
file(WRITE "${input}" "edge-ai")
set(model_input "${JOGGLE_OUTPUT}.model.joggle")
set(model_output "${JOGGLE_OUTPUT}.optimized.joggle")
set(model_emitted "${JOGGLE_OUTPUT}.emitted.joggle")
set(loaded_output "${JOGGLE_OUTPUT}.loaded.joggle")
file(WRITE "${model_input}" [=[joggle 1;

module cli_model@1.0.0 {
  type word();
  fn keep(input: word) -> word;
  fn main(input: word) -> word {
    return keep(input);
  }
}
]=])

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" pipeline "${input}"
          --unknown
  RESULT_VARIABLE unknown_option_result
  ERROR_VARIABLE unknown_option_error
)
string(FIND "${unknown_option_error}" "unknown option '--unknown'"
  unknown_option_position)
if(unknown_option_result EQUAL 0 OR unknown_option_position EQUAL -1)
  message(FATAL_ERROR
    "run accepted an unknown option:\n${unknown_option_error}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" pipeline
  RESULT_VARIABLE missing_input_result
  ERROR_VARIABLE missing_input_error
)
string(FIND "${missing_input_error}" "input file" missing_input_position)
if(missing_input_result EQUAL 0 OR missing_input_position EQUAL -1)
  message(FATAL_ERROR
    "run accepted a missing byte input:\n${missing_input_error}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" wrong_boundary "${input}"
          --with "${JOGGLE_DEPENDENCY}"
  RESULT_VARIABLE boundary_result
  ERROR_VARIABLE boundary_error
)
string(FIND "${boundary_error}"
  "must have signature bytes -> bytes, bytes -> module"
  boundary_position)
if(boundary_result EQUAL 0 OR boundary_position EQUAL -1)
  message(FATAL_ERROR
    "run accepted an unsupported pipeline boundary:\n${boundary_error}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" read_model "${input}"
          --with "${JOGGLE_DEPENDENCY}"
          --load-behavior "behavior_plugin=${JOGGLE_BEHAVIOR}"
          -o "${loaded_output}"
  RESULT_VARIABLE load_result
  ERROR_VARIABLE load_error
)
if(NOT load_result EQUAL 0)
  message(FATAL_ERROR "typed bytes -> module loader failed:\n${load_error}")
endif()
file(READ "${loaded_output}" loaded_source)
string(FIND "${loaded_source}" "module loaded_model@1.0.0" loaded_module)
string(FIND "${loaded_source}" "fn main()" loaded_main)
if(loaded_module EQUAL -1 OR loaded_main EQUAL -1)
  message(FATAL_ERROR
    "typed loader did not emit its Module result:\n${loaded_source}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" optimize_model
          "${model_input}" --with "${JOGGLE_DEPENDENCY}"
          -o "${model_output}"
  RESULT_VARIABLE optimize_result
  ERROR_VARIABLE optimize_error
)
if(NOT optimize_result EQUAL 0)
  message(FATAL_ERROR
    "typed module -> module transform failed:\n${optimize_error}")
endif()
file(READ "${model_output}" optimized_source)
string(FIND "${optimized_source}"
  "v1: cli_model.word = cli_model.keep(arg0);" materialized_entry)
if(materialized_entry EQUAL -1)
  message(FATAL_ERROR
    "Module input was not materialized before transformation:\n${optimized_source}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" emit_model
          "${model_output}" --with "${JOGGLE_DEPENDENCY}"
          --load-behavior "behavior_plugin=${JOGGLE_BEHAVIOR}"
          -o "${model_emitted}"
  RESULT_VARIABLE emit_result
  ERROR_VARIABLE emit_error
)
if(NOT emit_result EQUAL 0)
  message(FATAL_ERROR "typed module -> bytes emitter failed:\n${emit_error}")
endif()
file(READ "${model_emitted}" emitted_source)
if(NOT emitted_source STREQUAL optimized_source)
  message(FATAL_ERROR
    "typed emitter changed the canonical Module representation")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" pipeline "${input}"
          --with "${JOGGLE_DEPENDENCY}"
          --load-behavior "behavior_plugin=${JOGGLE_BEHAVIOR}"
          -o "${JOGGLE_OUTPUT}"
  RESULT_VARIABLE file_result
  OUTPUT_VARIABLE file_stdout
  ERROR_VARIABLE file_error
)
if(NOT file_result EQUAL 0)
  message(FATAL_ERROR "typed byte pipeline failed:\n${file_error}")
endif()
if(NOT file_stdout STREQUAL "")
  message(FATAL_ERROR "run wrote stdout despite -o")
endif()
file(READ "${JOGGLE_OUTPUT}" file_output)
if(NOT file_output STREQUAL "ia-egde")
  message(FATAL_ERROR
    "typed byte pipeline produced '${file_output}', expected 'ia-egde'")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" function_cli.pipeline
          "${input}" --with "${JOGGLE_DEPENDENCY}"
          --load-behavior "behavior_plugin=${JOGGLE_BEHAVIOR}"
  RESULT_VARIABLE stdout_result
  OUTPUT_VARIABLE stdout_output
  ERROR_VARIABLE stdout_error
)
if(NOT stdout_result EQUAL 0 OR NOT stdout_output STREQUAL "ia-egde")
  message(FATAL_ERROR
    "qualified byte pipeline failed:\n${stdout_error}${stdout_output}")
endif()
