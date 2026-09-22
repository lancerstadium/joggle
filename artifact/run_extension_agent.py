#!/usr/bin/env python3
"""Run one pinned local-model extension trajectory with isolated native tools."""

from __future__ import annotations

import argparse
import csv
import difflib
import hashlib
import json
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

from run_extension_task import ROOT, SUPPORTED_TASKS, digest


ACTIONS, TOKENS = 30, 32000
SUFFIXES = {"Joggle": "jog", "MLIR": "cpp", "xDSL": "py"}
CARD_NAMES = {"Joggle": "joggle", "MLIR": "mlir", "xDSL": "xdsl"}
ACTION_SCHEMA = {"type": "object", "properties": {
    "action": {"type": "string", "enum": ["inspect", "edit", "test", "finish"]},
    "source": {"type": "string"}}, "required": ["action"], "additionalProperties": False}


def local_api(path: str, payload: dict | None = None) -> dict:
    # The installed model is used as-is: no remote provider and no model pull.
    request = urllib.request.Request("http://127.0.0.1:11434/api/" + path,
        data=json.dumps(payload).encode() if payload is not None else None,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=600) as response:
        return json.load(response)


def public_case_ids(task: dict) -> list[str]:
    return [cases[0]["id"] for cases in (task["positive_cases"], task["negative_cases"]) if cases]


def tool_feedback(report: dict) -> dict:
    if report.get("complete_task") is not False:
        raise ValueError("only public-fixture reports may enter model feedback")
    return {"build": [{"exit_code": item["exit_code"],
                       "stderr": item["stderr"][-6000:]} for item in report["setup"]],
            "cases": [{key: case[key] for key in ("id", "passed", "actual", "expected",
                       "exit_code", "decode_error")} | {"stderr": case["stderr"][-6000:]}
                      for case in report["cases"]]}


def native_identity(args: argparse.Namespace) -> dict:
    if args.system == "Joggle":
        return {"executable_sha256": digest(args.joggle),
                "mods_sha256": hashlib.sha256(json.dumps([
                    (str(path.relative_to(args.builtin_mods)), digest(path))
                    for path in sorted(args.builtin_mods.rglob("*.jog"))
                ]).encode()).hexdigest()}
    if args.system == "xDSL":
        program = ("import importlib.metadata,json,sys; "
                   "print(json.dumps({'xdsl':importlib.metadata.version('xdsl'),"
                   "'python':sys.version}))")
        return json.loads(subprocess.check_output([str(args.xdsl_python), "-c", program], text=True))
    config = args.mlir_dir / "MLIRConfig.cmake"
    llvm = args.mlir_dir.parent / "llvm/LLVMConfig.cmake"
    return {"mlir_config_sha256": digest(config), "llvm_config_sha256": digest(llvm),
            "libraries_sha256": hashlib.sha256(json.dumps([
                (path.name, digest(path)) for path in sorted(args.mlir_dir.parents[1].glob("libMLIR*.a"))
            ]).encode()).hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True)
    parser.add_argument("--system", choices=SUFFIXES, required=True)
    parser.add_argument("--task", choices=sorted(SUPPORTED_TASKS), required=True)
    parser.add_argument("--run", type=int, choices=range(10), default=0)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--demo", action="append", default=[])
    parser.add_argument("--output", type=Path, required=True, help="new trajectory directory")
    parser.add_argument("--joggle", type=Path)
    parser.add_argument("--builtin-mods", type=Path)
    parser.add_argument("--mlir-dir", type=Path)
    parser.add_argument("--xdsl-python", type=Path)
    parser.add_argument("--sandbox-read", type=Path, action="append", default=[])
    parser.add_argument("--allow-dirty", action="store_true", help="integration only; never release eligible")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("native tool isolation currently requires macOS")
    if len(args.demo) not in (0, 2) or len(set(args.demo)) != len(args.demo) or args.task in args.demo:
        parser.error("provide zero or two distinct, task-disjoint demonstrations")
    if args.seed < 0 or args.output.exists():
        parser.error("seed must be non-negative and output directory must not exist")
    required = {"Joggle": [args.joggle, args.builtin_mods], "MLIR": [args.mlir_dir],
                "xDSL": [args.xdsl_python]}[args.system]
    if any(path is None or not path.exists() for path in required):
        parser.error("missing native tool paths for the selected system")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT))
    if dirty and not args.allow_dirty:
        parser.error("commit the harness before measurement or select --allow-dirty for integration")
    tags = local_api("tags")["models"]
    installed = [model for model in tags if args.model in (model["name"], model.get("model"))]
    if len(installed) != 1:
        parser.error("model must name one already installed local model")
    model_revision = installed[0]["digest"]
    system_identity = native_identity(args)
    system_revision = hashlib.sha256(json.dumps(system_identity, sort_keys=True).encode()).hexdigest()
    server_version = local_api("version")
    spec_path = ROOT / "manifests/extension-specs.json"
    spec = json.loads(spec_path.read_text())
    tasks = {task["id"]: task for task in spec["tasks"]}
    if any(task not in SUPPORTED_TASKS for task in args.demo):
        parser.error("demonstrations must have executable reference implementations")
    task = tasks[args.task]
    card = ROOT / "extensions/api" / (CARD_NAMES[args.system] + ".md")
    suffix = SUFFIXES[args.system]
    starter = ROOT / "extensions" / ("starter." + suffix)
    source_identity = {str(path.relative_to(ROOT)): digest(path) for path in (
        Path(__file__).resolve(), ROOT / "run_extension_task.py", spec_path, card, starter,
        ROOT / "extensions/CMakeLists.txt", ROOT / "extensions/mlir-driver.cpp",
        ROOT / "extensions/xdsl-driver.py")}
    demos = []
    for name in args.demo:
        source = ROOT / "extensions" / name / ("reference." + suffix)
        demos.append({"task": name, "contract": tasks[name]["contract"],
                      "source": source.read_text(), "source_sha256": digest(source)})
    root = args.output.resolve()
    root.mkdir(parents=True)
    candidate = root / ("candidate." + suffix)
    candidate.write_text(starter.read_text())
    public_ids = public_case_ids(task)
    messages = [{"role": "system", "content":
        "Implement a compiler extension. Respond with one JSON action per turn: "
        "inspect (read your current file), edit (replace it with the source string), "
        "test (build and run public fixtures), or finish (submit the current file). "
        "You have 30 actions and 32000 generated tokens. Do not invent tools. "
        "Use the supplied native API. Generalize the semantic contract; final scoring "
        "also includes fixtures not exposed by test. Return JSON only."},
        {"role": "user", "content": json.dumps({"system": args.system,
            "task": args.task, "contract": task["contract"], "api": card.read_text(),
            "starter": candidate.read_text(), "public_cases": [case for case in
            task["positive_cases"] + task["negative_cases"] if case["id"] in public_ids],
            "demonstrations": demos})}]
    events = []
    infrastructure_error = None
    prompt_tokens = completion_tokens = edits = tool_calls = 0
    submitted = False
    stop = "budget"
    options = {"temperature": 0.2, "top_p": 0.95, "top_k": 40, "num_ctx": 32768}
    started = time.perf_counter()

    def oracle(name: str, public: bool) -> dict:
        output = root / (name + ".json")
        argv = [sys.executable, str(ROOT / "run_extension_task.py"), "--isolate",
                "--task", args.task, "--system", args.system, "--source", str(candidate),
                "--output", str(output), "--build-root", str(root / "build")]
        for key in ("joggle", "builtin_mods", "mlir_dir", "xdsl_python"):
            value = getattr(args, key)
            if value:
                argv.extend(["--" + key.replace("_", "-"), str(value.absolute())])
        for path in args.sandbox_read:
            argv.extend(["--sandbox-read", str(path.resolve())])
        if public:
            for case in public_ids:
                argv.extend(["--case", case])
        result = subprocess.run(argv, capture_output=True, text=True)
        if result.returncode not in (0, 1) or not output.exists():
            raise RuntimeError("oracle infrastructure failed: " + result.stderr[-6000:])
        report = json.loads(output.read_text())
        if public and any(case["id"] not in public_ids for case in report["cases"]):
            raise RuntimeError("oracle returned a non-public fixture during testing")
        return report

    for action_index in range(ACTIONS):
        remaining = TOKENS - completion_tokens
        if remaining <= 0:
            break
        request = {"model": args.model, "messages": messages, "stream": False,
                   "think": False, "format": ACTION_SCHEMA,
                   "options": {**options, "seed": args.seed + action_index,
                               "num_predict": min(4096, remaining)}}
        try:
            response = local_api("chat", request)
            if (not response.get("done") or response.get("model") != args.model or
                    not isinstance(response.get("message", {}).get("content"), str)):
                raise ValueError("incomplete or mismatched model response")
        except Exception as failure:
            events.append({"action_index": action_index, "provider_error": str(failure)})
            stop = "agent_error"
            break
        prompt_tokens += int(response["prompt_eval_count"])
        completion_tokens += int(response["eval_count"])
        events.append({"action_index": action_index, "response": response,
                       "candidate_before_sha256": digest(candidate)})
        content = response["message"]["content"]
        messages.append({"role": "assistant", "content": content})
        try:
            action = json.loads(content)
            kind = action["action"]
            if kind not in ("inspect", "edit", "test", "finish"):
                raise ValueError("unknown action")
            if completion_tokens > TOKENS:
                break
            if kind == "finish":
                submitted = True
                stop = "submitted"
                break
            tool_calls += 1
            if kind == "inspect":
                feedback = {"source": candidate.read_text()}
            elif kind == "edit":
                if not isinstance(action.get("source"), str):
                    raise ValueError("edit requires a source string")
                candidate.write_text(action["source"])
                edits += 1
                feedback = {"edited": True, "source_sha256": digest(candidate)}
            else:
                feedback = tool_feedback(oracle(f"public-{action_index}", public=True))
        except (ValueError, KeyError, TypeError) as failure:
            feedback = {"error": str(failure)}
        except RuntimeError as failure:
            infrastructure_error = str(failure)
            stop = "agent_error"
            break
        events[-1]["feedback"] = feedback
        messages.append({"role": "user", "content": json.dumps(feedback)})
        (root / "checkpoint.json").write_text(json.dumps({"events": events,
            "messages": messages, "completion_tokens": completion_tokens}, indent=2) + "\n")
    wall_ms = round((time.perf_counter() - started) * 1000)
    final = oracle("final-oracle", public=False)
    final_tags = local_api("tags")["models"]
    identity_stable = (any(model.get("name") == args.model and model.get("digest") == model_revision
                          for model in final_tags) and native_identity(args) == system_identity and
                       all(digest(ROOT / path) == value for path, value in source_identity.items()))
    passed = bool(final["passed"] and completion_tokens <= TOKENS and identity_stable and
                  not infrastructure_error and stop != "agent_error")
    if passed:
        stop = "success"
    elif stop == "submitted":
        stop = "build" if any(step["exit_code"] != 0 for step in final["setup"]) else "semantic"
    gates = all(step["exit_code"] == 0 for step in final["setup"]) and any(
        case["exit_code"] == 0 and not case["decode_error"] for case in final["cases"])
    patch_text = "".join(difflib.unified_diff(starter.read_text().splitlines(keepends=True),
        candidate.read_text().splitlines(keepends=True), fromfile="starter." + suffix,
        tofile="candidate." + suffix))
    (root / "candidate.patch").write_text(patch_text)
    payload = {"schema": "extension-agent-trajectory/v1", "model": installed[0],
        "ollama_version": server_version, "system": args.system, "revision": revision,
        "system_identity": system_identity, "identity_stable": identity_stable,
        "source_identity": source_identity,
        "infrastructure_error": infrastructure_error,
        "dirty": dirty, "task": args.task, "task_spec_sha256": source_identity[str(spec_path.relative_to(ROOT))],
        "api_card_sha256": source_identity[str(card.relative_to(ROOT))], "seed": args.seed, "run": args.run,
        "options": options, "think": False, "public_case_ids": public_ids,
        "demonstrations": demos, "messages": messages, "events": events,
        "submitted": submitted, "wall_ms": wall_ms, "final_oracle_sha256": digest(root / "final-oracle.json")}
    trajectory = root / "trajectory.json"
    trajectory.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n")
    row = {"model": args.model, "model_revision": model_revision, "system": args.system,
        "system_revision": system_revision, "task": args.task, "family": task["family"],
        "demo_count": len(args.demo), "demo_ids": ";".join(args.demo), "run": args.run,
        "seed": args.seed, "budget_actions": ACTIONS, "budget_tokens": TOKENS,
        "wall_ms": wall_ms, "prompt_tokens": prompt_tokens, "completion_tokens": completion_tokens,
        "tool_calls": tool_calls, "edit_attempts": edits, "files_touched": int(edits > 0),
        # Successful native execution establishes these gates; a failed process
        # alone does not distinguish parsing, type checking, and code generation.
        "parsed": "true" if gates else "", "typed": "true" if gates else "",
        "built": "true" if gates else "",
        "passed": str(passed).lower(), "stop_reason": stop, "task_spec_sha256": payload["task_spec_sha256"],
        "api_card_sha256": payload["api_card_sha256"], "trajectory_sha256": digest(trajectory),
        "patch_sha256": digest(root / "candidate.patch"),
        "reference_nll": "", "reference_tokens": ""}
    output = root / "result.csv"
    with (ROOT / "templates/figure-04-extension.csv").open() as stream:
        columns = next(csv.reader(stream))
    with output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        writer.writerow(row)
    record = {"schema": "agent-provider/v1", "dirty": dirty, "output_sha256": digest(output),
              "trajectory": str(trajectory), "trajectory_sha256": digest(trajectory),
              "reference_scoring": "not-collected", "complete_population": False,
              "release_eligible": False,
              "phase_reporting": "successful-execution-only"}
    output.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"task": args.task, "system": args.system, "passed": passed,
                      "stop": stop, "actions": len(events), "tokens": completion_tokens,
                      "csv": str(output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
