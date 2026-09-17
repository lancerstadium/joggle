#!/usr/bin/env python3
"""Time the recorded Joggle derivation route for the UltraFace storage-planner change.

Stages follow paper/experiments/derive-prepare.md (reproduction block): derive the
planner, prepare the model, plan with the original and derived planners, place,
emit, then compile and check both artifacts with paper/experiments/reuse/check.py.
No compiler rebuild occurs in this route. Wall times are per-process medians on
a shared laptop host; they are feedback-time observations, not throughput claims.
"""
import argparse, hashlib, json, platform, shutil, statistics, subprocess, sys, time
from pathlib import Path

REPO = Path("/Users/lancer/Documents/Item/joggle")
TOOL = REPO / "build/joggle"
MODS = ["-M", "modules", "-M", "build/modules"]
REF = REPO / "build-study/derivation/ultraface"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def timed(cmd, out=None, cwd=REPO):
    t0 = time.perf_counter()
    with (open(out, "wb") if out else subprocess.DEVNULL) as sink:
        result = subprocess.run([str(c) for c in cmd], cwd=cwd, stdout=sink, stderr=subprocess.PIPE)
    dt = time.perf_counter() - t0
    if result.returncode:
        raise RuntimeError(f"{cmd[:3]} failed: {result.stderr.decode()[:500]}")
    return dt


def one_round(root):
    for sub in ("original", "derived", "modules/planned"):
        (root / sub).mkdir(parents=True, exist_ok=True)
    t = {}
    t["derive_planner"] = timed([TOOL, "run", "reuse.derive", "paper/experiments/reuse/compiler.jog",
                                 "-M", "paper/experiments", *MODS], root / "modules/planned/module.jog")
    t["prepare_model"] = timed([TOOL, "run", "c.prepare", "build-study/ultraface-block/canonical.jog", *MODS],
                               root / "prepared.jog")
    t["plan_original"] = timed([TOOL, "run", "mem.plan", root / "prepared.jog", *MODS], root / "original/planned.jog")
    t["plan_derived"] = timed([TOOL, "run", "planned.apply", root / "prepared.jog", "-M", root / "modules",
                               "-M", "paper/experiments", *MODS], root / "derived/planned.jog")
    for v in ("original", "derived"):
        d = root / v
        t[f"place_{v}"] = timed([TOOL, "run", "c.place", d / "planned.jog", "--arg", '"static"', *MODS], d / "placed.jog")
        e = timed([TOOL, "emit", "c.source", d / "placed.jog", "--arg", '"weights"', *MODS], d / "model.c")
        e += timed([TOOL, "emit", "c.header", d / "placed.jog", "--arg", '"weights"', *MODS], d / "model.h")
        e += timed([TOOL, "emit", "c.data", d / "placed.jog", *MODS], d / "weights.bin")
        t[f"emit_{v}"] = e
    t0 = time.perf_counter()
    check = subprocess.run([sys.executable, "paper/experiments/reuse/check.py", root, "build-matrix/ultraface-rfb-320"],
                           cwd=REPO, capture_output=True, text=True)
    t["compile_and_check_both"] = time.perf_counter() - t0
    (root / "check.json").write_text(check.stdout)
    report = json.loads(check.stdout) if check.stdout.strip().startswith("{") else {"passed": False, "stderr": check.stderr[:800]}
    return t, report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--repeat", type=int, default=5)
    args = ap.parse_args()
    work = args.out.parent / "joggle-run"
    rounds, reports = [], []
    for i in range(args.repeat):
        root = work / f"round{i}"
        if root.exists():
            shutil.rmtree(root)
        t, report = one_round(root)
        rounds.append(t)
        reports.append(report)
    stages = {k: statistics.median(r[k] for r in rounds) for k in rounds[0]}
    stages["derive_and_plan_derived"] = statistics.median(r["derive_planner"] + r["plan_derived"] for r in rounds)
    last = work / f"round{args.repeat - 1}"
    record = {"route": "Joggle structural derivation (recorded reproduction block)", "workload": "UltraFace RFB-320",
              "tool_sha256": sha(TOOL), "platform": platform.platform(), "repeat": args.repeat,
              "compiler_rebuild": "none", "stage_seconds_per_round": rounds, "stage_seconds_median": stages,
              "all_rounds_passed": all(r.get("passed") for r in reports),
              "declared_static_slots": {v: reports[-1]["variants"][v]["static_slots"]["declared_bytes"]
                                        for v in ("original", "derived")} if reports[-1].get("passed") else None,
              "max_abs_error": {v: max(c[o]["max_absolute_error"] for c in reports[-1]["variants"][v]["calls"] for o in c)
                                for v in ("original", "derived")} if reports[-1].get("passed") else None,
              "matches_recorded_study": {
                  "derived_planned.jog": sha(last / "derived/planned.jog") == sha(REF / "derived/planned.jog"),
                  "original_planned.jog": sha(last / "original/planned.jog") == sha(REF / "original/planned.jog"),
                  "derived_module.jog": sha(last / "modules/planned/module.jog") == sha(REF / "modules/planned/module.jog")}}
    args.out.write_text(json.dumps(record, indent=1) + "\n")
    print(json.dumps({k: round(v, 3) for k, v in stages.items()}, indent=1))
    print("passed", record["all_rounds_passed"], record["declared_static_slots"], record["matches_recorded_study"])


if __name__ == "__main__":
    main()
