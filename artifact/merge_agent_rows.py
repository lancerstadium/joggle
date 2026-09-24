#!/usr/bin/env python3
"""Assemble agent-provider CSVs into the Figure 4 release matrix."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import subprocess
import sys
import tempfile
from collections import Counter
from itertools import product
from statistics import mean
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_trajectory(path: Path, provider: dict, rows: list[dict[str, str]]) -> None:
    """Bind a primary measurement to its isolated final oracle and tool record."""
    if len(rows) != 1:
        raise SystemExit(f"{path}: expected one trajectory per provider file")
    row = rows[0]
    trajectory = Path(provider["trajectory"])
    if sha256(trajectory) != provider["trajectory_sha256"] or sha256(trajectory) != row["trajectory_sha256"]:
        raise SystemExit(f"{path}: trajectory digest differs")
    data = json.loads(trajectory.read_text())
    backend = data.get("provider", "ollama")
    from run_extension_agent import HOSTED_OPTIONS, HOSTED_ENDPOINT, HOSTED_CONTEXT, response_content, response_usage
    if (data.get("schema") != "extension-agent-trajectory/v1" or data.get("dirty")
            or data.get("identity_stable") is not True or data.get("infrastructure_error")
            or data.get("demonstrations") or data.get("think") is not False):
        raise SystemExit(f"{path}: trajectory violates the frozen execution protocol")
    if backend == "siliconflow":
        if (data.get("options") != HOSTED_OPTIONS or data.get("endpoint") != HOSTED_ENDPOINT
                or data.get("seed_applied") is not False
                or data.get("context_check") != {"policy": "full-history-provider-window", "window_tokens": HOSTED_CONTEXT}
                or data.get("context_policy") != {"history": "complete", "window_tokens": HOSTED_CONTEXT, "overflow": "provider-error"}
                or data["model"].get("revision_kind") != "hosted-alias"
                or data["model"].get("weight_revision") is not None
                or data["model"].get("catalog_entry", {}).get("id") != row["model"]):
            raise SystemExit(f"{path}: invalid hosted protocol or model identity")
        model_revision = "hosted-alias:" + row["model"]
    elif backend == "ollama":
        overflow = data.get("context_check", {}).get("overflow", {})
        if (data.get("options") != {"temperature": 0.0, "num_ctx": 32768}
                or data.get("context_check", {}).get("verified") is not True
                or overflow.get("type") != "exceed_context_size_error" or overflow.get("n_ctx") != 32768
                or type(overflow.get("n_prompt_tokens")) is not int or overflow["n_prompt_tokens"] <= 32768):
            raise SystemExit(f"{path}: missing native context-budget check")
        model_revision = data["model"]["digest"]
    else:
        raise SystemExit(f"{path}: unrecognized provider")
    for field in ("task", "system", "task_spec_sha256", "api_card_sha256"):
        if data[field] != row[field]:
            raise SystemExit(f"{path}: {field} differs from trajectory")
    if model_revision != row["model_revision"] or data["model"]["name"] != row["model"]:
        raise SystemExit(f"{path}: model identity differs")
    identity = hashlib.sha256(json.dumps(data["system_identity"], sort_keys=True).encode()).hexdigest()
    if identity != row["system_revision"]:
        raise SystemExit(f"{path}: native system identity differs")
    for field in ("seed", "run", "wall_ms"):
        if data[field] != int(row[field]):
            raise SystemExit(f"{path}: {field} differs from trajectory")
    responses = [event["response"] for event in data["events"] if event.get("response")]
    if backend == "siliconflow":
        used = 0
        for response in responses:
            try:
                _, completion = response_usage(response, row["model"], HOSTED_CONTEXT,
                                               min(4096, 32000-used), backend)
            except (ValueError, KeyError, TypeError) as failure:
                raise SystemExit(f"{path}: invalid hosted response: {failure}") from failure
            used += completion
    for field, usage in (("completion_tokens", "eval_count"), ("prompt_tokens", "prompt_eval_count")):
        count = sum(response["usage"][field] if backend == "siliconflow" else response[usage]
                    for response in responses)
        if count != int(row[field]):
            raise SystemExit(f"{path}: {field} differs from provider usage")
    calls = edits = 0
    for event in data["events"]:
        if not event.get("response"):
            continue
        try:
            action = json.loads(response_content(event["response"], backend))["action"]
        except (ValueError, TypeError, KeyError):
            continue
        if action in ("inspect", "edit", "test"):
            calls += 1
            if action == "edit" and "edited" in event.get("feedback", {}):
                edits += 1
    if calls != int(row["tool_calls"]) or edits != int(row["edit_attempts"]):
        raise SystemExit(f"{path}: tool counts differ from recorded actions")
    if sha256(trajectory.parent / "candidate.patch") != row["patch_sha256"]:
        raise SystemExit(f"{path}: patch digest differs")
    oracle_path = trajectory.parent / "final-oracle.json"
    if sha256(oracle_path) != data["final_oracle_sha256"]:
        raise SystemExit(f"{path}: final oracle digest differs")
    oracle = json.loads(oracle_path.read_text())
    if (oracle.get("complete_task") is not True
            or oracle.get("execution_isolation", {}).get("kind") != "macos-seatbelt"
            or oracle["task"] != row["task"] or oracle["system"] != row["system"]
            or oracle["task_spec_sha256"] != row["task_spec_sha256"]
            or (row["passed"] == "true") != oracle["passed"]):
        raise SystemExit(f"{path}: final oracle does not support the measured result")


def audited_assembly(path: Path) -> list[dict[str, str]]:
    """Revalidate a complete export before plotting, including its raw trajectories."""
    from validate_figure import extension, load
    root = Path(__file__).resolve().parent
    template = root / "templates/figure-04-extension.csv"
    rows = load(path, template)
    record = json.loads(path.with_suffix(".json").read_text())
    if (record.get("schema") != "extension-agent-assembly/v1" or
            record.get("output", {}).get("sha256") != sha256(path) or
            record.get("rows") != len(rows)):
        raise SystemExit(f"{path}: invalid assembly provenance")
    contributed = []
    for source in record.get("inputs", []):
        csv_path, record_path = Path(source["path"]), Path(source["record"])
        if source["sha256"] != sha256(csv_path) or source["record_sha256"] != sha256(record_path):
            raise SystemExit(f"{path}: source hashes differ")
        provider = json.loads(record_path.read_text())
        if (provider.get("schema") != "agent-provider/v1" or provider.get("dirty") or
                provider.get("release_eligible") is not True or
                provider.get("output_sha256") != sha256(csv_path)):
            raise SystemExit(f"{path}: invalid agent provider")
        source_rows = load(csv_path, template)
        validate_trajectory(csv_path, provider, source_rows)
        contributed.extend(source_rows)
    canonical = lambda items: Counter(json.dumps(row, sort_keys=True) for row in items)
    if canonical(contributed) != canonical(rows):
        raise SystemExit(f"{path}: assembled rows differ from source trajectories")
    with (root / "manifests/extension-tasks.csv").open(newline="") as stream:
        tasks = {row["task_id"]: row["family"] for row in csv.DictReader(stream)
                 if row["footprint"] == "true"}
    extension(rows, False, tasks, sha256(root / "manifests/extension-specs.json"))
    return rows


def summarize(rows: list[dict[str, str]]) -> list[dict]:
    """Describe each family without turning an absent successful cost into zero."""
    families = ("definition", "analysis", "rewrite", "conversion", "emission", "vertical")
    systems = ("Joggle", "MLIR", "xDSL")
    output = []
    for model in sorted({row["model"] for row in rows}):
        for system in systems:
            for family in families:
                group = [r for r in rows if (r["model"], r["system"], r["family"]) ==
                         (model, system, family)]
                if len(group) != 2 or len({r["task"] for r in group}) != 2:
                    raise ValueError("summary requires two distinct tasks per family and system")
                successes = [r for r in group if r["passed"] == "true"]
                output.append({"model": model, "system": system, "family": family,
                    "tasks": len(group), "successes": len(successes),
                    "success_percent": 100 * len(successes) / len(group),
                    "completion_tokens": mean(int(r["completion_tokens"]) for r in successes) if successes else None,
                    "tool_calls": mean(int(r["tool_calls"]) for r in successes) if successes else None})
    return output


def success_intervals(rows: list[dict[str, str]]) -> list[dict]:
    """Exact stratified task bootstrap: enumerate 4^6 paired resamples, no run resampling."""
    families = ("definition", "analysis", "rewrite", "conversion", "emission", "vertical")
    systems = ("Joggle", "MLIR", "xDSL")
    result = []
    for model in sorted({r["model"] for r in rows}):
        indexed = {(r["family"], r["task"], r["system"]): int(r["passed"] == "true")
                   for r in rows if r["model"] == model}
        pairs = [sorted({task for f, task, _ in indexed if f == family}) for family in families]
        if any(len(pair) != 2 for pair in pairs):
            raise ValueError("stratified bootstrap requires two tasks in each family")
        for system in systems:
            samples = []
            for choices in product(((0, 0), (0, 1), (1, 0), (1, 1)), repeat=6):
                total = sum(indexed[family, pair[i], system]
                            for family, pair, choice in zip(families, pairs, choices) for i in choice)
                samples.append(total / 12)
            samples.sort()
            def percentile(q):
                index = (len(samples) - 1) * q
                lower = int(index)
                upper = min(lower + 1, len(samples) - 1)
                return samples[lower] + (samples[upper] - samples[lower]) * (index - lower)
            result.append({"model": model, "system": system,
                "success_rate": sum(indexed[family, task, system]
                                    for family, pair in zip(families, pairs) for task in pair) / 12,
                "ci95_low": percentile(0.025), "ci95_high": percentile(0.975),
                "resamples": len(samples), "unit": "task within family; shared paired indices"})
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    record_path = args.output.with_suffix(".json")
    if args.output.exists() or record_path.exists():
        raise SystemExit("refusing to replace an existing output or record")

    root = Path(__file__).resolve().parent
    with (root / "templates/figure-04-extension.csv").open(
        newline="", encoding="utf-8"
    ) as stream:
        header = next(csv.reader(stream))
    rows: list[dict[str, str]] = []
    records = []
    for path in args.inputs:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames != header:
                raise SystemExit(f"{path}: columns differ from Figure 4 schema")
            source_rows = list(reader)
            rows.extend(source_rows)
        provider = path.with_suffix(".json")
        payload = json.loads(provider.read_text(encoding="utf-8"))
        if (payload.get("schema") != "agent-provider/v1"
                or payload.get("dirty")
                or payload.get("release_eligible") is not True
                or payload.get("output_sha256") != sha256(path)):
            raise SystemExit(f"{path}: invalid agent-provider record")
        validate_trajectory(path, payload, source_rows)
        records.append({
            "path": str(path.resolve()), "sha256": sha256(path),
            "record": str(provider.resolve()), "record_sha256": sha256(provider),
        })

    rows.sort(key=lambda row: (
        row["model"], row["task"], int(row["demo_count"]), row["system"],
        int(row["run"]), int(row["seed"]),
    ))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(
        prefix=f".{args.output.name}.", suffix=".tmp", dir=args.output.parent
    )
    os.close(handle)
    temporary_path = Path(temporary)
    try:
        with temporary_path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=header, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        subprocess.run(
            [sys.executable, str(root / "validate_figure.py"), "4", str(temporary_path)],
            check=True,
        )
        temporary_path.replace(args.output)
    finally:
        temporary_path.unlink(missing_ok=True)

    record = {
        "schema": "extension-agent-assembly/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "inputs": records,
        "output": {"path": str(args.output.resolve()), "sha256": sha256(args.output)},
        "rows": len(rows),
    }
    temporary_record = record_path.with_name(f".{record_path.name}.tmp")
    with temporary_record.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(record, indent=2, sort_keys=True) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary_record.replace(record_path)
    print(f"assembled {len(rows)} Figure 4 agent trajectories in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
