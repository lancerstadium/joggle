"""Line-oriented source reader constructing native xDSL SSA operations."""
import math
import re

from xdsl.dialects.builtin import FloatAttr, IntegerAttr, IntegerType, ModuleOp, TensorType, f32, f64
from xdsl.dialects.func import CallOp, FuncOp, ReturnOp
from xdsl.ir import Block, Region


def read(source, context):
    body = Block()
    values, declarations = {}, {}
    returned = None
    started = False

    def name(token):
        if not re.fullmatch(r"%[A-Za-z_][A-Za-z_0-9]*", token):
            raise ValueError("syntax-error")
        return token[1:]

    def tensor(token):
        match = re.fullmatch(r"tensor<((?:\d+x)*)(f32|f64|i8|i16|i32|i64)>", token)
        if not match:
            raise ValueError("syntax-error")
        dims, element = match.groups()
        element = {"f32": f32, "f64": f64}.get(element) or IntegerType(int(element[1:]))
        return TensorType(element, [int(d) for d in dims.split("x") if d])

    def lookup(token):
        key = name(token)
        if key not in values:
            raise ValueError("undefined-value")
        return values[key]

    for line in source.splitlines():
        tokens = line.split()
        if not tokens:
            continue
        if returned is not None:
            raise ValueError("syntax-error")
        if tokens[0] == "return":
            if len(tokens) != 2:
                raise ValueError("syntax-error")
            returned = lookup(tokens[1])
            body.add_op(ReturnOp(returned))
            continue
        if tokens[0] == "input":
            if len(tokens) != 3 or started:
                raise ValueError("syntax-error")
            key = name(tokens[1])
            if key in values:
                raise ValueError("duplicate-value")
            values[key] = body.insert_arg(tensor(tokens[2]), len(body.args))
            continue
        # Assignments allow whitespace around '=' but do not require it.
        assignment = re.fullmatch(r"\s*(%[A-Za-z_][A-Za-z_0-9]*)\s*=\s*(.+?)\s*", line)
        if not assignment:
            raise ValueError("syntax-error")
        key = name(assignment[1])
        if key in values:
            raise ValueError("duplicate-value")
        parts = assignment[2].split()
        operation, attributes = parts[0], {}
        if operation == "const" and len(parts) == 3:
            type = tensor(parts[2])
            element = type.element_type
            if isinstance(element, IntegerType):
                if not re.fullmatch(r"[+-]?\d+", parts[1]):
                    raise ValueError("type-error")
                value = int(parts[1])
                width = element.width.data
                if not -(1 << (width - 1)) <= value < (1 << (width - 1)):
                    raise ValueError("literal-out-of-range")
                attributes["value"] = IntegerAttr(value, element)
            else:
                if not re.fullmatch(r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?", parts[1]):
                    raise ValueError("syntax-error")
                try:
                    attribute = FloatAttr(float(parts[1]), element)
                except (OverflowError, ValueError) as failure:
                    raise ValueError("literal-out-of-range") from failure
                if not math.isfinite(attribute.value.data):
                    raise ValueError("literal-out-of-range")
                attributes["value"] = attribute
            operation, operands = "splat", []
        elif operation in ("add", "mul", "relu") and len(parts) == (2 if operation == "relu" else 3):
            operands = [lookup(token) for token in parts[1:]]
            type = operands[0].type
            if any(value.type != type for value in operands):
                raise ValueError("type-error")
        else:
            raise ValueError("syntax-error")
        signature = ([value.type for value in operands], [type])
        key_type = (operation, tuple(signature[0]), type)
        if key_type not in declarations:
            symbol = operation + "__type" + str(len(declarations))
            declarations[key_type] = FuncOp.external(symbol, *signature)
        symbol = declarations[key_type].sym_name.data
        call = CallOp(symbol, operands, [type])
        call.attributes.update(attributes)
        body.add_op(call)
        values[key] = call.res[0]
        started = True
    if returned is None:
        raise ValueError("syntax-error")
    subject = FuncOp("subject", ([arg.type for arg in body.args], [returned.type]), Region(body))
    module = ModuleOp([*declarations.values(), subject])
    module.verify()
    return module
