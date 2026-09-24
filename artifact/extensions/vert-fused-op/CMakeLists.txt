cmake_minimum_required(VERSION 3.20)
project(StudyQconv LANGUAGES C CXX)
find_package(MLIR REQUIRED CONFIG)
list(APPEND CMAKE_MODULE_PATH "${MLIR_CMAKE_DIR}" "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(AddMLIR)
set(LLVM_REQUIRES_EH ON)
add_llvm_library(StudyQconv MODULE BUILDTREE_ONLY
  plugin.cpp reference.cpp
  PLUGIN_TOOL mlir-opt)
# Use the host's exported MLIR/LLVM symbols, including its pass registry.
# Statically linking MLIRPass here creates a second registry on macOS.
if(APPLE)
  target_link_options(StudyQconv PRIVATE "-undefined" "dynamic_lookup")
endif()
target_compile_features(StudyQconv PRIVATE cxx_std_17)
target_include_directories(StudyQconv SYSTEM PRIVATE
  ${MLIR_INCLUDE_DIRS} ${LLVM_INCLUDE_DIRS})
separate_arguments(STUDY_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(StudyQconv PRIVATE ${STUDY_LLVM_DEFINITIONS})
install(TARGETS StudyQconv LIBRARY DESTINATION lib)
