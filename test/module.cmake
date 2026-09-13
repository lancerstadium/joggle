if(NOT DEFINED TOOL OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED BUILD_ROOT OR
   NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR
          "module test requires TOOL, SOURCE_ROOT, BUILD_ROOT, TEST_ROOT")
endif()

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

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

set(fragment_root "${TEST_ROOT}.fragment-source")
file(REMOVE_RECURSE "${fragment_root}")
file(MAKE_DIRECTORY "${fragment_root}/fragment_error/lib")
file(WRITE "${fragment_root}/fragment_error/module.jog"
     "module fragment_error\nfn declared(x: i32) -> i32;\n")
file(WRITE "${fragment_root}/fragment_error/lib/broken.jog"
     "local fn valid(x: i32) -> i32 { return x }\n"
     "local fn broken( -> i32 { return 0 }\n")
invoke(fail "${TOOL}" module check fragment_error -M "${fragment_root}")
set(fragment_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
if(NOT fragment_diagnostics MATCHES "broken.jog:2:")
  message(FATAL_ERROR
          "fragment diagnostic lost its source location:\n${fragment_diagnostics}")
endif()
if(fragment_diagnostics MATCHES "module.jog:[3-9][0-9]*:")
  message(FATAL_ERROR
          "fragment diagnostic was attributed to the entry source:\n${fragment_diagnostics}")
endif()
invoke(fail "${TOOL}" module install
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

set(graph_root "${TEST_ROOT}.module-graph")
file(REMOVE_RECURSE "${graph_root}")
foreach(module IN ITEMS graph.leaf graph.left graph.right graph.top
                        graph.first graph.second graph.ambiguous
                        graph.qualified graph.cycle_a graph.cycle_b)
  file(MAKE_DIRECTORY "${graph_root}/${module}")
endforeach()
file(WRITE "${graph_root}/graph.leaf/module.jog"
     "module graph.leaf\n"
     "fn identity<T: Ty>(x: T) -> T { return x }\n")
file(WRITE "${graph_root}/graph.left/module.jog"
     "module graph.left\nuse graph.leaf\n"
     "fn left(x: i32) -> i32 { return identity(x) }\n")
file(WRITE "${graph_root}/graph.right/module.jog"
     "module graph.right\nuse graph.leaf\n"
     "fn right(x: i32) -> i32 { return identity(x) }\n")
file(WRITE "${graph_root}/graph.top/module.jog"
     "module graph.top\nuse graph.left\nuse graph.right\n"
     "fn main(x: i32) -> i32 {\n"
     "  return identity(graph.left.left(graph.right.right(x)))\n}\n")
invoke(ok "${TOOL}" module check graph.top -M "${graph_root}")

file(WRITE "${graph_root}/graph.first/module.jog"
     "module graph.first\n"
     "fn choose(x: i32) -> i32 { return x }\n")
file(WRITE "${graph_root}/graph.second/module.jog"
     "module graph.second\n"
     "fn choose(x: i32) -> i32 { return x + 1 }\n")
file(WRITE "${graph_root}/graph.ambiguous/module.jog"
     "module graph.ambiguous\nuse graph.first\nuse graph.second\n"
     "fn main(x: i32) -> i32 { return choose(x) }\n")
invoke(fail "${TOOL}" module check graph.ambiguous -M "${graph_root}")
set(graph_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
if(NOT graph_diagnostics MATCHES "call to 'choose' is ambiguous")
  message(FATAL_ERROR
          "module ambiguity was not diagnosed:\n${graph_diagnostics}")
endif()
file(WRITE "${graph_root}/graph.qualified/module.jog"
     "module graph.qualified\nuse graph.first\nuse graph.second\n"
     "fn main(x: i32) -> i32 { return graph.second.choose(x) }\n")
invoke(ok "${TOOL}" module check graph.qualified -M "${graph_root}")

file(WRITE "${graph_root}/graph.cycle_a/module.jog"
     "module graph.cycle_a\nuse graph.cycle_b\nfn a() -> i32 { return 1 }\n")
file(WRITE "${graph_root}/graph.cycle_b/module.jog"
     "module graph.cycle_b\nuse graph.cycle_a\nfn b() -> i32 { return 2 }\n")
invoke(fail "${TOOL}" module check graph.cycle_a -M "${graph_root}")
set(graph_diagnostics "${COMMAND_OUTPUT}${COMMAND_ERROR}")
if(NOT graph_diagnostics MATCHES "module dependency cycle")
  message(FATAL_ERROR
          "module dependency cycle was not diagnosed:\n${graph_diagnostics}")
endif()
file(REMOVE_RECURSE "${graph_root}")

invoke(ok "${TOOL}" module list -M "${SOURCE_ROOT}")
set(expected
    "base\nbounds\nc\nir\nmath\nmem\nnn\nonnx\nonnx.nn\nopt\nquant\nsat\nsat.c\nsat.vm\nstat\ntensor\ntflite\ntflite.nn\ntile\nvm\n")
if(NOT COMMAND_OUTPUT STREQUAL expected)
  message(FATAL_ERROR "module list is not canonical:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module check nn -M "${SOURCE_ROOT}")
invoke(ok "${TOOL}" module info tensor -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES
   "^module tensor\npath .+\nuse base\nsource module.jog\nfn tensor<E: Ty, S: list<int>>\\(\\) -> Ty;\n")
  message(FATAL_ERROR "unexpected module info:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module info c -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn source\\(m: Mod\\) -> str;\n" OR
   NOT COMMAND_OUTPUT MATCHES "fn header\\(m: Mod\\) -> str;\n" OR
   COMMAND_OUTPUT MATCHES "fn (label|expr|block)\\(")
  message(FATAL_ERROR
          "module info did not isolate the C module API:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module info bounds -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn infer\\(m: Mod\\) -> dict;\n" OR
   NOT COMMAND_OUTPUT MATCHES
       "fn get\\(known: dict, value: Val\\) -> list<int>;\n" OR
   NOT COMMAND_OUTPUT MATCHES
       "fn fits\\(known: dict, value: Val, type: Ty\\) -> bool;\n" OR
   NOT COMMAND_OUTPUT MATCHES "fn report\\(m: Mod\\) -> dict;\n" OR
   COMMAND_OUTPUT MATCHES "fn (limits|mul_value|result)\\(")
  message(FATAL_ERROR
          "module info did not isolate the bounds API:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module info onnx.nn -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn infer\\(m: Mod\\) -> bool;\n" OR
   NOT COMMAND_OUTPUT MATCHES "fn convert\\(m: Mod\\) -> bool;\n" OR
   COMMAND_OUTPUT MATCHES "fn (conv_type|convert_conv|infer_once)\\(")
  message(FATAL_ERROR
          "module info exposed ONNX bridge implementation:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module info tflite.nn -M "${SOURCE_ROOT}")
if(NOT COMMAND_OUTPUT MATCHES "fn convert\\(m: Mod\\) -> bool;\n" OR
   COMMAND_OUTPUT MATCHES "fn (convert_conv|convert_binary|convert_tensor)\\(")
  message(FATAL_ERROR
          "module info exposed TFLite bridge implementation:\n${COMMAND_OUTPUT}")
endif()

invoke(ok "${TOOL}" module install "${BUILD_ROOT}/sample" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
if(NOT EXISTS "${TEST_ROOT}/sample/module.jog")
  message(FATAL_ERROR "installed module is missing")
endif()
invoke(fail "${TOOL}" module install "${BUILD_ROOT}/sample" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
invoke(ok "${TOOL}" module check sample -M "${TEST_ROOT}")
invoke(ok "${TOOL}" module info sample -M "${TEST_ROOT}")
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
file(REMOVE_RECURSE "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}")
file(MAKE_DIRECTORY "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}")
file(COPY "${BUILD_ROOT}/sample/" DESTINATION "${upgrade_source}")
file(READ "${upgrade_source}/module.jog" upgrade_module)
string(REPLACE "fn keep<T: Ty>(x: T) -> T;"
               "fn keep<U: Ty>(x: U) -> U;"
               upgrade_module "${upgrade_module}")
file(WRITE "${upgrade_source}/module.jog"
     "${upgrade_module}\nfn added(x: i32) -> i32;\n")
invoke(ok "${TOOL}" module upgrade "${upgrade_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
invoke(ok "${TOOL}" module check sample -M "${TEST_ROOT}")
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
invoke(fail "${TOOL}" module upgrade "${incompatible_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" retained_source)
if(NOT retained_source STREQUAL upgraded_source)
  message(FATAL_ERROR "failed upgrade changed the installed module")
endif()

file(COPY "${upgrade_source}/" DESTINATION "${invalid_source}")
file(READ "${invalid_source}/module.jog" invalid_module)
string(REPLACE "module sample\n" "module sample\nuse absent\n"
               invalid_module "${invalid_module}")
file(WRITE "${invalid_source}/module.jog" "${invalid_module}")
invoke(fail "${TOOL}" module upgrade "${invalid_source}" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
file(READ "${TEST_ROOT}/sample/module.jog" retained_source)
if(NOT retained_source STREQUAL upgraded_source)
  message(FATAL_ERROR "invalid staged upgrade changed the installed module")
endif()

invoke(fail "${TOOL}" module install "${BUILD_ROOT}/bad" "${TEST_ROOT}"
       -M "${BUILD_ROOT}")
if(EXISTS "${TEST_ROOT}/bad")
  message(FATAL_ERROR "invalid module was committed")
endif()

file(WRITE "${dependent_source}/module.jog"
     "module dependent\nuse sample\nfn call(x: i32) -> i32 { return sample.ping(x) }\n")
invoke(ok "${TOOL}" module install "${dependent_source}" "${TEST_ROOT}"
       -M "${TEST_ROOT}")
invoke(fail "${TOOL}" module uninstall sample "${TEST_ROOT}")
if(NOT EXISTS "${TEST_ROOT}/sample/module.jog" OR
   NOT EXISTS "${TEST_ROOT}/dependent/module.jog")
  message(FATAL_ERROR "blocked uninstall changed the installed modules")
endif()
invoke(ok "${TOOL}" module uninstall dependent "${TEST_ROOT}")
invoke(ok "${TOOL}" module uninstall sample "${TEST_ROOT}")
if(EXISTS "${TEST_ROOT}/sample")
  message(FATAL_ERROR "uninstalled module remains")
endif()
invoke(fail "${TOOL}" module uninstall sample "${TEST_ROOT}")

file(REMOVE_RECURSE "${upgrade_source}" "${incompatible_source}"
                    "${invalid_source}" "${dependent_source}")

file(GLOB residue "${TEST_ROOT}/*" "${TEST_ROOT}/.*")
foreach(path IN LISTS residue)
  get_filename_component(name "${path}" NAME)
  if(NOT name STREQUAL "." AND NOT name STREQUAL "..")
    message(FATAL_ERROR "module transaction left residue: ${path}")
  endif()
endforeach()
file(REMOVE_RECURSE "${TEST_ROOT}")
