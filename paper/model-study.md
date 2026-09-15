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
`onnx-zoo-record` set, runs that set, validates one schema-1 runtime record per
model, and rejects inconsistent stage/frontier combinations. Its schema-2 CSV
rows bind every result to the Joggle Git revision and model SHA-256, so rows
from different compiler revisions cannot be silently merged. Collection also
rejects modified tracked compiler, module, tool, or test sources; paper edits
do not invalidate an otherwise identical compiler build.

The ledger contains fourteen models and may preserve rows measured at different
revisions; the revision column makes that distinction explicit. A partial-cache
rerun at revision `5e15c29` completes semantic conversion with zero source calls
for MobileNetV2, TinyYOLOv3, EfficientNet-Lite4 INT8, and XCiT-Tiny. The older
SSD-MobileNetV1 row remains a 386-call negative result until that pinned model
is rerun. These are structural compatibility observations only; a model still
requires the separate artifact-execution gate before it can be reported as
generated-code support.

These records are structural regression and compatibility evidence. They do
not establish task accuracy, supported-operator percentage, generated-C
correctness, latency, or compatibility with models absent from the configured
cache. Those claims require the separate execution and performance protocols.
