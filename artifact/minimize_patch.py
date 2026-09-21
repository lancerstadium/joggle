#!/usr/bin/env python3
"""Reduce a passing Git patch to a deterministic hunk-level fixed point."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path


def git(repo: Path, args: list[str], *, input_text: str | None = None) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        input=input_text,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout


def split_hunks(patch: str) -> list[tuple[str, str]]:
    if "GIT binary patch" in patch or "Binary files " in patch:
        raise SystemExit("binary patches cannot be hunk-minimized")
    lines = patch.splitlines(keepends=True)
    hunks: list[tuple[str, str]] = []
    index = 0
    while index < len(lines):
        if not lines[index].startswith("diff --git "):
            raise SystemExit("unexpected text before diff header")
        end = index + 1
        while end < len(lines) and not lines[end].startswith("diff --git "):
            end += 1
        file_lines = lines[index:end]
        starts = [offset for offset, line in enumerate(file_lines)
                  if line.startswith("@@ ")]
        if not starts:
            raise SystemExit("mode-only or header-only changes are outside hunk minimization")
        header = "".join(file_lines[:starts[0]])
        for position, start in enumerate(starts):
            stop = starts[position + 1] if position + 1 < len(starts) else len(file_lines)
            value = header + "".join(file_lines[start:stop])
            identity = hashlib.sha256(value.encode()).hexdigest()[:16]
            hunks.append((identity, value))
        index = end
    if not hunks:
        raise SystemExit("patch contains no textual hunks")
    identities = [identity for identity, _value in hunks]
    if len(identities) != len(set(identities)):
        raise SystemExit("patch contains duplicate hunk identities")
    return hunks


def apply(worktree: Path, patch: str, *, reverse: bool = False) -> None:
    args = ["apply", "--recount", "--whitespace=nowarn"]
    if reverse:
        args.append("--reverse")
    git(worktree, args, input_text=patch)


def oracle(worktree: Path, argv: list[str], timeout: float) -> tuple[int, float, str]:
    begin = time.monotonic_ns()
    try:
        result = subprocess.run(
            argv,
            cwd=worktree,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired as error:
        raise SystemExit(
            f"oracle exceeded the {timeout:g}-second timeout; minimization aborted"
        ) from error
    elapsed = (time.monotonic_ns() - begin) / 1e9
    return result.returncode, elapsed, result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--output-patch", type=Path, required=True)
    parser.add_argument("--oracle-log", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--oracle-timeout", type=float, default=1800.0)
    parser.add_argument("oracle", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.oracle[:1] == ["--"]:
        args.oracle = args.oracle[1:]
    if not args.oracle:
        raise SystemExit("provide the oracle command after --")
    if args.oracle_timeout <= 0:
        raise SystemExit("--oracle-timeout must be positive")
    repo = args.repo.resolve()
    base = git(repo, ["rev-parse", f"{args.base}^{{commit}}"] ).strip()
    head = git(repo, ["rev-parse", f"{args.head}^{{commit}}"] ).strip()
    original = git(
        repo,
        ["diff", "--no-ext-diff", "--no-renames", "--no-color", "--unified=3",
         base, head, "--"],
    )
    hunks = split_hunks(original)
    trials: list[dict[str, object]] = []

    with tempfile.TemporaryDirectory(prefix="joggle-minimize-") as temporary:
        worktree = Path(temporary) / "worktree"
        git(repo, ["worktree", "add", "--detach", str(worktree), base])
        try:
            apply(worktree, original)
            code, elapsed, output = oracle(worktree, args.oracle, args.oracle_timeout)
            trials.append({"phase": "initial", "returncode": code,
                           "seconds": elapsed,
                           "output_sha256": hashlib.sha256(output.encode()).hexdigest()})
            if code != 0:
                raise SystemExit("the original patch does not pass the oracle")

            retained = list(hunks)
            changed = True
            sweep = 0
            while changed:
                changed = False
                sweep += 1
                for identity, value in list(retained):
                    apply(worktree, value, reverse=True)
                    code, elapsed, output = oracle(
                        worktree, args.oracle, args.oracle_timeout
                    )
                    removed = code == 0
                    trials.append(
                        {
                            "phase": "trial",
                            "sweep": sweep,
                            "hunk": identity,
                            "removed": removed,
                            "returncode": code,
                            "seconds": elapsed,
                            "output_sha256": hashlib.sha256(output.encode()).hexdigest(),
                        }
                    )
                    if removed:
                        retained.remove((identity, value))
                        changed = True
                    else:
                        apply(worktree, value)

            final_patch = git(
                worktree,
                ["diff", "--no-ext-diff", "--no-renames", "--no-color",
                 "--binary", base, "--"],
            )
            if not final_patch:
                raise SystemExit("oracle permits an empty patch; task is not discriminating")
            code, elapsed, final_output = oracle(
                worktree, args.oracle, args.oracle_timeout
            )
            if code != 0:
                raise SystemExit("final minimized patch failed the oracle")
            trials.append({"phase": "final", "returncode": code,
                           "seconds": elapsed,
                           "output_sha256": hashlib.sha256(final_output.encode()).hexdigest()})
        finally:
            git(repo, ["worktree", "remove", "--force", str(worktree)])

    for path in (args.output_patch, args.oracle_log, args.log):
        path.parent.mkdir(parents=True, exist_ok=True)
    args.output_patch.write_text(final_patch, encoding="utf-8")
    args.oracle_log.write_text(final_output, encoding="utf-8")
    record = {
        "schema": "hunk-minimization/v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "repo": str(repo),
        "base": base,
        "head": head,
        "oracle": args.oracle,
        "oracle_timeout_seconds": args.oracle_timeout,
        "original_patch_sha256": hashlib.sha256(original.encode()).hexdigest(),
        "final_patch_sha256": hashlib.sha256(final_patch.encode()).hexdigest(),
        "original_hunks": [identity for identity, _value in hunks],
        "retained_hunks": [identity for identity, _value in retained],
        "trials": trials,
    }
    args.log.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"retained {len(retained)} of {len(hunks)} hunks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
