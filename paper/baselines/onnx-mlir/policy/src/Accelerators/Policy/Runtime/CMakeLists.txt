# SPDX-License-Identifier: Apache-2.0

add_onnx_mlir_library(RuntimePolicy STATIC
  RuntimePolicy.c

  EXCLUDE_FROM_OM_LIBS
  )

set_target_properties(RuntimePolicy PROPERTIES
  LANGUAGE C
  POSITION_INDEPENDENT_CODE TRUE
  )
