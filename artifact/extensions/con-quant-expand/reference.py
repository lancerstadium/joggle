from xdsl.dialects.builtin import TensorType, f32
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    ops = [op for op in block.ops if isinstance(op, CallOp) and op.callee.root_reference.data == "qadd"]
    for op in ops:
        if any(op.attributes[key].value.data <= 0 for key in ("lhs_scale", "rhs_scale", "output_scale")):
            raise ValueError("invalid-scale")
    for op in ops:
        attrs = op.attributes
        zeros = list(attrs["zeros"])
        floating = TensorType(f32, op.res[0].type.get_shape())
        def call(name, inputs, result_type, scale=None, zero=None):
            node = CallOp(name, inputs, [result_type])
            if scale is not None:
                node.attributes.update(scale=scale, zero=zero)
            block.insert_op_before(node, op)
            return node.res[0]
        left = call("dequantize", [op.arguments[0]], floating, attrs["lhs_scale"], zeros[0])
        right = call("dequantize", [op.arguments[1]], floating, attrs["rhs_scale"], zeros[1])
        total = call("add", [left, right], floating)
        result = call("quantize", [total], op.res[0].type, attrs["output_scale"], zeros[2])
        op.res[0].replace_all_uses_with(result)
        block.erase_op(op)
