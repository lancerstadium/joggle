#include <filesystem>
#include <iostream>

#include <joggle/joggle.h>

int main(int argc, char** argv) {
  if (argc != 3) {
    return 1;
  }
  joggle::Compiler compiler;
  compiler.search(std::filesystem::path(argv[2]));
  compiler.load(std::filesystem::path(argv[1]));
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }
  const auto module = compiler.module("external");
  if (!module) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }
  const auto make = module->function("make");
  const auto converted = module->function("converted");
  if (!make || !converted || !compiler.load_behavior("external")) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }

  auto function = compiler.function("external.main");
  if (!function || !compiler.run(*function, "external.convert")) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }
  const auto operations = function->instructions();
  if (operations.size() != 1U || operations.front().callee() != *converted) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }

  auto constructed = compiler.function();
  if (!constructed) {
    return 1;
  }
  auto edit = constructed->edit();
  auto value =
      edit.append(*make, {}, joggle::property("bits", std::int64_t{12}))
          .value();
  edit.ret(constructed->entry(), {value});
  joggle::Diagnostics diagnostics;
  if (!edit.commit(diagnostics)) {
    diagnostics.print(std::cerr);
    return 1;
  }
  const auto output_count = constructed->entry().terminator().returned().size();
  const auto bits = value.type().get<std::int64_t>("bits");
  if (output_count != 1U || !bits || *bits != 12) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }
  return 0;
}
