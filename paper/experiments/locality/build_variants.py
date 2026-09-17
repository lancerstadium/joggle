#!/usr/bin/env python3
"""Build ordinary and locality-policy C artifacts for one prepared model.

Both variants start from the same prepared subject and differ only by the
source-defined locality.apply policy, so any difference in the emitted C is
attributable to that policy. Weights, plan, placement, and emission use the
identical command chain for both.

Writes <out>/<variant>/{model.c,model.h,weights.bin,model.<so>,api.json} plus
<out>/build.json recording every command, timing, and digest.
"""
import argparse, hashlib, json, platform, subprocess, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
TOOL = REPO / "build/joggle"
MODS = ["-M", str(REPO / "modules"), "-M", str(REPO / "build/modules")]
EXTENSIONS = ["-M", str(REPO / "extensions"), "-M", str(REPO / "modules")]
SO = "dylib" if platform.system() == "Darwin" else "so"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(command, out=None, record=None):
    start = time.perf_counter()
    result = subprocess.run([str(c) for c in command], capture_output=True)
    elapsed = time.perf_counter() - start
    if result.returncode:
        raise SystemExit(f"failed ({result.returncode}): {' '.join(map(str, command))}\n"
                         f"{result.stderr.decode()[-2000:]}")
    if out is not None:
        Path(out).write_bytes(result.stdout)
    if record is not None:
        record.append({"command": [str(c) for c in command], "seconds": elapsed,
                       "output": str(out) if out else None})
    return result.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("prepared", type=Path, help="prepared .jog subject")
    ap.add_argument("out", type=Path, help="output directory")
    ap.add_argument("--cc", default="cc")
    ap.add_argument("--data-arg", default='"weights"')
    ap.add_argument("--planner", default="mem.plan",
                    help="planner entry point; a derived module supplies its own")
    ap.add_argument("--planner-modules", nargs="*", default=[],
                    help="extra -M roots the planner module needs")
    ap.add_argument("--variant", choices=["base", "locality", "both"],
                    default="both",
                    help="build one artifact or the pair; a one-artifact span "
                         "is what a cross-system compile-time comparison needs")
    a = ap.parse_args()

    a.out.mkdir(parents=True, exist_ok=True)
    log = {"prepared": str(a.prepared), "prepared_sha256": digest(a.prepared),
           "host": platform.platform(), "commands": [], "variants": {}}
    commands = log["commands"]

    localised = a.out / "prepared-locality.jog"
    run([TOOL, "run", "locality.apply", a.prepared, *EXTENSIONS],
        out=localised, record=commands)
    log["locality_sha256"] = digest(localised)
    log["locality_changed"] = log["locality_sha256"] != log["prepared_sha256"]

    wanted = [("base", a.prepared)] if a.variant == "base" else \
             [("locality", localised)] if a.variant == "locality" else \
             [("base", a.prepared), ("locality", localised)]
    for variant, subject in wanted:
        d = a.out / variant
        d.mkdir(exist_ok=True)
        steps = []
        planner_mods = []
        for root in a.planner_modules:
            planner_mods += ["-M", root]
        run([TOOL, "run", a.planner, subject, *planner_mods, *MODS],
            out=d / "planned.jog", record=steps)
        run([TOOL, "run", "c.place", d / "planned.jog", "--arg", '"static"', *MODS],
            out=d / "placed.jog", record=steps)
        run([TOOL, "emit", "c.source", d / "placed.jog", "--arg", a.data_arg, *MODS],
            out=d / "model.c", record=steps)
        run([TOOL, "emit", "c.header", d / "placed.jog", "--arg", a.data_arg, *MODS],
            out=d / "model.h", record=steps)
        run([TOOL, "emit", "c.data", d / "placed.jog", *MODS],
            out=d / "weights.bin", record=steps)
        api = run([TOOL, "query", "c.api", d / "placed.jog", "--arg", a.data_arg, *MODS],
                  record=steps)
        (d / "api.json").write_bytes(api)
        run([a.cc, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-fPIC",
             "-c", d / "model.c", "-o", d / "model.o"], record=steps)
        run([a.cc, "-dynamiclib" if SO == "dylib" else "-shared",
             d / "model.o", "-lm", "-o", d / f"model.{SO}"], record=steps)
        commands.extend(steps)
        log["variants"][variant] = {
            "directory": str(d), "steps": steps,
            "sha256": {n: digest(d / n) for n in
                       ("planned.jog", "placed.jog", "model.c", "model.h", "weights.bin")},
            "c_bytes": (d / "model.c").stat().st_size}

    if a.variant == "base":
        log["c_bytes"] = {"base": log["variants"]["base"]["c_bytes"]}
        log["weights_identical"] = True
        a.out.joinpath("build.json").write_text(json.dumps(log, indent=1) + "\n")
        print(json.dumps({"variants": list(log["variants"]),
                          "c_bytes": log["c_bytes"]}, indent=1))
        return
    base, loc = log["variants"]["base"], log["variants"]["locality"]
    log["weights_identical"] = base["sha256"]["weights.bin"] == loc["sha256"]["weights.bin"]
    log["c_identical"] = base["sha256"]["model.c"] == loc["sha256"]["model.c"]
    (a.out / "build.json").write_text(json.dumps(log, indent=1) + "\n")
    print(json.dumps({"locality_changed_subject": log["locality_changed"],
                      "weights_identical": log["weights_identical"],
                      "emitted_c_identical": log["c_identical"],
                      "c_bytes": {k: v["c_bytes"] for k, v in log["variants"].items()}},
                     indent=1))


if __name__ == "__main__":
    main()
