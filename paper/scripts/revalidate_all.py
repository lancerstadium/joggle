"""Re-validate every system's artifact against the corrected references.

One run per variant is enough: the reference is an admission check, so a single
pass or fail line settles the verdict. No timing is repeated.
"""
import json, os, pathlib, subprocess, sys
ROOT = pathlib.Path("build-study/locality-matrix")
ONNX = pathlib.Path(".cache/onnx-zoo")
TVM = pathlib.Path("/Users/lancer/Documents/Item/joggle-study/tvm")
PY = str(TVM/".venv/bin/python") if (TVM/".venv/bin/python").exists() else sys.executable
SYS_PY = sys.executable
ENV = {**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1",
       "MKL_NUM_THREADS": "1", "NUMEXPR_NUM_THREADS": "1",
       "VECLIB_MAXIMUM_THREADS": "1",
       "TVM_LIBRARY_PATH": str(TVM / "build-make/lib"),
       "PYTHONPATH": str(TVM / "python")}
HERE = pathlib.Path("paper/experiments/locality")

rows = []
for d in sorted(p for p in ROOT.iterdir() if p.is_dir()):
    refs = sorted(d.glob("reference_*.bin"))
    if not refs or not (d/"input.bin").exists():
        continue
    variants = {}
    if (d/"base"/"api.json").exists(): variants["base"] = ("joggle", d/"base")
    if (d/"locality"/"api.json").exists(): variants["locality"] = ("joggle", d/"locality")
    if (d/"tvm.json").exists(): variants["tvm"] = ("tvm", None)
    if (d/"onnxmlir.json").exists(): variants["onnxmlir"] = ("onnxmlir", None)
    if (ONNX/f"{d.name}.onnx").exists(): variants["ort"] = ("ort", None)
    for name, (kind, artifact) in variants.items():
        if kind == "joggle":
            cmd = [PY, str(HERE/"subject_generic.py"), str(artifact),
                   "--input", str(d/"input.bin")]
            for r in refs: cmd += ["--reference", str(r)]
        elif kind == "ort":
            cmd = [PY, str(HERE/"subject_ort.py"), str(ONNX/f"{d.name}.onnx"), "--root", str(d)]
        elif kind == "tvm":
            cmd = [PY, str(HERE/"subject_tvm.py"), str(d)]
        else:
            cmd = [SYS_PY, str(HERE/"subject_onnxmlir.py"), str(d)]
        cmd += ["--inner", "1", "--warmup", "0", "--atol", "1e-4"]
        r = subprocess.run(cmd, capture_output=True, text=True, env=ENV)
        line = [l for l in r.stderr.splitlines() if "max_abs_error" in l]
        verdict = line[-1] if line else f"no validation line (exit {r.returncode})"
        rows.append({"model": d.name, "variant": name, "verdict": verdict,
                     "pass": verdict.endswith("pass")})
        print(f"  {d.name:30s} {name:11s} {verdict}")
out = pathlib.Path("paper/data/revalidation-all-systems.json")
out.write_text(json.dumps({"method": "one run per variant, no timing repeated",
                           "tolerance": 1e-4, "rows": rows}, indent=2) + "\n")
fails = [r for r in rows if not r["pass"]]
print(f"\n共 {len(rows)} 个变体, 通过 {len(rows)-len(fails)}, 失败 {len(fails)}")
for f in fails: print("  失败:", f["model"], f["variant"], f["verdict"][:80])
