#!/usr/bin/env python3
"""Balanced fresh-process trials for the same-host UltraFace comparison.

Mirrors the rotation, one-thread environment, and per-process aggregation of
paper/scripts/measure_systems.py; setup excluded, outputs validated per process.
"""
import argparse, csv, json, os, platform, statistics, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
TVM = Path("/Users/lancer/Documents/Item/joggle-study/tvm")
ENV = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1", "MKL_NUM_THREADS": "1",
       "NUMEXPR_NUM_THREADS": "1", "VECLIB_MAXIMUM_THREADS": "1",
       "TVM_LIBRARY_PATH": str(TVM / "build-make/lib"), "PYTHONPATH": str(TVM / "python")}
PY = str(TVM / ".venv/bin/python")

def rotation(values, trial):
    off = trial % len(values); out = values[off:] + values[:off]
    if (trial // len(values)) % 2: out.reverse()
    return out

ap = argparse.ArgumentParser()
ap.add_argument("--trials", type=int, default=20); ap.add_argument("--inner", type=int, default=1); ap.add_argument("--warmup", type=int, default=3)
ap.add_argument("--out", type=Path, default=HERE / "same-host-ultraface")
a = ap.parse_args()
systems = ["joggle", "tvm", "ort"]
rows, load = [], os.getloadavg()[0]
for trial in range(a.trials):
    for position, system in enumerate(rotation(systems, trial)):
        r = subprocess.run([PY, str(HERE / "subject.py"), system, "--inner", str(a.inner), "--warmup", str(a.warmup)], env=ENV, capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"{system} failed: {r.stderr[-800:]}")
        data = list(csv.DictReader(r.stdout.splitlines()))
        secs = [float(d["seconds"]) for d in data]; sums = {d["checksum"] for d in data}
        assert len(sums) == 1, f"{system}: checksum varied within process"
        validation = [l for l in r.stderr.splitlines() if "max_abs_error" in l][-1]
        assert validation.endswith("pass"), f"{system}: {validation}"
        rows.append({"study": "ultraface-same-host", "trial": trial, "position": position, "system": system,
                     "seconds": sum(secs) / len(secs), "inner_iterations": len(secs), "checksum": sums.pop(), "validation": validation})
a.out.with_suffix(".csv").write_text("")
with open(a.out.with_suffix(".csv"), "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
summary = {}
for system in systems:
    v = sorted(r["seconds"] * 1000 for r in rows if r["system"] == system)
    med = statistics.median(v); mad = statistics.median(abs(x - med) for x in v)
    summary[system] = {"median_ms": med, "mad_ms": mad, "min_ms": v[0], "max_ms": v[-1], "n": len(v)}
meta = {"host": platform.platform(), "cpu": subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True).stdout.strip(),
        "python": PY, "trials": a.trials, "inner_iterations": a.inner, "warmup": a.warmup, "loadavg_before": load, "loadavg_after": os.getloadavg()[0],
        "date_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "threads": 1,
        "artifacts": {"joggle": "build-study/derivation/ultraface/original/model.dylib (clang -O2, recorded study artifact)",
                      "tvm": "build-study/tvm-control/ultraface_tvm_O2.dylib (Relax C target, default lowering, cc -O2)",
                      "ort": "onnxruntime CPU EP, intra/inter threads 1, original opset-9 graph"},
        "summary_ms": summary}
a.out.with_suffix(".json").write_text(json.dumps(meta, indent=1) + "\n")
print(json.dumps(summary, indent=1))
