#!/usr/bin/env python3
"""Measure what the derived storage planner changes in the produced artifact.

The manuscript's only artifact-level consequence for the derivation case is a
declared-slot proxy on one model. This measures two quantities that are not
proxies, on several models: the size of the emitted artifact, and the peak
resident set size of running it, for the installed planner and for the derived
one. Validation against the stored reference is checked on every run, so a
planner whose artifact is smaller but wrong cannot look better.
"""

import argparse, csv, json, re, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent
STUDY = REPO / "build-study" / "locality-matrix"
ART = REPO / "build-study" / "artifact"
TOOL = REPO / "build" / "joggle"
RSS = re.compile(r"(\d+)\s+maximum resident set size")


def build(model, planner, extra_modules, out):
    if out.exists():
        shutil.rmtree(out)
    command = ["python3", "paper/experiments/locality/build_variants.py",
               str(STUDY / model / "prepared.jog"), str(out),
               "--variant", "base", "--planner", planner]
    if extra_modules:
        command += ["--planner-modules", *extra_modules]
    result = subprocess.run(command, cwd=REPO, capture_output=True, text=True)
    return result.returncode == 0, (result.stderr or result.stdout)[-200:]


def measure(model, out):
    """Run the artifact on the study input and report peak RSS and agreement."""
    report = json.loads((STUDY / model / "reference.json").read_text())
    subject = REPO / "paper/experiments/locality/subject_generic.py"
    command = ["/usr/bin/time", "-l", "python3", str(subject), str(out / "base"),
               "--input", str(STUDY / model / "input.bin")]
    for ref in sorted((STUDY / model).glob("reference_*.bin")):
        command += ["--reference", str(ref)]
    command += ["--inner", "1", "--warmup", "3", "--atol", "1e-4"]
    result = subprocess.run(command, cwd=REPO, capture_output=True, text=True)
    rss = RSS.search(result.stderr)
    validation = [l for l in result.stderr.splitlines() if "max_abs_error" in l]
    return {
        "peak_rss_bytes": int(rss.group(1)) if rss else "",
        "validated": bool(validation) and validation[-1].endswith("pass"),
        "validation": validation[-1] if validation else "no validation line",
        "error": "" if result.returncode == 0 else result.stderr[-160:],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", nargs="+",
                    default=["mnist-8", "squeezenet1.1-7", "ultraface-rfb-320",
                             "mobilenetv2-7", "shufflenet-v2-12"])
    ap.add_argument("--out", type=Path,
                    default=ROOT / "data" / "planner-artifact-same-host.csv")
    a = ap.parse_args()

    derived_modules = [str(ART / "modules"), str(REPO / "paper/experiments")]
    rows = []
    for model in a.models:
        for planner, label, extra in (("mem.plan", "installed", []),
                                      ("planned.apply", "derived", derived_modules)):
            out = ART / f"{model}-{label}"
            ok, message = build(model, planner, extra, out)
            row = {"model": model, "planner": label, "build_ok": ok,
                   "c_bytes": "", "peak_rss_bytes": "", "validated": "",
                   "validation": message if not ok else ""}
            if ok:
                build_log = json.loads((out / "build.json").read_text())
                row["c_bytes"] = build_log["c_bytes"]["base"]
                row.update(measure(model, out))
            rows.append(row)
            print(f"{model:20s} {label:10s} build={'ok' if ok else 'FAIL'} "
                  f"bytes={row['c_bytes']} rss={row['peak_rss_bytes']} "
                  f"validated={row['validated']}")

    with a.out.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nwrote {a.out.name}")


if __name__ == "__main__":
    main()
