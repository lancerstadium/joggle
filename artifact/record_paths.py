"""Locate retained inputs without rewriting historical provenance records."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def record_path(value: str | Path, root: Path = ROOT) -> Path:
    path = Path(value)
    path = path if path.is_absolute() else root / path
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
