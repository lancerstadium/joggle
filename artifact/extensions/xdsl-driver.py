import json
import sys
from pathlib import Path
from types import ModuleType

from xdsl.context import Context
from xdsl.dialects.builtin import Builtin
from xdsl.dialects.func import Func
from xdsl.parser import Parser


def main() -> None:
    inspect = sys.argv[1] == "--inspect"
    if inspect:
        implementation = Path(__file__).parent / "emit-graph-manifest/reference.py"
        input_path = Path(sys.argv[2])
    else:
        implementation, input_path = map(Path, sys.argv[1:3])
    rewrite = not inspect and sys.argv[3:] == ["--rewrite"]
    definition = not inspect and sys.argv[3:] == ["--definition"]
    # Execute exactly the submitted bytes rather than a timestamp-keyed .pyc
    # left by a previous candidate with the same filename and size.
    candidate = ModuleType("candidate")
    candidate.__file__ = str(implementation)
    sys.modules["candidate"] = candidate
    exec(compile(implementation.read_bytes(), str(implementation), "exec"), candidate.__dict__)
    context = Context()
    context.load_dialect(Builtin)
    context.load_dialect(Func)
    if definition:
        candidate.register(context)
    module = Parser(context, input_path.read_text()).parse_module()
    module.verify()
    if definition:
        from io import StringIO
        from xdsl.printer import Printer
        from xdsl.dialects.func import FuncOp
        output = StringIO()
        Printer(stream=output).print_op(module)
        module = Parser(context, output.getvalue()).parse_module()
        module.verify()
        subject = next(op for op in module.ops if isinstance(op, FuncOp) and op.sym_name.data == "subject")
        print(json.dumps({"types": [str(value.type) for value in subject.body.block.args]}))
    elif rewrite:
        from xdsl.printer import Printer
        candidate.transform(module)
        module.verify()
        Printer().print_op(module)
    else:
        if inspect:
            from xdsl.dialects.func import CallOp, FuncOp, ReturnOp
            subject = next(op for op in module.ops
                           if isinstance(op, FuncOp) and op.sym_name.data == "subject")
            if len(subject.body.blocks) != 1 or any(
                    not isinstance(op, (CallOp, ReturnOp)) for op in subject.body.block.ops):
                raise ValueError("rewrite result contains unsupported control or operations")
        print(json.dumps(candidate.analyze(module), sort_keys=True, allow_nan=False))


if __name__ == "__main__":
    main()
