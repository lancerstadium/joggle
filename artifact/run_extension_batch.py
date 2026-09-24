#!/usr/bin/env python3
"""Continue one frozen agent matrix; preserve results and bound network retries."""

import argparse
import csv
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parent.parent


def read(path):
    return json.loads(path.read_text())


def save(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def directory(batch, index, condition):
    model = condition["model"].split("/")[-1]
    return batch / f'{index:02d}-{model}-{condition["system"]}-{condition["task"]}'


def state(folder, maximum):
    from run_extension_agent import resumable_interruption
    record = folder / "result.json"
    if not folder.exists():
        return "new"
    if not record.exists():
        return "incomplete-record"
    provider = read(record)
    if provider.get("release_eligible") is True:
        return "complete"
    trajectory = read(folder / "trajectory.json")
    if (resumable_interruption(trajectory.get("infrastructure_error", ""))
            and len(trajectory.get("continuations", [])) < maximum):
        return "resume"
    return "blocked"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("--max-continuations", type=int, default=6)
    parser.add_argument("--max-hours", type=float, default=24)
    parser.add_argument("--export", type=Path)
    args = parser.parse_args()
    if args.max_continuations < 0 or args.max_hours <= 0:
        parser.error("limits must be nonnegative continuations and positive hours")
    if args.export and args.export.resolve() != ROOT / "paper/data/figure-04-extension.csv":
        parser.error("export must use the canonical paper/data/figure-04-extension.csv")
    batch = args.batch.resolve()
    manifest_path = batch / "manifest.json"
    manifest = read(manifest_path)
    # A process lock prevents simultaneous collectors; no credential is stored.
    with (batch / "collector.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        from run_baseline_benchmarks import git_state
        revision, dirty = git_state(ROOT)
        if dirty:
            raise SystemExit("commit non-paper changes before collecting")
        if len(manifest["conditions"]) != 72 or not manifest.get("primary"):
            raise SystemExit("expected the designated 72-condition primary matrix")
        amendment = {
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "revision": revision, "policy": "bounded-background-continuation/v2",
            "approved_by": "author", "max_continuations_per_condition": args.max_continuations,
            "max_hours": args.max_hours,
            "unchanged": ["tasks", "api_cards", "models", "sampling", "budgets",
                          "native_tools", "scoring", "received_responses"],
        }
        manifest.setdefault("transport_amendments", []).append(amendment)
        manifest.update(status="collecting", collector_pid=os.getpid())
        manifest.pop("error", None)
        manifest.pop("failed_condition", None)
        deadline = time.monotonic() + args.max_hours * 3600
        launch_failures = {}
        while time.monotonic() < deadline:
            progressed = False
            completed, pending = [], []
            for index, condition in enumerate(manifest["conditions"], 1):
                folder = directory(batch, index, condition)
                current = state(folder, args.max_continuations)
                if current == "complete":
                    with (folder / "result.csv").open() as stream:
                        row = next(csv.DictReader(stream))
                    completed.append({"index": index, "csv": str(folder / "result.csv"),
                                      "passed": row["passed"],
                                      "completion_tokens": int(row["completion_tokens"])})
                    continue
                pending.append({"index": index, "status": current})
                if (current not in ("new", "resume") or
                        launch_failures.get(index, 0) >= 2 or time.monotonic() >= deadline):
                    continue
                if git_state(ROOT) != (revision, False):
                    manifest.update(status="source-changed", completed=completed)
                    save(manifest_path, manifest)
                    raise SystemExit("non-paper source changed; collection stopped")
                manifest.update(current_condition=index, completed=completed,
                                updated_utc=datetime.now(timezone.utc).isoformat())
                save(manifest_path, manifest)
                command = [sys.executable, str(ROOT / "artifact/run_extension_agent.py"),
                           "--provider", "siliconflow", "--model", condition["model"],
                           "--system", condition["system"], "--task", condition["task"],
                           "--seed", str(manifest["condition_seed"]),
                           "--run", str(manifest["run"]), "--output", str(folder),
                           *manifest["native_arguments"]]
                if current == "resume":
                    command.append("--resume")
                print(f'{current.upper()} {index}/72 {condition["system"]} {condition["task"]}', flush=True)
                with (batch / f"{index:02d}.log").open("a") as log:
                    result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
                progressed = True
                if result.returncode:
                    launch_failures[index] = launch_failures.get(index, 0) + 1
                print(f'DONE {index}/72 {state(folder, args.max_continuations)}', flush=True)
            manifest.update(completed=completed, pending=pending)
            save(manifest_path, manifest)
            if len(completed) == 72:
                manifest["status"] = "complete"
                break
            if not progressed:
                manifest["status"] = "collection-blocked"
                break
            time.sleep(5)
        else:
            manifest["status"] = "time-limit"
        manifest.pop("current_condition", None)
        manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
        save(manifest_path, manifest)
        if manifest["status"] != "complete":
            return 1
        if args.export:
            output = args.export.resolve()
            output.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run([sys.executable, str(ROOT / "artifact/merge_agent_rows.py"),
                            *[item["csv"] for item in completed], "--output", str(output)], check=True)
            subprocess.run(["make", "-C", str(ROOT / "paper"), "agent-figure"], check=True)
        print("COMPLETE 72/72" + ("; validated export and figure ready" if args.export else ""), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
