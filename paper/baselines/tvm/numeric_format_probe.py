#!/usr/bin/env python3
"""Probe the pinned TVM custom-datatype boundary for the numeric task."""

import argparse
import json
from pathlib import Path

import tvm


def attempt(action) -> dict[str, str]:
    try:
        return {"status": "pass", "value": str(action())}
    except Exception as error:  # The exception type and text are evidence here.
        message = str(error)
        if isinstance(error, ImportError):
            message = message.split(" (", 1)[0]
        return {
            "status": "unsupported",
            "diagnostic": f"{type(error).__name__}: {message}",
        }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("contract", type=Path)
    parser.add_argument("format_map", type=Path)
    args = parser.parse_args()

    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    format_map = json.loads(args.format_map.read_text(encoding="utf-8"))
    minimum = int(contract["valid_widths"]["minimum"])
    maximum = int(contract["valid_widths"]["maximum"])
    widths = [int(item["width"]) for item in format_map["operations"]]
    if (
        format_map.get("schema") != 1
        or format_map.get("type") != contract["type"]
        or not widths
        or any(width < minimum or width > maximum for width in widths)
    ):
        raise ValueError("format map does not satisfy the frozen contract")

    package = Path(tvm.__file__).resolve().parent

    def import_registration_module() -> str:
        from tvm.target import datatype  # type: ignore[attr-defined]

        return str(datatype)

    report = {
        "task": "numeric-format",
        "status": "unsupported",
        "blocking_requirement": (
            "define signed sat<W> for widths 2 through 63 with saturating "
            "addition"
        ),
        "probe": {
            "target_datatype_module": attempt(import_registration_module),
            "parametric_dtype": attempt(lambda: tvm.DataType("custom[sat]5")),
            "registration_source_present": (
                package / "target" / "datatype.py"
            ).is_file(),
        },
    }
    if any(
        item.get("status") == "pass"
        for item in (
            report["probe"]["target_datatype_module"],
            report["probe"]["parametric_dtype"],
        )
    ) or report["probe"]["registration_source_present"]:
        raise RuntimeError("TVM exposes a custom-datatype path; implement the task")
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
