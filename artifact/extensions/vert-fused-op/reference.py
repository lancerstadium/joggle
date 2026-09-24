"""Fuse single-use quantized convolution chains and emit a runtime-input kernel."""
from xdsl.dialects.func import CallOp, FuncOp


def transform(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    block = subject.body.block

    def named(op, name):
        return isinstance(op, CallOp) and op.callee.root_reference.data == name

    for relu in list(block.ops):
        if not named(relu, "relu"):
            continue
        quant = relu.arguments[0].owner
        if not named(quant, "requantize"):
            continue
        bias = quant.arguments[0].owner
        if not named(bias, "bias"):
            continue
        conv = bias.arguments[0].owner
        if not named(conv, "qconv"):
            continue
        if any(len(tuple(op.res[0].uses)) != 1 for op in (conv, bias, quant)):
            continue
        if conv.attributes["layout"].data != "NHWC" or conv.attributes["kernel_layout"].data != "HWIO":
            continue
        if relu.attributes["output_zero"] != quant.attributes["output_zero"]:
            continue
        fused = CallOp("fused_qconv_relu", [*conv.arguments, bias.arguments[1]], [relu.res[0].type])
        for op in (conv, quant):
            fused.attributes.update({key: value for key, value in op.attributes.items() if key != "callee"})
        block.insert_op_before(fused, relu)
        relu.res[0].replace_all_uses_with(fused.res[0])
        for op in (relu, quant, bias, conv):
            block.erase_op(op)


def analyze(module):
    subject = next(op for op in module.ops
                   if isinstance(op, FuncOp) and op.sym_name.data == "subject")
    calls = [op for op in subject.body.block.ops if isinstance(op, CallOp)
             and op.callee.root_reference.data == "fused_qconv_relu"]
    if len(calls) != 1:
        raise ValueError("expected-fused-convolution")
    op = calls[0]
    n, h, w, ci = op.arguments[0].type.get_shape()
    kh, kw, wi, co = op.arguments[1].type.get_shape()
    if wi != ci or op.arguments[2].type.get_shape() != (co,):
        raise ValueError("shape-mismatch")
    constants = dict(N=n, H=h, W=w, CI=ci, KH=kh, KW=kw, CO=co,
                     ACC_SCALE=op.attributes["acc_scale"].value.data,
                     OUT_SCALE=op.attributes["out_scale"].value.data,
                     ZERO=op.attributes["output_zero"].value.data)
    source = "#include <stdint.h>\n#include <math.h>\n"
    source += "".join(f"#define {key} ({value!r})\n" for key, value in constants.items())
    source += """
int task_kernel(const int8_t *x, const int8_t *w, const int32_t *b, int8_t *y) {
  const int oh=H-KH+1, ow=W-KW+1;
  for (int n=0; n<N; ++n) for (int h=0; h<oh; ++h)
    for (int col=0; col<ow; ++col) for (int co=0; co<CO; ++co) {
      int64_t acc=b[co];
      for (int kh=0; kh<KH; ++kh) for (int kw=0; kw<KW; ++kw)
        for (int ci=0; ci<CI; ++ci)
          acc+=(int64_t)x[((n*H+h+kh)*W+col+kw)*CI+ci]*w[((kh*KW+kw)*CI+ci)*CO+co];
      if (acc<INT32_MIN || acc>INT32_MAX) return 1;
      double scaled=(double)acc*ACC_SCALE/OUT_SCALE;
      if (isnan(scaled)) return 2;
      double value=scaled<=0 ? ZERO : scaled>=127-ZERO ? 127 : nearbyint(scaled)+ZERO;
      y[((n*oh+h)*ow+col)*CO+co]=(int8_t)value;
    }
  return 0;
}
"""
    return {"symbol": "task_kernel", "source": source}
