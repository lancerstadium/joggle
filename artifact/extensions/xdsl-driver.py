import json
import re
import sys
from pathlib import Path
from types import ModuleType

from xdsl.context import Context
from xdsl.dialects.builtin import Builtin
from xdsl.dialects.func import Func
from xdsl.parser import Parser


def inspect_definition(path, target="extension.qadd"):
    from xdsl.dialects.builtin import FloatAttr, IntegerAttr, StringAttr, UnregisteredOp, f64, i32
    from xdsl.dialects.func import FuncOp, ReturnOp
    context = Context(allow_unregistered=True)
    context.load_dialect(Builtin); context.load_dialect(Func)
    module = Parser(context, path.read_text()).parse_module()
    module.verify()
    subject = next(op for op in module.ops if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    if len(subject.body.blocks) != 1: raise ValueError("unexpected-definition-structure")
    block = subject.body.block
    ops = list(block.ops)
    arity = {"extension.qadd": 2, "extension.relayout": 1, "extension.dot4_i8": 3}[target]
    if (len(ops) != 2 or len(block.args) != arity or not isinstance(ops[0], UnregisteredOp) or
            ops[0].op_name.data != target or ops[0].regions or
            tuple(ops[0].operands) != tuple(block.args) or len(ops[0].results) != 1 or
            not isinstance(ops[1], ReturnOp) or tuple(ops[1].arguments) != tuple(ops[0].results)):
        raise ValueError("unexpected-definition-structure")
    attributes = {}
    for name, value in ops[0].attributes.items():
        if name == "op_name__": continue
        if isinstance(value, FloatAttr) and value.type == f64:
            attributes[name] = value.value.data
        elif isinstance(value, IntegerAttr) and value.type == i32:
            attributes[name] = value.value.data
        elif isinstance(value, StringAttr):
            attributes[name] = value.data
        else:
            raise ValueError("unexpected-definition-attribute")
    print(json.dumps({"inputs": [str(value.type) for value in block.args],
                      "result": str(ops[0].results[0].type), "attributes": attributes}, allow_nan=False))


def main() -> None:
    if sys.argv[1] == "--inspect-definition":
        inspect_definition(Path(sys.argv[2]), sys.argv[3] if len(sys.argv) == 4 else "extension.qadd")
        return
    inspect = sys.argv[1] in ("--inspect", "--inspect-input-format")
    if inspect:
        implementation = Path(__file__).parent / "emit-graph-manifest/reference.py"
        input_path = Path(sys.argv[2])
    else:
        implementation, input_path = map(Path, sys.argv[1:3])
    rewrite = not inspect and sys.argv[3:] == ["--rewrite"]
    importing = not inspect and sys.argv[3:] == ["--input-format"]
    construct = not inspect and sys.argv[3:] == ["--construct"]
    definition = not inspect and sys.argv[3:] in (["--definition"], ["--construct"])
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
    if importing:
        from io import StringIO
        from xdsl.dialects.builtin import ModuleOp
        from xdsl.printer import Printer
        module = candidate.read(input_path.read_text(), context)
        if not isinstance(module, ModuleOp):
            raise ValueError("importer must return a native ModuleOp")
        module.verify()
        first = StringIO()
        Printer(stream=first).print_op(module)
        reparsed = Parser(context, first.getvalue()).parse_module()
        reparsed.verify()
        second = StringIO()
        Printer(stream=second).print_op(reparsed)
        if first.getvalue() != second.getvalue():
            raise ValueError("unstable-native-roundtrip")
        print(second.getvalue())
        return
    module = Parser(context, input_path.read_text()).parse_module()
    module.verify()
    if definition:
        from io import StringIO
        from xdsl.printer import Printer
        from xdsl.dialects.func import FuncOp, ReturnOp
        if construct:
            from xdsl.dialects.builtin import UnregisteredOp
            subject = next(op for op in module.ops if isinstance(op, FuncOp) and op.sym_name.data == "subject")
            block = subject.body.block
            terminator = block.last_op
            operation = candidate.construct(tuple(block.args), dict(module.attributes["study.attributes"].data))
            if (operation.parent is not None or len(operation.results) != 1 or
                    isinstance(operation, UnregisteredOp) or context.get_op(operation.name) is not type(operation)):
                raise ValueError("constructor must return one detached single-result operation")
            block.insert_op_before(operation, terminator)
            block.insert_op_before(ReturnOp(operation.results[0]), terminator)
            block.erase_op(terminator)
            subject.update_function_type()
            module.verify()
        output = StringIO()
        Printer(stream=output).print_op(module)
        module = Parser(context, output.getvalue()).parse_module()
        module.verify()
        subject = next(op for op in module.ops if isinstance(op, FuncOp) and op.sym_name.data == "subject")
        if construct:
            Printer().print_op(module)
        else:
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
        observed = candidate.analyze(module)
        if sys.argv[1] == "--inspect-input-format":
            # Importers may monomorphize external primitive declarations.
            # Normalize only this reserved spelling; retain types and all edges.
            for node in observed["nodes"]:
                match = re.fullmatch(r"(splat|add|mul|relu)__type\d+", node["op"])
                if match:
                    node["op"] = match[1]
        print(json.dumps(observed, sort_keys=True, allow_nan=False))


if __name__ == "__main__":
    main()
