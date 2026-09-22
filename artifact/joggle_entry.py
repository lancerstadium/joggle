"""Shared fixed-shape entry specialization for Joggle artifact collectors."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any


JOGGLE_DTYPES = {
    "bool": "bool",
    "float32": "f32",
    "float64": "f64",
    "int8": "i8",
    "int16": "i16",
    "int32": "i32",
    "int64": "i64",
    "uint8": "u8",
    "uint16": "u16",
    "uint32": "u32",
    "uint64": "u64",
}


def concrete_entry_types(inputs: list[dict[str, Any]]) -> list[str]:
    """Translate the pinned workload inputs into concrete Joggle tensor types."""
    types = []
    for tensor in inputs:
        dtype = tensor.get("dtype")
        shape = tensor.get("shape")
        if dtype not in JOGGLE_DTYPES:
            raise ValueError(f"unsupported Joggle entry dtype: {dtype!r}")
        if not isinstance(shape, list) or any(
            not isinstance(extent, int) or isinstance(extent, bool) or extent < 0
            for extent in shape
        ):
            raise ValueError(f"entry input {tensor.get('name')!r} has no concrete shape")
        dimensions = ", ".join(str(extent) for extent in shape)
        types.append(f"tensor<{JOGGLE_DTYPES[dtype]}, [{dimensions}]>")
    return types


def signature_command(
    joggle: Path, current: Path, modules: Path, inputs: list[dict[str, Any]],
) -> list[str | Path]:
    """Build the one canonical command that fixes a model entry signature."""
    return [
        joggle, "run", "opt.signature", current,
        "--arg", json.dumps("main"),
        "--arg", json.dumps(concrete_entry_types(inputs)),
        "-M", modules,
    ]
