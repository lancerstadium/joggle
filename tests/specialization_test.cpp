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
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = compiler.materialize("source_model.main");
  const auto opaque = compiler.materialize("opaque_model.main");
  joggle::Diagnostics diagnostics;
  joggle::Mod program("program", {1, 0, 0});
  joggle::Mod bad_program("bad_program", {1, 0, 0});
  if (!source || !opaque || !program.insert("main", *source, diagnostics) ||
      !bad_program.insert("main", *opaque, diagnostics)) {
    diagnostics.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto boundary = [&](const joggle::Mod::FnDecl& fn) {
    return fn.symbol().mod_name() == "boundary";
  };
  const auto specialized = compiler.specialize(program, boundary, diagnostics);
  const auto repeated = compiler.specialize(program, boundary, diagnostics);
  joggle::Diagnostics rejected_diagnostics;
  const auto rejected =
      compiler.specialize(bad_program, boundary, rejected_diagnostics);

  const auto main = specialized ? specialized->fn("main") : std::nullopt;
  const joggle::Fn* main_body = main ? main->body() : nullptr;
  const auto generated =
      specialized ? specialized->fn("specialized_0_square") : std::nullopt;
  const joggle::Fn* generated_body = generated ? generated->body() : nullptr;
  const joggle::Fn* original_body = program.fns().front().body();

  bool ok = true;
  ok &= expect(specialized && repeated && main_body && generated_body &&
                   original_body,
               "a source Fn specializes to an accepted boundary");
  ok &= expect(specialized && repeated && specialized->fns().size() == 2U &&
                   main_body && main_body->ops().size() == 1U &&
                   main_body->ops().front().callee().symbol().mod_name() ==
                       specialized->name() &&
                   generated_body && generated_body->ops().size() == 1U &&
                   generated_body->ops().front().callee().symbol().mod_name() ==
                       "boundary" &&
                   original_body &&
                   original_body->ops().front().callee().symbol().mod_name() ==
                       "source_kernel" &&
                   specialized->digest() == repeated->digest(),
               "specialization is local, non-mutating, and deterministic");
  ok &= expect(!rejected && !rejected_diagnostics.ok(),
               "an opaque call outside the boundary fails closed");
  if (!ok) {
    diagnostics.print(std::cerr);
    rejected_diagnostics.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
