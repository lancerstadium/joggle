#!/usr/bin/env python3
"""Materialize the paired, provider-neutral request matrix for Figure 4."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
from typing import Any

from validate_extension_specs import validate_spec


SYSTEMS = ("Joggle", "MLIR", "xDSL")
DEMO_COUNTS = (0, 1, 2, 4)


def fail(message: str) -> None:
    raise SystemExit(message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def text(path: Path, label: str) -> str:
    if not path.is_file():
        fail(f"missing {label}: {path}")
    value = path.read_text(encoding="utf-8")
    if not value.strip():
        fail(f"empty {label}: {path}")
    return value.rstrip() + "\n"


def exact(value: dict[str, Any], fields: set[str], where: str) -> None:
    missing = fields - value.keys()
    extra = value.keys() - fields
    if missing or extra:
        fail(f"{where}: fields differ; missing={sorted(missing)} extra={sorted(extra)}")


def load_config(path: Path) -> dict[str, Any]:
    config = json.loads(path.read_text(encoding="utf-8"))
    exact(
        config,
        {"schema_version", "experiment_seed", "sample_count", "demo_counts",
         "models", "systems", "demonstrations"},
        "config",
    )
    if config["schema_version"] != 1:
        fail("config.schema_version must equal 1")
    if not isinstance(config["experiment_seed"], int) or config["experiment_seed"] < 0:
        fail("config.experiment_seed must be a non-negative integer")
    if config["sample_count"] != 50:
        fail("config.sample_count must equal 50")
    if tuple(config["demo_counts"]) != DEMO_COUNTS:
        fail("config.demo_counts must equal [0, 1, 2, 4]")
    models = config["models"]
    if not isinstance(models, list) or len(models) != 2:
        fail("config.models must contain exactly two models")
    model_ids: set[str] = set()
    for index, model in enumerate(models):
        exact(
            model,
            {"id", "revision", "temperature", "top_p", "max_new_tokens",
             "api_card_budget_tokens"},
            f"config.models[{index}]",
        )
        if not all(isinstance(model[field], str) and model[field].strip()
                   for field in ("id", "revision")):
            fail(f"config.models[{index}]: id and revision must be non-empty")
        if model["id"] in model_ids:
            fail(f"config.models[{index}]: duplicate model id")
        model_ids.add(model["id"])
        if not isinstance(model["temperature"], (int, float)) or not 0 < model["temperature"] <= 2:
            fail(f"config.models[{index}].temperature must lie in (0, 2]")
        if not isinstance(model["top_p"], (int, float)) or not 0 < model["top_p"] <= 1:
            fail(f"config.models[{index}].top_p must lie in (0, 1]")
        for field in ("max_new_tokens", "api_card_budget_tokens"):
            if not isinstance(model[field], int) or model[field] <= 0:
                fail(f"config.models[{index}].{field} must be positive")
    systems = config["systems"]
    if not isinstance(systems, list) or {item.get("id") for item in systems} != set(SYSTEMS):
        fail(f"config.systems must contain exactly {list(SYSTEMS)}")
    for index, system in enumerate(systems):
        exact(
            system,
            {"id", "revision", "api_card", "reference_dir", "demonstration_dir",
             "oracle"},
            f"config.systems[{index}]",
        )
        if not isinstance(system["revision"], str) or not system["revision"].strip():
            fail(f"config.systems[{index}].revision must be non-empty")
        if (not isinstance(system["oracle"], list) or not system["oracle"]
                or not all(isinstance(part, str) and part for part in system["oracle"])):
            fail(f"config.systems[{index}].oracle must be a non-empty argv array")
    demos = config["demonstrations"]
    if not isinstance(demos, list) or len(demos) < max(DEMO_COUNTS):
        fail("config.demonstrations must contain at least four entries")
    seen: set[str] = set()
    for index, demo in enumerate(demos):
        exact(demo, {"id", "title", "contract"}, f"config.demonstrations[{index}]")
        if not all(isinstance(demo[field], str) and demo[field].strip()
                   for field in ("id", "title", "contract")):
            fail(f"config.demonstrations[{index}] contains empty text")
        if demo["id"] in seen:
            fail(f"config.demonstrations[{index}]: duplicate id")
        seen.add(demo["id"])
    return config


def resolve(base: Path, value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else base / path


def order_demos(task_id: str, demos: list[dict[str, str]]) -> list[dict[str, str]]:
    return sorted(
        demos,
        key=lambda demo: digest(f"{task_id}\0{demo['id']}".encode()),
    )


def render_prompt(
    system: str, api_card: str, demos: list[tuple[dict[str, str], str]],
    task: dict[str, Any],
) -> str:
    blocks = [
        f"You are extending {system}. Return only the candidate source files in "
        "the exact envelope required by the API card. Do not explain the answer.\n",
        "## API card\n\n" + api_card,
    ]
    if demos:
        rendered = []
        for demo, solution in demos:
            rendered.append(
                f"### {demo['id']}: {demo['title']}\n\n"
                f"Contract: {demo['contract']}\n\n"
                f"Reference extension:\n{solution}"
            )
        blocks.append("## Demonstrations\n\n" + "\n\n".join(rendered))
    task_view = {
        "id": task["id"],
        "family": task["family"],
        "required_roles": task["required_roles"],
        "contract": task["contract"],
        "positive_cases": task["positive_cases"],
        "negative_cases": task["negative_cases"],
        "oracle": task["oracle"],
    }
    blocks.append(
        "## Held-out task\n\nImplement this task without changing its fixtures or oracle.\n\n"
        + json.dumps(task_view, indent=2, sort_keys=True)
        + "\n"
    )
    return "\n".join(blocks)


def atomic_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f".{path.name}.tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        for row in rows:
            stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parent
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--spec", type=Path,
                        default=root / "manifests/extension-specs.json")
    parser.add_argument("--task-index", type=Path,
                        default=root / "manifests/extension-tasks.csv")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--references-output", type=Path, required=True)
    args = parser.parse_args()
    for output in (args.output, args.references_output):
        if output.exists():
            fail(f"refusing to replace {output}")
    if args.output.resolve() == args.references_output.resolve():
        fail("requests and private references must use different files")

    import csv
    with args.task_index.open(newline="", encoding="utf-8") as stream:
        task_index = list(csv.DictReader(stream))
    spec_bytes = args.spec.read_bytes()
    spec = json.loads(spec_bytes)
    validate_spec(spec, task_index)
    config = load_config(args.config)
    base = args.config.resolve().parent
    systems = {item["id"]: item for item in config["systems"]}
    held_out = {task["id"] for task in spec["tasks"]}
    if held_out & {demo["id"] for demo in config["demonstrations"]}:
        fail("demonstration IDs overlap held-out task IDs")

    material: dict[str, dict[str, Any]] = {}
    for system_name in SYSTEMS:
        system = systems[system_name]
        card_path = resolve(base, system["api_card"])
        card = text(card_path, f"{system_name} API card")
        references = resolve(base, system["reference_dir"])
        demonstrations = resolve(base, system["demonstration_dir"])
        material[system_name] = {
            "card": card,
            "card_hash": digest(card.encode()),
            "references": references,
            "demonstrations": demonstrations,
        }
        for task in spec["tasks"]:
            text(references / f"{task['id']}.txt", f"{system_name} reference {task['id']}")
        for demo in config["demonstrations"]:
            text(demonstrations / f"{demo['id']}.txt",
                 f"{system_name} demonstration {demo['id']}")

    rows: list[dict[str, Any]] = []
    reference_rows: list[dict[str, str]] = []
    for model in config["models"]:
        for task in spec["tasks"]:
            ranked = order_demos(task["id"], config["demonstrations"])
            for demo_count in DEMO_COUNTS:
                chosen = ranked[:demo_count]
                demo_ids = [demo["id"] for demo in chosen]
                sample_seeds = [
                    int(digest(
                        (
                            f"{config['experiment_seed']}\0{model['id']}\0{task['id']}\0"
                            f"{demo_count}\0{index}"
                        ).encode()
                    )[:16], 16) & 0x7fffffff
                    for index in range(config["sample_count"])
                ]
                for system_name in SYSTEMS:
                    system = systems[system_name]
                    system_material = material[system_name]
                    demos = [
                        (demo, text(
                            system_material["demonstrations"] / f"{demo['id']}.txt",
                            f"{system_name} demonstration {demo['id']}",
                        ))
                        for demo in chosen
                    ]
                    prompt = render_prompt(system_name, system_material["card"], demos, task)
                    reference = text(
                        system_material["references"] / f"{task['id']}.txt",
                        f"{system_name} reference {task['id']}",
                    )
                    request_id = f"{model['id']}::{system_name}::{task['id']}::{demo_count}"
                    rows.append({
                        "schema_version": 1,
                        "request_id": request_id,
                        "model": model["id"],
                        "model_revision": model["revision"],
                        "system": system_name,
                        "system_revision": system["revision"],
                        "task": task["id"],
                        "family": task["family"],
                        "demo_count": demo_count,
                        "demo_ids": demo_ids,
                        "sample_seeds": sample_seeds,
                        "temperature": model["temperature"],
                        "top_p": model["top_p"],
                        "max_new_tokens": model["max_new_tokens"],
                        "api_card_budget_tokens": model["api_card_budget_tokens"],
                        "task_spec_sha256": digest(spec_bytes),
                        "api_card_sha256": system_material["card_hash"],
                        "prompt_sha256": digest(prompt.encode()),
                        "reference_sha256": digest(reference.encode()),
                        "api_card": system_material["card"],
                        "prompt": prompt,
                    })
                    reference_rows.append({
                        "request_id": request_id,
                        "reference_sha256": digest(reference.encode()),
                        "reference": reference,
                    })
    atomic_jsonl(args.output, rows)
    atomic_jsonl(args.references_output, reference_rows)
    expected = 2 * len(SYSTEMS) * 24 * len(DEMO_COUNTS)
    if len(rows) != expected:
        fail(f"internal matrix error: expected {expected} conditions, wrote {len(rows)}")
    print(f"wrote {len(rows)} paired extension conditions to {args.output}")
    print(f"wrote private reference bundle to {args.references_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
