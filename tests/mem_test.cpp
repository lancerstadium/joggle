#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::string callee(const joggle::Op& op) {
  const auto fn = op.callee().referenced_fn();
  return fn ? std::string(fn->name()) : std::string{};
}

bool call_is(const joggle::Op& op, std::string_view owner,
             std::string_view name) {
  const auto fn = op.callee().referenced_fn();
  return fn && fn->symbol().mod_name() == owner && fn->name() == name;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MOD);
  compiler.load(JOGGLE_TENSOR_MOD);
  compiler.load(JOGGLE_MEM_MOD);
  compiler.load(JOGGLE_NN_MOD);
  compiler.add(R"(
joggle 1;

mod mem_use@1.0.0 {
  import arith@1 as a;
  import mem@1 as m;
  import nn@2 as n;
  import tensor@7 as t;

  fn fill(value: i32) -> t.tensor<i32, [2, 3]> {
    output: m.sink<i32, [2, 3]>,
      state: effect<m.sink<i32, [2, 3]>> = m.alloc([2, 3]);
    for row, column in 2, 3 {
      state = m.store(output, state, value, row, column);
    }
    return m.seal(output, state);
  }

  fn product(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return n.matmul(lhs, rhs);
  }

  fn activate(
    input: t.tensor<f32, [2, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return n.relu(input);
  }

  fn compose(
    input: t.tensor<f32, [2, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    first: t.tensor<f32, [2, 3]> =
      t.map(input, (value) => a.max(value, a.zero(value)));
    return t.map(first, (value) => a.max(value, value));
  }
}
)",
               "mem-use.joggle");
  if (!compiler.link() || !compiler.load_native("mem", JOGGLE_MEM_NATIVE)) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto mem = compiler.mod("mem");
  const auto declarations =
      mem ? mem->fns() : std::vector<joggle::Mod::FnDecl>{};
  std::vector<std::string> names;
  for (const auto& fn : declarations) {
    names.emplace_back(fn.name());
  }
  ok &= expect(names == std::vector<std::string>({"view", "[]", "alloc",
                                                  "store", "seal", "realize"}),
               "Mem exposes only logical read and destination capabilities");

  const auto fill = compiler.materialize("mem_use.fill");
  if (!fill) {
    compiler.diag().print(std::cerr);
  }
  const auto operations = fill ? fill->ops() : std::vector<joggle::Op>{};
  std::size_t stores = 0;
  for (const auto& op : operations) {
    stores += callee(op) == "store" ? 1U : 0U;
  }
  ok &= expect(fill && compiler.verify(*fill) && fill->blks().size() > 1U &&
                   stores == 1U &&
                   std::any_of(operations.begin(), operations.end(),
                               [](const auto& op) {
                                 return call_is(op, "mem", "alloc");
                               }) &&
                   std::any_of(operations.begin(), operations.end(),
                               [](const auto& op) {
                                 return call_is(op, "mem", "seal");
                               }),
               "a paired loop carries one affine sink state to seal");

  const auto product = compiler.materialize("mem_use.product");
  const auto realized_product =
      product ? compiler.run<joggle::Fn>("mem.realize", *product)
              : std::optional<joggle::Fn>{};
  const auto product_ops =
      realized_product ? realized_product->ops() : std::vector<joggle::Op>{};
  const bool product_has_tensor_basis =
      std::any_of(product_ops.begin(), product_ops.end(), [](const auto& op) {
        return call_is(op, "tensor", "tensor") ||
               call_is(op, "tensor", "reduce") || call_is(op, "tensor", "[]");
      });
  ok &= expect(realized_product && compiler.verify(*realized_product) &&
                   !product_has_tensor_basis &&
                   std::any_of(product_ops.begin(), product_ops.end(),
                               [](const auto& op) {
                                 return call_is(op, "mem", "store");
                               }),
               "MatMul realizes generically through construction and reduce");

  const auto activate = compiler.materialize("mem_use.activate");
  const auto realized_activate =
      activate ? compiler.run<joggle::Fn>("mem.realize", *activate)
               : std::optional<joggle::Fn>{};
  const auto activate_ops =
      realized_activate ? realized_activate->ops() : std::vector<joggle::Op>{};
  const bool activate_has_tensor_basis =
      std::any_of(activate_ops.begin(), activate_ops.end(), [](const auto& op) {
        return op.callee().referenced_fn() &&
               op.callee().referenced_fn()->symbol().mod_name() == "tensor";
      });
  ok &= expect(realized_activate && compiler.verify(*realized_activate) &&
                   !activate_has_tensor_basis,
               "Relu realizes through generic map rather than an NN case");

  const auto compose = compiler.materialize("mem_use.compose");
  const auto realized_compose =
      compose ? compiler.run<joggle::Fn>("mem.realize", *compose)
              : std::optional<joggle::Fn>{};
  const auto compose_ops =
      realized_compose ? realized_compose->ops() : std::vector<joggle::Op>{};
  const auto count = [&](std::string_view owner, std::string_view name) {
    return std::count_if(
        compose_ops.begin(), compose_ops.end(),
        [&](const auto& op) { return call_is(op, owner, name); });
  };
  ok &= expect(realized_compose && compiler.verify(*realized_compose) &&
                   count("mem", "alloc") == 1 && count("mem", "store") == 1 &&
                   count("tensor", "map") == 0,
               "nested maps fuse by recursive coordinate sampling");

  const std::string formatted = mem ? joggle::format(*mem) : std::string{};
  joggle::Diag diagnostics;
  const auto roundtrip =
      mem ? joggle::parse_mod(formatted, diagnostics, "mem-roundtrip.joggle")
          : std::optional<joggle::Mod>{};
  ok &= expect(roundtrip && diagnostics.ok() &&
                   joggle::format(*roundtrip) == formatted,
               "the logical memory surface round-trips");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
