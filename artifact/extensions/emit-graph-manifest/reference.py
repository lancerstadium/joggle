from xdsl.dialects.builtin import ArrayAttr, FloatAttr, IntegerAttr, StringAttr
from xdsl.dialects.func import CallOp, FuncOp, ReturnOp


def attribute(value):
    if isinstance(value, IntegerAttr):
        return bool(value.value.data) if value.type.width.data == 1 else value.value.data
    if isinstance(value, FloatAttr):
        return value.value.data
    if isinstance(value, StringAttr):
        return value.data
    if isinstance(value, ArrayAttr):
        return [attribute(item) for item in value]
    raise TypeError(f"unsupported fixture attribute {value}")


def analyze(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    ids = {}

    def define(value):
        name = f"v{len(ids)}"
        ids[value] = name
        return {"id": name, "type": {"element": str(value.type.element_type),
                "shape": [dim if dim >= 0 else -1 for dim in value.type.get_shape()]}}

    inputs = [define(value) for value in subject.body.block.args]
    nodes, outputs = [], []
    for op in subject.body.block.ops:
        if isinstance(op, CallOp):
            operands = [ids[value] for value in op.arguments]
            nodes.append({"id": f"n{len(nodes)}", "op": op.callee.root_reference.data,
                          "inputs": operands, "results": [define(value) for value in op.res],
                          "attrs": [[key, attribute(value)] for key, value in sorted(op.attributes.items())
                                    if key != "callee"]})
        elif isinstance(op, ReturnOp):
            outputs = [ids[value] for value in op.arguments]
    return {"schema_version": 1, "inputs": inputs, "nodes": nodes, "outputs": outputs}
