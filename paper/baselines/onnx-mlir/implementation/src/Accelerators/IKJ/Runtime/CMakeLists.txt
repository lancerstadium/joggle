# SPDX-License-Identifier: Apache-2.0

add_onnx_mlir_library(RuntimeIKJ STATIC
  RuntimeIKJ.c

  EXCLUDE_FROM_OM_LIBS
  )

set_target_properties(RuntimeIKJ PROPERTIES
  LANGUAGE C
  POSITION_INDEPENDENT_CODE TRUE
  )
