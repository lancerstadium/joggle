#!/usr/bin/env python3
"""Generate and score Figure 4 completions with a pinned Transformers model."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
from typing import Any

from prepare_extension_requests import load_config


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


def append(path: Path, row: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps(row, sort_keys=True, separators=(",", ":")) + "\n")
        stream.flush()
        os.fsync(stream.fileno())


def load_backend(model_id: str, revision: str, dtype: str, device_map: str):
    try:
        import torch
        from transformers import AutoModelForCausalLM, AutoTokenizer
    except ImportError as error:
        fail("torch and transformers are required for model inference")
    torch_dtype = "auto" if dtype == "auto" else getattr(torch, dtype)
    tokenizer = AutoTokenizer.from_pretrained(
        model_id, revision=revision, trust_remote_code=False,
    )
    model = AutoModelForCausalLM.from_pretrained(
        model_id, revision=revision, torch_dtype=torch_dtype, device_map=device_map,
        trust_remote_code=False,
    )
    model.eval()
    if tokenizer.eos_token_id is None:
        fail(f"model {model_id} has no EOS token")
    if tokenizer.pad_token_id is None:
        tokenizer.pad_token_id = tokenizer.eos_token_id
    return torch, tokenizer, model


def token_ids(tokenizer, value: str, *, special: bool) -> list[int]:
    result = tokenizer(value, add_special_tokens=special, return_attention_mask=False)
    ids = result["input_ids"]
    if not isinstance(ids, list) or (ids and isinstance(ids[0], list)):
        fail("tokenizer returned an unexpected input shape")
    return ids


def model_device(model):
    try:
        return next(model.parameters()).device
    except StopIteration:
        fail("model has no parameters")


def validate_requests(
    rows: list[dict[str, Any]], model_id: str, revision: str,
) -> list[dict[str, Any]]:
    selected = [row for row in rows if row.get("model") == model_id]
    if len(selected) != 3 * 24 * 4:
        fail(f"expected 288 conditions for {model_id}; found {len(selected)}")
    ids: set[str] = set()
    for line, row in enumerate(selected, start=1):
        request_id = row.get("request_id")
        if not isinstance(request_id, str) or request_id in ids:
            fail(f"request {line}: missing or duplicate request_id")
        ids.add(request_id)
        if row.get("model_revision") != revision:
            fail(f"request {request_id}: model revision differs from config")
        if digest(row.get("prompt", "")) != row.get("prompt_sha256"):
            fail(f"request {request_id}: prompt hash mismatch")
        seeds = row.get("sample_seeds")
        if not isinstance(seeds, list) or len(seeds) != 50 or len(set(seeds)) != 50:
            fail(f"request {request_id}: expected 50 unique seeds")
    return selected


def generate(args: argparse.Namespace, model_config: dict[str, Any], requests, backend) -> None:
    torch, tokenizer, model = backend
    completed: dict[tuple[str, int], str] = {}
    if args.output.exists():
        if not args.resume:
            fail(f"refusing to replace {args.output}; pass --resume")
        for line, row in enumerate(read_jsonl(args.output), start=1):
            key = (row.get("request_id"), row.get("sample_index"))
            if key in completed or not isinstance(row.get("output"), str):
                fail(f"output line {line}: duplicate key or invalid output")
            if digest(row["output"]) != row.get("output_sha256"):
                fail(f"output line {line}: output hash mismatch")
            completed[key] = row["output_sha256"]
        print(f"resuming after {len(completed)} samples")
    device = model_device(model)
    for request in requests:
        context = token_ids(tokenizer, request["prompt"], special=True)
        card_tokens = len(token_ids(tokenizer, request["api_card"], special=False))
        if card_tokens > request["api_card_budget_tokens"]:
            fail(f"request {request['request_id']}: API card exceeds its token budget")
        input_ids = torch.tensor([context], dtype=torch.long, device=device)
        attention = torch.ones_like(input_ids)
        for sample_index, seed in enumerate(request["sample_seeds"]):
            key = (request["request_id"], sample_index)
            if key in completed:
                continue
            torch.manual_seed(seed)
            if torch.cuda.is_available():
                torch.cuda.manual_seed_all(seed)
            with torch.inference_mode():
                result = model.generate(
                    input_ids=input_ids,
                    attention_mask=attention,
                    do_sample=True,
                    temperature=request["temperature"],
                    top_p=request["top_p"],
                    max_new_tokens=request["max_new_tokens"],
                    pad_token_id=tokenizer.pad_token_id,
                    eos_token_id=tokenizer.eos_token_id,
                )
            generated = result[0, input_ids.shape[1]:].tolist()
            output = tokenizer.decode(generated, skip_special_tokens=True)
            row = {
                "request_id": request["request_id"],
                "sample_index": sample_index,
                "seed": seed,
                "output": output,
                "output_sha256": digest(output),
                "context_tokens": len(context),
                "api_card_tokens": card_tokens,
            }
            append(args.output, row)
            completed[key] = row["output_sha256"]
    print(f"generated {len(completed)} samples for {model_config['id']}")


def load_references(path: Path, requests: list[dict[str, Any]]) -> dict[str, str]:
    expected = {row["request_id"]: row for row in requests}
    references: dict[str, str] = {}
    for line, row in enumerate(read_jsonl(path), start=1):
        request_id = row.get("request_id")
        if request_id not in expected:
            continue
        if set(row) != {"request_id", "reference_sha256", "reference"}:
            fail(f"references line {line}: unexpected fields")
        if request_id in references or digest(row["reference"]) != row["reference_sha256"]:
            fail(f"references line {line}: duplicate or hash mismatch")
        if row["reference_sha256"] != expected[request_id]["reference_sha256"]:
            fail(f"references line {line}: request hash mismatch")
        references[request_id] = row["reference"]
    if set(references) != set(expected):
        fail("private reference bundle is incomplete for this model")
    return references


def require_frozen_samples(path: Path, requests: list[dict[str, Any]]) -> None:
    expected = {(row["request_id"], index) for row in requests for index in range(50)}
    request_ids = {request["request_id"] for request in requests}
    observed = set()
    for line, row in enumerate(read_jsonl(path), start=1):
        key = (row.get("request_id"), row.get("sample_index"))
        if key[0] not in request_ids:
            continue
        if key in observed or digest(row.get("output", "")) != row.get("output_sha256"):
            fail(f"samples line {line}: duplicate or output hash mismatch")
        observed.add(key)
    if observed != expected:
        fail("score mode requires the complete frozen sample matrix for this model")


def score(args: argparse.Namespace, model_config: dict[str, Any], requests, backend) -> None:
    torch, tokenizer, model = backend
    require_frozen_samples(args.samples, requests)
    references = load_references(args.references, requests)
    completed: set[str] = set()
    if args.output.exists():
        if not args.resume:
            fail(f"refusing to replace {args.output}; pass --resume")
        for line, row in enumerate(read_jsonl(args.output), start=1):
            request_id = row.get("request_id")
            if request_id in completed:
                fail(f"score line {line}: duplicate request_id")
            completed.add(request_id)
        print(f"resuming after {len(completed)} reference scores")
    device = model_device(model)
    for request in requests:
        if request["request_id"] in completed:
            continue
        context = token_ids(tokenizer, request["prompt"], special=True)
        target = token_ids(tokenizer, references[request["request_id"]], special=False)
        if not context or not target:
            fail(f"request {request['request_id']}: empty context or target tokens")
        card_tokens = len(token_ids(tokenizer, request["api_card"], special=False))
        ids = torch.tensor([context + target], dtype=torch.long, device=device)
        with torch.inference_mode():
            logits = model(input_ids=ids).logits[:, :-1, :]
            labels = ids[:, 1:]
            start = len(context) - 1
            selected_logits = logits[:, start:, :].float()
            selected_labels = labels[:, start:]
            losses = torch.nn.functional.cross_entropy(
                selected_logits.reshape(-1, selected_logits.shape[-1]),
                selected_labels.reshape(-1), reduction="sum",
            )
        nll = float(losses.item())
        if not math.isfinite(nll) or nll < 0:
            fail(f"request {request['request_id']}: invalid reference NLL")
        append(args.output, {
            "request_id": request["request_id"],
            "nll": nll,
            "target_tokens": len(target),
            "context_tokens": len(context),
            "api_card_tokens": card_tokens,
        })
        completed.add(request["request_id"])
    print(f"scored {len(completed)} references for {model_config['id']}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("generate", "score"))
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--requests", type=Path, required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--references", type=Path)
    parser.add_argument("--samples", type=Path)
    parser.add_argument("--dtype", choices=("auto", "float16", "bfloat16", "float32"),
                        default="auto")
    parser.add_argument("--device-map", default="auto")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    if args.mode == "score" and (args.references is None or args.samples is None):
        parser.error("score mode requires --references and --samples")
    config = load_config(args.config)
    models = {item["id"]: item for item in config["models"]}
    if args.model not in models:
        fail(f"model {args.model} is absent from the release config")
    model_config = models[args.model]
    requests = validate_requests(
        read_jsonl(args.requests), args.model, model_config["revision"]
    )
    backend = load_backend(args.model, model_config["revision"],
                           args.dtype, args.device_map)
    if args.mode == "generate":
        generate(args, model_config, requests, backend)
    else:
        score(args, model_config, requests, backend)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
