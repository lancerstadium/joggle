#include <filesystem>
#include <iostream>
#include <optional>
#include <utility>

#include <joggle/joggle.h>

int main(int argc, char** argv) {
  if (argc != 3) {
    return 1;
  }
  joggle::Compiler compiler;
  compiler.search(std::filesystem::path(argv[2]));
  compiler.load(std::filesystem::path(argv[1]));
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return 1;
  }
  const auto mod = compiler.mod("external");
  if (!mod) {
    compiler.diag().print(std::cerr);
    return 1;
  }
  const auto make = mod->fn("make");
  const auto converted = mod->fn("converted");
  if (!make || !converted || !compiler.load_native("external")) {
    compiler.diag().print(std::cerr);
    return 1;
  }

  auto fn = compiler.materialize("external.main");
  auto transformed =
      fn ? compiler.run<joggle::Fn>("external.convert", *fn) : std::nullopt;
  if (!transformed) {
    compiler.diag().print(std::cerr);
    return 1;
  }
  fn = std::move(transformed);
  const auto operations = fn->ops();
  if (operations.size() != 1U || operations.front().callee() != *converted) {
    compiler.diag().print(std::cerr);
    return 1;
  }

  auto constructed = compiler.create_fn();
  const auto int_type = compiler.make("int");
  const auto bits12 =
      int_type ? compiler.known(*int_type, std::int64_t{12}) : std::nullopt;
  if (!constructed || !bits12) {
    return 1;
  }
  auto edit = constructed->edit();
  auto value = edit.append(*make, {*bits12}).value();
  edit.ret(constructed->entry(), {value});
  joggle::Diag diagnostics;
  if (!edit.commit(diagnostics)) {
    diagnostics.print(std::cerr);
    return 1;
  }
  const auto output_count = constructed->entry().terminator().returned().size();
  const auto bits = value.type().get<std::int64_t>("bits");
  if (output_count != 1U || !bits || *bits != 12) {
    compiler.diag().print(std::cerr);
    return 1;
  }
  return 0;
}
