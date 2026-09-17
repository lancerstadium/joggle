#!/usr/bin/env python3
"""Time one uniform span on three systems: pinned ONNX file to loadable library.

The three systems do not record a comparable quantity by default. TVM's recorded
field covers only relax.build and export_library, excluding ONNX import;
ONNX-MLIR's covers its whole command line; Joggle records prepare and build as
separate stages. This script therefore times the subprocess wall clock of each
system's complete pipeline, so the three numbers share one span: the pinned ONNX
file on disk in, a loadable shared library out, one host, one thread.
"""

import argparse, csv, json, os, platform, shutil, subprocess, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent
STUDY = REPO / "build-study" / "locality-matrix"
TVM = Path("/Users/lancer/Documents/Item/joggle-study/tvm")
PY_TVM = str(TVM / ".venv/bin/python")
ENV = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1",
       "MKL_NUM_THREADS": "1", "VECLIB_MAXIMUM_THREADS": "1",
       "TVM_LIBRARY_PATH": str(TVM / "build-make/lib"),
       "PYTHONPATH": str(TVM / "python")}
STEPS = {
    "joggle": [
        ["python3", "paper/experiments/locality/prepare_model.py",
         ".cache/onnx-zoo/{model}.onnx", "{out}"],
        # One artifact, so the span matches the other systems' single build.
        ["python3", "paper/experiments/locality/build_variants.py",
         "{out}/prepared.jog", "{out}", "--variant", "base"],
    ],
    "tvm": [["{py}", "paper/experiments/locality/export_tvm.py",
             ".cache/onnx-zoo/{model}.onnx", "{out}"]],
    # The pinned CLI is the whole pipeline for this system: one invocation
    # parses ONNX, optimizes, generates and links the library.
    "onnxmlir": [["/Users/lancer/Documents/Item/joggle-study/onnx-mlir/build-ikj/"
                  "Release/bin/onnx-mlir", "-O3", "-o", "{out}/onnxmlir_O3",
                  ".cache/onnx-zoo/{model}.onnx"]],
}
ARTIFACT = {
    "joggle": "{out}/base/model.dylib",
    "tvm": "{out}/tvm_c_O2.dylib",
    "onnxmlir": "{out}/onnxmlir_O3.so",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", nargs="+", default=["ultraface-rfb-320"])
    ap.add_argument("--systems", nargs="+",
                    default=["joggle", "tvm", "onnxmlir"])
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--out", type=Path, default=ROOT / "data" / "compile-time-same-host.csv")
    a = ap.parse_args()

    rows = []
    for model in a.models:
        # The TVM subject consumes the ABI descriptor the study fixes for every
        # system, so that fixture is staged outside the timed span rather than
        # charged to one arm.
        abi = STUDY / model / "base"
        for system in a.systems:
            for run in range(a.runs):
                out = STUDY / f"{model}.compile-{system}-{run}"
                if out.exists():
                    shutil.rmtree(out)
                out.mkdir(parents=True)
                if system == "tvm" and abi.is_dir():
                    shutil.copytree(abi, out / "base")
                start = time.perf_counter()
                failure = None
                for step in STEPS[system]:
                    command = [part.format(model=model, out=str(out), py=PY_TVM)
                               for part in step]
                    result = subprocess.run(command, cwd=REPO, env=ENV,
                                            capture_output=True, text=True)
                    if result.returncode:
                        failure = (result.stderr or result.stdout)[-300:]
                        break
                seconds = time.perf_counter() - start
                artifact = Path(ARTIFACT[system].format(out=str(out)))
                rows.append({
                    "model": model, "system": system, "run": run,
                    "seconds": round(seconds, 4),
                    "artifact_bytes": artifact.stat().st_size if artifact.exists() else "",
                    "ok": failure is None and artifact.exists(),
                    "note": "" if failure is None else failure.replace("\n", " ")[:160],
                })
                print(f"{model:20s} {system:9s} run {run}: {seconds:8.3f}s "
                      f"{'ok' if rows[-1]['ok'] else 'FAILED'}")

    with a.out.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    meta = {"host": platform.platform(),
            "cpu": subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                                  capture_output=True, text=True).stdout.strip(),
            "threads": 1, "runs": a.runs, "systems": a.systems, "models": a.models,
            "span": "pinned ONNX file on disk to loadable shared library",
            "date_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    a.out.with_suffix(".json").write_text(json.dumps(meta, indent=2) + "\n")
    print(f"\nwrote {a.out.name}")


if __name__ == "__main__":
    main()
