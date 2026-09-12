# Model compatibility study

This study records where each pinned conventional neural-network model reaches
the shared semantic representation. It separates structural compatibility from
generated-artifact execution and task-level accuracy.

## Subjects

[`test/models.cmake`](../test/models.cmake) is the authoritative subject list.
It pins the ONNX Model Zoo revision, source path, SHA-256, test name, and
expected structural frontier. The normal list contains fourteen models; BiDAF
is an additional heavy, non-vision gate. A result table contains only models
present in the configured artifact cache. An absent model is not reported as a
pass or failure.

## Structural stages

Each `onnx-zoo-record` test performs the same ordered checks:

1. decode ONNX bytes and verify/canonically round-trip the source calls;
2. infer result types and record the remaining unknown-result frontier;
3. when inference completes, convert supported source calls to shared
   semantics and record the remaining ONNX-call frontier;
4. when conversion completes, verify idempotence and canonically round-trip
   the semantic module.

A nonzero frontier is an expected negative result only when the pinned model
declaration specifies that exact count. A zero inference frontier no longer
terminates the test early: conversion must also execute. This distinction
prevents successful shape inference from being mislabeled as semantic
compatibility.

## Reproduction

From a configured ONNX build and pinned model cache:

```sh
python3 paper/collect_models.py \
  --build build-zoo \
  --output paper/data/model-frontier-pilot.csv
```

The collector first asks CTest for the exact configured
`onnx-zoo-record` set, runs that set, validates one schema-1 record per model,
and rejects inconsistent stage/frontier combinations. The current local pilot
contains eleven models: nine complete semantic conversion, TinyYOLOv3 stops at
type inference with 219 unknown results, and SSD-MobileNetV1 stops at semantic
conversion with 710 ONNX calls.

These records are structural regression and compatibility evidence. They do
not establish task accuracy, supported-operator percentage, generated-C
correctness, latency, or compatibility with models absent from the configured
cache. Those claims require the separate execution and performance protocols.
