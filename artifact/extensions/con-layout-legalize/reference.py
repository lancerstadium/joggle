from xdsl.dialects.builtin import ArrayAttr, IntegerAttr, TensorType, i64
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    ops = [op for op in block.ops if isinstance(op, CallOp) and op.callee.root_reference.data == "conv2d_nhwc"]
    for op in ops:
        if op.arguments[0].type.get_shape()[3] != op.arguments[1].type.get_shape()[2]:
            raise ValueError("channel-mismatch")
    def permuted(type, p):
        return TensorType(type.element_type, [type.get_shape()[i] for i in p])
    for op in ops:
        def transpose(name, value, p):
            node = CallOp(name, [value], [permuted(value.type, p)])
            node.attributes["perm"] = ArrayAttr([IntegerAttr(i, i64) for i in p])
            block.insert_op_before(node, op)
            return node.res[0]
        x = transpose("transpose_input", op.arguments[0], [0, 3, 1, 2])
        w = transpose("transpose_weight", op.arguments[1], [3, 2, 0, 1])
        conv = CallOp("conv2d_nchw", [x, w], [permuted(op.res[0].type, [0, 3, 1, 2])])
        for key in ("stride", "pad", "dilation"):
            conv.attributes[key] = op.attributes[key]
        block.insert_op_before(conv, op)
        result = transpose("transpose_output", conv.res[0], [0, 2, 3, 1])
        op.res[0].replace_all_uses_with(result)
        block.erase_op(op)
