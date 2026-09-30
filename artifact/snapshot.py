#!/usr/bin/env python3
"""Verify or restore the Git-tracked EuroSys evidence snapshot."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SNAPSHOT = ROOT / "artifact/snapshot"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def restore(destination, verify_only=False):
    manifest = json.loads((SNAPSHOT / "index.json").read_text())
    destination.mkdir(parents=True, exist_ok=True)
    entries = {entry["path"]: entry for entry in manifest["files"]}
    if len(entries) != len(manifest["files"]):
        raise ValueError("duplicate snapshot entry")
    # The joined archive is temporary, not another permanent copy of the data.
    with tempfile.TemporaryFile(dir=destination) as joined:
        for part in manifest["parts"]:
            source = SNAPSHOT / part["name"]
            if source.stat().st_size != part["bytes"] or digest(source) != part["sha256"]:
                raise ValueError(f"damaged archive part: {source.name}")
            with source.open("rb") as stream:
                shutil.copyfileobj(stream, joined)
        joined.seek(0)
        seen = set()
        with tarfile.open(fileobj=joined, mode="r|gz") as archive:
            for member in archive:
                name = PurePosixPath(member.name)
                if (not member.isfile() or name.is_absolute() or ".." in name.parts
                        or member.name not in entries or member.name in seen):
                    raise ValueError(f"unexpected archive member: {member.name}")
                seen.add(member.name)
                entry = entries[member.name]
                if member.size != entry["bytes"]:
                    raise ValueError(f"size differs: {member.name}")
                target = destination.joinpath(*name.parts)
                if not target.resolve().is_relative_to(destination.resolve()):
                    raise ValueError(f"unsafe destination: {member.name}")
                source = archive.extractfile(member)
                if verify_only or target.exists():
                    if hashlib.file_digest(source, "sha256").hexdigest() != entry["sha256"]:
                        raise ValueError(f"content differs: {member.name}")
                    if not verify_only and digest(target) != entry["sha256"]:
                        raise ValueError(f"refusing to overwrite different local file: {target}")
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    # Verify before publishing a file; never replace existing data.
                    with tempfile.NamedTemporaryFile(dir=target.parent, prefix="restore-", delete=False) as output:
                        staging = Path(output.name)
                        shutil.copyfileobj(source, output)
                    try:
                        if digest(staging) != entry["sha256"]:
                            raise ValueError(f"content differs: {member.name}")
                        if target.exists():
                            raise ValueError(f"destination appeared during restore: {target}")
                        staging.rename(target)
                    finally:
                        staging.unlink(missing_ok=True)
        if seen != set(entries):
            raise ValueError("snapshot is incomplete")
    action = "Verified" if verify_only else "Restored"
    print(f"{action} {len(seen)} files ({manifest['uncompressed_bytes']:,} bytes).")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("verify", "restore"))
    parser.add_argument("--destination", type=Path, default=ROOT / "local")
    args = parser.parse_args()
    restore(args.destination.resolve(), args.command == "verify")


if __name__ == "__main__":
    main()
