#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

constexpr std::string_view source = R"(
joggle 1;

mod qdq@1.0.0 {
  import tensor@4 as t;
  import quant@2 as q;

  fn roundtrip(
    input: t.tensor<f32, [1, 4]>,
    scale: t.tensor<f32, []>,
    zero: t.tensor<i8, []>
  ) -> t.tensor<f32, [1, 4]> {
    stored: t.tensor<i8, [1, 4]> = q.quantize(
      input, scale, zero, axis: 1
    );
    return q.dequantize(stored, scale, zero, axis: 1);
  }
}
)";

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.load(JOGGLE_QUANT_MOD);
  compiler.add(source, "qdq.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto quant = compiler.mod("quant");
  const auto fn = compiler.materialize("qdq.roundtrip");
  if (!quant || !fn) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto ops = fn->ops();
  bool ok = true;
  ok &= expect(quant->version() == joggle::Version{2, 0, 0} &&
                   quant->fns().size() == 2U,
               "quant is a small Residual semantic Mod without host oracle "
               "overloads");
  ok &= expect(ops.size() == 2U &&
                   ops[0].callee().referenced_fn()->symbol().qualified_name() ==
                       "quant.quantize" &&
                   ops[1].callee().referenced_fn()->symbol().qualified_name() ==
                       "quant.dequantize",
               "QDQ source materializes as ordinary typed calls");
  ok &= expect(ops.size() == 2U &&
                   ops[0].callee().binding<std::int64_t>("axis") == 1 &&
                   ops[1].callee().binding<std::int64_t>("axis") == 1 &&
                   ops[0].value().type().get<joggle::Type>("element") ==
                       compiler.make("i8") &&
                   ops[1].value().type().get<joggle::Type>("element") ==
                       compiler.make("f32") &&
                   compiler.verify(*fn),
               "compiler parameters remain typed callee bindings while tensor "
               "values remain Residual");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
