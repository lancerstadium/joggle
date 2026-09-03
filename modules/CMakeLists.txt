set(JOGGLE_BUILD_MODULES "" CACHE STRING
  "Semicolon-separated native Modules to build")

file(GLOB joggle_module_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*/module.joggle")
list(SORT joggle_module_sources)

set(joggle_native_modules)
foreach(joggle_module_source IN LISTS joggle_module_sources)
  get_filename_component(joggle_module_dir
    "${joggle_module_source}" DIRECTORY)
  get_filename_component(joggle_module_name
    "${joggle_module_dir}" NAME)

  if(NOT joggle_module_name MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR
      "Module directory '${joggle_module_name}' is not snake_case")
  endif()

  file(STRINGS "${joggle_module_source}" joggle_module_declaration
    REGEX "^[ \t]*module[ \t]+${joggle_module_name}@"
    LIMIT_COUNT 1)
  if(NOT joggle_module_declaration)
    message(FATAL_ERROR
      "${joggle_module_source} must declare Module '${joggle_module_name}'")
  endif()

  if(EXISTS "${joggle_module_dir}/CMakeLists.txt")
    list(APPEND joggle_native_modules "${joggle_module_name}")
  endif()
endforeach()

set_property(CACHE JOGGLE_BUILD_MODULES PROPERTY STRINGS
  ${joggle_native_modules})

foreach(joggle_module_name IN LISTS JOGGLE_BUILD_MODULES)
  list(FIND joggle_native_modules "${joggle_module_name}"
    joggle_module_index)
  if(joggle_module_index EQUAL -1)
    list(JOIN joggle_native_modules ", " joggle_native_module_names)
    message(FATAL_ERROR
      "Unknown native Module '${joggle_module_name}'. Available Modules: "
      "${joggle_native_module_names}")
  endif()
  string(TOUPPER "${joggle_module_name}" joggle_module_key)
  set("JOGGLE_MODULE_${joggle_module_key}_ENABLED" ON)
endforeach()

foreach(joggle_module_source IN LISTS joggle_module_sources)
  get_filename_component(joggle_module_dir
    "${joggle_module_source}" DIRECTORY)
  get_filename_component(joggle_module_name
    "${joggle_module_dir}" NAME)
  string(TOUPPER "${joggle_module_name}" joggle_module_key)

  set(joggle_install_module ON)
  if(EXISTS "${joggle_module_dir}/CMakeLists.txt")
    if(JOGGLE_MODULE_${joggle_module_key}_ENABLED)
      add_subdirectory("${joggle_module_name}")
    else()
      set(joggle_install_module OFF)
    endif()
  endif()

  if(joggle_install_module)
    install(FILES "${joggle_module_source}"
      DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules
      RENAME "${joggle_module_name}.joggle")
  endif()
endforeach()

install(FILES README.md
  DESTINATION ${CMAKE_INSTALL_DATADIR}/joggle/modules)
