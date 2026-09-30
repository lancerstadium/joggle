"""Locate retained inputs without rewriting historical provenance records."""

from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]


def record_path(value: str | Path, root: Path = ROOT) -> Path:
    path = Path(value)
    path = path if path.is_absolute() else root / path
    # Frozen records retain their original absolute paths. Rebase them before
    # lookup so a checkout on another machine never depends on the old host.
    index = root / "artifact/snapshot/index.json"
    if not path.is_relative_to(root) and index.is_file():
        recorded_root = Path(json.loads(index.read_text())["recorded_root"])
        if path.is_relative_to(recorded_root):
            path = root / path.relative_to(recorded_root)
    if path.exists():
        return path
    try:
        relative = path.relative_to(root)
    except ValueError:
        return path
    if relative.parts and relative.parts[0] == ".cache":
        return root / "local/cache" / Path(*relative.parts[1:])
    if relative.parts and (relative.parts[0] == "build" or relative.parts[0].startswith("build-")):
        return root / "local/legacy-builds" / relative
    return path
