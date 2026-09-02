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

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TEST_MODULE);
  compiler.add(R"(
    joggle 1;
    module testing@1.0.0 {
      import test_ir@1;
      fn marker<T: type>(input: T) -> T;
      fn cleanup(input: graph) -> graph;
      fn optimize(input: graph) -> graph {
        return cleanup(test_ir.canonicalize(input));
      }
      fn abort(input: graph) -> graph;
    }
  )",
               "testing.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto test_ir = compiler.module("test_ir");
  const auto testing = compiler.module("testing");
  if (!test_ir || !testing) {
    return EXIT_FAILURE;
  }
  const auto integer_schema = test_ir->type("integer");
  const auto add_schema = test_ir->function("add");
  const auto cast_schema = test_ir->function("cast");
  const auto marker_schema = testing->function("marker");
  const auto cleanup_schema = testing->function("cleanup");
  const auto optimize_schema = testing->function("optimize");
  const auto abort_schema = testing->function("abort");
  if (!integer_schema || !add_schema || !cast_schema ||
      !marker_schema || !cleanup_schema || !optimize_schema || !abort_schema) {
    return EXIT_FAILURE;
  }

  compiler.bind(*integer_schema,
                [](const joggle::Type& type, joggle::Diagnostics& diagnostics) {
                  const auto width = type.get<std::int64_t>("width");
                  if (!width || *width <= 0) {
                    diagnostics.report("integer width must be positive");
                    return false;
                  }
                  return true;
                });
  const auto same_type = [](const joggle::Operation& operation,
                            joggle::Diagnostics& diagnostics) {
    const auto operands = operation.operands();
    const auto results = operation.results();
    if (results.empty() ||
        std::any_of(operands.begin(), operands.end(), [&](const auto& value) {
          return value.type() != results[0].type();
        })) {
      diagnostics.report("integer operation types must agree");
      return false;
    }
    return true;
  };
  compiler.bind(*add_schema, same_type);
  compiler.bind(*cast_schema, same_type);
  compiler.bind(*marker_schema, same_type);

  std::size_t query_runs = 0;
  const auto compute_nodes = [&](const joggle::Graph& graph) {
    ++query_runs;
    return graph.all_operations().size();
  };

  compiler.bind(
      *cleanup_schema,
      [&compute_nodes](joggle::Graph& graph,
                       joggle::Diagnostics& diagnostics) {
        static_cast<void>(compute_nodes(graph));
        const auto operations = graph.all_operations();
        const bool has_marker =
            std::any_of(operations.begin(), operations.end(),
                        [](const joggle::Operation& operation) {
                          return operation.schema().name() == "marker";
                        });
        if (!has_marker) {
          return true;
        }
        auto edit = graph.edit();
        for (const joggle::Operation& operation : operations) {
          if (operation.schema().name() != "marker") {
            continue;
          }
          edit.replace(operation.result(0), operation.operands()[0]);
          edit.erase(operation);
        }
        return edit.commit(diagnostics);
      });

  const auto integer = compiler.make(*integer_schema, 8);
  auto graph = compiler.graph();
  if (!integer || !graph) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  {
    auto edit = graph->edit();
    const auto lhs = edit.argument(*integer);
    const auto rhs = edit.argument(*integer);
    const auto add = edit.append(*add_schema, {lhs, rhs});
    const auto cast = edit.append(*cast_schema, {add.result(0)});
    edit.append(*marker_schema, {cast.result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  bool ok = true;
  ok &= expect(compiler.verify(*graph), "native operation verification");
  ok &= expect(compiler.run(*graph, *optimize_schema),
               "composed rule and C++ pass runs");
  ok &= expect(graph->operations().size() == 1U,
               "composed pass transforms through Graph::Edit");
  ok &= expect(query_runs == 1U,
               "pass-local analysis executes explicitly without a side API");

  compiler.bind(*abort_schema, [marker_schema,
                                integer](joggle::Compiler&, joggle::Graph& current,
                                      joggle::Diagnostics& diagnostics) {
    const auto producer = current.operations().front();
    auto edit = current.edit();
    edit.append(*marker_schema, {producer.result(0)});
    if (!edit.commit(diagnostics)) {
      return false;
    }
    return false;
  });
  ok &= expect(!compiler.run(*graph, *abort_schema),
               "failing pass reports failure");
  ok &= expect(graph->operations().size() == 1U,
               "pass-level checkpoint restores committed inner edits");

  joggle::Compiler invalid;
  invalid.load(JOGGLE_TEST_MODULE);
  if (!invalid.link()) {
    return EXIT_FAILURE;
  }
  const auto invalid_module = invalid.module("test_ir");
  const auto invalid_integer =
      invalid_module ? invalid_module->type("integer") : std::nullopt;
  if (!invalid_integer) {
    return EXIT_FAILURE;
  }
  invalid.bind(*invalid_integer,
               [](const joggle::Type& type, joggle::Diagnostics&) {
                 const auto width = type.get<std::int64_t>("width");
                 return width && *width > 0;
               });
  ok &= expect(!invalid.make(*invalid_integer, 0) && !invalid.ok(),
               "type verifier rejection is diagnosed");

  joggle::Compiler reported;
  reported.load(JOGGLE_TEST_MODULE);
  if (!reported.link()) {
    return EXIT_FAILURE;
  }
  const auto reported_module = reported.module("test_ir");
  const auto reported_integer =
      reported_module ? reported_module->type("integer") : std::nullopt;
  if (!reported_integer) {
    return EXIT_FAILURE;
  }
  reported.bind(*reported_integer,
                [](const joggle::Type&, joggle::Diagnostics& diagnostics) {
                  diagnostics.report("reported verifier failure");
                  return true;
                });
  ok &= expect(
      !reported.make(*reported_integer, 8, false) && !reported.ok(),
      "a verifier diagnostic rejects construction even if it returns true");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
