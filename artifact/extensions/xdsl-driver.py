import json
import sys
from pathlib import Path
from types import ModuleType

from xdsl.context import Context
from xdsl.dialects.builtin import Builtin
from xdsl.parser import Parser


def main() -> None:
    implementation, input_path = map(Path, sys.argv[1:])
    # Execute exactly the submitted bytes rather than a timestamp-keyed .pyc
    # left by a previous candidate with the same filename and size.
    candidate = ModuleType("candidate")
    candidate.__file__ = str(implementation)
    sys.modules["candidate"] = candidate
    exec(compile(implementation.read_bytes(), str(implementation), "exec"), candidate.__dict__)
    context = Context()
    context.load_dialect(Builtin)
    module = Parser(context, input_path.read_text()).parse_module()
    module.verify()
    print(json.dumps(candidate.analyze(module), sort_keys=True, allow_nan=False))


if __name__ == "__main__":
    main()
