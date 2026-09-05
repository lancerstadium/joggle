#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.add(R"(
joggle 1;
mod boundary@1.0.0 {
  fn multiply(lhs: f32, rhs: f32) -> f32;
}
)",
               "boundary.joggle");
  compiler.add(R"(
joggle 1;
mod source_kernel@1.0.0 {
  import boundary@1.0.0;

  fn square(input: f32) -> f32 {
    return boundary.multiply(input, input);
  }
}
)",
               "source-kernel.joggle");
  compiler.add(R"(
joggle 1;
mod source_model@1.0.0 {
  import source_kernel@1.0.0;

  fn main(input: f32) -> f32 {
    return source_kernel.square(input);
  }
}
)",
               "source-model.joggle");
  compiler.add(R"(
joggle 1;
mod opaque_model@1.0.0 {
  fn opaque(input: f32) -> f32;

  fn main(input: f32) -> f32 {
    return opaque(input);
  }
}
)",
               "opaque-model.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = compiler.materialize("source_model.main");
  const auto opaque = compiler.materialize("opaque_model.main");
  joggle::Diag diagnostics;
  joggle::Mod program("program", {1, 0, 0});
  joggle::Mod bad_program("bad_program", {1, 0, 0});
  if (!source || !opaque || !program.insert("main", *source, diagnostics) ||
      !bad_program.insert("main", *opaque, diagnostics)) {
    diagnostics.print(std::cerr);
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto resolved = compiler.resolve(program, diagnostics);
  const auto repeated = compiler.resolve(program, diagnostics);
  joggle::Diag rejected_diagnostics;
  const auto rejected = compiler.resolve(bad_program, rejected_diagnostics);

  const auto main = resolved ? resolved->fn("main") : std::nullopt;
  const joggle::Fn* main_body = main ? main->body() : nullptr;
  const auto generated =
      resolved ? resolved->fn("inst_0_square") : std::nullopt;
  const joggle::Fn* generated_body = generated ? generated->body() : nullptr;
  const joggle::Fn* original_body = program.fns().front().body();

  bool ok = true;
  ok &= expect(resolved && repeated && main_body && generated_body &&
                   original_body,
               "a source call resolves to one concrete Fn instance");
  ok &= expect(resolved && repeated && resolved->fns().size() == 2U &&
                   main_body && main_body->ops().size() == 1U &&
                   main_body->ops()
                           .front()
                           .callee()
                           .referenced_fn()
                           ->symbol()
                           .mod_name() == resolved->name() &&
                   generated_body && generated_body->ops().size() == 1U &&
                   generated_body->ops()
                           .front()
                           .callee()
                           .referenced_fn()
                           ->symbol()
                           .mod_name() == "boundary" &&
                   original_body &&
                   original_body->ops()
                           .front()
                           .callee()
                           .referenced_fn()
                           ->symbol()
                           .mod_name() == "source_kernel" &&
                   resolved->digest() == repeated->digest(),
               "resolution is local, non-mutating, and deterministic");
  const auto rejected_main = rejected ? rejected->fn("main") : std::nullopt;
  const joggle::Fn* rejected_body =
      rejected_main ? rejected_main->body() : nullptr;
  ok &= expect(rejected && rejected_body && rejected_diagnostics.ok() &&
                   rejected_body->ops().size() == 1U &&
                   rejected_body->ops()
                           .front()
                           .callee()
                           .referenced_fn()
                           ->symbol()
                           .mod_name() == "opaque_model",
               "a bodyless call remains an explicit implementation leaf");
  if (!ok) {
    diagnostics.print(std::cerr);
    rejected_diagnostics.print(std::cerr);
    compiler.diag().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
