from xdsl.dialects.builtin import ArrayAttr, IntegerAttr, SymbolRefAttr, TensorType, i64
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    for op in subject.body.block.ops:
        if (not isinstance(op, CallOp) or op.callee.root_reference.data != "matmul" or
                len(op.arguments) != 2 or len(op.res) != 1):
            continue
        a, b, c = op.arguments[0].type, op.arguments[1].type, op.res[0].type
        if not all(isinstance(t, TensorType) for t in (a, b, c)):
            continue
        if [str(t.element_type) for t in (a, b, c)] != ["f16", "f16", "f32"]:
            continue
        x, y, z = a.get_shape(), b.get_shape(), c.get_shape()
        if any(len(shape) != 2 for shape in (x, y, z)):
            continue
        m, k, n = x[0], x[1], y[1]
        if (x[1] != y[0] or z != (m, n) or
                any(d <= 0 or d % 16 for d in (m, k, n))):
            continue
        op.properties["callee"] = SymbolRefAttr("mma_m16n16k16")
        op.attributes["tiles"] = ArrayAttr([IntegerAttr(d // 16, i64) for d in (m, n, k)])
