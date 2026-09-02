#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_BITMATH_MODULE);
  compiler.load(JOGGLE_MINIAI_MODULE);
  compiler.load(JOGGLE_FEEDFORWARD_MODULE);
  compiler.load(JOGGLE_FIXED_MODULE);
  compiler.load(JOGGLE_EDGEVEC_MODULE);
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto edgevec = compiler.module("edgevec");
  if (!edgevec || !compiler.load_behavior("bitmath", JOGGLE_BITMATH_BEHAVIOR) ||
      !compiler.load_behavior("miniai", JOGGLE_MINIAI_BEHAVIOR) ||
      !compiler.load_behavior("fixed", JOGGLE_FIXED_BEHAVIOR) ||
      !compiler.load_behavior("edgevec", JOGGLE_EDGEVEC_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  auto graph = compiler.graph("feedforward.main");
  const auto dot = edgevec->operation("dot");
  const auto clamp = edgevec->operation("clamp");
  const auto lane_op = edgevec->interface("lane_op");
  const auto lanes = lane_op ? lane_op->method("lanes") : std::nullopt;
  const auto cycles = lane_op ? lane_op->method("cycles") : std::nullopt;
  if (!graph || !dot || !clamp || !lane_op || !lanes || !cycles) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  ok &= expect(compiler.run(*graph, "edgevec.lower"),
               "a target Module lowers an imported MiniAI graph");
  const auto operations = graph->operations();
  ok &= expect(operations.size() == 6U && operations[4].schema() == *dot &&
                   operations[5].schema() == *clamp &&
                   graph->outputs().size() == 1U &&
                   graph->outputs().front() == operations[5].result(0),
               "lowering preserves order while replacing target operations");
  const auto dot_lanes = compiler.call<std::int64_t>(operations[4], *lanes);
  const auto clamp_lanes = compiler.call<std::int64_t>(operations[5], *lanes);
  ok &= expect(dot_lanes && clamp_lanes && *dot_lanes == 4 && *clamp_lanes == 4,
               "target metadata is exposed through its versioned interface");
  const auto estimate_cycles =
      [&](const joggle::Graph& current) -> std::optional<std::int64_t> {
    std::int64_t total = 0;
    for (const joggle::Operation& operation : current.all_operations()) {
      if (!compiler.conforms(operation.schema(), *lane_op)) {
        continue;
      }
      const auto cost = compiler.call<std::int64_t>(operation, *cycles);
      if (!cost) {
        return std::nullopt;
      }
      total += *cost;
    }
    return total;
  };
  const auto cycle_count = compiler.query(*graph, estimate_cycles);
  ok &= expect(cycle_count && *cycle_count == 5,
               "a generic query consumes target-defined cycle semantics");
  ok &= expect(compiler.verify(*graph), "the lowered graph verifies");

  auto fixed_graph = compiler.graph("feedforward.fixed");
  ok &=
      expect(fixed_graph && compiler.run(*fixed_graph, "edgevec.lower") &&
                 compiler.verify(*fixed_graph) &&
                 fixed_graph->operations()[4].schema() == *dot &&
                 fixed_graph->operations()[5].schema() == *clamp,
             "the same target pass lowers a graph using a third-party format");

  auto invalid_edit = graph->edit();
  invalid_edit.set(operations[4], "lanes", std::int64_t{0});
  joggle::Diagnostics invalid_diagnostics;
  const bool invalid_committed = invalid_edit.commit(invalid_diagnostics);
  const bool invalid_verified = invalid_committed && compiler.verify(*graph);
  const auto lane_diagnostics = compiler.diagnostics().entries();
  ok &= expect(!invalid_verified && !lane_diagnostics.empty() &&
                   lane_diagnostics.back().message.find(
                       "lane count must be positive") != std::string::npos &&
                   lane_diagnostics.back().source &&
                   lane_diagnostics.back().source->source.find(
                       "feedforward.joggle") != std::string::npos &&
                   lane_diagnostics.back().source->begin.line == 19U,
               "lowered operations preserve source diagnostics");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
