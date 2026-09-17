#!/usr/bin/env python3
"""Create a deterministic input and LiteRT oracle for one TFLite model."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import ai_edge_litert
import numpy as np
from ai_edge_litert.interpreter import Interpreter


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("input", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("record", type=Path)
    args = parser.parse_args()

    interpreter = Interpreter(model_path=str(args.model), num_threads=1)
    interpreter.allocate_tensors()
    inputs = interpreter.get_input_details()
    outputs = interpreter.get_output_details()
    if len(inputs) != 1 or len(outputs) != 1:
        parser.error("fixture requires one input and one output")
    if inputs[0]["dtype"] != np.float32 or outputs[0]["dtype"] != np.float32:
        parser.error("fixture requires f32 input and output")
    shape = tuple(int(value) for value in inputs[0]["shape"])
    if not shape or any(value <= 0 for value in shape):
        parser.error("fixture requires a static positive input shape")

    count = int(np.prod(shape))
    indices = np.arange(count, dtype=np.int32)
    value = ((indices % 257) - 128).astype(np.float32) / np.float32(128.0)
    value = value.reshape(shape)
    interpreter.set_tensor(inputs[0]["index"], value)
    interpreter.invoke()
    result = np.asarray(
        interpreter.get_tensor(outputs[0]["index"]), dtype=np.float32
    )
    if not np.all(np.isfinite(result)):
        parser.error("LiteRT produced a nonfinite reference")

    args.input.parent.mkdir(parents=True, exist_ok=True)
    args.reference.parent.mkdir(parents=True, exist_ok=True)
    args.record.parent.mkdir(parents=True, exist_ok=True)
    value.tofile(args.input)
    result.tofile(args.reference)
    record = {
        "schema": 1,
        "litert": ai_edge_litert.__version__,
        "model_sha256": digest(args.model),
        "input": {
            "shape": list(shape),
            "bytes": args.input.stat().st_size,
            "sha256": digest(args.input),
        },
        "reference": {
            "shape": [int(value) for value in result.shape],
            "bytes": args.reference.stat().st_size,
            "sha256": digest(args.reference),
        },
    }
    args.record.write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
