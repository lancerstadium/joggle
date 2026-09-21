#!/usr/bin/env python3
"""Assemble validated Figure 4 observations from inference and oracle logs."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
from typing import Any


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
                row = json.loads(line)
            except json.JSONDecodeError as error:
                fail(f"{path}:{line_number}: invalid JSON: {error}")
            if not isinstance(row, dict):
                fail(f"{path}:{line_number}: expected an object")
            rows.append(row)
    return rows


def keyed(
    rows: list[dict[str, Any]], fields: tuple[str, ...], label: str,
) -> dict[tuple[Any, ...], dict[str, Any]]:
    result = {}
    for line, row in enumerate(rows, start=1):
        try:
            key = tuple(row[field] for field in fields)
        except KeyError as error:
            fail(f"{label} line {line}: missing {error.args[0]}")
        if key in result:
            fail(f"{label} line {line}: duplicate {key}")
        result[key] = row
    return result


def nonnegative_int(value: Any, where: str, *, positive: bool = False) -> int:
    if not isinstance(value, int) or value < (1 if positive else 0):
        fail(f"{where}: expected a {'positive' if positive else 'non-negative'} integer")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument("--requests", type=Path, required=True)
    parser.add_argument("--responses", type=Path, required=True)
    parser.add_argument("--scores", type=Path, required=True)
    parser.add_argument("--evaluations", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--template", type=Path,
                        default=root / "templates/figure-04-extension.csv")
    args = parser.parse_args()
    if args.output.exists():
        fail(f"refusing to replace {args.output}")

    requests = keyed(read_jsonl(args.requests), ("request_id",), "requests")
    responses = keyed(
        read_jsonl(args.responses), ("request_id", "sample_index"), "responses"
    )
    scores = keyed(read_jsonl(args.scores), ("request_id",), "scores")
    evaluations = keyed(
        read_jsonl(args.evaluations), ("request_id", "sample_index"), "evaluations"
    )
    if set(scores) != set(requests):
        fail("reference-score request IDs differ from the request matrix")
    expected_samples = {
        (request_id[0], index) for request_id in requests for index in range(50)
    }
    if set(responses) != expected_samples:
        fail("sample responses do not form 50 outputs per request")
    if set(evaluations) != expected_samples:
        fail("oracle evaluations do not form 50 outputs per request")

    with args.template.open(newline="", encoding="utf-8") as stream:
        header = next(csv.reader(stream))
    rows: list[dict[str, Any]] = []
    for (request_id,), request in requests.items():
        required_request = {
            "model", "model_revision", "system", "system_revision", "task", "family",
            "demo_count", "demo_ids", "sample_seeds", "temperature", "top_p",
            "max_new_tokens", "api_card_budget_tokens", "task_spec_sha256",
            "api_card_sha256", "prompt_sha256", "reference_sha256",
        }
        if not required_request <= request.keys():
            fail(f"request {request_id}: missing assembly metadata")
        score = scores[(request_id,)]
        if set(score) != {"request_id", "nll", "target_tokens", "context_tokens",
                          "api_card_tokens"}:
            fail(f"score {request_id}: unexpected fields")
        nll = score["nll"]
        if not isinstance(nll, (int, float)) or not math.isfinite(nll) or nll < 0:
            fail(f"score {request_id}: nll must be finite and non-negative")
        target_tokens = nonnegative_int(
            score["target_tokens"], f"score {request_id}.target_tokens", positive=True
        )
        context_tokens = nonnegative_int(
            score["context_tokens"], f"score {request_id}.context_tokens"
        )
        api_card_tokens = nonnegative_int(
            score["api_card_tokens"], f"score {request_id}.api_card_tokens"
        )
        if api_card_tokens > request["api_card_budget_tokens"]:
            fail(f"score {request_id}: API card exceeds its token budget")
        common = {
            "model": request["model"],
            "model_revision": request["model_revision"],
            "system": request["system"],
            "system_revision": request["system_revision"],
            "task": request["task"],
            "family": request["family"],
            "demo_count": request["demo_count"],
            "demo_ids": ";".join(request["demo_ids"]),
            "temperature": request["temperature"],
            "top_p": request["top_p"],
            "max_new_tokens": request["max_new_tokens"],
            "target_tokens": target_tokens,
            "context_tokens": context_tokens,
            "api_card_tokens": api_card_tokens,
            "api_card_budget_tokens": request["api_card_budget_tokens"],
            "task_spec_sha256": request["task_spec_sha256"],
            "api_card_sha256": request["api_card_sha256"],
            "prompt_sha256": request["prompt_sha256"],
        }
        reference = {name: "" for name in header}
        reference.update(common)
        reference.update(
            record_kind="reference", seed=request["sample_seeds"][0], nll=nll,
            output_sha256=request["reference_sha256"],
        )
        rows.append(reference)
        for sample_index in range(50):
            key = (request_id, sample_index)
            response = responses[key]
            evaluation = evaluations[key]
            required_response = {"request_id", "sample_index", "seed", "output",
                                 "output_sha256",
                                 "context_tokens", "api_card_tokens"}
            if set(response) != required_response:
                fail(f"response {key}: unexpected fields")
            if response["seed"] != request["sample_seeds"][sample_index]:
                fail(f"response {key}: seed differs from request")
            if response["context_tokens"] != context_tokens:
                fail(f"response {key}: context token count differs from reference score")
            if response["api_card_tokens"] != api_card_tokens:
                fail(f"response {key}: API-card token count differs from reference score")
            if set(evaluation) != {"request_id", "sample_index", "output_sha256", *PHASES}:
                fail(f"evaluation {key}: unexpected fields")
            output_hash = digest(response["output"])
            if response["output_sha256"] != output_hash:
                fail(f"response {key}: output hash mismatch")
            if evaluation["output_sha256"] != output_hash:
                fail(f"evaluation {key}: output hash differs from response")
            phase_values = [evaluation[field] for field in PHASES]
            if not all(isinstance(value, bool) for value in phase_values):
                fail(f"evaluation {key}: phase outcomes must be boolean")
            if phase_values != sorted(phase_values, reverse=True):
                fail(f"evaluation {key}: phase outcomes are non-monotone")
            sample = {name: "" for name in header}
            sample.update(common)
            sample.update(
                record_kind="sample", seed=response["seed"],
                sample_index=sample_index, output_sha256=output_hash,
                **{field: str(evaluation[field]).lower() for field in PHASES},
            )
            rows.append(sample)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_name(f".{args.output.name}.tmp")
    with temporary.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(args.output)
    print(f"wrote {len(rows)} Figure 4 rows to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
