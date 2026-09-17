#!/usr/bin/env python3
"""Compare a source-patch overlay with the existing body-derivation recipe.

Generated source snapshots stay in build-study. This is a local mechanism
control, not an external-framework or programmer-productivity benchmark.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


BASE = "62ed2a83efd47021a3272620aa60b2a041bd3964"
REPO = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(args, cwd=REPO):
    result = subprocess.run([str(arg) for arg in args], cwd=cwd,
                            capture_output=True, timeout=60)
    return result.returncode, result.stdout, result.stderr.decode()


def store(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def snapshot(data):
    # Explicit source boundaries for this control, not another DSL parser.
    # Copy only plan(Mod, Fn) and its private helper closure, not unrelated APIs.
    text = data.decode()
    elements = text[text.index("local fn elements("):text.index("local fn interval(")]
    planner = text[text.index("local fn allocated("):text.index("fn plan(m: Mod) -> bool")]
    planner = planner.replace("fn plan(m: Mod, fn: Fn)", "fn ranked(m: Mod, fn: Fn)")
    driver = (HERE / "compiler.jog").read_text()
    driver = driver[driver.index("fn apply("):].replace("mem.plan(m, fn)", "ranked(m, fn)")
    return ("module copied\nuse base\nuse ir\nuse tensor\nuse mem\nuse reuse\n\n" +
            elements + planner + driver).encode()


def rename(text, names):
    # Perturb identifiers only; strings contain semantic keys such as mem.counts.
    pattern = r'"(?:\\.|[^"\\])*"|//[^\n]*|\b[a-zA-Z_][a-zA-Z_0-9]*\b'
    return re.sub(pattern, lambda match: names.get(match[0], match[0]), text).encode()


def bindings(text, names):
    first = text.index("fn plan(m: Mod, fn: Fn)")
    last = text.index("fn plan(m: Mod) -> bool")
    return text[:first].encode() + rename(text[first:last], names) + text[last:].encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=REPO / "build-study/derivation/control")
    parser.add_argument("--cc", default="clang")
    parser.add_argument("--model", type=Path,
                        default=REPO / "build-study/derivation/ultraface/prepared.jog")
    parser.add_argument("--expected", type=Path,
                        default=REPO / "build-study/derivation/ultraface/derived/planned.jog")
    args = parser.parse_args()
    root = args.out.resolve()
    # Keep generated compiler snapshots away from installed modules and sources.
    if REPO / "build-study" not in root.parents:
        parser.error("--out must be beneath the repository's build-study directory")
    source = (REPO / "modules/mem/module.jog").read_bytes()
    status, historical, error = run(["git", "show", f"{BASE}:modules/mem/module.jog"])
    if status:
        raise RuntimeError(error)
    text = source.decode()
    cases = {
        "current": source,
        "historical": historical,
        "helper_rename": rename(text, {"use_ends": "lifetime_ends"}),
        "operand_rename": bindings(text, {"counts": "sizes"}),
        "binding_rename": bindings(text, {name: "renamed_" + name for name in
                                        ("selected", "slot", "item", "counts", "slot_counts",
                                         "slot_types", "types", "slot_ends", "firsts")}),
        "guard_change": text.replace("if selected < 0 &&", "if selected <= 0 &&").encode(),
    }
    if any(cases[name] == source for name in ("helper_rename", "operand_rename", "guard_change")):
        raise ValueError("A source perturbation did not change its input")
    report = {"scope": "local source-patch versus body-derivation control",
              "historical_commit": BASE, "source_sha256": sha(source),
              "checker_sha256": sha(Path(__file__).read_bytes()),
              "patch_sha256": sha((HERE / "source.patch").read_bytes()),
              "recipe_sha256": sha((HERE / "module.jog").read_bytes()),
              "cases": {}}
    tool = REPO / "build/joggle"
    standard = ["-M", REPO / "modules", "-M", REPO / "build/modules",
                "-M", REPO / "paper/experiments"]

    def oracle(route, modules, destination):
        command = ["cmake", f"-DTOOL={tool}", f"-DCC={args.cc}",
                   f"-DMODEL={REPO / 'test/data/mem_safety.jog'}",
                   f"-DHARNESS={REPO / 'test/data/mem_safety_main.c'}",
                   "-DMODULES=" + ";".join(str(p) for p in modules),
                   f"-DROOT={destination}", f"-DPLANNER={route}",
                   "-P", REPO / "test/mem_safety.cmake"]
        status, output, error = run(command)
        return {"status": status, "stdout": output.decode(), "stderr": error}

    for name, data in cases.items():
        case = root / name
        upstream = case / "upstream"
        store(upstream / "mem/module.jog", data)
        copied = case / "copy/copied/module.jog"
        store(copied, snapshot(data))
        # No git index or tracked source is changed; apply only to a generated snapshot.
        status, _, error = run(["git", "apply", "--ignore-whitespace",
                                "--directory=" + str(copied.parent.relative_to(REPO)),
                                HERE / "source.patch"])
        copy_record = {"patch_status": status, "patch_stderr": error}
        search = ["-M", upstream, *standard]
        if status == 0:
            status, _, error = run([tool, "module", "check", "copied", "-M", copied.parent.parent, *search])
            copy_record.update({"check_status": status, "check_stderr": error,
                                "materialized_sha256": sha(copied.read_bytes())})
        copied_ok = copy_record.get("check_status") == 0
        derived = case / "derive/planned/module.jog"
        status, output, error = run([tool, "run", "reuse.derive", HERE / "compiler.jog", *search])
        derive_record = {"status": status, "stderr": error, "output_bytes": len(output)}
        if status == 0:
            store(derived, output)
            derive_record["materialized_sha256"] = sha(output)
        elif output:
            raise ValueError(f"{name}: failed derivation published output")
        derived_ok = status == 0
        copy_expected = name in ("current", "historical", "helper_rename")
        derive_expected = name != "guard_change"
        if (copied_ok, derived_ok) != (copy_expected, derive_expected):
            raise ValueError(f"{name}: unexpected construction outcome: {copy_record}, {derive_record}")
        entry = {"source_sha256": sha(data),
                 "expected_construction_success": {"copy": copy_expected, "derive": derive_expected},
                 "expected_oracle_success": name != "historical" if derive_expected else None,
                 "copy": copy_record, "derive": derive_record}
        report["cases"][name] = entry
        if not derive_expected:
            continue
        for route, directory, planner in (("copy", copied.parent.parent, "copied.apply"),
                                           ("derive", derived.parent.parent, "planned.apply")):
            if route == "copy" and not copied_ok:
                continue
            modules = [directory, upstream, REPO / "modules", REPO / "build/modules",
                       REPO / "paper/experiments"]
            result = oracle(planner, modules, case / route / "safety")
            entry[route]["oracle"] = result
            expected_success = name != "historical"
            if (result["status"] == 0) != expected_success:
                raise ValueError(f"{name}/{route}: unexpected numerical outcome: {result}")
            if not expected_success and "planned memory safety oracle failed" not in result["stderr"]:
                raise ValueError(f"{name}/{route}: failed before executing the oracle")
        if name != "historical":
            plans = []
            for route, directory, planner in (("copy", copied.parent.parent, "copied.apply"),
                                               ("derive", derived.parent.parent, "planned.apply")):
                if route == "copy" and not copied_ok:
                    continue
                status, output, error = run([tool, "run", planner, args.model,
                                             "-M", directory, *search])
                if status:
                    raise RuntimeError(error)
                store(case / route / "model.jog", output)
                entry[route]["model_sha256"] = sha(output)
                plans.append(output)
            entry["all_successful_plans_match_reference"] = all(
                plan == args.expected.read_bytes() for plan in plans)
            if not entry["all_successful_plans_match_reference"]:
                raise ValueError(f"{name}: model plan differs from the checked UltraFace artifact")

    # Old compiler bodies do not update just because the installed mem source changes.
    # Both routes retain their generated private implementation until refreshed.
    report["stale"] = {}
    for route, planner in (("copy", "copied.apply"), ("derive", "planned.apply")):
        directory = root / "historical" / route
        modules = [directory, REPO / "modules", REPO / "build/modules", REPO / "paper/experiments"]
        result = oracle(planner, modules, root / "stale" / route)
        if result["status"] == 0 or "planned memory safety oracle failed" not in result["stderr"]:
            raise ValueError(f"{route}: stale snapshot did not reproduce the old oracle failure")
        report["stale"][route] = result
    if (REPO / "modules/mem/module.jog").read_bytes() != source:
        raise ValueError("Installed source was modified")
    report["model_sha256"] = sha(args.model.read_bytes())
    report["expected_plan_sha256"] = sha(args.expected.read_bytes())
    report["control_conditions_met"] = True
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
