#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <tuple>
#include <vector>

#include <joggle/joggle.h>

namespace {

struct Token {
  std::int64_t reads = 0;
};

struct View {
  const std::vector<std::int8_t>* values = nullptr;
};

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.add(R"(
joggle 1;
mod dot@1.0.0 {
  type memory();
  type view(size: int);

  fn load<N: int>(token: effect<memory>, input: view<N>, index: int)
      -> (effect<memory>, i8);
  fn mac(accumulator: i32, lhs: i8, rhs: i8) -> i32;

  fn dot<N: int>(
    token: effect<memory>,
    lhs: view<N>,
    rhs: view<N>,
    initial: i32
  ) -> (effect<memory>, i32) {
    accumulator = initial;
    for index in range(N) {
      token, left = load(token, lhs, index);
      token, right = load(token, rhs, index);
      accumulator = mac(accumulator, left, right);
    }
    return token, accumulator;
  }

  fn dot4(
    token: effect<memory>,
    lhs: view<4>,
    rhs: view<4>,
    initial: i32
  ) -> (effect<memory>, i32) {
    token, result = dot(token, lhs, rhs, initial);
    return token, result;
  }
}
)",
               "dot.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto mod = compiler.mod("dot");
  const auto prelude = compiler.mod("prelude");
  const auto memory = mod ? mod->type("memory") : std::nullopt;
  const auto view = mod ? mod->type("view") : std::nullopt;
  const auto effect = prelude ? prelude->type("effect") : std::nullopt;
  const auto memory_type = memory ? compiler.make(*memory) : std::nullopt;
  const auto load = mod ? mod->fn("load") : std::nullopt;
  const auto mac = mod ? mod->fn("mac") : std::nullopt;
  const auto dot4 = mod ? mod->fn("dot4") : std::nullopt;
  if (!mod || !memory_type || !view || !effect || !load || !mac || !dot4 ||
      !compiler.represent<Token>(*effect, [memory_type](const Token&) {
        return std::tuple{*memory_type};
      }) ||
      !compiler.represent<View>(*view, [](const View& input) {
        return std::tuple{static_cast<std::int64_t>(input.values->size())};
      })) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  compiler.bind(*load,
                [](Token token, const View& input, std::int64_t index) {
                  ++token.reads;
                  const auto position = static_cast<std::size_t>(index);
                  return std::tuple{token, input.values->at(position)};
                });
  compiler.bind(*mac,
                [](std::int32_t accumulator, std::int8_t lhs,
                   std::int8_t rhs) {
                  return static_cast<std::int32_t>(
                      accumulator + static_cast<std::int32_t>(lhs) *
                                        static_cast<std::int32_t>(rhs));
                });

  const std::vector<std::int8_t> lhs{1, -2, 3, -4};
  const std::vector<std::int8_t> rhs{5, 6, -7, 8};
  const auto result = compiler.run<std::tuple<Token, std::int32_t>>(
      *dot4, Token{}, View{&lhs}, View{&rhs}, std::int32_t{3});
  if (!result || !compiler.ok()) {
    compiler.diag().print(std::cerr);
  }
  bool ok = expect(result && std::get<0>(*result).reads == 8 &&
                       std::get<1>(*result) == -57 && compiler.ok(),
                   "an ordinary source fn executes an i8 dot kernel");

  auto materialized = compiler.materialize(*mod);
  joggle::Diag diagnostics;
  auto specialized = materialized
                         ? compiler.specialize(
                               *materialized,
                               [](const joggle::Mod::FnDecl& fn) {
                                 return fn.form() ==
                                        joggle::Mod::FnDecl::Form::External;
                               },
                               diagnostics)
                         : std::optional<joggle::Mod>{};
  const joggle::Fn* implementation = nullptr;
  if (specialized) {
    for (const auto& fn : specialized->fns()) {
      if (fn.name().starts_with("specialized_") && fn.body() != nullptr) {
        implementation = fn.body();
        break;
      }
    }
  }
  if (!diagnostics.ok()) {
    diagnostics.print(std::cerr);
  }
  ok &= expect(specialized && implementation &&
                   implementation->ops().size() == 12U &&
                   compiler.verify(*specialized) && diagnostics.ok(),
               "specialization exposes the same kernel as typed Fn IR");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
