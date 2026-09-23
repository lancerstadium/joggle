from xdsl.dialects.builtin import IntegerAttr, TensorType
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block
    def call(op, name):
        return isinstance(op, CallOp) and op.callee.root_reference.data == name
    for relu in list(block.ops):
        if not call(relu, "relu"):
            continue
        bias = relu.arguments[0].owner
        if not call(bias, "bias_add"):
            continue
        conv = bias.arguments[0].owner
        if not call(conv, "conv2d"):
            continue
        if len(tuple(conv.res[0].uses)) != 1 or len(tuple(bias.res[0].uses)) != 1:
            continue
        axis = 1 if conv.attributes["layout"].data == "NCHW" else 3
        shape, bshape = conv.res[0].type, bias.arguments[1].type
        if not isinstance(shape, TensorType) or not isinstance(bshape, TensorType):
            continue
        shape, bshape = shape.get_shape(), bshape.get_shape()
        declared_axis = bias.attributes.get("axis")
        if (len(shape) != 4 or len(bshape) != 1 or shape[axis] != bshape[0]
                or not isinstance(declared_axis, IntegerAttr) or declared_axis.value.data != axis):
            continue
        fused = CallOp("fused_conv_bias_relu", [*conv.arguments, bias.arguments[1]], [relu.res[0].type])
        fused.attributes.update(conv.attributes)
        block.insert_op_before(fused, relu)
        relu.res[0].replace_all_uses_with(fused.res[0])
        for op in (relu, bias, conv):
            block.erase_op(op)
