#!/usr/bin/env python3
"""Import a pinned ONNX model and prepare it through the documented chain.

    onnx.read -> onnx.nn.convert opt.basic -> c.prepare

Every model in the study goes through this one chain at one compiler revision,
so the locality comparison is not measuring differences between fixture ages.
Also records the ONNX SHA-256 and each stage's time and digest.
"""
import argparse, hashlib, json, subprocess, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
TOOL = REPO / "build/joggle"
# build/modules first: it carries the built native bindings (onnx.read) that the
# source tree alone cannot resolve.
MODS = ["-M", str(REPO / "build/modules"), "-M", str(REPO / "modules")]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def stage(command, out, record):
    start = time.perf_counter()
    result = subprocess.run([str(c) for c in command], capture_output=True)
    elapsed = time.perf_counter() - start
    if result.returncode:
        raise SystemExit(f"failed ({result.returncode}): {' '.join(map(str, command))}\n"
                         f"{result.stderr.decode()[-3000:]}")
    Path(out).write_bytes(result.stdout)
    record.append({"command": [str(c) for c in command], "seconds": elapsed,
                   "output": str(out), "bytes": Path(out).stat().st_size,
                   "sha256": digest(out)})
    return elapsed


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("model", type=Path, help="pinned .onnx file")
    ap.add_argument("out", type=Path, help="output directory")
    ap.add_argument("--entry", default="main")
    ap.add_argument("--batch",
                    help="bind a symbolic entry dimension (e.g. 1) before preparation; "
                         "required by models whose ONNX batch axis is a free variable")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)

    log = {"model": str(a.model), "model_sha256": digest(a.model),
           "entry": a.entry, "batch": a.batch, "stages": []}
    stage([TOOL, "read", "onnx.read", a.model, *MODS], a.out / "imported.jog", log["stages"])
    stage([TOOL, "run", "onnx.nn.convert", "opt.basic", a.out / "imported.jog", *MODS],
          a.out / "semantic.jog", log["stages"])
    source = a.out / "semantic.jog"
    if a.batch:
        stage([TOOL, "run", "opt.instantiate", source, "--arg", f'"{a.entry}"',
               "--arg", f'["{a.batch}"]', *MODS], a.out / "instantiated.jog", log["stages"])
        source = a.out / "instantiated.jog"
    stage([TOOL, "run", "c.prepare", source, *MODS],
          a.out / "prepared.jog", log["stages"])
    (a.out / "prepare.json").write_text(json.dumps(log, indent=1) + "\n")
    print(json.dumps({"model_sha256": log["model_sha256"][:16],
                      "stages": [{"out": Path(s["output"]).name,
                                  "seconds": round(s["seconds"], 2),
                                  "bytes": s["bytes"]} for s in log["stages"]]}, indent=1))


if __name__ == "__main__":
    main()
