#!/usr/bin/env python3
"""Balanced fresh-process trials for the UltraFace loop-locality comparison.

Four systems on one host: the ordinary Joggle artifact, the same artifact after
the source-defined locality.apply policy, TVM's default (unscheduled) C target,
and one-thread ONNX Runtime. Reuses the same subject, rotation, one-thread
environment, and per-process validation as the same-host study so the two
Joggle columns differ only in the applied policy.

Setup is excluded from timing; every process validates its own output.
"""
import argparse, csv, json, os, platform, statistics, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = Path("/Users/lancer/Documents/Item/joggle")
SUBJECT = REPO / "paper/experiments/reuse/external/bench/subject.py"
TVM = Path("/Users/lancer/Documents/Item/joggle-study/tvm")
ENV = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1",
       "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1",
       "VECLIB_MAXIMUM_THREADS": "1",
       "TVM_LIBRARY_PATH": str(TVM / "build-make/lib"),
       "PYTHONPATH": str(TVM / "python")}
PY = str(TVM / ".venv/bin/python")

BASE_DIR = REPO / "build-study/derivation/ultraface/original"
LOCALITY_DIR = REPO / "build-study/locality-probe/ultraface"

# label -> (subject system, extra args)
SYSTEMS = {
    "joggle": ("joggle", ["--joggle-dir", str(BASE_DIR)]),
    "joggle-locality": ("joggle", ["--joggle-dir", str(LOCALITY_DIR)]),
    "tvm": ("tvm", []),
    "ort": ("ort", []),
}


def rotation(values, trial):
    off = trial % len(values)
    out = values[off:] + values[:off]
    if (trial // len(values)) % 2:
        out.reverse()
    return out


ap = argparse.ArgumentParser()
ap.add_argument("--trials", type=int, default=20)
ap.add_argument("--inner", type=int, default=1)
ap.add_argument("--warmup", type=int, default=3)
ap.add_argument("--out", type=Path, default=HERE / "ultraface-locality")
a = ap.parse_args()

labels = list(SYSTEMS)
rows, load = [], os.getloadavg()[0]
for trial in range(a.trials):
    for position, label in enumerate(rotation(labels, trial)):
        system, extra = SYSTEMS[label]
        r = subprocess.run(
            [PY, str(SUBJECT), system, "--inner", str(a.inner),
             "--warmup", str(a.warmup), *extra],
            env=ENV, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"{label} failed: {r.stderr[-800:]}")
        data = list(csv.DictReader(r.stdout.splitlines()))
        secs = [float(d["seconds"]) for d in data]
        sums = {d["checksum"] for d in data}
        assert len(sums) == 1, f"{label}: checksum varied within process"
        validation = [l for l in r.stderr.splitlines() if "max_abs_error" in l][-1]
        assert validation.endswith("pass"), f"{label}: {validation}"
        rows.append({"study": "ultraface-locality", "trial": trial,
                     "position": position, "system": label,
                     "seconds": sum(secs) / len(secs), "inner_iterations": len(secs),
                     "checksum": sums.pop(), "validation": validation})

with open(a.out.with_suffix(".csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)

summary = {}
for label in labels:
    v = sorted(r["seconds"] * 1000 for r in rows if r["system"] == label)
    med = statistics.median(v)
    summary[label] = {"median_ms": med,
                      "mad_ms": statistics.median(abs(x - med) for x in v),
                      "min_ms": v[0], "max_ms": v[-1], "n": len(v)}

checksums = {label: {r["checksum"] for r in rows if r["system"] == label}
             for label in labels}
meta = {"host": platform.platform(),
        "cpu": subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                              capture_output=True, text=True).stdout.strip(),
        "python": PY, "trials": a.trials, "inner_iterations": a.inner,
        "warmup": a.warmup, "loadavg_before": load,
        "loadavg_after": os.getloadavg()[0],
        "date_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "threads": 1,
        "checksums": {k: sorted(v) for k, v in checksums.items()},
        "artifacts": {
            "joggle": f"{BASE_DIR}/model.dylib (clang -O2, recorded study artifact)",
            "joggle-locality": f"{LOCALITY_DIR}/model.dylib "
                               "(same prepared subject after locality.apply, clang -O2)",
            "tvm": "build-study/tvm-control/ultraface_tvm_O2.dylib "
                   "(Relax C target, default lowering, cc -O2)",
            "ort": "onnxruntime CPU EP, intra/inter threads 1, original opset-9 graph"},
        "summary_ms": summary}
a.out.with_suffix(".json").write_text(json.dumps(meta, indent=1) + "\n")
print(json.dumps(summary, indent=1))
