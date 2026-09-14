# SPDX-License-Identifier: Apache-2.0

set(SAT_ENABLED 1 BOOL PARENT_SCOPE)
add_subdirectory(Runtime)

add_onnx_mlir_library(OMSatAccel
  SatAccelerator.cpp
  SatDialect.cpp

  EXCLUDE_FROM_OM_LIBS

  DEPENDS
  RuntimeSat

  INCLUDE_DIRS PUBLIC
  ${ONNX_MLIR_SRC_ROOT}
  ${ONNX_MLIR_SRC_ROOT}/include

  LINK_LIBS PUBLIC
  OMAccelerator
  OMKrnlOps
  OMMlirDialects
  OMONNXOps
  RuntimeSat
  MLIRFuncDialect
  MLIRIR
  MLIRPass
  MLIRTransformUtils
  )
