foreach(required JOGGLE_CLI JOGGLE_REPOSITORY_TEST_DIR JOGGLE_LOCK_CONSUMER
                 JOGGLE_NATIVE_MOD JOGGLE_NATIVE_LIBRARY
                 JOGGLE_NATIVE_FAILURE JOGGLE_NATIVE_LOADER)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} was not provided")
  endif()
endforeach()

file(REMOVE_RECURSE "${JOGGLE_REPOSITORY_TEST_DIR}")
file(MAKE_DIRECTORY "${JOGGLE_REPOSITORY_TEST_DIR}")
set(root "${JOGGLE_REPOSITORY_TEST_DIR}/mods")
set(lock "${JOGGLE_REPOSITORY_TEST_DIR}/joggle.lock")
set(native_lock "${JOGGLE_REPOSITORY_TEST_DIR}/native.lock")

function(expect_success label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
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
    ERROR_VARIABLE error)
  if(result EQUAL 0)
    message(FATAL_ERROR "${label} unexpectedly succeeded:\n${output}${error}")
  endif()
  set(command_error "${error}" PARENT_SCOPE)
endfunction()

expect_success("show help" "${JOGGLE_CLI}" help)
expect_failure("reject unknown command" "${JOGGLE_CLI}" compile)
string(FIND "${command_error}" "unknown command 'compile'" unknown_position)
if(unknown_position EQUAL -1)
  message(FATAL_ERROR "unknown command lacked a direct diagnostic")
endif()

set(format_case "${JOGGLE_REPOSITORY_TEST_DIR}/format-case.joggle")
file(WRITE "${format_case}"
  "joggle 1; mod formatted@1.0.0 { type word(width:int); }")
expect_success("format in place" "${JOGGLE_CLI}" fmt "${format_case}" --write)
file(READ "${format_case}" formatted_text)
set(expected_format [=[joggle 1;

mod formatted@1.0.0 {
  type word(width: int);
}
]=])
if(NOT formatted_text STREQUAL expected_format)
  message(FATAL_ERROR "in-place formatting is not canonical")
endif()

set(missing "${JOGGLE_REPOSITORY_TEST_DIR}/missing.joggle")
file(WRITE "${missing}" [=[joggle 1;

mod missing@1.0.0 {
  import unavailable@1;
}
]=])
expect_failure("reject unresolved import"
  "${JOGGLE_CLI}" install "${missing}" --root "${root}")
if(EXISTS "${root}/missing")
  message(FATAL_ERROR "failed install published an invalid mod")
endif()

set(base "${JOGGLE_REPOSITORY_TEST_DIR}/base.joggle")
set(leaf "${JOGGLE_REPOSITORY_TEST_DIR}/leaf.joggle")
file(WRITE "${base}" [=[joggle 1;

mod repository_base@1.0.0 {
  type value();
}
]=])
file(WRITE "${leaf}" [=[joggle 1;

mod repository_leaf@1.0.0 {
  import repository_base@1;
  type wrapper(element: type);
}
]=])
expect_success("check local closure"
  "${JOGGLE_CLI}" check "${leaf}" --with "${base}" --root "${root}")
expect_success("install base"
  "${JOGGLE_CLI}" install "${base}" --root "${root}")
expect_success("idempotent base install"
  "${JOGGLE_CLI}" install "${base}" --root "${root}")
expect_success("install leaf"
  "${JOGGLE_CLI}" install "${leaf}" --root "${root}")

set(generic "${JOGGLE_REPOSITORY_TEST_DIR}/generic.joggle")
file(WRITE "${generic}" [=[joggle 1;

mod repository_generic@1.0.0 {
  type word(width: int);
  fn identity<T: type>(input: T) -> T;

  fn staged<S: list<bool>>(steps: S, input: word<8>) -> word<8> {
    current = input;
    for enabled in S {
      if enabled {
        current = identity(current);
      }
    }
    return current;
  }
}
]=])
expect_success("check generic staged body"
  "${JOGGLE_CLI}" check "${generic}" --root "${root}")
expect_success("install generic staged body"
  "${JOGGLE_CLI}" install "${generic}" --root "${root}")

set(invalid_generic "${JOGGLE_REPOSITORY_TEST_DIR}/invalid-generic.joggle")
file(WRITE "${invalid_generic}" [=[joggle 1;

mod repository_invalid_generic@1.0.0 {
  fn invalid<T: type>(input: T) -> T {
    return missing(input);
  }
}
]=])
expect_failure("reject invalid generic body"
  "${JOGGLE_CLI}" check "${invalid_generic}" --root "${root}")
string(FIND "${command_error}" "no visible overload of 'missing'"
  missing_generic_call)
if(missing_generic_call EQUAL -1)
  message(FATAL_ERROR
    "invalid generic body lacked its source diagnostic:\n${command_error}")
endif()

expect_success("list mods" "${JOGGLE_CLI}" list --root "${root}")
string(FIND "${command_output}" "repository_base@1.0.0#" base_position)
string(FIND "${command_output}" "repository_leaf@1.0.0#" leaf_position)
string(FIND "${command_output}" "repository_generic@1.0.0#" generic_position)
if(base_position EQUAL -1 OR leaf_position EQUAL -1 OR
   generic_position EQUAL -1)
  message(FATAL_ERROR "mod list is incomplete:\n${command_output}")
endif()

expect_success("lock closure"
  "${JOGGLE_CLI}" lock "${leaf}" --root "${root}" -o "${lock}")
file(READ "${lock}" lock_text)
string(FIND "${lock_text}" "root repository_leaf@1.0.0#" root_position)
string(FIND "${lock_text}" "mod repository_base@1.0.0#" dependency_position)
if(root_position EQUAL -1 OR dependency_position EQUAL -1)
  message(FATAL_ERROR "lock file is incomplete:\n${lock_text}")
endif()
expect_success("consume locked closure"
  "${JOGGLE_LOCK_CONSUMER}" "${root}" "${lock}" "${leaf}" 2)

expect_success("install native Mod release"
  "${JOGGLE_CLI}" install "${JOGGLE_NATIVE_MOD}"
  --native "${JOGGLE_NATIVE_LIBRARY}" --root "${root}")
string(STRIP "${command_output}" installed_native_mod)
expect_success("load installed native"
  "${JOGGLE_NATIVE_LOADER}" "${installed_native_mod}"
  "${JOGGLE_NATIVE_LIBRARY}" "${JOGGLE_NATIVE_FAILURE}")
expect_success("lock native Mod release"
  "${JOGGLE_CLI}" lock "${JOGGLE_NATIVE_MOD}" --root "${root}"
  -o "${native_lock}")
file(READ "${native_lock}" native_lock_text)
string(FIND "${native_lock_text}"
  "native native_plugin@1.0.0#" native_position)
if(native_position EQUAL -1)
  message(FATAL_ERROR "native lock entry is missing")
endif()
expect_success("replay locked native"
  "${JOGGLE_NATIVE_LOADER}" "${installed_native_mod}"
  "${JOGGLE_NATIVE_LIBRARY}" "${JOGGLE_NATIVE_FAILURE}"
  "${root}" "${native_lock}")

set(conflict "${JOGGLE_REPOSITORY_TEST_DIR}/conflict.joggle")
file(WRITE "${conflict}" [=[joggle 1;
mod repository_base@1.0.0 {
  type conflicting();
}
]=])
expect_failure("reject conflicting identity"
  "${JOGGLE_CLI}" install "${conflict}" --root "${root}")

expect_success("uninstall dependency"
  "${JOGGLE_CLI}" uninstall repository_base@1.0.0 --root "${root}")
expect_failure("reject lock with missing dependency"
  "${JOGGLE_CLI}" lock "${leaf}" --root "${root}")
expect_failure("reject replay with missing digest"
  "${JOGGLE_LOCK_CONSUMER}" "${root}" "${lock}" "${leaf}" 2)
