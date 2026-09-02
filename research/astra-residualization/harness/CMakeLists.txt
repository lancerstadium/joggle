cmake_minimum_required(VERSION 3.20)

project(joggle_residual_diagnostics LANGUAGES CXX)

add_executable(residual_oracle
  src/artifact_encoding.cpp
  src/baseline.cpp
  src/branch_oracle.cpp
  src/branch_table.cpp
  src/conditioned.cpp
  src/decision_diagram.cpp
  src/exploration.cpp
  src/main.cpp
  src/materializer.cpp
  src/oracle.cpp
  src/transition.cpp
)
target_compile_features(residual_oracle PRIVATE cxx_std_20)
target_include_directories(residual_oracle PRIVATE include)

if(MSVC)
  target_compile_options(residual_oracle PRIVATE /W4 /WX)
else()
  target_compile_options(residual_oracle PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
  )
endif()

enable_testing()
add_test(NAME f1_oracle COMMAND residual_oracle --self-test)
