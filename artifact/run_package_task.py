#!/usr/bin/env python3
"""Build, install and check one native package against a frozen change contract.

Parent and changed runs use identical fixtures and observers. Outputs contain
the actual source patch, tool commands, graph observations and runtime vectors.
This is a correctness/footprint experiment, not a compilation timing benchmark.
"""

from __future__ import annotations

import argparse
import csv
import difflib
import json
from pathlib import Path
import shutil
import sys

import numpy as np

import package_cases as cases
from run_extension_task import (QCONV_DRIVER, QINT4_DRIVER, digest, execute,
                                graph_fixture, graph_manifest, qconv_parameters)


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n")


def runtime(change: str, request: dict, source: str, root: Path, cc: str) -> dict:
    lowbit = change.startswith("lowbit-")
    kernel, driver, binary = root / "kernel.c", root / "driver.c", root / "run"
    kernel.write_text(source)
    if lowbit:
        driver.write_text(QINT4_DRIVER)
    else:
        # A parent may still emit stride-one code. Allocate its maximum output
        # extent, but guard EVERY byte after the contracted stride-two extent.
        # Wrong output geometry becomes a checked failure, not a buffer overrun.
        p = qconv_parameters(request)
        capacity = int(np.prod(p["output_shape"]))
        text = QCONV_DRIVER.replace("malloc(ny+2)", f"malloc({capacity}+2)")
        text = text.replace("memset(y,85,ny+2)", f"memset(y,85,{capacity}+2)")
        text = text.replace("  for (size_t i=0;i<nx;i++) if", 
            f"  for (size_t i=ny+1;i<{capacity}+2;i++) if (y[i]!=85) return 4;\n"
            "  for (size_t i=0;i<nx;i++) if")
        driver.write_text(text)
    result = {"source_sha256": digest(kernel), "driver_sha256": digest(driver),
              "passed": False, "runs": []}
    result["build"] = execute([cc, "-std=c99", "-O2", "-fno-fast-math",
                               "-ffp-contract=off", kernel, driver, "-lm", "-o", binary], 120)
    if result["build"]["exit_code"] != 0:
        return result
    result["executable_sha256"] = digest(binary)
    if lowbit:
        count = len(request["lhs"])
        probes = [request] + [{
            "lhs": [(pair//16 + lane) % 16 - 8 for lane in range(count)],
            "rhs": [(pair % 16 + 2*lane) % 16 - 8 for lane in range(count)]}
            for pair in range(256) if count]
        run = execute([binary, *map(str, request["lhs"]), *map(str, request["rhs"])], 30)
        expected = [cases.lowbit_expected(change, probe) for probe in probes]
        try:
            observed = [json.loads(line) for line in run["stdout"].splitlines()]
        except ValueError:
            observed = None
        result.update(run=run, expected=expected, observed=observed,
                      probes=len(probes), passed=run["exit_code"] == 0 and observed == expected)
        return result
    p = qconv_parameters(request)
    rng = np.random.default_rng(20260924)
    probes = [request] + [{**request,
        "x": rng.integers(-128, 128, size=p["x"].shape).tolist(),
        "w": rng.integers(-128, 128, size=p["w"].shape).tolist(),
        "bias": rng.integers(-256, 257, size=p["bias"].shape).tolist()}
        for _ in range(32)]
    for probe in probes:
        p = qconv_parameters(probe)
        arrays = [p[key].reshape(-1).tolist() for key in ("x", "w", "bias")]
        expected = cases.qconv_expected(change, probe)
        run = execute([binary, *map(str, [*map(len, arrays), len(expected)]),
                       *(str(value) for array in arrays for value in array)], 30)
        try:
            observed = [int(value) for value in run["stdout"].split()]
        except ValueError:
            observed = None
        result["runs"].append({**run, "expected": expected, "observed": observed,
                               "passed": run["exit_code"] == 0 and observed == expected})
    result.update(probes=len(probes), passed=all(run["passed"] for run in result["runs"]))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", required=True)
    parser.add_argument("--system", choices=["Joggle", "MLIR", "xDSL"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--parent", action="store_true")
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--joggle", type=Path, default=Path("build/joggle"))
    parser.add_argument("--builtin-mods", type=Path, default=Path("build/modules"))
    parser.add_argument("--xdsl-python", type=Path, default=Path(sys.executable))
    parser.add_argument("--mlir-dir", type=Path, required=True)
    parser.add_argument("--cc", default=shutil.which("cc"))
    args = parser.parse_args()
    repo, root = args.source_root.resolve(), args.output.resolve()
    root.mkdir(parents=True, exist_ok=False)
    manifest_path = repo / "artifact/manifests/package-changes.json"
    manifest = json.loads(manifest_path.read_text())
    contract = next(c for c in manifest["cases"] if c["id"] == args.case)
    spec_path = repo / "artifact/manifests/extension-specs.json"
    original = next(t for t in json.loads(spec_path.read_text())["tasks"]
                    if t["id"] == contract["feature"])
    fixtures = cases.fixtures(args.case, original)
    write_json(root / "fixtures.json", fixtures)
    source = root / "source"
    source.mkdir()
    files, patches = [], []
    source_manifest = repo / "artifact/manifests/package-sources.csv"
    for entry in csv.DictReader(source_manifest.open()):
        if entry["feature"] != contract["feature"] or entry["system"] != args.system:
            continue
        path = repo / entry["source"]
        before = path.read_text()
        after = (cases.candidate_source(before, args.system, args.case)
                 if entry["role"] == "implementation" and not args.parent else before)
        target = source / entry["deployed_path"]
        target.write_text(after)
        baseline = "" if contract["kind"] == "integration" else before
        patch = "".join(difflib.unified_diff(baseline.splitlines(True), after.splitlines(True),
                     fromfile="a/"+entry["deployed_path"], tofile="b/"+entry["deployed_path"]))
        patches.append(patch)
        changed = [line for line in patch.splitlines() if line[:1] in ("+", "-")
                   and not line.startswith(("+++", "---"))]
        files.append({**entry, "before_sha256": digest(path), "after_sha256": digest(target),
                      "added": sum(line.startswith("+") for line in changed),
                      "deleted": sum(line.startswith("-") for line in changed)})
    (root / "change.patch").write_text("".join(patches))
    report = {"schema": "native-package-task/v1", "case": args.case, "system": args.system,
              "parent": args.parent, "contract": contract, "sources": files,
              "revision": execute(["git", "-C", repo, "rev-parse", "HEAD"], 10)["stdout"].strip(),
              "hashes": {str(path.relative_to(repo)): digest(path) for path in
                         [manifest_path, spec_path, source_manifest, Path(__file__).resolve(),
                          Path(cases.__file__).resolve(), repo / "artifact/run_extension_task.py"]},
              "fixtures_sha256": digest(root / "fixtures.json"),
              "patch_sha256": digest(root / "change.patch"), "setup": [], "cases": [], "passed": False}
    write_json(root / "result.json", report)
    # Keep the venv path: resolving its interpreter symlink loses site-packages.
    python = args.xdsl_python.absolute()
    joggle = args.joggle.resolve()
    toolchain = args.mlir_dir.resolve().parents[2]
    mlir_opt = toolchain / "bin/mlir-opt"
    report["tool_sha256"] = digest(joggle if args.system == "Joggle" else
                                   mlir_opt if args.system == "MLIR" else python)
    feature = "lowbit" if args.case.startswith("lowbit-") else "qconv"
    pass_name = "study-lowbit-lower" if feature == "lowbit" else "study-qconv-fuse"
    site = root / "site"

    def call(argv, timeout=120):
        if args.system == "xDSL" and str(argv[0]) == str(python):
            argv = ["env", "PYTHONNOUSERSITE=1", "PYTHONPATH="+str(site), *argv]
        return execute(argv, timeout)

    def setup(argv):
        step = call(argv, 300)
        report["setup"].append(step)
        write_json(root / "result.json", report)
        if step["exit_code"] != 0:
            raise RuntimeError(step["stderr"][-2000:])

    try:
        installed = root / "installed"
        if args.system == "Joggle":
            setup([joggle, "mod", "install", source, installed, "-M", args.builtin_mods.resolve()])
            observer = root / "observers/observer/module.jog"
            observer.parent.mkdir(parents=True)
            observer_source = (repo / "artifact/extensions/emit-graph-manifest/reference.jog").read_text()
            observer_source = observer_source.replace("mod extension", "mod observer", 1)
            observer_source = observer_source.replace("  for op in ir.ops(subject) {",
                '  for op in ir.ops(subject) {\n    assert(ir.kind(op) == "call" || ir.kind(op) == "return", "unexpected operation")')
            observer.write_text(observer_source)
            flags = ["-M", args.builtin_mods.resolve(), "-M", installed, "-M", root / "observers"]
        elif args.system == "xDSL":
            setup([python, "-m", "pip", "wheel", "--no-deps", "--wheel-dir", root / "wheels", source])
            wheel, = (root / "wheels").glob("*.whl")
            setup([python, "-m", "pip", "install", "--no-deps", "--target", site, wheel])
        else:
            setup(["cmake", "-S", source, "-B", root / "build", "-G", "Unix Makefiles",
                   "-DMLIR_DIR="+str(args.mlir_dir.resolve()), "-DCMAKE_BUILD_TYPE=Release",
                   "-DCMAKE_INSTALL_PREFIX="+str(installed)])
            setup(["cmake", "--build", root / "build", "--parallel", "2"])
            setup(["cmake", "--install", root / "build"])
            plugin, = (installed / "lib").glob("Study*.*")
        checks = root / "checks"
        checks.mkdir()
        for fixture in fixtures:
            work = checks / fixture["id"]
            work.mkdir()
            request = fixture["input"]
            suffix = ".jog" if args.system == "Joggle" else ".mlir"
            before, after = work / ("before"+suffix), work / ("after"+suffix)
            before.write_text(graph_fixture(cases.graph(args.case, request), args.system))
            row = {"case": fixture["id"], "passed": False}
            report["cases"].append(row)
            if args.system == "Joggle":
                step = call([joggle, "run", "extension.transform", before, *flags])
            elif args.system == "xDSL":
                step = call([python, "-m", "xdsl.tools.xdsl_opt", before, "--passes", pass_name])
            else:
                step = call([mlir_opt, "--load-pass-plugin="+str(plugin), before,
                             "--pass-pipeline=builtin.module("+pass_name+")"])
            row["transform"] = step
            if "error" in fixture["expect"]:
                row["passed"] = (step["exit_code"] not in (None, 0) and not step["timeout"]
                                 and fixture["expect"]["error"] in step["stderr"])
            elif step["exit_code"] == 0:
                after.write_text(step["stdout"])
                observation = call([joggle, "query", "observer.analyze", after, *flags]
                    if args.system == "Joggle" else
                    [python, repo / "artifact/extensions/xdsl-driver.py", "--inspect", after])
                row["observation"] = observation
                expected = graph_manifest(cases.graph(args.case, request, transformed=True))
                row["expected_graph"] = expected
                try:
                    observed = json.loads(observation["stdout"])
                    row["graph_passed"] = observation["exit_code"] == 0 and observed == expected
                except ValueError:
                    row["graph_passed"] = False
                if request.get("conv_uses", 1) != 1:
                    row["passed"] = row["graph_passed"]
                else:
                    if args.system == "Joggle":
                        emission = call([joggle, "query", "extension.analyze", after, *flags])
                        emitted = json.loads(emission["stdout"]) if emission["exit_code"] == 0 else {}
                        code = emitted.get("source", "")
                    elif args.system == "xDSL":
                        emission = call([python, "-m", "xdsl.tools.xdsl_opt", after,
                                         "-t", "study-"+feature+"-c"])
                        code = emission["stdout"]
                    else:
                        output = work / "emitted.c"
                        emission = call([mlir_opt, "--load-pass-plugin="+str(plugin), after,
                            "--pass-pipeline=builtin.module(study-"+feature+"-c{output="+str(output)+"})"])
                        code = output.read_text() if output.exists() else ""
                    row["emission"] = emission
                    if emission["exit_code"] == 0 and code:
                        row["runtime"] = runtime(args.case, request, code, work, args.cc)
                        row["passed"] = row["graph_passed"] and row["runtime"]["passed"]
            write_json(root / "result.json", report)
        report["passed"] = all(row["passed"] for row in report["cases"])
        report["runtime_probes"] = sum(row.get("runtime", {}).get("probes", 0) for row in report["cases"])
    except Exception as failure:
        report["error"] = str(failure)
    write_json(root / "result.json", report)
    print(json.dumps({key: report.get(key) for key in
                     ("case", "system", "parent", "passed", "runtime_probes", "error")}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
