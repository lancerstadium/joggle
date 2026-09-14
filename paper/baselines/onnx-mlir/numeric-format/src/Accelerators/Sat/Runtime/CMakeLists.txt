# SPDX-License-Identifier: Apache-2.0

add_onnx_mlir_library(RuntimeSat STATIC
  RuntimeSat.c
  EXCLUDE_FROM_OM_LIBS
  )

set_target_properties(RuntimeSat PROPERTIES
  LANGUAGE C
  POSITION_INDEPENDENT_CODE TRUE
  )
