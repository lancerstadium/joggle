include(CMakeParseArguments)

function(joggle_module target)
  cmake_parse_arguments(JOGGLE_MODULE "" "SOURCE" "NATIVE" ${ARGN})
  if(JOGGLE_MODULE_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR
      "joggle_module received unknown arguments: ${JOGGLE_MODULE_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT JOGGLE_MODULE_SOURCE OR NOT JOGGLE_MODULE_NATIVE)
    message(FATAL_ERROR
      "joggle_module needs SOURCE and at least one NATIVE source")
  endif()
  if(TARGET ${target})
    message(FATAL_ERROR "target '${target}' already exists")
  endif()

  get_filename_component(module "${JOGGLE_MODULE_SOURCE}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(identity_source
    "${CMAKE_CURRENT_BINARY_DIR}/${target}_module_identity.cpp")

  add_library(${target} SHARED ${JOGGLE_MODULE_NATIVE})
  add_custom_command(
    OUTPUT "${identity_source}"
    COMMAND ${CMAKE_COMMAND}
      "-DJOGGLE_CLI=$<TARGET_FILE:joggle::joggle_cli>"
      "-DJOGGLE_MODULE=${module}"
      "-DJOGGLE_OUTPUT=${identity_source}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateNativeIdentity.cmake"
    DEPENDS
      joggle::joggle_cli
      "${module}"
      ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateNativeIdentity.cmake
    VERBATIM
  )
  target_sources(${target} PRIVATE "${identity_source}")
  set_source_files_properties("${identity_source}" PROPERTIES GENERATED TRUE)
  target_link_libraries(${target} PRIVATE joggle::joggle)
  set_target_properties(${target} PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN TRUE
  )
endfunction()
