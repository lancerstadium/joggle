file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/modules/artifact.reactive")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/mods/reactive/module.jog"
  "${CMAKE_BINARY_DIR}/modules/artifact.reactive/module.jog"
  COPYONLY
)

add_executable(joggle-artifact-reactive reactive.cpp)
target_link_libraries(joggle-artifact-reactive PRIVATE Joggle::joggle)
target_compile_features(joggle-artifact-reactive PRIVATE cxx_std_20)
target_compile_definitions(
  joggle-artifact-reactive
  PRIVATE
    JOGGLE_ARTIFACT_PERSISTENT_PLANS=$<BOOL:${JOGGLE_EVALUATOR_PERSISTENT_PLANS}>
)
set_target_properties(joggle-artifact-reactive PROPERTIES CXX_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(joggle-artifact-reactive PRIVATE /W4)
else()
  target_compile_options(
    joggle-artifact-reactive PRIVATE -Wall -Wextra -Wpedantic
  )
endif()
