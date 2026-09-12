if(NOT DEFINED OUT OR OUT STREQUAL "")
  message(FATAL_ERROR "set OUT to the model directory")
endif()

set(download_all OFF)
if(NOT DEFINED MODELS OR MODELS STREQUAL "")
  set(download_all ON)
  set(pending)
else()
  set(pending ${MODELS})
endif()
file(MAKE_DIRECTORY "${OUT}")

function(joggle_onnx_model)
  cmake_parse_arguments(
    MODEL "HEAVY"
    "NAME;SOURCE;SHA256;TEST;APP_SOURCE;APP_TOP;APP_SHA256" "ARGS" ${ARGN}
  )
  foreach(field NAME SOURCE SHA256)
    if("${MODEL_${field}}" STREQUAL "")
      message(FATAL_ERROR "invalid ONNX model declaration: missing ${field}")
    endif()
  endforeach()
  if(download_all)
    if(MODEL_HEAVY)
      return()
    endif()
  elseif(NOT MODEL_NAME IN_LIST pending)
    return()
  endif()

  list(REMOVE_ITEM pending "${MODEL_NAME}")
  set(pending "${pending}" PARENT_SCOPE)
  set(url
      "https://media.githubusercontent.com/media/onnx/models/${joggle_onnx_zoo_revision}/${MODEL_SOURCE}")
  set(output "${OUT}/${MODEL_NAME}.onnx")
  if(EXISTS "${output}")
    file(SHA256 "${output}" actual)
    if(NOT "${actual}" STREQUAL "${MODEL_SHA256}")
      message(FATAL_ERROR "existing ${MODEL_NAME} has the wrong SHA-256")
    endif()
    message(STATUS "Using pinned ${MODEL_NAME}")
  else()
    set(part "${output}.part")
    file(REMOVE "${part}")
    message(STATUS "Downloading ${MODEL_NAME}")
    file(
      DOWNLOAD "${url}" "${part}"
      EXPECTED_HASH "SHA256=${MODEL_SHA256}"
      SHOW_PROGRESS STATUS status TLS_VERIFY ON
    )
    list(GET status 0 code)
    list(GET status 1 message)
    if(NOT code EQUAL 0)
      file(REMOVE "${part}")
      message(FATAL_ERROR "${MODEL_NAME} download failed: ${message}")
    endif()
    file(RENAME "${part}" "${output}")
  endif()

  if(NOT DEFINED APP OR NOT APP OR "${MODEL_APP_SOURCE}" STREQUAL "")
    return()
  endif()
  set(app "${OUT}/app/${MODEL_NAME}")
  if(IS_DIRECTORY "${app}")
    message(STATUS "Using extracted ${MODEL_NAME} application")
    return()
  endif()
  set(archive "${OUT}/${MODEL_NAME}.tar.gz")
  set(part "${archive}.part")
  set(stage "${OUT}/.app-${MODEL_NAME}")
  file(REMOVE "${part}")
  file(
    DOWNLOAD
      "https://media.githubusercontent.com/media/onnx/models/${joggle_onnx_zoo_revision}/${MODEL_APP_SOURCE}"
      "${part}"
    EXPECTED_HASH "SHA256=${MODEL_APP_SHA256}"
    SHOW_PROGRESS STATUS status TLS_VERIFY ON
  )
  list(GET status 0 code)
  list(GET status 1 message)
  if(NOT code EQUAL 0)
    file(REMOVE "${part}")
    message(FATAL_ERROR "${MODEL_NAME} application download failed: ${message}")
  endif()
  file(RENAME "${part}" "${archive}")
  file(REMOVE_RECURSE "${stage}")
  file(MAKE_DIRECTORY "${stage}" "${OUT}/app")
  file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${stage}")
  if(NOT IS_DIRECTORY "${stage}/${MODEL_APP_TOP}")
    message(FATAL_ERROR
            "${MODEL_NAME} archive has no ${MODEL_APP_TOP} entry")
  endif()
  file(RENAME "${stage}/${MODEL_APP_TOP}" "${app}")
  file(REMOVE_RECURSE "${stage}")
  file(REMOVE "${archive}")
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/models.cmake")
if(pending)
  list(JOIN pending ", " unknown)
  message(FATAL_ERROR "unknown model: ${unknown}")
endif()
