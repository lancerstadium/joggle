#!/usr/bin/env python3
"""Run one pinned local-model extension trajectory with isolated native tools."""

from __future__ import annotations

import argparse
import shutil
import csv
import difflib
import hashlib
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

from run_extension_task import ROOT, SUPPORTED_TASKS, REWRITE_TASKS, DEFINITION_TASKS, IMPORT_TASKS, COMPOUND_TASKS, digest, case_completed


ACTIONS, TOKENS = 30, 32000
GENERATION_OPTIONS = {"temperature": 0.0, "num_ctx": 32768}
SUFFIXES = {"Joggle": "jog", "MLIR": "cpp", "xDSL": "py"}
CARD_NAMES = {"Joggle": "joggle", "MLIR": "mlir", "xDSL": "xdsl"}
ACTION_SCHEMA = {"type": "object", "properties": {
    "action": {"type": "string", "enum": ["inspect", "edit", "test", "finish"]},
    "source": {"type": "string"}}, "required": ["action"], "additionalProperties": False}


class ContextLimitError(ValueError):
    def __init__(self, detail: dict):
        super().__init__(detail["message"])
        self.detail = detail


def context_limit(body: bytes) -> dict | None:
    """Recognize the native runner's structured rejection, not message substrings."""
    try:
        value = json.loads(body)
        for _ in range(3):
            if isinstance(value, str):
                value = json.loads(value)
            elif isinstance(value, dict) and "error" in value:
                value = value["error"]
            else:
                break
        if (isinstance(value, dict) and value.get("type") == "exceed_context_size_error"
                and value.get("code") == 400 and isinstance(value.get("message"), str)
                and type(value.get("n_prompt_tokens")) is int
                and type(value.get("n_ctx")) is int
                and value["n_prompt_tokens"] > value["n_ctx"] > 0):
            return value
    except (ValueError, TypeError):
        pass
    return None


def local_api(path: str, payload: dict | None = None) -> dict:
    # The installed model is used as-is: no remote provider and no model pull.
    request = urllib.request.Request("http://127.0.0.1:11434/api/" + path,
        data=json.dumps(payload).encode() if payload is not None else None,
        headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=600) as response:
            return json.load(response)
    except urllib.error.HTTPError as failure:
        body = failure.read()
        detail = context_limit(body) if failure.code == 400 else None
        if detail is not None:
            raise ContextLimitError(detail) from failure
        raise RuntimeError(f"provider HTTP {failure.code}: {body.decode(errors='replace')[:6000]}") from failure


def check_context_policy(model: str) -> dict:
    """Verify that this provider rejects overflow instead of dropping history."""
    request = {"model": model, "stream": False, "think": False,
               "truncate": False, "shift": False,
               "messages": [{"role": "user", "content": "context_probe " * 40000}],
               "options": {**GENERATION_OPTIONS, "num_predict": 1}}
    try:
        local_api("chat", request)
    except ContextLimitError as error:
        if error.detail["n_ctx"] != GENERATION_OPTIONS["num_ctx"]:
            raise ValueError("provider context differs from the experimental budget") from error
        return {"verified": True, "overflow": error.detail,
                "request_sha256": hashlib.sha256(json.dumps(request, sort_keys=True).encode()).hexdigest()}
    raise ValueError("provider accepted an over-budget prompt; context policy is not enforced")


def response_usage(response: dict, model: str, context: int, prediction: int) -> tuple[int, int]:
    """Reject missing usage and reported context rollover before accepting an action."""
    if (not isinstance(response, dict) or response.get("done") is not True or
            response.get("model") != model or
            not isinstance(response.get("message"), dict) or
            not isinstance(response["message"].get("content"), str)):
        raise ValueError("incomplete or mismatched model response")
    prompt, completion = response.get("prompt_eval_count"), response.get("eval_count")
    if (type(prompt) is not int or type(completion) is not int or
            prompt <= 0 or completion <= 0):
        raise ValueError("missing or invalid provider token counts")
    if completion > prediction:
        raise ValueError("provider exceeded the requested generation limit")
    if prompt + completion > context:
        raise ValueError("provider token counts exceed the context window")
    return prompt, completion


def final_checks(oracle, identity_check) -> tuple[dict | None, bool, list[str]]:
    """Keep failed final checks observable without inventing semantic results."""
    final, stable, errors = None, False, []
    try:
        report = oracle("final-oracle", public=False)
        if (not isinstance(report, dict) or type(report.get("passed")) is not bool or
                not isinstance(report.get("setup"), list) or
                not isinstance(report.get("cases"), list)):
            raise ValueError("invalid final oracle report")
        for step in report["setup"]:
            if not isinstance(step, dict) or "exit_code" not in step:
                raise ValueError("invalid final oracle setup record")
        for case in report["cases"]:
            if not isinstance(case, dict) or not {"exit_code", "decode_error"} <= case.keys():
                raise ValueError("invalid final oracle case record")
        if report["passed"] and (not report["cases"] or
                any(step["exit_code"] != 0 for step in report["setup"]) or
                any(not case_completed(case) or case["decode_error"] or
                    case.get("passed") is not True for case in report["cases"])):
            raise ValueError("inconsistent final oracle success")
        final = report
    except Exception as failure:
        errors.append(f"final-oracle: {type(failure).__name__}: {failure}")
    try:
        stable = identity_check() is True
        if not stable:
            errors.append("identity: model or native tool identity changed")
    except Exception as failure:
        errors.append(f"identity: {type(failure).__name__}: {failure}")
    return final, stable, errors


def candidate_outcome(report: dict) -> str:
    """Classify observed outcomes without guessing a compiler phase from stderr."""
    if any(step["exit_code"] != 0 for step in report["setup"]):
        return "build"
    if not report["cases"]:
        return "observation"
    for case in report["cases"]:
        emission = case.get("emission")
        numerics = case.get("numerics") or {}
        if emission and emission["exit_code"] != 0:
            return "execution"
        if "build" in numerics and numerics["build"]["exit_code"] != 0:
            return "build"
        if "run" in numerics and numerics["run"]["exit_code"] != 0:
            return "execution"
        if any(run["exit_code"] != 0 for run in numerics.get("runs", [])):
            return "execution"
    if any(case.get("timeout") is True or
           (case["exit_code"] != 0 and not case_completed(case))
           for case in report["cases"]):
        return "execution"
    if any(case["decode_error"] for case in report["cases"]):
        return "observation"
    return "success" if report["passed"] else "semantic"


def public_case_ids(task: dict) -> list[str]:
    return [cases[0]["id"] for cases in (task["positive_cases"], task["negative_cases"]) if cases]


def demonstration_tasks(target: str, policy: dict, tasks: dict) -> list[str]:
    """Select the same ordered, target-disjoint examples for every native system."""
    if (not isinstance(policy, dict) or
            policy.get("schema") != "extension-demonstrations/v1" or
            policy.get("selection") != "first-task-distinct-from-target-per-slot" or
            not isinstance(policy.get("slots"), list) or len(policy["slots"]) != 2 or
            target not in tasks):
        raise ValueError("invalid demonstration protocol")
    selected = []
    for slot in policy["slots"]:
        if (not isinstance(slot, list) or len(slot) < 2 or
                any(not isinstance(name, str) or name not in tasks or
                    name not in SUPPORTED_TASKS for name in slot) or
                len(set(slot)) != len(slot)):
            raise ValueError("invalid demonstration slot")
        name = next(name for name in slot if name != target)
        if name in selected:
            raise ValueError("demonstration slots select duplicate tasks")
        for suffix in SUFFIXES.values():
            if not (ROOT / "extensions" / name / ("reference." + suffix)).is_file():
                raise ValueError("demonstration lacks a native reference")
        selected.append(name)
    return selected


def edit_candidate(candidate: Path, source: str) -> dict:
    if not isinstance(source, str):
        raise ValueError("edit requires a source string")
    content = source.encode("utf-8")
    changed = candidate.read_bytes() != content
    if changed:
        candidate.write_bytes(content)
    feedback = {"edited": changed, "source_sha256": digest(candidate)}
    if not changed:
        feedback["reason"] = "unchanged source; no file modification"
    return feedback


def tool_feedback(report: dict) -> dict:
    if report.get("complete_task") is not False:
        raise ValueError("only public-fixture reports may enter model feedback")
    def artifact_feedback(case):
        result = case.get("numerics") or {}
        feedback = {}
        if case.get("emission"):
            feedback["emission"] = {key: case["emission"][key][-6000:] if key in ("stdout", "stderr")
                                    else case["emission"][key] for key in
                                    ("exit_code", "stdout", "stderr", "timeout")}
        if "build" not in result:
            return feedback
        feedback["artifact"] = {phase: {key: result[phase][key][-6000:] if key in ("stdout", "stderr")
                                     else result[phase][key] for key in
                                     ("exit_code", "stdout", "stderr", "timeout")}
                             for phase in ("build", "run") if phase in result}
        failed_run = next((run for run in result.get("runs", []) if not run["passed"]), None)
        if failed_run:
            feedback["artifact"]["run"] = {
                key: failed_run[key][-6000:] if key in ("stdout", "stderr") else failed_run[key]
                for key in ("exit_code", "stdout", "stderr", "timeout", "expected", "observed")}
        return feedback
    return {"build": [{"exit_code": item["exit_code"],
                       "stderr": item["stderr"][-6000:]} for item in report["setup"]],
            "cases": [{key: case[key] for key in ("id", "passed", "actual", "expected",
                       "exit_code", "decode_error")} | {"stderr": case["stderr"][-6000:]} | artifact_feedback(case)
                      for case in report["cases"]]}


def native_identity(args: argparse.Namespace) -> dict:
    identity = backend_identity(args)
    if getattr(args, "task", None) in {"emit-kernel-wrapper", *COMPOUND_TASKS}:
        compiler = args.cc.resolve(strict=True)
        identity["host_compiler"] = {"path": str(compiler), "sha256": digest(compiler),
                                     "version": subprocess.check_output([str(compiler), "--version"], text=True)}
    return identity


def backend_identity(args: argparse.Namespace) -> dict:
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
    parser.add_argument("--demo", action="append", default=[],
                        help="two ordered tasks from the frozen demonstration protocol; omit for zero-shot")
    parser.add_argument("--output", type=Path, required=True, help="new trajectory directory")
    parser.add_argument("--joggle", type=Path)
    parser.add_argument("--builtin-mods", type=Path)
    parser.add_argument("--mlir-dir", type=Path)
    parser.add_argument("--xdsl-python", type=Path)
    parser.add_argument("--cc", type=Path, default=Path(shutil.which("clang") or "/usr/bin/cc"))
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
    context_check = check_context_policy(args.model)
    spec_path = ROOT / "manifests/extension-specs.json"
    spec = json.loads(spec_path.read_text())
    tasks = {task["id"]: task for task in spec["tasks"]}
    demo_path = ROOT / "manifests/extension-demonstrations.json"
    try:
        expected_demos = demonstration_tasks(args.task, json.loads(demo_path.read_text()), tasks)
    except (ValueError, OSError) as failure:
        parser.error(str(failure))
    if args.demo and args.demo != expected_demos:
        parser.error("demonstration protocol requires this order: " + ", ".join(expected_demos))
    if any(task not in SUPPORTED_TASKS for task in args.demo):
        parser.error("demonstrations must have executable reference implementations")
    task = tasks[args.task]
    card = ROOT / "extensions/api" / (CARD_NAMES[args.system] + ".md")
    suffix = SUFFIXES[args.system]
    starter = ROOT / "extensions" / (
        ("compound-starter." if args.task in COMPOUND_TASKS else
         "import-starter." if args.task in IMPORT_TASKS else
         "definition-starter." if args.task in DEFINITION_TASKS else
         "rewrite-starter." if args.task in REWRITE_TASKS else "starter.") + suffix)
    source_identity = {str(path.relative_to(ROOT)): digest(path) for path in (
        Path(__file__).resolve(), ROOT / "run_extension_task.py", spec_path, demo_path, card, starter,
        ROOT / "extensions/CMakeLists.txt", ROOT / "extensions/mlir-driver.cpp",
        ROOT / "extensions/xdsl-driver.py")}
    if args.task in REWRITE_TASKS or args.task in IMPORT_TASKS or args.task in COMPOUND_TASKS:
        for path in (ROOT / "extensions/emit-graph-manifest/reference.jog",
                     ROOT / "extensions/emit-graph-manifest/reference.py"):
            source_identity[str(path.relative_to(ROOT))] = digest(path)
    if args.task in DEFINITION_TASKS:
        path = ROOT / "extensions/definition-observer.jog"
        source_identity[str(path.relative_to(ROOT))] = digest(path)
    demos = []
    for name in args.demo:
        source = ROOT / "extensions" / name / ("reference." + suffix)
        source_identity[str(source.relative_to(ROOT))] = digest(source)
        demos.append({"task": name, "contract": tasks[name]["contract"],
                      "source": source.read_text(),
                      "source_sha256": source_identity[str(source.relative_to(ROOT))]})
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
    stop = "action_budget"
    options = dict(GENERATION_OPTIONS)
    started = time.perf_counter()

    def oracle(name: str, public: bool) -> dict:
        output = root / (name + ".json")
        argv = [sys.executable, str(ROOT / "run_extension_task.py"), "--isolate",
                "--task", args.task, "--system", args.system, "--source", str(candidate),
                "--output", str(output), "--build-root", str(root / "build")]
        for key in ("joggle", "builtin_mods", "mlir_dir", "xdsl_python", "cc"):
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
        try:
            report = json.loads(output.read_text())
            if report["complete_task"] is not (not public):
                raise ValueError("oracle returned the wrong fixture visibility")
            if all(step["exit_code"] == 0 for step in report["setup"]):
                expected_ids = (public_ids if public else [case["id"] for case in
                    task["positive_cases"] + task["negative_cases"]])
                actual_ids = [case["id"] for case in report["cases"]]
                if len(actual_ids) != len(expected_ids) or set(actual_ids) != set(expected_ids):
                    raise ValueError("oracle returned an incomplete or unexpected fixture population")
        except (ValueError, KeyError, TypeError) as failure:
            raise RuntimeError(f"invalid oracle report: {failure}") from failure
        return report

    for action_index in range(ACTIONS):
        remaining = TOKENS - completion_tokens
        if remaining <= 0:
            stop = "token_budget"
            break
        request = {"model": args.model, "messages": messages, "stream": False,
                   "truncate": False, "shift": False,
                   "think": False, "format": ACTION_SCHEMA,
                   "options": {**options, "seed": args.seed + action_index,
                               "num_predict": min(4096, remaining)}}
        response = None
        request_hash = hashlib.sha256(json.dumps(request, sort_keys=True).encode()).hexdigest()
        try:
            response = local_api("chat", request)
            prompt_count, completion_count = response_usage(
                response, args.model, options["num_ctx"], request["options"]["num_predict"])
        except ContextLimitError as failure:
            events.append({"action_index": action_index, "request_sha256": request_hash,
                           "context_limit": failure.detail})
            stop = "context_budget"
            break
        except Exception as failure:
            infrastructure_error = f"provider: {type(failure).__name__}: {failure}"
            events.append({"action_index": action_index, "provider_error": infrastructure_error,
                           "request_sha256": request_hash, "response": response})
            stop = "agent_error"
            break
        prompt_tokens += prompt_count
        completion_tokens += completion_count
        events.append({"action_index": action_index, "response": response,
                       "request_sha256": request_hash,
                       "candidate_before_sha256": digest(candidate)})
        content = response["message"]["content"]
        messages.append({"role": "assistant", "content": content})
        try:
            action = json.loads(content)
            kind = action["action"]
            if kind not in ("inspect", "edit", "test", "finish"):
                raise ValueError("unknown action")
            if completion_tokens > TOKENS:
                stop = "token_budget"
                break
            if kind == "finish":
                submitted = True
                stop = "submitted"
                break
            tool_calls += 1
            if kind == "inspect":
                feedback = {"source": candidate.read_text()}
            elif kind == "edit":
                feedback = edit_candidate(candidate, action.get("source"))
                edits += 1
            else:
                try:
                    feedback = tool_feedback(oracle(f"public-{action_index}", public=True))
                except Exception as failure:
                    raise RuntimeError(f"public oracle failed: {failure}") from failure
        except (ValueError, KeyError, TypeError) as failure:
            feedback = {"error": str(failure)}
        except Exception as failure:
            infrastructure_error = f"tool: {type(failure).__name__}: {failure}"
            stop = "agent_error"
            break
        events[-1]["feedback"] = feedback
        messages.append({"role": "user", "content": json.dumps(feedback)})
        (root / "checkpoint.json").write_text(json.dumps({"events": events,
            "messages": messages, "completion_tokens": completion_tokens}, indent=2) + "\n")
    if stop == "action_budget" and completion_tokens >= TOKENS:
        stop = "token_budget"
    wall_ms = round((time.perf_counter() - started) * 1000)
    def identity_check() -> bool:
        final_tags = local_api("tags")["models"]
        return (any(model.get("name") == args.model and model.get("digest") == model_revision
                    for model in final_tags) and local_api("version") == server_version and
                native_identity(args) == system_identity and
                all(digest(ROOT / path) == value for path, value in source_identity.items()))

    final, identity_stable, final_errors = final_checks(oracle, identity_check)
    if final_errors:
        infrastructure_error = "; ".join(filter(None, [infrastructure_error, *final_errors]))
        stop = "agent_error"
    passed = bool(final and final["passed"] and completion_tokens <= TOKENS and identity_stable and
                  not infrastructure_error and stop != "agent_error")
    outcome = candidate_outcome(final) if final else None
    if passed:
        stop = "success"
    elif stop == "submitted":
        stop = outcome
    gates = passed
    patch_text = "".join(difflib.unified_diff(starter.read_text().splitlines(keepends=True),
        candidate.read_text().splitlines(keepends=True), fromfile=starter.name,
        tofile="candidate." + suffix))
    (root / "candidate.patch").write_text(patch_text)
    payload = {"schema": "extension-agent-trajectory/v1", "model": installed[0],
        "ollama_version": server_version, "system": args.system, "revision": revision,
        "system_identity": system_identity, "identity_stable": identity_stable,
        "source_identity": source_identity,
        "infrastructure_error": infrastructure_error,
        "dirty": dirty, "task": args.task, "task_spec_sha256": source_identity[str(spec_path.relative_to(ROOT))],
        "api_card_sha256": source_identity[str(card.relative_to(ROOT))], "seed": args.seed, "run": args.run,
        "options": options, "think": False,
        "context_check": context_check,
        "context_policy": {"truncate": False, "shift": False, "overflow": "stop-budget"},
        "public_case_ids": public_ids,
        "demonstrations": demos, "messages": messages, "events": events,
        "submitted": submitted, "wall_ms": wall_ms, "final_check_errors": final_errors,
        "candidate_outcome": outcome,
        "final_oracle_sha256": (digest(root / "final-oracle.json")
                                if (root / "final-oracle.json").is_file() else None)}
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
              "release_eligible": bool(not dirty and identity_stable and final
                                       and not infrastructure_error and not args.demo),
              "phase_reporting": "successful-execution-only"}
    output.with_suffix(".json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"task": args.task, "system": args.system, "passed": passed,
                      "stop": stop, "actions": len(events), "tokens": completion_tokens,
                      "csv": str(output)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
