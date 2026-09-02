foreach(required JOGGLE_CLI JOGGLE_ARITH JOGGLE_TENSOR JOGGLE_NN JOGGLE_EDGEVEC JOGGLE_FIXED
                 JOGGLE_PACKAGE_TEST_DIR JOGGLE_LOCK_CONSUMER JOGGLE_BEHAVIOR_MODULE
                 JOGGLE_BEHAVIOR_LIBRARY JOGGLE_BEHAVIOR_FAILURE
                 JOGGLE_BEHAVIOR_LOADER JOGGLE_ARITH_BEHAVIOR
                 JOGGLE_TENSOR_BEHAVIOR JOGGLE_EDGEVEC_BEHAVIOR
                 JOGGLE_FIXED_BEHAVIOR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} was not provided")
  endif()
endforeach()

file(REMOVE_RECURSE "${JOGGLE_PACKAGE_TEST_DIR}")
file(MAKE_DIRECTORY "${JOGGLE_PACKAGE_TEST_DIR}")
set(root "${JOGGLE_PACKAGE_TEST_DIR}/modules")
set(lock "${JOGGLE_PACKAGE_TEST_DIR}/joggle.lock")
set(behavior_lock "${JOGGLE_PACKAGE_TEST_DIR}/behavior.lock")
set(fixed_lock "${JOGGLE_PACKAGE_TEST_DIR}/fixed.lock")
set(invalid_lock "${JOGGLE_PACKAGE_TEST_DIR}/invalid.lock")

function(expect_success label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} failed:\n${output}${error}")
  endif()
  set(command_output "${output}" PARENT_SCOPE)
endfunction()

function(expect_failure label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(result EQUAL 0)
    message(FATAL_ERROR "${label} unexpectedly succeeded:\n${output}${error}")
  endif()
  set(command_output "${output}" PARENT_SCOPE)
  set(command_error "${error}" PARENT_SCOPE)
endfunction()

expect_success("show help" "${JOGGLE_CLI}" help)
string(FIND "${command_output}" "[--behavior <library>] [--root <directory>]"
  run_help_options_position)
if(run_help_options_position EQUAL -1)
  message(FATAL_ERROR
    "run help omitted supported behavior/root options:\n${command_output}")
endif()

expect_failure("reject unknown command" "${JOGGLE_CLI}" compile)
string(FIND "${command_error}" "unknown command 'compile'"
  unknown_command_position)
if(unknown_command_position EQUAL -1)
  message(FATAL_ERROR
    "unknown command lacked a direct diagnostic:\n${command_error}")
endif()

set(format_case "${JOGGLE_PACKAGE_TEST_DIR}/format-case.joggle")
file(WRITE "${format_case}"
  "joggle 1; module formatted@1.0.0 { type word(width:i64); }")
expect_success("format in place" "${JOGGLE_CLI}" fmt "${format_case}" --write)
file(READ "${format_case}" formatted_text)
set(expected_format [=[joggle 1;

module formatted@1.0.0 {
  type word(width: i64);
}
]=])
if(NOT formatted_text STREQUAL expected_format)
  message(FATAL_ERROR "in-place formatting is not canonical:\n${formatted_text}")
endif()

expect_failure("reject unknown option"
  "${JOGGLE_CLI}" check "${format_case}" --unknown)
string(FIND "${command_error}" "unknown option '--unknown'"
  unknown_option_position)
if(unknown_option_position EQUAL -1)
  message(FATAL_ERROR
    "unknown option was not diagnosed directly:\n${command_error}")
endif()

expect_failure("reject missing option value"
  "${JOGGLE_CLI}" check "${format_case}" --root --unknown)
string(FIND "${command_error}" "--root needs a directory"
  missing_option_value_position)
if(missing_option_value_position EQUAL -1)
  message(FATAL_ERROR
    "option consumed another option as its value:\n${command_error}")
endif()

expect_failure("reject ignored formatter root"
  "${JOGGLE_CLI}" fmt "${format_case}" --root "${root}")
string(FIND "${command_error}" "fmt does not accept --root"
  formatter_root_position)
if(formatter_root_position EQUAL -1)
  message(FATAL_ERROR
    "formatter root misuse was not diagnosed:\n${command_error}")
endif()

expect_failure("reject duplicate output"
  "${JOGGLE_CLI}" fmt "${format_case}" -o first.joggle -o second.joggle)
string(FIND "${command_error}" "duplicate option '--output'"
  duplicate_output_position)
if(duplicate_output_position EQUAL -1)
  message(FATAL_ERROR
    "duplicate output was not diagnosed:\n${command_error}")
endif()

expect_failure("reject formatter output conflict"
  "${JOGGLE_CLI}" fmt "${format_case}" --write -o output.joggle)
string(FIND "${command_error}" "--output and --write are mutually exclusive"
  formatter_output_conflict_position)
if(formatter_output_conflict_position EQUAL -1)
  message(FATAL_ERROR
    "formatter output conflict was not diagnosed:\n${command_error}")
endif()

if(UNIX)
  set(format_link_target "${JOGGLE_PACKAGE_TEST_DIR}/format-link-target.joggle")
  set(format_link "${JOGGLE_PACKAGE_TEST_DIR}/format-link.joggle")
  file(WRITE "${format_link_target}"
    "joggle 1; module linked@1.0.0 { type word(width:i64); }")
  file(CREATE_LINK "${format_link_target}" "${format_link}" SYMBOLIC
    RESULT link_result)
  if(NOT link_result STREQUAL "0")
    message(FATAL_ERROR "cannot create formatter symlink: ${link_result}")
  endif()
  expect_success("format through symlink"
    "${JOGGLE_CLI}" fmt "${format_link}" --write)
  if(NOT IS_SYMLINK "${format_link}")
    message(FATAL_ERROR "in-place formatting replaced a symbolic link")
  endif()
  file(READ "${format_link_target}" linked_text)
  string(FIND "${linked_text}" "module linked@1.0.0 {" linked_module_position)
  string(FIND "${linked_text}" "type word(width: i64);" linked_type_position)
  if(linked_module_position EQUAL -1 OR linked_type_position EQUAL -1)
    message(FATAL_ERROR "symbolic-link target was not formatted:\n${linked_text}")
  endif()
endif()

set(format_directory "${JOGGLE_PACKAGE_TEST_DIR}/format-output-directory")
file(MAKE_DIRECTORY "${format_directory}")
file(WRITE "${format_directory}/sentinel" "preserve")
expect_failure("reject directory format output"
  "${JOGGLE_CLI}" fmt "${format_case}" -o "${format_directory}")
if(NOT EXISTS "${format_directory}/sentinel")
  message(FATAL_ERROR "failed formatting damaged its output directory")
endif()

file(GLOB pending_format_writes
  "${JOGGLE_PACKAGE_TEST_DIR}/.*.joggle-*")
if(pending_format_writes)
  message(FATAL_ERROR
    "formatter left pending writes: ${pending_format_writes}")
endif()

set(missing_import "${JOGGLE_PACKAGE_TEST_DIR}/missing-import.joggle")
file(WRITE "${missing_import}" [=[joggle 1;

module missing_import@1.0.0 {
  import unavailable@1;
}
]=])
expect_failure("check unresolved import"
  "${JOGGLE_CLI}" check "${missing_import}" --root "${root}")
string(FIND "${command_error}" "imports missing module 'unavailable'"
  missing_import_position)
string(FIND "${command_error}" "${missing_import}:4:3:"
  missing_import_source_position)
if(missing_import_position EQUAL -1 OR missing_import_source_position EQUAL -1)
  message(FATAL_ERROR
    "check did not explain the unresolved import:\n${command_error}")
endif()
expect_failure("reject install with unresolved import"
  "${JOGGLE_CLI}" install "${missing_import}" --root "${root}")
if(EXISTS "${root}/missing_import")
  message(FATAL_ERROR "failed install published a module with missing imports")
endif()

expect_success("check local source closure"
  "${JOGGLE_CLI}" check "${JOGGLE_NN}"
  --with "${JOGGLE_ARITH}" --with "${JOGGLE_TENSOR}" --root "${root}")

set(invalid_graph "${JOGGLE_PACKAGE_TEST_DIR}/invalid-graph.joggle")
file(WRITE "${invalid_graph}" [=[joggle 1;

module invalid_graph@1.0.0 {
  type scalar();

  graph main(%x: scalar) -> scalar {
    %y = unknown(%x);
    return %y;
  }
}
]=])
expect_failure("check invalid graph"
  "${JOGGLE_CLI}" check "${invalid_graph}" --root "${root}")
string(FIND "${command_error}" "unknown operation 'unknown'"
  unknown_operation_position)
if(unknown_operation_position EQUAL -1)
  message(FATAL_ERROR
    "check did not instantiate the named graph:\n${command_error}")
endif()
expect_failure("reject install with invalid graph"
  "${JOGGLE_CLI}" install "${invalid_graph}" --root "${root}")
if(EXISTS "${root}/invalid_graph")
  message(FATAL_ERROR "failed install published a module with an invalid graph")
endif()
file(WRITE "${invalid_lock}" "preserve previous lock\n")
expect_failure("reject lock with invalid graph"
  "${JOGGLE_CLI}" lock "${invalid_graph}" --root "${root}"
  -o "${invalid_lock}")
file(READ "${invalid_lock}" invalid_lock_text)
if(NOT invalid_lock_text STREQUAL "preserve previous lock\n")
  message(FATAL_ERROR "failed lock generation replaced its previous output")
endif()

expect_success("install arith"
  "${JOGGLE_CLI}" install "${JOGGLE_ARITH}"
  --behavior "${JOGGLE_ARITH_BEHAVIOR}" --root "${root}")
string(STRIP "${command_output}" installed_arith_module)
get_filename_component(installed_arith_identity
  "${installed_arith_module}" DIRECTORY)
expect_success("check behavior identity"
  "${JOGGLE_CLI}" check "${JOGGLE_ARITH}"
  --behavior "${JOGGLE_ARITH_BEHAVIOR}" --root "${root}")
expect_success("check resolved closure"
  "${JOGGLE_CLI}" check "${JOGGLE_TENSOR}" --root "${root}")
expect_success("install tensor"
  "${JOGGLE_CLI}" install "${JOGGLE_TENSOR}"
  --behavior "${JOGGLE_TENSOR_BEHAVIOR}" --root "${root}")
expect_success("install nn"
  "${JOGGLE_CLI}" install "${JOGGLE_NN}" --root "${root}")
expect_success("install edgevec"
  "${JOGGLE_CLI}" install "${JOGGLE_EDGEVEC}"
  --behavior "${JOGGLE_EDGEVEC_BEHAVIOR}" --root "${root}")
expect_success("install fixed"
  "${JOGGLE_CLI}" install "${JOGGLE_FIXED}"
  --behavior "${JOGGLE_FIXED_BEHAVIOR}" --root "${root}")
expect_success("idempotent install"
  "${JOGGLE_CLI}" install "${JOGGLE_ARITH}"
  --behavior "${JOGGLE_ARITH_BEHAVIOR}" --root "${root}")

file(GLOB_RECURSE installed_arith_behaviors
  "${installed_arith_identity}/behavior/*/*/behavior.so"
  "${installed_arith_identity}/behavior/*/*/behavior.dylib"
  "${installed_arith_identity}/behavior/*/*/behavior.dll")
list(LENGTH installed_arith_behaviors arith_behavior_count)
if(NOT arith_behavior_count EQUAL 1)
  message(FATAL_ERROR "expected one installed arith behavior")
endif()
list(GET installed_arith_behaviors 0 installed_arith_behavior)
get_filename_component(arith_behavior_filename
  "${installed_arith_behavior}" NAME)
file(COPY_FILE "${installed_arith_behavior}"
  "${installed_arith_identity}/${arith_behavior_filename}")
expect_failure("reject ambiguous behavior lock"
  "${JOGGLE_CLI}" lock "${JOGGLE_ARITH}" --root "${root}")
file(REMOVE "${installed_arith_identity}/${arith_behavior_filename}")

expect_success("install behavior package"
  "${JOGGLE_CLI}" install "${JOGGLE_BEHAVIOR_MODULE}"
  --behavior "${JOGGLE_BEHAVIOR_LIBRARY}" --root "${root}")
string(STRIP "${command_output}" installed_behavior_module)
get_filename_component(installed_behavior_identity
  "${installed_behavior_module}" DIRECTORY)
expect_success("load installed behavior"
  "${JOGGLE_BEHAVIOR_LOADER}" "${installed_behavior_module}"
  "${JOGGLE_BEHAVIOR_LIBRARY}" "${JOGGLE_BEHAVIOR_FAILURE}")
expect_success("lock behavior package"
  "${JOGGLE_CLI}" lock "${JOGGLE_BEHAVIOR_MODULE}" --root "${root}"
  -o "${behavior_lock}")
file(READ "${behavior_lock}" behavior_lock_text)
string(FIND "${behavior_lock_text}"
  "behavior behavior_plugin@1.0.0#" behavior_lock_position)
if(behavior_lock_position EQUAL -1)
  message(FATAL_ERROR "behavior lock entry is missing:\n${behavior_lock_text}")
endif()
expect_success("replay locked behavior"
  "${JOGGLE_BEHAVIOR_LOADER}" "${installed_behavior_module}"
  "${JOGGLE_BEHAVIOR_LIBRARY}" "${JOGGLE_BEHAVIOR_FAILURE}"
  "${root}" "${behavior_lock}")
file(GLOB_RECURSE installed_behavior_libraries
  "${installed_behavior_identity}/behavior/*/*/behavior.so"
  "${installed_behavior_identity}/behavior/*/*/behavior.dylib"
  "${installed_behavior_identity}/behavior/*/*/behavior.dll")
list(LENGTH installed_behavior_libraries behavior_library_count)
if(NOT behavior_library_count EQUAL 1)
  message(FATAL_ERROR "expected one installed behavior library")
endif()
list(GET installed_behavior_libraries 0 installed_behavior_library)
set(behavior_backup "${JOGGLE_PACKAGE_TEST_DIR}/behavior.backup")
file(COPY_FILE "${installed_behavior_library}" "${behavior_backup}")
file(APPEND "${installed_behavior_library}" "tampered")
execute_process(
  COMMAND "${JOGGLE_BEHAVIOR_LOADER}" "${installed_behavior_module}"
          "${JOGGLE_BEHAVIOR_LIBRARY}" "${JOGGLE_BEHAVIOR_FAILURE}"
          "${root}" "${behavior_lock}"
  RESULT_VARIABLE tampered_behavior_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(tampered_behavior_result EQUAL 0)
  message(FATAL_ERROR "locked behavior replay ignored modified binary content")
endif()
file(COPY_FILE "${behavior_backup}" "${installed_behavior_library}")
file(REMOVE "${behavior_backup}")
file(REMOVE "${installed_behavior_libraries}")
execute_process(
  COMMAND "${JOGGLE_BEHAVIOR_LOADER}" "${installed_behavior_module}"
          "${JOGGLE_BEHAVIOR_LIBRARY}" "${JOGGLE_BEHAVIOR_FAILURE}"
          "${root}" "${behavior_lock}"
  RESULT_VARIABLE missing_behavior_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(missing_behavior_result EQUAL 0)
  message(FATAL_ERROR "locked behavior replay ignored a missing binary")
endif()

expect_success("list modules" "${JOGGLE_CLI}" list --root "${root}")
string(FIND "${command_output}" "arith@1.0.0#" arith_position)
string(FIND "${command_output}" "tensor@1.0.0#" tensor_position)
string(FIND "${command_output}" "nn@1.0.0#" nn_position)
string(FIND "${command_output}" "edgevec@1.0.0#" edgevec_position)
string(FIND "${command_output}" "fixed@1.0.0#" fixed_position)
string(FIND "${command_output}" "behavior_plugin@1.0.0#" behavior_position)
if(arith_position EQUAL -1 OR tensor_position EQUAL -1 OR nn_position EQUAL -1 OR
   edgevec_position EQUAL -1 OR fixed_position EQUAL -1 OR
   behavior_position EQUAL -1)
  message(FATAL_ERROR "module list is incomplete:\n${command_output}")
endif()

file(WRITE "${lock}" "previous lock contents\n")
expect_success("lock closure"
  "${JOGGLE_CLI}" lock "${JOGGLE_EDGEVEC}" --root "${root}" -o "${lock}")
file(READ "${lock}" lock_text)
string(FIND "${lock_text}" "root edgevec@1.0.0#" root_position)
string(FIND "${lock_text}" "module arith@1.0.0#" arith_dependency_position)
string(FIND "${lock_text}" "module tensor@1.0.0#" tensor_dependency_position)
string(FIND "${lock_text}" "module nn@1.0.0#" nn_dependency_position)
string(FIND "${lock_text}" "behavior arith@1.0.0#"
  arith_behavior_position)
string(FIND "${lock_text}" "behavior tensor@1.0.0#"
  tensor_behavior_position)
string(FIND "${lock_text}" "behavior edgevec@1.0.0#"
  edgevec_behavior_position)
if(root_position EQUAL -1 OR arith_dependency_position EQUAL -1 OR
   tensor_dependency_position EQUAL -1 OR nn_dependency_position EQUAL -1 OR
   arith_behavior_position EQUAL -1 OR
   tensor_behavior_position EQUAL -1 OR edgevec_behavior_position EQUAL -1)
  message(FATAL_ERROR "lock file is incomplete:\n${lock_text}")
endif()
file(GLOB pending_lock_writes
  "${JOGGLE_PACKAGE_TEST_DIR}/.*.joggle-*")
if(pending_lock_writes)
  message(FATAL_ERROR "lock command left pending writes: ${pending_lock_writes}")
endif()

expect_success("consume locked closure"
  "${JOGGLE_LOCK_CONSUMER}" "${root}" "${lock}" "${JOGGLE_EDGEVEC}" 4)

expect_success("lock fixed closure"
  "${JOGGLE_CLI}" lock "${JOGGLE_FIXED}" --root "${root}" -o "${fixed_lock}")
file(READ "${fixed_lock}" fixed_lock_text)
string(FIND "${fixed_lock_text}" "root fixed@1.0.0#" fixed_root_position)
string(FIND "${fixed_lock_text}" "module arith@1.0.0#"
  fixed_dependency_position)
string(FIND "${fixed_lock_text}" "behavior fixed@1.0.0#"
  fixed_behavior_position)
if(fixed_root_position EQUAL -1 OR fixed_dependency_position EQUAL -1 OR
   fixed_behavior_position EQUAL -1)
  message(FATAL_ERROR "fixed lock file is incomplete:\n${fixed_lock_text}")
endif()
expect_success("consume locked fixed closure"
  "${JOGGLE_LOCK_CONSUMER}" "${root}" "${fixed_lock}" "${JOGGLE_FIXED}" 2)

set(conflict "${JOGGLE_PACKAGE_TEST_DIR}/conflict.joggle")
file(WRITE "${conflict}" [=[joggle 1;
module arith@1.0.0 {
  type conflicting();
}
]=])
execute_process(
  COMMAND "${JOGGLE_CLI}" install "${conflict}" --root "${root}"
  RESULT_VARIABLE conflict_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(conflict_result EQUAL 0)
  message(FATAL_ERROR "same name/version with another digest was accepted")
endif()

expect_success("uninstall module"
  "${JOGGLE_CLI}" uninstall arith@1.0.0 --root "${root}")
execute_process(
  COMMAND "${JOGGLE_CLI}" lock "${JOGGLE_EDGEVEC}" --root "${root}"
  RESULT_VARIABLE missing_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(missing_result EQUAL 0)
  message(FATAL_ERROR "lock succeeded after a required module was removed")
endif()

execute_process(
  COMMAND "${JOGGLE_LOCK_CONSUMER}" "${root}" "${lock}" "${JOGGLE_EDGEVEC}" 4
  RESULT_VARIABLE missing_locked_result
  OUTPUT_QUIET
  ERROR_QUIET
)
if(missing_locked_result EQUAL 0)
  message(FATAL_ERROR "Compiler replayed a lock with a missing digest")
endif()
