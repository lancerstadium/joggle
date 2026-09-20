include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require("module test requires TOOL, SOURCE_ROOT, BUILD_ROOT, TEST_ROOT" VARS TOOL SOURCE_ROOT BUILD_ROOT TEST_ROOT)

function(invoke expected)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(expected STREQUAL "ok" AND NOT result EQUAL 0)
    message(FATAL_ERROR
            "command failed (${result}): ${ARGN}\n${output}${error}")
  endif()
  if(expected STREQUAL "fail" AND result EQUAL 0)
    message(FATAL_ERROR "command unexpectedly succeeded: ${ARGN}\n${output}")
  endif()
  set(COMMAND_OUTPUT "${output}" PARENT_SCOPE)
  set(COMMAND_ERROR "${error}" PARENT_SCOPE)
endfunction()

joggle_workspace("${TEST_ROOT}")

# The CLI rename is breaking just like the declaration rename: the old command
# must fail instead of becoming a compatibility alias.
invoke(fail "${TOOL}" module list -M "${SOURCE_ROOT}")

set(fragment_root "${TEST_ROOT}.fragment-source")
file(REMOVE_RECURSE "${fragment_root}")

# Installation must be closed over declared module roots. A sibling beside the
# source is not an implicit dependency root: accepting it would produce an
# installation that stops loading when the source checkout moves away.
set(closure_root "${TEST_ROOT}.closure-source")
file(REMOVE_RECURSE "${closure_root}")
file(MAKE_DIRECTORY "${closure_root}/closure_dep"
                    "${closure_root}/closure_user")
file(WRITE "${closure_root}/closure_dep/module.jog"
     "mod closure_dep\nfn value() -> i32 { return 7 }\n")
file(WRITE "${closure_root}/closure_user/module.jog"
     "mod closure_user\nuse closure_dep\n"
     "fn value() -> i32 { return closure_dep.value() }\n")
invoke(fail "${TOOL}" mod install
       "${closure_root}/closure_user" "${TEST_ROOT}")
if(EXISTS "${TEST_ROOT}/closure_user")
  message(FATAL_ERROR "module with an implicit sibling dependency was installed")
endif()
invoke(ok "${TOOL}" mod install
       "${closure_root}/closure_dep" "${TEST_ROOT}")
invoke(ok "${TOOL}" mod install
       "${closure_root}/closure_user" "${TEST_ROOT}")
invoke(ok "${TOOL}" mod check closure_user -M "${TEST_ROOT}")
invoke(ok "${TOOL}" mod uninstall closure_user "${TEST_ROOT}")
invoke(ok "${TOOL}" mod uninstall closure_dep "${TEST_ROOT}")
file(REMOVE_RECURSE "${closure_root}")
file(MAKE_DIRECTORY "${fragment_root}/fragment_error/lib")
file(WRITE "${fragment_root}/fragment_error/module.jog"
     "mod fragment_error\nfn declared(x: i32) -> i32;\n")
file(WRITE "${fragment_root}/fragment_error/lib/broken.jog"
     "local fn valid(x: i32) -> i32 { return x }\n"
     "local fn broken( -> i32 { return 0 }\n")
invoke(fail "${TOOL}" mod check fragment_error -M "${fragment_root}")
set(fragment_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
if(NOT fragment_diagnostics MATCHES "broken.jog:2:")
  message(FATAL_ERROR
          "fragment diagnostic lost its source location:\n${fragment_diagnostics}")
endif()
if(fragment_diagnostics MATCHES "module.jog:[3-9][0-9]*:")
  message(FATAL_ERROR
          "fragment diagnostic was attributed to the entry source:\n${fragment_diagnostics}")
endif()
invoke(fail "${TOOL}" mod install
       "${fragment_root}/fragment_error" "${TEST_ROOT}"
       -M "${fragment_root}")
set(fragment_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
if(NOT fragment_diagnostics MATCHES "broken.jog:2:")
  message(FATAL_ERROR
          "install diagnostic lost its fragment location:\n${fragment_diagnostics}")
endif()
if(EXISTS "${TEST_ROOT}/fragment_error")
  message(FATAL_ERROR "invalid fragmented module was installed")
endif()
file(REMOVE_RECURSE "${fragment_root}")

foreach(action IN ITEMS check info)
  invoke(fail "${TOOL}" mod "${action}" "../base" -M "${SOURCE_ROOT}")
  set(name_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
  if(NOT name_diagnostics MATCHES "invalid module name")
    message(FATAL_ERROR
            "module ${action} inspected an invalid identity:\n${name_diagnostics}")
  endif()
endforeach()

set(graph_root "${TEST_ROOT}.module-graph")
file(REMOVE_RECURSE "${graph_root}")
foreach(module IN ITEMS graph.leaf graph.left graph.right graph.top
                        graph.first graph.second graph.ambiguous
                        graph.qualified graph.cycle_a graph.cycle_b)
  file(MAKE_DIRECTORY "${graph_root}/${module}")
endforeach()
file(WRITE "${graph_root}/graph.leaf/module.jog"
     "mod graph.leaf\n"
     "fn identity<T: Ty>(x: T) -> T { return x }\n")
file(WRITE "${graph_root}/graph.left/module.jog"
     "mod graph.left\nuse graph.leaf\n"
     "fn left(x: i32) -> i32 { return identity(x) }\n")
file(WRITE "${graph_root}/graph.right/module.jog"
     "mod graph.right\nuse graph.leaf\n"
     "fn right(x: i32) -> i32 { return identity(x) }\n")
file(WRITE "${graph_root}/graph.top/module.jog"
     "mod graph.top\nuse graph.left\nuse graph.right\n"
     "fn main(x: i32) -> i32 {\n"
     "  return identity(graph.left.left(graph.right.right(x)))\n}\n")
invoke(ok "${TOOL}" mod check graph.top -M "${graph_root}")

file(WRITE "${graph_root}/graph.first/module.jog"
     "mod graph.first\n"
     "fn choose(x: i32) -> i32 { return x }\n")
file(WRITE "${graph_root}/graph.second/module.jog"
     "mod graph.second\n"
     "fn choose(x: i32) -> i32 { return x + 1 }\n")
file(WRITE "${graph_root}/graph.ambiguous/module.jog"
     "mod graph.ambiguous\nuse graph.first\nuse graph.second\n"
     "fn main(x: i32) -> i32 { return choose(x) }\n")
invoke(fail "${TOOL}" mod check graph.ambiguous -M "${graph_root}")
set(graph_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
if(NOT graph_diagnostics MATCHES "call to 'choose' is ambiguous")
  message(FATAL_ERROR
          "module ambiguity was not diagnosed:\n${graph_diagnostics}")
endif()
file(WRITE "${graph_root}/graph.qualified/module.jog"
     "mod graph.qualified\nuse graph.first\nuse graph.second\n"
     "fn main(x: i32) -> i32 { return graph.second.choose(x) }\n")
invoke(ok "${TOOL}" mod check graph.qualified -M "${graph_root}")

file(WRITE "${graph_root}/graph.cycle_a/module.jog"
     "mod graph.cycle_a\nuse graph.cycle_b\nfn a() -> i32 { return 1 }\n")
file(WRITE "${graph_root}/graph.cycle_b/module.jog"
     "mod graph.cycle_b\nuse graph.cycle_a\nfn b() -> i32 { return 2 }\n")
invoke(fail "${TOOL}" mod check graph.cycle_a -M "${graph_root}")
set(graph_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
if(NOT graph_diagnostics MATCHES "module dependency cycle")
  message(FATAL_ERROR
          "module dependency cycle was not diagnosed:\n${graph_diagnostics}")
endif()
file(REMOVE_RECURSE "${graph_root}")

invoke(ok "${TOOL}" mod list -M "${SOURCE_ROOT}")
set(expected
    "base\nbounds\nc\nir\nmath\nmem\nnn\nonnx\nonnx.nn\nopt\nquant\nsat\nsat.c\nsat.vm\nstat\ntensor\ntflite\ntflite.nn\ntile\nvm\n")
if(NOT COMMAND_OUTPUT STREQUAL expected)
  message(FATAL_ERROR "mod list is not canonical:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" mod check nn -M "${SOURCE_ROOT}")
invoke(ok "${TOOL}" mod info tensor -M "${SOURCE_ROOT}")
# A module may be split across lib/ fragments. module.jog is always reported
# first and the fragments follow in sorted order, so the listing stays
# deterministic as the split changes.
if(NOT COMMAND_OUTPUT MATCHES
   "^mod tensor\npath .+\nuse base\nsource module.jog\n(source lib/[a-z_]+\\.jog\n)+fn tensor<E: Ty, S: list<int>>\\(\\) -> Ty;\n")
  message(FATAL_ERROR "unexpected mod info:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" mod info c -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn source\\(m: Mod\\) -> str;\n" OR
   NOT COMMAND_OUTPUT MATCHES "fn header\\(m: Mod\\) -> str;\n" OR
   COMMAND_OUTPUT MATCHES "fn (label|expr|block)\\(")
  message(FATAL_ERROR
          "mod info did not isolate the C module API:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" mod info bounds -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn infer\\(m: Mod\\) -> dict;\n" OR
   NOT COMMAND_OUTPUT MATCHES
       "fn get\\(known: dict, value: Val\\) -> list<int>;\n" OR
   NOT COMMAND_OUTPUT MATCHES
       "fn fits\\(known: dict, value: Val, type: Ty\\) -> bool;\n" OR
   NOT COMMAND_OUTPUT MATCHES "fn report\\(m: Mod\\) -> dict;\n" OR
   COMMAND_OUTPUT MATCHES "fn (limits|mul_value|result)\\(")
  message(FATAL_ERROR
          "mod info did not isolate the bounds API:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" mod info onnx.nn -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn infer\\(m: Mod\\) -> bool;\n" OR
   NOT COMMAND_OUTPUT MATCHES "fn convert\\(m: Mod\\) -> bool;\n" OR
   COMMAND_OUTPUT MATCHES "fn (conv_type|convert_conv|infer_once)\\(")
  message(FATAL_ERROR
          "mod info exposed ONNX bridge implementation:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" mod info tflite.nn -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn convert\\(m: Mod\\) -> bool;\n" OR
   COMMAND_OUTPUT MATCHES "fn (convert_conv|convert_binary|convert_tensor)\\(")
  message(FATAL_ERROR
          "mod info exposed TFLite bridge implementation:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" mod install "${BUILD_ROOT}/sample" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
if(NOT EXISTS "${TEST_ROOT}/sample/module.jog")
  message(FATAL_ERROR "installed module is missing")
endif()
invoke(fail "${TOOL}" mod install "${BUILD_ROOT}/sample" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
invoke(ok "${TOOL}" mod check sample -M "${TEST_ROOT}")
invoke(ok "${TOOL}" mod info sample -M "${TEST_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "native joggle_sample\\.(so|dylib|dll)\n")
  message(FATAL_ERROR "native library is not reported:\n${COMMAND_OUTPUT}")
endif()
if(NOT COMMAND_OUTPUT MATCHES
   "fn keep<T: Ty>\\(x: T\\) -> T;\n")
  message(FATAL_ERROR "module declarations are not reported:\n${COMMAND_OUTPUT}")
endif()

set(upgrade_source "${TEST_ROOT}.upgrade")
set(incompatible_source "${TEST_ROOT}.incompatible")
set(invalid_source "${TEST_ROOT}.invalid-upgrade")
set(dependent_source "${TEST_ROOT}.dependent")
set(provider_source "${TEST_ROOT}.provider")
set(facade_source "${TEST_ROOT}.facade")
set(facade_upgrade_source "${TEST_ROOT}.facade-upgrade")
set(client_source "${TEST_ROOT}.client")
set(upgrade_sibling_root "${TEST_ROOT}.upgrade-siblings")
set(hidden_upgrade_source "${upgrade_sibling_root}/sample")
set(hidden_dependency_source "${upgrade_sibling_root}/hidden_dependency")
file(REMOVE_RECURSE "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}"
                    "${provider_source}" "${facade_source}"
                    "${facade_upgrade_source}" "${client_source}"
                    "${upgrade_sibling_root}")
file(MAKE_DIRECTORY "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}"
                    "${provider_source}" "${facade_source}"
                    "${facade_upgrade_source}" "${client_source}"
                    "${hidden_upgrade_source}" "${hidden_dependency_source}")
file(COPY "${BUILD_ROOT}/sample/" DESTINATION "${upgrade_source}")
file(READ "${upgrade_source}/module.jog" upgrade_module)
string(REPLACE "fn keep<T: Ty>(x: T) -> T;"
               "fn keep<U: Ty>(x: U) -> U;"
               upgrade_module "${upgrade_module}")
file(WRITE "${upgrade_source}/module.jog"
     "${upgrade_module}\nfn added(x: i32) -> i32;\n")
invoke(ok "${TOOL}" mod upgrade "${upgrade_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
invoke(ok "${TOOL}" mod check sample -M "${TEST_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" upgraded_source)
if(NOT upgraded_source MATCHES "fn added\\(x: i32\\) -> i32;")
  message(FATAL_ERROR "compatible upgrade was not committed")
endif()

file(COPY "${upgrade_source}/" DESTINATION "${incompatible_source}")
file(READ "${incompatible_source}/module.jog" incompatible_module)
string(REPLACE "fn ping(x: i32) -> i32;"
               "fn ping(x: i64) -> i64;"
               incompatible_module "${incompatible_module}")
file(WRITE "${incompatible_source}/module.jog" "${incompatible_module}")
invoke(fail "${TOOL}" mod upgrade "${incompatible_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" retained_source)
if(NOT retained_source STREQUAL upgraded_source)
  message(FATAL_ERROR "failed upgrade changed the installed module")
endif()

file(COPY "${upgrade_source}/" DESTINATION "${invalid_source}")
file(READ "${invalid_source}/module.jog" invalid_module)
string(REPLACE "mod sample\n" "mod sample\nuse absent\n"
               invalid_module "${invalid_module}")
file(WRITE "${invalid_source}/module.jog" "${invalid_module}")
invoke(fail "${TOOL}" mod upgrade "${invalid_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" retained_source)
if(NOT retained_source STREQUAL upgraded_source)
  message(FATAL_ERROR "invalid staged upgrade changed the installed module")
endif()

# Upgrade validation obeys the same closure rule. Put the missing dependency
# beside the candidate to prove that its parent directory is not searched
# implicitly.
file(WRITE "${hidden_dependency_source}/module.jog"
     "mod hidden_dependency\nfn value() -> i32 { return 1 }\n")
file(COPY "${upgrade_source}/" DESTINATION "${hidden_upgrade_source}")
file(READ "${hidden_upgrade_source}/module.jog" hidden_upgrade)
string(REPLACE "mod sample\n"
               "mod sample\nuse hidden_dependency\n"
               hidden_upgrade "${hidden_upgrade}")
file(WRITE "${hidden_upgrade_source}/module.jog" "${hidden_upgrade}")
invoke(fail "${TOOL}" mod upgrade "${hidden_upgrade_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" retained_source)
if(NOT retained_source STREQUAL upgraded_source)
  message(FATAL_ERROR "hidden-dependency upgrade changed the installed module")
endif()

invoke(fail "${TOOL}" mod install "${BUILD_ROOT}/bad" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
if(EXISTS "${TEST_ROOT}/bad")
  message(FATAL_ERROR "invalid module was committed")
endif()

file(WRITE "${dependent_source}/module.jog"
     "mod dependent\nuse sample\nfn call(x: i32) -> i32 { return sample.ping(x) }\n")
invoke(ok "${TOOL}" mod install "${dependent_source}" "${TEST_ROOT}"
       -M "${TEST_ROOT}")

# Preserving every old declaration is not sufficient when a new overload
# silently retargets an installed dependent. Validate the reverse-dependency
# closure against the staged candidate before committing it.
file(WRITE "${provider_source}/module.jog"
     "mod provider\nfn choose<T: Ty>(x: T) -> T { return x }\n")
file(WRITE "${facade_source}/module.jog"
     "mod facade\nuse provider\nfn keep(x: i32) -> i32 { return x }\n")
file(WRITE "${facade_upgrade_source}/module.jog"
     "mod facade\nuse provider\n"
     "fn keep(x: i32) -> i32 { return x }\n"
     "fn choose(x: i32) -> i32 { return x + 1 }\n")
file(WRITE "${client_source}/module.jog"
     "mod client\nuse facade\n"
     "fn call(x: i32) -> i32 { return choose(x) }\n")
invoke(ok "${TOOL}" mod install "${provider_source}" "${TEST_ROOT}")
invoke(ok "${TOOL}" mod install "${facade_source}" "${TEST_ROOT}"
       -M "${TEST_ROOT}")
invoke(ok "${TOOL}" mod install "${client_source}" "${TEST_ROOT}"
       -M "${TEST_ROOT}")
invoke(fail "${TOOL}" mod upgrade "${facade_upgrade_source}"
       "${TEST_ROOT}" -M "${TEST_ROOT}")
if(NOT COMMAND_ERROR MATCHES
   "upgrade would break installed module 'client'")
  message(FATAL_ERROR
          "dependent-breaking upgrade lacked a precise diagnostic:\n${COMMAND_ERROR}")
endif()
file(READ "${TEST_ROOT}/facade/module.jog" retained_facade)
file(READ "${facade_source}/module.jog" expected_facade)
if(NOT retained_facade STREQUAL expected_facade)
  message(FATAL_ERROR "dependent-breaking upgrade changed the installed facade")
endif()
invoke(ok "${TOOL}" mod uninstall client "${TEST_ROOT}")
invoke(ok "${TOOL}" mod uninstall facade "${TEST_ROOT}")
invoke(ok "${TOOL}" mod uninstall provider "${TEST_ROOT}")

invoke(fail "${TOOL}" mod uninstall sample "${TEST_ROOT}")
if(NOT EXISTS "${TEST_ROOT}/sample/module.jog" OR
   NOT EXISTS "${TEST_ROOT}/dependent/module.jog")
  message(FATAL_ERROR "blocked uninstall changed the installed modules")
endif()
invoke(ok "${TOOL}" mod uninstall dependent "${TEST_ROOT}")
invoke(ok "${TOOL}" mod uninstall sample "${TEST_ROOT}")
if(EXISTS "${TEST_ROOT}/sample")
  message(FATAL_ERROR "uninstalled module remains")
endif()
invoke(fail "${TOOL}" mod uninstall sample "${TEST_ROOT}")

file(REMOVE_RECURSE "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}"
                    "${provider_source}" "${facade_source}"
                    "${facade_upgrade_source}" "${client_source}"
                    "${upgrade_sibling_root}")

file(GLOB residue "${TEST_ROOT}/*" "${TEST_ROOT}/.*")
foreach(path IN LISTS residue)
  get_filename_component(name "${path}" NAME)
  if(NOT name STREQUAL "." AND NOT name STREQUAL "..")
    message(FATAL_ERROR "module transaction left residue: ${path}")
  endif()
endforeach()
file(REMOVE_RECURSE "${TEST_ROOT}")
