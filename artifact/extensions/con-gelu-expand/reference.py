import math
from xdsl.dialects.builtin import FloatAttr
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    ops = [op for op in block.ops if isinstance(op, CallOp) and op.callee.root_reference.data == "gelu"]
    for op in ops:
        if str(op.arguments[0].type.element_type) not in ("f32", "f64"):
            raise ValueError("unsupported-element-type")
    for op in ops:
        x = op.arguments[0]
        def call(name, operands, value=None):
            node = CallOp(name, operands, [x.type])
            if value is not None:
                node.attributes["value"] = FloatAttr(value, 64)
            block.insert_op_before(node, op)
            return node.res[0]
        half, one, root = call("splat", [], 0.5), call("splat", [], 1.0), call("splat", [], math.sqrt(2))
        scaled = call("div", [x, root])
        error = call("erf", [scaled])
        total = call("add", [one, error])
        hx = call("mul", [half, x])
        result = call("mul", [hx, total])
        op.res[0].replace_all_uses_with(result)
        block.erase_op(op)
