if(NOT DEFINED TOOL OR NOT DEFINED MODEL OR NOT DEFINED MODULES OR
   NOT DEFINED OUT)
  message(FATAL_ERROR "instance pilot requires TOOL, MODEL, MODULES, and OUT")
endif()

file(MAKE_DIRECTORY "${OUT}")
set(input "${MODEL}")
if(DEFINED ENTRY_TYPES AND NOT ENTRY_TYPES STREQUAL "")
  if(NOT DEFINED ENTRY OR ENTRY STREQUAL "")
    message(FATAL_ERROR "ENTRY_TYPES requires ENTRY")
  endif()
  set(root "${OUT}/root.jog")
  execute_process(
    COMMAND "${TOOL}" run opt.instantiate "${MODEL}"
            --arg "\"${ENTRY}\"" --arg "${ENTRY_TYPES}" -M "${MODULES}"
    RESULT_VARIABLE result
    OUTPUT_FILE "${root}"
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "entry instantiation failed (${result}):\n${error}")
  endif()
  set(input "${root}")
endif()

set(instanced "${OUT}/instanced.jog")
set(prepared "${OUT}/prepared.jog")
set(planned "${OUT}/planned.jog")
set(placed "${OUT}/placed.jog")
set(source "${OUT}/model.c")
set(header "${OUT}/model.h")
set(data "${OUT}/weights.bin")

execute_process(
  COMMAND "${TOOL}" run opt.instantiate "${input}"
          --arg "\"nn\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${instanced}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "automatic instantiation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.prepare "${instanced}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${prepared}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "instanced C preparation failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run mem.plan "${prepared}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${planned}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "instance memory planning failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" run c.place "${planned}"
          --arg "\"static\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${placed}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "instance placement failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.data "${placed}" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${data}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "instance data emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.source "${placed}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${source}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "instanced C emission failed (${result}):\n${error}")
endif()

execute_process(
  COMMAND "${TOOL}" emit c.header "${placed}"
          --arg "\"weights\"" -M "${MODULES}"
  RESULT_VARIABLE result
  OUTPUT_FILE "${header}"
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "instanced C header emission failed (${result}):\n${error}")
endif()

file(READ "${instanced}" text)
string(REGEX MATCHALL "opt\\.instance:" instances "${text}")
list(LENGTH instances instance_count)
file(SIZE "${instanced}" ir_bytes)
file(SIZE "${source}" source_bytes)
file(SIZE "${header}" header_bytes)
file(SIZE "${data}" data_bytes)
message(STATUS "instances=${instance_count}")
message(STATUS "ir_bytes=${ir_bytes}")
message(STATUS "source_bytes=${source_bytes}")
message(STATUS "header_bytes=${header_bytes}")
message(STATUS "data_bytes=${data_bytes}")
