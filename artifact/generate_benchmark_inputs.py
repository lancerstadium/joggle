#!/usr/bin/env python3
"""Materialize byte-identical operator and model benchmark inputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path
from typing import Any


MASK64 = (1 << 64) - 1
GOLDEN = 0x9E3779B97F4A7C15
DTYPE_FORMAT = {
    "float32": "<f",
    "uint8": "<B",
    "int8": "<b",
    "int32": "<i",
}


class SplitMix64:
    def __init__(self, seed: int) -> None:
        self.state = seed & MASK64

    def next(self) -> int:
        self.state = (self.state + GOLDEN) & MASK64
        value = self.state
        value = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        value = ((value ^ (value >> 27)) * 0x94D049BB133111EB) & MASK64
        return value ^ (value >> 31)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sample(spec: dict[str, Any]) -> bytes:
    count = math.prod(spec["shape"])
    dtype = spec["dtype"]
    try:
        pack = struct.Struct(DTYPE_FORMAT[dtype]).pack
    except KeyError as error:
        raise SystemExit(f"unsupported dtype {dtype}") from error
    if "values" in spec:
        values = spec["values"]
    else:
        random = SplitMix64(spec["seed"])
        low = spec["low"]
        high = spec["high"]
        if spec["distribution"] == "uniform":
            scale = float(high) - float(low)
            values = [float(low) + scale * ((random.next() >> 40) / (1 << 24))
                      for _ in range(count)]
        elif spec["distribution"] == "uniform-integer":
            width = int(high) - int(low) + 1
            values = [int(low) + random.next() % width for _ in range(count)]
        else:
            raise SystemExit(f"unsupported distribution {spec['distribution']}")
    try:
        return b"".join(pack(value) for value in values)
    except (OverflowError, struct.error) as error:
        raise SystemExit(f"value cannot be encoded as {dtype}") from error


def write_once(path: Path, data: bytes) -> None:
    if path.exists():
        if path.read_bytes() != data:
            raise SystemExit(f"refusing to replace non-matching {path}")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def materialize_case(
    output: Path, group: str, case: dict[str, Any], spec_hash: str
) -> dict[str, Any]:
    tensors: list[tuple[str, str, dict[str, Any]]] = [
        ("input", value["name"], value) for value in case["inputs"]
    ]
    tensors.extend(
        ("initializer", name, value)
        for name, value in sorted(case.get("initializers", {}).items())
    )
    records = []
    digest = hashlib.sha256()
    for index, (kind, name, tensor) in enumerate(tensors):
        data = sample(tensor)
        relative = Path(group) / case["id"] / f"{index:03d}-{kind}.bin"
        write_once(output / relative, data)
        record = {
            "kind": kind,
            "name": name,
            "dtype": tensor["dtype"],
            "shape": tensor["shape"],
            "bytes": len(data),
            "sha256": sha256(data),
            "path": relative.as_posix(),
        }
        encoded = json.dumps(record, sort_keys=True, separators=(",", ":")).encode()
        digest.update(len(encoded).to_bytes(8, "little"))
        digest.update(encoded)
        records.append(record)
    return {
        "id": case["id"],
        "group": group,
        "spec_sha256": spec_hash,
        "input_digest": digest.hexdigest(),
        "tensors": records,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument("--spec", type=Path,
                        default=root / "manifests" / "benchmark-cases.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    spec_bytes = args.spec.read_bytes()
    spec = json.loads(spec_bytes)
    spec_hash = sha256(spec_bytes)
    records = [
        materialize_case(args.output, "operators", case, spec_hash)
        for case in spec["operator_cases"]
    ]
    records.extend(
        materialize_case(args.output, "models", case, spec_hash)
        for case in spec["model_cases"]
    )
    index = {
        "schema_version": 1,
        "spec_sha256": spec_hash,
        "cases": records,
    }
    encoded = (json.dumps(index, indent=2, sort_keys=True) + "\n").encode()
    write_once(args.output / "index.json", encoded)
    print(
        f"materialized {len(records)} cases and "
        f"{sum(len(record['tensors']) for record in records)} tensors in {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
