from xdsl.dialects.builtin import ModuleOp, TensorType
from xdsl.dialects.func import CallOp, FuncOp


def analyze(module: ModuleOp) -> dict:
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    calls = [op for op in subject.body.block.ops if isinstance(op, CallOp)]
    positions = {op: index for index, op in enumerate(calls)}
    axis = 1 if subject.attributes["layout"].data == "NCHW" else 3
    matches = []
    for relu in calls:
        if relu.callee.root_reference.data != "relu" or len(relu.arguments) != 1:
            continue
        bias = relu.arguments[0].owner
        if (not isinstance(bias, CallOp) or bias.callee.root_reference.data != "bias_add"
                or len(bias.arguments) != 2 or not bias.res[0].has_one_use()):
            continue
        conv = bias.arguments[0].owner
        if (not isinstance(conv, CallOp) or conv.callee.root_reference.data != "conv2d"
                or not conv.res[0].has_one_use()):
            continue
        output_type, bias_type = conv.res[0].type, bias.arguments[1].type
        if not isinstance(output_type, TensorType) or not isinstance(bias_type, TensorType):
            continue
        shape, bias_shape = output_type.get_shape(), bias_type.get_shape()
        if len(shape) == 4 and len(bias_shape) == 1 and shape[axis] == bias_shape[0]:
            matches.append([positions[conv], positions[bias], positions[relu]])
    return {"matches": matches}
