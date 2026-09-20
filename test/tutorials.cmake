include("${CMAKE_CURRENT_LIST_DIR}/joggle_test.cmake")

joggle_require(
  "tutorial smoke test requires the CLI, C compiler, fixtures, and output root"
  VARS TOOL CC MODULES SOURCE_ROOT ROOT)
joggle_workspace("${ROOT}")

set(model "${SOURCE_ROOT}/test/data/matmul.jog")
set(c_model "${SOURCE_ROOT}/test/data/c.jog")
set(c_harness "${SOURCE_ROOT}/test/data/c_main.c")
set(tutorial_root "${SOURCE_ROOT}/test/tutorial")

# Reference-page programs: these files mirror the complete source printed in
# the nn, tensor, and C mod pages so documentation cannot drift silently.
foreach(reference classifier normalize scalar_model)
  joggle_run("${reference} documentation example did not check"
    COMMAND "${TOOL}" check "${tutorial_root}/${reference}.jog" -M "${MODULES}")
endforeach()
joggle_run("classifier documentation example remained untyped"
  COMMAND "${TOOL}" query opt.untyped
          "${tutorial_root}/classifier.jog" -M "${MODULES}"
  OUTPUT_VARIABLE classifier_untyped)
if(NOT classifier_untyped STREQUAL "[]\n")
  message(FATAL_ERROR
    "classifier documentation example has open types:\n${classifier_untyped}")
endif()
joggle_run("scalar C documentation example did not prepare"
  COMMAND "${TOOL}" run c.prepare mem.plan
          "${tutorial_root}/scalar_model.jog" -M "${MODULES}"
  OUTPUT_FILE "${ROOT}/scalar-planned.jog")
joggle_run("scalar C documentation frontier query failed"
  COMMAND "${TOOL}" query c.frontier "${ROOT}/scalar-planned.jog"
          -M "${MODULES}"
  OUTPUT_VARIABLE scalar_frontier)
if(NOT scalar_frontier STREQUAL "[]\n")
  message(FATAL_ERROR
    "scalar C documentation example has a frontier:\n${scalar_frontier}")
endif()

# Getting started and transform tutorial: validate, transform, and inspect the
# exact fixture named by the documentation.
joggle_run("tutorial input did not check"
  COMMAND "${TOOL}" check "${model}" -M "${MODULES}")
joggle_run("tutorial transform failed"
  COMMAND "${TOOL}" run opt.fold_add_zero "${model}" -M "${MODULES}"
  OUTPUT_FILE "${ROOT}/folded.jog")
joggle_expect("tutorial transform did not fold the identity"
  FILE "${ROOT}/folded.jog" MATCHES "return x")
joggle_run("tutorial type query failed"
  COMMAND "${TOOL}" query opt.untyped "${ROOT}/folded.jog" -M "${MODULES}"
  OUTPUT_VARIABLE untyped)
if(NOT untyped STREQUAL "[]\n")
  message(FATAL_ERROR "tutorial output contains untyped values:\n${untyped}")
endif()

# Module-authoring tutorial: load the package from a second root and verify the
# observable metadata edit, not merely a successful process exit.
joggle_run("tutorial mod did not check"
  COMMAND "${TOOL}" mod check choose_lut
          -M "${MODULES}" -M "${tutorial_root}")
joggle_run("tutorial mod did not run"
  COMMAND "${TOOL}" run choose_lut.apply
          "${tutorial_root}/demo.jog"
          -M "${MODULES}" -M "${tutorial_root}"
  OUTPUT_FILE "${ROOT}/selected.jog")
joggle_expect("tutorial mod did not record its selection"
  FILE "${ROOT}/selected.jog" MATCHES "implementation: \"lut\"")
joggle_run("tutorial mod produced invalid IR"
  COMMAND "${TOOL}" check "${ROOT}/selected.jog" -M "${MODULES}")

# C tutorial: preserve each inspectable stage, prove the capability frontier is
# empty, compile strict C11, and execute the same numerical harness as the page.
joggle_run("tutorial C preparation failed"
  COMMAND "${TOOL}" run c.prepare "${c_model}" -M "${MODULES}"
  OUTPUT_FILE "${ROOT}/prepared.jog")
joggle_run("tutorial storage planning failed"
  COMMAND "${TOOL}" run mem.plan "${ROOT}/prepared.jog" -M "${MODULES}"
  OUTPUT_FILE "${ROOT}/planned.jog")
joggle_run("tutorial C frontier query failed"
  COMMAND "${TOOL}" query c.frontier "${ROOT}/planned.jog" -M "${MODULES}"
  OUTPUT_VARIABLE frontier)
if(NOT frontier STREQUAL "[]\n")
  message(FATAL_ERROR "tutorial C frontier is not empty:\n${frontier}")
endif()
joggle_run("tutorial C header emission failed"
  COMMAND "${TOOL}" emit c.header "${ROOT}/planned.jog" -M "${MODULES}"
  OUTPUT_FILE "${ROOT}/model.h")
joggle_run("tutorial C source emission failed"
  COMMAND "${TOOL}" emit c.source "${ROOT}/planned.jog" -M "${MODULES}"
  OUTPUT_FILE "${ROOT}/model.c")
joggle_run("tutorial C artifact did not compile"
  COMMAND "${CC}" -std=c11 -O2 -Wall -Wextra -Werror -pedantic-errors
          -include "${ROOT}/model.h" "${ROOT}/model.c" "${c_harness}" -lm
          -o "${ROOT}/model")
joggle_run("tutorial C oracle failed" COMMAND "${ROOT}/model")
