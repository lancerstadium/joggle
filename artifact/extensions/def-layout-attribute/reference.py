"""Registered relayout operation with layout-directed result inference."""

from xdsl.dialects.builtin import StringAttr, TensorType
from xdsl.ir import Dialect
from xdsl.irdl import IRDLOperation, attr_def, irdl_op_definition, operand_def, result_def
from xdsl.utils.exceptions import VerifyException


def permutation(source, destination):
    if source not in ("NCHW", "NHWC") or destination not in ("NCHW", "NHWC"):
        raise VerifyException("invalid-layout")
    return tuple(source.index(axis) for axis in destination)


def inferred_type(operand_type, attributes):
    if not isinstance(operand_type, TensorType) or len(operand_type.get_shape()) != 4:
        raise VerifyException("rank-mismatch")
    if any(not isinstance(attributes.get(name), StringAttr) for name in ("src", "dst")):
        raise VerifyException("invalid-layout")
    axes = permutation(attributes["src"].data, attributes["dst"].data)
    return TensorType(operand_type.element_type, [operand_type.get_shape()[axis] for axis in axes])


@irdl_op_definition
class Relayout(IRDLOperation):
    name = "extension.relayout"
    input = operand_def(TensorType)
    result = result_def(TensorType)
    src = attr_def(StringAttr)
    dst = attr_def(StringAttr)

    def verify_(self):
        if self.result.type != inferred_type(self.input.type, self.attributes):
            raise VerifyException("result-type-mismatch")


def register(context):
    context.load_dialect(Dialect("extension", [Relayout], []))


def construct(operands, attributes):
    if len(operands) != 1:
        raise VerifyException("operand-count-mismatch")
    return Relayout.create(operands=operands,
                          result_types=[inferred_type(operands[0].type, attributes)],
                          attributes=attributes)
