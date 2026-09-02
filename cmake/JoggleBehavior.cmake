include(CMakeParseArguments)

function(joggle_add_behavior target)
  cmake_parse_arguments(JOGGLE_BEHAVIOR "" "MODULE" "SOURCES" ${ARGN})
  if(NOT JOGGLE_BEHAVIOR_MODULE OR NOT JOGGLE_BEHAVIOR_SOURCES)
    message(FATAL_ERROR
      "joggle_add_behavior needs MODULE and at least one source")
  endif()
  if(TARGET ${target})
    message(FATAL_ERROR "target '${target}' already exists")
  endif()

  get_filename_component(module "${JOGGLE_BEHAVIOR_MODULE}" ABSOLUTE
    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  set(identity_source
    "${CMAKE_CURRENT_BINARY_DIR}/${target}_module_identity.cpp")

  add_library(${target} SHARED ${JOGGLE_BEHAVIOR_SOURCES})
  add_custom_command(
    OUTPUT "${identity_source}"
    COMMAND ${CMAKE_COMMAND}
      "-DJOGGLE_CLI=$<TARGET_FILE:joggle::joggle_cli>"
      "-DJOGGLE_MODULE=${module}"
      "-DJOGGLE_OUTPUT=${identity_source}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateBehaviorIdentity.cmake"
    DEPENDS
      joggle::joggle_cli
      "${module}"
      ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/GenerateBehaviorIdentity.cmake
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
