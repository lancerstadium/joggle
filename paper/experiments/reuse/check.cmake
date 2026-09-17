if(NOT DEFINED EXPERIMENTS OR NOT DEFINED TEST_SCRIPT OR NOT DEFINED MODULES OR
   NOT DEFINED ROOT OR NOT DEFINED TOOL)
  message(FATAL_ERROR "derivation check requires EXPERIMENTS, TEST_SCRIPT, MODULES, ROOT, TOOL")
endif()

set(template "${EXPERIMENTS}/reuse/compiler.jog")
set(source "${MODULES}/mem/module.jog")
file(SHA256 "${source}" original_hash)
file(MAKE_DIRECTORY "${ROOT}/modules/planned")
set(derived "${ROOT}/modules/planned/module.jog")
execute_process(
  COMMAND "${TOOL}" run reuse.derive "${template}" -M "${EXPERIMENTS}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${derived}" ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "compiler derivation failed: ${error}")
endif()

# Changing the driver shape must fail, not silently retarget just one call.
file(READ "${template}" text)
string(REPLACE "mem.plan(m, fn)" "(mem.plan(m, fn) || mem.plan(m, fn))" invalid "${text}")
file(WRITE "${ROOT}/ambiguous.jog" "${invalid}")
execute_process(
  COMMAND "${TOOL}" run reuse.derive "${ROOT}/ambiguous.jog"
          -M "${EXPERIMENTS}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_VARIABLE rejected ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT rejected STREQUAL "" OR
   NOT error MATCHES "expected one planner call in driver")
  message(FATAL_ERROR "ambiguous derivation was not rejected: ${rejected}${error}")
endif()

# Reject decision changes instead of editing a merely similar comparison.
file(READ "${source}" text)
function(reject_change name old next diagnostic)
  string(REPLACE "${old}" "${next}" changed "${text}")
  if(changed STREQUAL text)
    message(FATAL_ERROR "missing source site for ${name}")
  endif()
  file(MAKE_DIRECTORY "${ROOT}/${name}/mem")
  file(WRITE "${ROOT}/${name}/mem/module.jog" "${changed}")
  execute_process(
    COMMAND "${TOOL}" run reuse.derive "${template}" -M "${ROOT}/${name}"
            -M "${EXPERIMENTS}" -M "${MODULES}"
    RESULT_VARIABLE result OUTPUT_VARIABLE rejected ERROR_VARIABLE error
  )
  if(result EQUAL 0 OR NOT rejected STREQUAL "" OR NOT error MATCHES "${diagnostic}")
    message(FATAL_ERROR "${name} was not rejected: ${rejected}${error}")
  endif()
endfunction()
reject_change(guard "if selected < 0 &&" "if selected <= 0 &&"
              "expected one slot-selection guard")
reject_change(disjunction "if selected < 0 &&" "if selected < 0 ||"
              "selection conjunction changed")
reject_change(lifetime "slot_ends[slot] < firsts[item]" "slot_ends[slot] <= firsts[item]"
              "slot eligibility changed")
reject_change(assignment "selected = slot\n" "selected = slot + 1\n"
              "slot assignment changed")
reject_change(capacity "slot_counts[selected] = counts[item]"
              "slot_counts[selected] = counts[item] + 1" "capacity update changed")

# Rename only source bindings, keeping the mem.counts metadata key untouched.
string(REPLACE "var counts: list<int>" "var sizes: list<int>" renamed "${text}")
string(REPLACE "counts += [count]" "sizes += [count]" renamed "${renamed}")
string(REPLACE "counts[item]" "sizes[item]" renamed "${renamed}")
file(MAKE_DIRECTORY "${ROOT}/renamed/mem" "${ROOT}/renamed/modules/planned")
file(WRITE "${ROOT}/renamed/mem/module.jog" "${renamed}")
execute_process(
  COMMAND "${TOOL}" run reuse.derive "${template}" -M "${ROOT}/renamed"
          -M "${EXPERIMENTS}" -M "${MODULES}"
  RESULT_VARIABLE result OUTPUT_FILE "${ROOT}/renamed/modules/planned/module.jog"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "binding rename broke derivation: ${error}")
endif()
file(SHA256 "${source}" after_hash)
if(NOT original_hash STREQUAL after_hash)
  message(FATAL_ERROR "derivation changed the installed memory module")
endif()

# The same execution oracle checks both original and renamed source bodies.
function(check_execution planner_modules upstream destination)
  set(PLANNER planned.apply)
  set(MODULES "${planner_modules};${upstream};${MODULES};${EXPERIMENTS}")
  set(ROOT "${destination}")
  include("${TEST_SCRIPT}")
  file(READ "${ROOT}/planned.jog" output)
  string(FIND "${output}" "[mem.counts: [4, 16], mem.types: [\"f32\", \"f32\"]]\nfn selection" selection)
  if(selection EQUAL -1)
    message(FATAL_ERROR "derived planner did not change the selection fixture")
  endif()
endfunction()
check_execution("${ROOT}/modules" "${MODULES}" "${ROOT}/execution")
check_execution("${ROOT}/renamed/modules" "${ROOT}/renamed" "${ROOT}/renamed/execution")
