if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED MODULES OR
   NOT DEFINED EXAMPLES OR NOT DEFINED HARNESS OR NOT DEFINED INPUT OR
   NOT DEFINED REFERENCE OR NOT DEFINED CC OR NOT DEFINED OUT)
  message(FATAL_ERROR
          "locality pilot requires TOOL, MODEL, MODULES, EXAMPLES, HARNESS, "
          "INPUT, REFERENCE, CC, and OUT")
endif()

if(NOT DEFINED WARMUP)
  set(WARMUP 3)
endif()
if(NOT DEFINED REPEATS)
  set(REPEATS 10)
endif()

file(MAKE_DIRECTORY "${OUT}")
set(instanced "${OUT}/instanced.jog")
set(canonical "${OUT}/canonical.jog")
set(reordered "${OUT}/reordered.jog")
set(baseline_plan "${OUT}/baseline-plan.jog")
set(candidate_plan "${OUT}/candidate-plan.jog")
set(baseline "${OUT}/baseline.jog")
set(candidate "${OUT}/candidate.jog")
set(baseline_c "${OUT}/baseline.c")
set(candidate_c "${OUT}/candidate.c")
set(header "${OUT}/model-blob.h")
set(candidate_header "${OUT}/candidate.h")
set(data "${OUT}/weights.bin")
set(candidate_data "${OUT}/candidate.bin")
set(baseline_program "${OUT}/baseline")
set(candidate_program "${OUT}/candidate")

execute_process(
  COMMAND "${TOOL}" run opt.instantiate "${MODEL}"
          --arg "\"nn\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${instanced}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "semantic instantiation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare "${instanced}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${canonical}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "canonical C preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run locality.apply "${canonical}"
          -M "${EXAMPLES}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${reordered}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "locality policy failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${canonical}" "${reordered}"
  RESULT_VARIABLE unchanged
)
if(unchanged EQUAL 0)
  message(FATAL_ERROR "locality policy did not change the prepared model")
endif()

foreach(variant IN ITEMS baseline candidate)
  if(variant STREQUAL "baseline")
    set(source_ir "${canonical}")
    set(planned_ir "${baseline_plan}")
    set(placed_ir "${baseline}")
  else()
    set(source_ir "${reordered}")
    set(planned_ir "${candidate_plan}")
    set(placed_ir "${candidate}")
  endif()
  execute_process(
    COMMAND "${TOOL}" run mem.plan "${source_ir}" -M "${MODULES}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${planned_ir}"
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${variant} memory planning failed (${result}):\n${error}")
  endif()
  execute_process(
    COMMAND "${TOOL}" run c.place "${planned_ir}"
            --arg "\"static\"" -M "${MODULES}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${placed_ir}"
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${variant} C placement failed (${result}):\n${error}")
  endif()
endforeach()

execute_process(
  COMMAND "${TOOL}" emit c.source "${baseline}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${baseline_c}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "baseline C emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.source "${candidate}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${candidate_c}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "candidate C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.header "${baseline}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "baseline header emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.header "${candidate}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${candidate_header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "candidate header emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${header}" "${candidate_header}"
  RESULT_VARIABLE different_header
)
if(NOT different_header EQUAL 0)
  message(FATAL_ERROR "locality policy changed the public C interface")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.data "${baseline}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${data}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "baseline payload emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${TOOL}" emit c.data "${candidate}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${candidate_data}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "candidate payload emission failed (${result}):\n${error}")
endif()
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E compare_files "${data}" "${candidate_data}"
  RESULT_VARIABLE different_data
)
if(NOT different_data EQUAL 0)
  message(FATAL_ERROR "locality policy changed the external weight payload")
endif()

foreach(variant IN ITEMS baseline candidate)
  if(variant STREQUAL "baseline")
    set(source "${baseline_c}")
    set(program "${baseline_program}")
  else()
    set(source "${candidate_c}")
    set(program "${candidate_program}")
  endif()
  execute_process(
    COMMAND "${CC}" -std=c11 -O3 -DNDEBUG -Wall -Wextra
            -Wstrict-prototypes -Werror "-I${OUT}"
            "${source}" "${HARNESS}" -lm -o "${program}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
            "${variant} strict C compilation failed (${result}):\n${output}${error}")
  endif()
endforeach()

function(measure variant process)
  if(variant STREQUAL "baseline")
    set(program "${baseline_program}")
  else()
    set(program "${candidate_program}")
  endif()
  execute_process(
    COMMAND "${program}" "${INPUT}" "${data}" "${REFERENCE}"
            "${WARMUP}" "${REPEATS}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${OUT}/${variant}-${process}.csv"
    ERROR_FILE "${OUT}/${variant}-${process}.err"
  )
  if(NOT result EQUAL 0)
    file(READ "${OUT}/${variant}-${process}.err" error)
    message(FATAL_ERROR
            "${variant} process ${process} failed (${result}):\n${error}")
  endif()
endfunction()

# Reverse the order in the second pair to reduce one simple order bias.
measure(baseline a)
measure(candidate a)
measure(candidate b)
measure(baseline b)

file(SIZE "${baseline_c}" baseline_bytes)
file(SIZE "${candidate_c}" candidate_bytes)
file(SIZE "${data}" data_bytes)
file(SHA256 "${data}" data_sha256)
file(STRINGS "${canonical}" conv_functions REGEX "^local fn conv2d")
list(LENGTH conv_functions conv_count)
message(STATUS "conv_functions=${conv_count}")
message(STATUS "baseline_c_bytes=${baseline_bytes}")
message(STATUS "candidate_c_bytes=${candidate_bytes}")
message(STATUS "data_bytes=${data_bytes}")
message(STATUS "data_sha256=${data_sha256}")
message(STATUS "raw measurements are in ${OUT}")
