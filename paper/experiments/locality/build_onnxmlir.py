#!/usr/bin/env python3
"""Build the pinned ONNX-MLIR -O3 library for every campaign model.

Mirrors the recipe already recorded for ultraface-rfb-320: one CLI invocation
per model at the optimizer's -O3 route, then a sidecar naming the compiler and
LLVM revisions, the flag, the library hash, and the PyRuntime directory.
"""
import hashlib, json, os, platform, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
STUDY = REPO / "build-study/locality-matrix"
CLI = Path("/Users/lancer/Documents/Item/joggle-study/onnx-mlir/build-ikj/Release/bin/onnx-mlir")
RUNTIME = "/Users/lancer/Documents/Item/joggle-study/onnx-mlir/build-ikj/Release/lib"
MODELS = ["mnist-8", "mobilenetv2-7", "resnet18-v1-7", "squeezenet1.1-7",
          "squeezenet1.0-13-qdq", "ultraface-rfb-320", "tinyyolov2-8",
          "shufflenet-v2-12", "densenet-12", "googlenet-12"]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


rows = []
for name in MODELS:
    root = STUDY / name
    model = REPO / ".cache/onnx-zoo" / f"{name}.onnx"
    library = root / "onnxmlir_O3.so"
    log = root / "onnxmlir-build.log"
    if not model.exists():
        rows.append((name, "absent model", None))
        continue
    start = time.time()
    with log.open("w") as stream:
        result = subprocess.run([str(CLI), "-O3", "-o", str(root / "onnxmlir_O3"),
                                 str(model)], stdout=stream, stderr=subprocess.STDOUT)
    elapsed = time.time() - start
    if result.returncode or not library.exists():
        rows.append((name, f"compiler exit {result.returncode}", None))
        continue
    record = {
        "model": str(model.relative_to(REPO)),
        "model_sha256": sha256(model),
        "onnxmlir": "0.4.2",
        "revision": "4a13c34aa695b228599d637cdb772c19b4b18dba",
        "llvm": "1053047a4be7d1fece3adaf5e7597f838058c947",
        "optimization": "-O3",
        "cli_default_optimization": "-O0",
        "opset_converted": False,
        "build_and_link_seconds": elapsed,
        "library": str(library.relative_to(REPO)),
        "library_sha256": sha256(library),
        "runtime_dir": RUNTIME,
        "host": platform.platform(),
    }
    (root / "onnxmlir.json").write_text(json.dumps(record, indent=1) + "\n")
    rows.append((name, "ok", elapsed))

for name, status, elapsed in rows:
    print(f"{name:24s} {status:20s} " + (f"{elapsed:.2f}s" if elapsed else ""))
