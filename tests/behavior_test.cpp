#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include <joggle/joggle.h>

namespace {

struct NodeCount {
  std::size_t value = 0;
};

struct ScaledCount {
  std::size_t scale = 1;
  std::size_t* runs = nullptr;

  NodeCount operator()(const joggle::Graph& graph) const;
};

NodeCount ScaledCount::operator()(const joggle::Graph& graph) const {
  ++*runs;
  return NodeCount{graph.all_operations().size() * scale};
}

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
      import arith@1;
      op marker<T: type>(input: T) -> T;
      pass cleanup;
      pass optimize = arith.canonicalize, cleanup;
      pass abort;
    }
  )",
               "testing.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto arith = compiler.module("arith");
  const auto testing = compiler.module("testing");
  if (!arith || !testing) {
    return EXIT_FAILURE;
  }
  const auto integer_schema = arith->type("integer");
  const auto add_schema = arith->operation("add");
  const auto cast_schema = arith->operation("cast");
  const auto marker_schema = testing->operation("marker");
  const auto cleanup_schema = testing->pass("cleanup");
  const auto optimize_schema = testing->pass("optimize");
  const auto abort_schema = testing->pass("abort");
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
    return NodeCount{graph.all_operations().size()};
  };

  compiler.bind(
      *cleanup_schema,
      [&compute_nodes](joggle::Compiler& current, joggle::Graph& graph,
                       joggle::Diagnostics& diagnostics) {
        const auto nodes = current.query(graph, compute_nodes);
        if (!nodes) {
          return false;
        }
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
  const auto first = compiler.query(*graph, compute_nodes);
  const auto second = compiler.query(*graph, compute_nodes);
  ok &= expect(first && second && first->value == 3U && query_runs == 2U,
               "query callables execute without hidden cache state");
  std::size_t isolated_runs = 0;
  ScaledCount double_count{2U, &isolated_runs};
  ScaledCount triple_count{3U, &isolated_runs};
  const auto doubled = compiler.query(*graph, double_count);
  const auto tripled = compiler.query(*graph, triple_count);
  const auto doubled_again = compiler.query(*graph, double_count);
  ok &= expect(doubled && tripled && doubled_again && doubled->value == 6U &&
                   tripled->value == 9U && isolated_runs == 3U,
               "stateful query objects always observe their current state");
  std::size_t temporary_runs = 0;
  const auto temporary_first =
      compiler.query(*graph, ScaledCount{4U, &temporary_runs});
  const auto temporary_second =
      compiler.query(*graph, ScaledCount{4U, &temporary_runs});
  ok &= expect(temporary_first && temporary_second && temporary_runs == 2U,
               "temporary stateful queries execute independently");
  ok &= expect(compiler.run(*graph, *optimize_schema),
               "composed rule and C++ pass runs");
  ok &= expect(graph->operations().size() == 1U,
               "composed pass transforms through Graph::Edit");
  const auto after = compiler.query(*graph, compute_nodes);
  ok &= expect(after && after->value == 1U && query_runs == 4U,
               "a query observes the graph after a committed pass");

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

  const auto diagnosed_query = compiler.query(
      *graph, [](const joggle::Graph&, joggle::Diagnostics& diagnostics) {
        diagnostics.report("query rejected its input");
        return NodeCount{1U};
      });
  ok &= expect(!diagnosed_query,
               "a query diagnostic suppresses its produced value");

  joggle::Compiler invalid;
  invalid.load(JOGGLE_TEST_MODULE);
  if (!invalid.link()) {
    return EXIT_FAILURE;
  }
  const auto invalid_module = invalid.module("arith");
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
  const auto reported_module = reported.module("arith");
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

  joggle::Compiler guarded_query;
  guarded_query.add(R"(
    joggle 1;
    module guarded_query@1.0.0 {
      type a();
      op identity<T: type>(input: T) -> T;
    }
  )",
                    "guarded-query.joggle");
  if (!guarded_query.link()) {
    return EXIT_FAILURE;
  }
  const auto guarded_module = guarded_query.module("guarded_query");
  const auto guarded_a_decl =
      guarded_module ? guarded_module->type("a") : std::nullopt;
  const auto guarded_identity =
      guarded_module ? guarded_module->operation("identity") : std::nullopt;
  const auto guarded_a =
      guarded_a_decl ? guarded_query.make(*guarded_a_decl) : std::nullopt;
  auto guarded_graph = guarded_query.graph();
  if (!guarded_identity || !guarded_a || !guarded_graph) {
    return EXIT_FAILURE;
  }
  {
    auto edit = guarded_graph->edit();
    const auto input = edit.argument(*guarded_a);
    edit.append(*guarded_identity, {input});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  guarded_query.bind(
      *guarded_identity,
      [](const joggle::Operation&, joggle::Diagnostics& diagnostics) {
        diagnostics.report("guarded query input rejected");
        return false;
      });
  bool query_called = false;
  const auto guarded_result = guarded_query.query(
      *guarded_graph, [&](const joggle::Graph&, joggle::Diagnostics&) {
        query_called = true;
        return NodeCount{1U};
      });
  ok &= expect(!guarded_result && !query_called && !guarded_query.ok(),
               "a query does not observe a Graph rejected by bound domain "
               "semantics");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
