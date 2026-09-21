#!/usr/bin/env python3
"""Run system-specific Figure 4 oracles over provider-neutral model outputs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Any

from prepare_extension_requests import load_config, resolve


PHASES = ("parsed", "typed", "built", "passed")


def fail(message: str) -> None:
    raise SystemExit(message)


def digest(value: str) -> str:
    return hashlib.sha256(value.encode()).hexdigest()


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    rows = []
    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                fail(f"{path}:{line_number}: invalid JSON: {error}")
            if not isinstance(value, dict):
                fail(f"{path}:{line_number}: expected an object")
            rows.append(value)
    return rows


def response_index(
    responses: list[dict[str, Any]], requests: dict[str, dict[str, Any]],
) -> dict[tuple[str, int], dict[str, Any]]:
    observed: dict[tuple[str, int], dict[str, Any]] = {}
    for line, row in enumerate(responses, start=1):
        required = {"request_id", "sample_index", "seed", "output", "output_sha256",
                    "context_tokens", "api_card_tokens"}
        if set(row) != required:
            fail(f"responses line {line}: fields differ from {sorted(required)}")
        request_id = row["request_id"]
        if request_id not in requests:
            fail(f"responses line {line}: unknown request_id {request_id}")
        index = row["sample_index"]
        if not isinstance(index, int) or not 0 <= index < 50:
            fail(f"responses line {line}: sample_index must lie in [0, 49]")
        request = requests[request_id]
        if row["seed"] != request["sample_seeds"][index]:
            fail(f"responses line {line}: seed differs from the request")
        if not isinstance(row["output"], str):
            fail(f"responses line {line}: output must be text")
        if digest(row["output"]) != row["output_sha256"]:
            fail(f"responses line {line}: output hash mismatch")
        for field in ("context_tokens", "api_card_tokens"):
            if not isinstance(row[field], int) or row[field] < 0:
                fail(f"responses line {line}: {field} must be non-negative")
        if row["api_card_tokens"] > request["api_card_budget_tokens"]:
            fail(f"responses line {line}: API card exceeds its token budget")
        key = (request_id, index)
        if key in observed:
            fail(f"responses line {line}: duplicate {key}")
        observed[key] = row
    expected = {(request_id, index) for request_id in requests for index in range(50)}
    if set(observed) != expected:
        missing = sorted(expected - set(observed))
        extra = sorted(set(observed) - expected)
        fail(f"response matrix differs; missing={missing[:3]} extra={extra[:3]}")
    return observed


def oracle(
    command: list[str], candidate: str, request: dict[str, Any], spec: Path,
    timeout: float, cwd: Path,
) -> dict[str, bool]:
    with tempfile.TemporaryDirectory(prefix="joggle-extension-oracle-") as temporary:
        work = Path(temporary)
        candidate_path = work / "candidate.txt"
        candidate_path.write_text(candidate, encoding="utf-8")
        replacements = {
            "{candidate}": str(candidate_path),
            "{task}": request["task"],
            "{spec}": str(spec.resolve()),
            "{work}": str(work),
        }
        argv = [replacements.get(part, part) for part in command]
        environment = {
            **os.environ,
            "JOGGLE_EXTENSION_CANDIDATE": str(candidate_path),
            "JOGGLE_EXTENSION_TASK": request["task"],
            "JOGGLE_EXTENSION_SYSTEM": request["system"],
            "JOGGLE_EXTENSION_SPEC": str(spec.resolve()),
            "JOGGLE_EXTENSION_WORK": str(work),
        }
        try:
            result = subprocess.run(
                argv, cwd=cwd, env=environment, capture_output=True, text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired as error:
            fail(f"oracle timed out for {request['request_id']}: {error}")
        if result.returncode:
            fail(
                f"oracle infrastructure failed for {request['request_id']} "
                f"({result.returncode}): {result.stderr[-2000:]}"
            )
        try:
            outcome = json.loads(result.stdout)
        except json.JSONDecodeError as error:
            fail(f"oracle returned invalid JSON for {request['request_id']}: {error}")
        if not isinstance(outcome, dict) or set(outcome) != set(PHASES):
            fail(f"oracle for {request['request_id']} must return exactly {list(PHASES)}")
        if not all(isinstance(outcome[field], bool) for field in PHASES):
            fail(f"oracle for {request['request_id']} returned non-boolean phases")
        values = [outcome[field] for field in PHASES]
        if values != sorted(values, reverse=True):
            fail(f"oracle for {request['request_id']} returned non-monotone phases")
        return outcome


def append(path: Path, row: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--requests", type=Path, required=True)
    parser.add_argument("--references", type=Path, required=True)
    parser.add_argument("--responses", type=Path, required=True)
    parser.add_argument("--spec", type=Path,
                        default=root / "manifests/extension-specs.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.timeout <= 0:
        fail("--timeout must be positive")
    if args.output.exists() and not args.resume:
        fail(f"refusing to replace {args.output}; pass --resume")

    config = load_config(args.config)
    base = args.config.resolve().parent
    systems = {item["id"]: item for item in config["systems"]}
    request_rows = read_jsonl(args.requests)
    requests: dict[str, dict[str, Any]] = {}
    for line, request in enumerate(request_rows, start=1):
        request_id = request.get("request_id")
        if not isinstance(request_id, str) or request_id in requests:
            fail(f"requests line {line}: missing or duplicate request_id")
        if digest(request.get("prompt", "")) != request.get("prompt_sha256"):
            fail(f"requests line {line}: prompt hash mismatch")
        requests[request_id] = request
    reference_rows = read_jsonl(args.references)
    references: dict[str, str] = {}
    for line, row in enumerate(reference_rows, start=1):
        if set(row) != {"request_id", "reference_sha256", "reference"}:
            fail(f"references line {line}: unexpected fields")
        request_id = row["request_id"]
        if request_id not in requests or request_id in references:
            fail(f"references line {line}: unknown or duplicate request_id")
        if digest(row["reference"]) != row["reference_sha256"]:
            fail(f"references line {line}: content hash mismatch")
        if row["reference_sha256"] != requests[request_id].get("reference_sha256"):
            fail(f"references line {line}: request hash mismatch")
        references[request_id] = row["reference"]
    if set(references) != set(requests):
        fail("private reference bundle differs from the request matrix")
    responses = response_index(read_jsonl(args.responses), requests)

    completed: dict[tuple[str, int], dict[str, Any]] = {}
    if args.output.exists():
        for line, row in enumerate(read_jsonl(args.output), start=1):
            if set(row) != {"request_id", "sample_index", "output_sha256", *PHASES}:
                fail(f"evaluation line {line}: unexpected fields")
            key = (row["request_id"], row["sample_index"])
            if key in completed:
                fail(f"evaluation line {line}: duplicate {key}")
            response = responses.get(key)
            if response is None or digest(response["output"]) != row["output_sha256"]:
                fail(f"evaluation line {line}: response changed")
            completed[key] = row
        print(f"resuming after {len(completed)} evaluated samples")

    verified_references: set[tuple[str, str]] = set()
    for request in request_rows:
        key = (request["system"], request["task"])
        if key in verified_references:
            continue
        system = systems[request["system"]]
        outcome = oracle(system["oracle"], references[request["request_id"]], request,
                         args.spec, args.timeout, base)
        if not outcome["passed"]:
            fail(f"reference solution failed its oracle: {request['request_id']}")
        verified_references.add(key)

    for request in request_rows:
        system = systems[request["system"]]
        for sample_index in range(50):
            key = (request["request_id"], sample_index)
            if key in completed:
                continue
            response = responses[key]
            outcome = oracle(system["oracle"], response["output"], request,
                             args.spec, args.timeout, base)
            row = {
                "request_id": request["request_id"],
                "sample_index": sample_index,
                "output_sha256": digest(response["output"]),
                **outcome,
            }
            append(args.output, row)
            completed[key] = row
    print(f"verified 72 reference solutions and evaluated {len(completed)} samples")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
