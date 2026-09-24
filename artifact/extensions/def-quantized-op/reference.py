"""Native qadd definition: inferred shape, quantization attributes, and verifier."""

import math

from xdsl.dialects.builtin import FloatAttr, IntegerAttr, TensorType, f64, i8, i32
from xdsl.ir import Dialect, SSAValue
from xdsl.irdl import IRDLOperation, attr_def, irdl_op_definition, operand_def, result_def
from xdsl.utils.exceptions import VerifyException


@irdl_op_definition
class QAdd(IRDLOperation):
    name = "extension.qadd"
    lhs = operand_def(TensorType)
    rhs = operand_def(TensorType)
    result = result_def(TensorType)
    lhs_scale = attr_def(FloatAttr)
    rhs_scale = attr_def(FloatAttr)
    output_scale = attr_def(FloatAttr)
    lhs_zero = attr_def(IntegerAttr)
    rhs_zero = attr_def(IntegerAttr)
    output_zero = attr_def(IntegerAttr)

    @classmethod
    def construct(cls, lhs: SSAValue, rhs: SSAValue, attributes: dict):
        # The result type comes from the operand, not an expected-type argument.
        return cls.create(operands=[lhs, rhs], result_types=[lhs.type], attributes=attributes)

    def verify_(self):
        types = [self.lhs.type, self.rhs.type, self.result.type]
        if any(type.element_type != i8 for type in types):
            raise VerifyException("operand-type-mismatch")
        if any(type.get_shape() != types[0].get_shape() for type in types[1:]):
            raise VerifyException("shape-mismatch")
        for scale in (self.lhs_scale, self.rhs_scale, self.output_scale):
            if scale.type != f64 or not math.isfinite(scale.value.data) or scale.value.data <= 0:
                raise VerifyException("invalid-scale")
        for zero in (self.lhs_zero, self.rhs_zero, self.output_zero):
            if zero.type != i32 or not -(1 << 31) <= zero.value.data < (1 << 31):
                raise VerifyException("invalid-zero-point")


def register(context):
    context.load_dialect(Dialect("extension", [QAdd], []))


def construct(operands, attributes):
    return QAdd.construct(*operands, attributes)
