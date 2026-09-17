#!/usr/bin/env python3
"""Balanced fresh-process comparison of ordinary and locality artifacts.

For every model directory it alternates the two variants across trials so
position in the run order cannot favour either, validates each process against
the model's own ONNX Runtime reference, and reports per-model medians.

Models are measured independently and are never pooled into a single figure.
"""
import argparse, csv, json, os, platform, statistics, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SUBJECT = HERE / "subject_generic.py"
TVM = Path("/Users/lancer/Documents/Item/joggle-study/tvm")
PY = str(TVM / ".venv/bin/python") if (TVM / ".venv/bin/python").exists() else sys.executable
ENV = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1",
       "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1",
       "VECLIB_MAXIMUM_THREADS": "1",
       "TVM_LIBRARY_PATH": str(TVM / "build-make/lib"),
       "PYTHONPATH": str(TVM / "python")}

# The two Joggle artifacts are the comparison; the external columns are only
# measured when the model has them.
#
# Each external subject names the interpreter that can import its runtime. TVM
# and ONNX Runtime live in the study venv, while ONNX-MLIR's PyRuntime extension
# is compiled for the system Python, so one interpreter cannot run all three.
JOGGLE_VARIANTS = ("base", "locality")
SYSTEM_PY = sys.executable
EXTERNAL = {"tvm": (HERE / "subject_tvm.py", PY),
            "ort": (HERE / "subject_ort.py", PY),
            "onnxmlir": (HERE / "subject_onnxmlir.py", SYSTEM_PY)}


def rotation(values, trial):
    out = list(values[trial % len(values):]) + list(values[:trial % len(values)])
    if (trial // len(values)) % 2:
        out.reverse()
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("roots", nargs="+", type=Path, help="per-model directories")
    ap.add_argument("--trials", type=int, default=20)
    ap.add_argument("--inner", type=int, default=1)
    ap.add_argument("--warmup", type=int, default=3)
    ap.add_argument("--atol", type=float, default=1e-4)
    ap.add_argument("--out", type=Path, default=HERE / "locality-matrix")
    ap.add_argument("--onnx-dir", type=Path,
                    default=REPO / ".cache/onnx-zoo",
                    help="pinned models, enabling the ONNX Runtime column")
    ap.add_argument("--joggle-only", action="store_true",
                    help="measure only the two Joggle artifacts")
    a = ap.parse_args()

    rows, models, load = [], {}, os.getloadavg()[0]
    for root in a.roots:
        refs = sorted(root.glob("reference_*.bin"))
        if not refs or not (root / "input.bin").exists():
            print(f"skip {root.name}: no fixture", file=sys.stderr)
            continue
        missing = [v for v in JOGGLE_VARIANTS if not (root / v / "api.json").exists()]
        if missing:
            print(f"skip {root.name}: missing variants {missing}", file=sys.stderr)
            continue
        reference_args = []
        for r in refs:
            reference_args += ["--reference", str(r)]

        variants = list(JOGGLE_VARIANTS)
        if not a.joggle_only:
            if (root / "tvm.json").exists():
                variants.append("tvm")
            if (root / "onnxmlir.json").exists():
                variants.append("onnxmlir")
            if a.onnx_dir and (a.onnx_dir / f"{root.name}.onnx").exists():
                variants.append("ort")

        def command(variant):
            # ONNX Runtime is given the pinned model rather than a study
            # directory, so it keeps its own argument form.
            if variant == "ort":
                return [PY, str(EXTERNAL["ort"][0]),
                        str(a.onnx_dir / f"{root.name}.onnx"), "--root", str(root)]
            if variant in EXTERNAL:
                script, interpreter = EXTERNAL[variant]
                return [interpreter, str(script), str(root)]
            return [PY, str(SUBJECT), str(root / variant),
                    "--input", str(root / "input.bin"), *reference_args]

        for trial in range(a.trials):
            for position, variant in enumerate(rotation(variants, trial)):
                r = subprocess.run(
                    [*command(variant), "--inner", str(a.inner),
                     "--warmup", str(a.warmup), "--atol", str(a.atol)],
                    env=ENV, capture_output=True, text=True)
                if r.returncode:
                    sys.exit(f"{root.name}/{variant} failed: {r.stderr[-1200:]}")
                data = list(csv.DictReader(r.stdout.splitlines()))
                secs = [float(d["seconds"]) for d in data]
                sums = {d["checksum"] for d in data}
                assert len(sums) == 1, f"{root.name}/{variant}: checksum varied"
                validation = [l for l in r.stderr.splitlines() if "max_abs_error" in l][-1]
                assert validation.endswith("pass"), f"{root.name}/{variant}: {validation}"
                rows.append({"model": root.name, "trial": trial, "position": position,
                             "variant": variant, "seconds": sum(secs) / len(secs),
                             "checksum": sums.pop(), "validation": validation})
        models[root.name] = {"build": json.loads((root / "build.json").read_text()),
                             "reference": json.loads((root / "reference.json").read_text())
                             if (root / "reference.json").exists() else None}

    if not rows:
        sys.exit("no model produced measurements")
    with open(a.out.with_suffix(".csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    summary = {}
    for name in models:
        entry = {}
        measured = sorted({r["variant"] for r in rows if r["model"] == name})
        for variant in measured:
            v = sorted(r["seconds"] * 1000 for r in rows
                       if r["model"] == name and r["variant"] == variant)
            med = statistics.median(v)
            entry[variant] = {"median_ms": med,
                              "mad_ms": statistics.median(abs(x - med) for x in v),
                              "n": len(v)}
        # Only the two Joggle artifacts must agree bit for bit; tvm and ort
        # compute the same function with a different operation order.
        checks = {r["checksum"] for r in rows
                  if r["model"] == name and r["variant"] in JOGGLE_VARIANTS}
        entry["speedup"] = entry["base"]["median_ms"] / entry["locality"]["median_ms"]
        entry["checksum_identical_across_variants"] = len(checks) == 1
        entry["c_bytes"] = {v: models[name]["build"]["variants"][v]["c_bytes"]
                            for v in JOGGLE_VARIANTS}
        entry["weights_identical"] = models[name]["build"]["weights_identical"]
        summary[name] = entry

    meta = {"host": platform.platform(),
            "cpu": subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                                  capture_output=True, text=True).stdout.strip(),
            "python": PY, "trials": a.trials, "inner_iterations": a.inner,
            "warmup": a.warmup, "atol": a.atol, "threads": 1,
            "loadavg_before": load, "loadavg_after": os.getloadavg()[0],
            "date_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "summary": summary}
    a.out.with_suffix(".json").write_text(json.dumps(meta, indent=1) + "\n")
    for name, e in summary.items():
        extra = "".join(f"  {v}={e[v]['median_ms']:9.3f}" for v in ("tvm", "ort") if v in e)
        print(f"{name:26s} {e['base']['median_ms']:9.3f} -> {e['locality']['median_ms']:8.3f} ms"
              f"  {e['speedup']:5.2f}x  same={e['checksum_identical_across_variants']}{extra}")


if __name__ == "__main__":
    main()
