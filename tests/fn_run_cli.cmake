foreach(required JOGGLE_CLI JOGGLE_SOURCE JOGGLE_DEPENDENCY JOGGLE_NATIVE
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
set(loaded_emitted "${JOGGLE_OUTPUT}.loaded-emitted.joggle")
set(bundle_root "${JOGGLE_OUTPUT}.bundle-mods")
set(failed_output "${JOGGLE_OUTPUT}.failed.joggle")
file(REMOVE_RECURSE "${loaded_output}" "${bundle_root}")
file(WRITE "${model_input}" [=[joggle 1;

mod cli_model@1.0.0 {
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
  "must have signature bytes -> bytes, bytes -> mod"
  boundary_position)
if(boundary_result EQUAL 0 OR boundary_position EQUAL -1)
  message(FATAL_ERROR
    "run accepted an unsupported pipeline boundary:\n${boundary_error}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" read_model "${input}"
          --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
          -o "${loaded_output}"
  RESULT_VARIABLE load_result
  ERROR_VARIABLE load_error
)
if(NOT load_result EQUAL 0)
  message(FATAL_ERROR "typed bytes -> mod loader failed:\n${load_error}")
endif()
file(READ "${loaded_output}/mod.joggle" loaded_source)
string(FIND "${loaded_source}" "mod loaded_model@1.0.0" loaded_mod)
string(FIND "${loaded_source}" "fn main()" loaded_main)
if(loaded_mod EQUAL -1 OR loaded_main EQUAL -1)
  message(FATAL_ERROR
    "typed loader did not emit its Mod result:\n${loaded_source}")
endif()
file(GLOB loaded_data "${loaded_output}/data/*")
list(LENGTH loaded_data loaded_data_count)
if(NOT loaded_data_count EQUAL 1)
  message(FATAL_ERROR "typed loader did not emit one Mod data payload")
endif()
list(GET loaded_data 0 loaded_data_file)
get_filename_component(loaded_data_name "${loaded_data_file}" NAME)
file(SHA256 "${loaded_data_file}" loaded_data_digest)
if(NOT loaded_data_name STREQUAL loaded_data_digest)
  message(FATAL_ERROR "bundle payload filename does not match its content")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" read_model "${input}"
          --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
  RESULT_VARIABLE lost_data_result
  ERROR_VARIABLE lost_data_error
)
string(FIND "${lost_data_error}"
  "data-bearing Mod requires -o <bundle-directory>" lost_data_position)
if(lost_data_result EQUAL 0 OR lost_data_position EQUAL -1)
  message(FATAL_ERROR
    "run silently printed a data-bearing Mod:\n${lost_data_error}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" check "${loaded_output}"
  RESULT_VARIABLE bundle_check_result
  ERROR_VARIABLE bundle_check_error
)
if(NOT bundle_check_result EQUAL 0)
  message(FATAL_ERROR
    "lossless Mod bundle check failed:\n${bundle_check_error}")
endif()
execute_process(
  COMMAND "${JOGGLE_CLI}" install "${loaded_output}" --root "${bundle_root}"
  RESULT_VARIABLE bundle_install_result
  OUTPUT_VARIABLE bundle_install_output
  ERROR_VARIABLE bundle_install_error
)
if(NOT bundle_install_result EQUAL 0)
  message(FATAL_ERROR
    "lossless Mod bundle install failed:\n${bundle_install_error}")
endif()
string(STRIP "${bundle_install_output}" bundle_installed_source)
get_filename_component(bundle_identity "${bundle_installed_source}" DIRECTORY)
file(GLOB installed_data "${bundle_identity}/data/*")
list(LENGTH installed_data installed_data_count)
if(NOT installed_data_count EQUAL 1)
  message(FATAL_ERROR "installed Mod bundle lost its payload")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" emit_model
          "${loaded_output}" --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
          -o "${loaded_emitted}"
  RESULT_VARIABLE bundle_input_result
  ERROR_VARIABLE bundle_input_error
)
if(NOT bundle_input_result EQUAL 0)
  message(FATAL_ERROR
    "Mod bundle input failed:\n${bundle_input_error}")
endif()
file(READ "${loaded_emitted}" loaded_emitted_source)
if(NOT loaded_emitted_source STREQUAL loaded_source)
  message(FATAL_ERROR "Mod bundle input changed canonical source")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" optimize_model
          "${model_input}" --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
          -o "${model_output}"
  RESULT_VARIABLE optimize_result
  ERROR_VARIABLE optimize_error
)
if(NOT optimize_result EQUAL 0)
  message(FATAL_ERROR
    "typed mod -> mod transform failed:\n${optimize_error}")
endif()
file(READ "${model_output}" optimized_source)
string(FIND "${optimized_source}"
  "v1: cli_model.word = cli_model.keep(arg0);" materialized_entry)
string(FIND "${optimized_source}" "fn normalized()" normalized_marker)
string(FIND "${optimized_source}" "fn specialized()" specialized_marker)
if(materialized_entry EQUAL -1 OR normalized_marker EQUAL -1 OR
   specialized_marker EQUAL -1)
  message(FATAL_ERROR
    "typed fn pipeline did not materialize and compose both transforms:\n${optimized_source}")
endif()

file(REMOVE "${failed_output}")
execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" failing_entry
          "${model_input}" --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
          -o "${failed_output}"
  RESULT_VARIABLE failing_result
  ERROR_VARIABLE failing_error
)
string(FIND "${failing_error}" "test transform requested rejection"
  failing_position)
string(FIND "${failing_error}"
  "while calling 'native_plugin.reject_model' from 'fn_cli.failing_model'"
  failing_inner_context)
string(FIND "${failing_error}"
  "while calling 'fn_cli.failing_model' from 'fn_cli.failing_entry'"
  failing_outer_context)
if(failing_result EQUAL 0 OR failing_position EQUAL -1 OR
   failing_inner_context EQUAL -1 OR failing_outer_context EQUAL -1 OR
   EXISTS "${failed_output}")
  message(FATAL_ERROR
    "failed typed fn pipeline published an intermediate Mod:\n${failing_error}")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" emit_model
          "${model_output}" --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
          -o "${model_emitted}"
  RESULT_VARIABLE emit_result
  ERROR_VARIABLE emit_error
)
if(NOT emit_result EQUAL 0)
  message(FATAL_ERROR "typed mod -> bytes emitter failed:\n${emit_error}")
endif()
file(READ "${model_emitted}" emitted_source)
if(NOT emitted_source STREQUAL optimized_source)
  message(FATAL_ERROR
    "typed emitter changed the canonical Mod representation")
endif()

execute_process(
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" pipeline "${input}"
          --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
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
  COMMAND "${JOGGLE_CLI}" run "${JOGGLE_SOURCE}" fn_cli.pipeline
          "${input}" --with "${JOGGLE_DEPENDENCY}"
          --load-native "native_plugin=${JOGGLE_NATIVE}"
  RESULT_VARIABLE stdout_result
  OUTPUT_VARIABLE stdout_output
  ERROR_VARIABLE stdout_error
)
if(NOT stdout_result EQUAL 0 OR NOT stdout_output STREQUAL "ia-egde")
  message(FATAL_ERROR
    "qualified byte pipeline failed:\n${stdout_error}${stdout_output}")
endif()
