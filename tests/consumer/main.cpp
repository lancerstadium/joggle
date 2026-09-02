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
  const auto make = module->operation("make");
  const auto lowered = module->operation("lowered");
  if (!make || !lowered || !compiler.load_behavior("external")) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }

  auto graph = compiler.graph("external.main");
  if (!graph || !compiler.run(*graph, "external.lower")) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }
  const auto operations = graph->operations();
  if (operations.size() != 1U || operations.front().schema() != *lowered) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }

  auto constructed = compiler.graph();
  if (!constructed) {
    return 1;
  }
  auto edit = constructed->edit();
  auto value =
      edit.append(*make, {}, joggle::property("bits", std::int64_t{12}))
          .value();
  edit.output(value);
  joggle::Diagnostics diagnostics;
  if (!edit.commit(diagnostics)) {
    diagnostics.print(std::cerr);
    return 1;
  }
  const auto output_count = compiler.query(
      *constructed,
      [](const joggle::Graph& current) { return current.outputs().size(); });
  const auto bits = value.type().get<std::int64_t>("bits");
  if (!output_count || *output_count != 1U || !bits || *bits != 12) {
    compiler.diagnostics().print(std::cerr);
    return 1;
  }
  return 0;
}
