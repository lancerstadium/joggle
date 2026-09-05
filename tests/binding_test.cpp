#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

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
  compiler.load(JOGGLE_TEST_MOD);
  compiler.add(R"(
    joggle 1;
    mod testing@1.0.0 {
      import test_ir@1;
      fn marker<T>(input: T) -> T;
      fn cleanup(input: fn) -> fn;
      fn optimize(input: fn) -> fn {
        return cleanup(test_ir.canonicalize(input));
      }
      fn abort(input: fn) -> fn;
    }
  )",
               "testing.joggle");
  if (!compiler.link()) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto test_ir = compiler.mod("test_ir");
  const auto testing = compiler.mod("testing");
  if (!test_ir || !testing) {
    return EXIT_FAILURE;
  }
  const auto integer_schema = test_ir->type("integer");
  const auto add_schema = test_ir->fn("+");
  const auto cast_schema = test_ir->fn("cast");
  const auto canonicalize_schema = test_ir->fn("canonicalize");
  const auto marker_schema = testing->fn("marker");
  const auto cleanup_schema = testing->fn("cleanup");
  const auto optimize_schema = testing->fn("optimize");
  const auto abort_schema = testing->fn("abort");
  if (!integer_schema || !add_schema || !cast_schema || !canonicalize_schema ||
      !marker_schema || !cleanup_schema || !optimize_schema || !abort_schema) {
    return EXIT_FAILURE;
  }

  compiler.verify(*integer_schema,
                  [](const joggle::Type& type, joggle::Diag& diagnostics) {
                    const auto width = type.get<std::int64_t>("width");
                    if (!width || *width <= 0) {
                      diagnostics.report("integer width must be positive");
                      return false;
                    }
                    return true;
                  });
  const auto same_type = [](const joggle::Op& op, joggle::Diag& diagnostics) {
    const auto arguments = op.arguments();
    const auto results = op.results();
    if (results.empty() ||
        std::any_of(arguments.begin(), arguments.end(), [&](const auto& value) {
          return value.type() != results[0].type();
        })) {
      diagnostics.report("integer Op types must agree");
      return false;
    }
    return true;
  };
  compiler.verify(*add_schema, same_type);
  compiler.verify(*cast_schema, same_type);
  compiler.verify(*marker_schema, same_type);
  compiler.bind(*canonicalize_schema,
                [cast_schema](joggle::Fn fn, joggle::Diag& diagnostics)
                    -> std::optional<joggle::Fn> {
                  auto edit = fn.edit();
                  for (const joggle::Op& op : fn.ops()) {
                    if (op.callee().referenced_fn() != cast_schema) {
                      continue;
                    }
                    edit.replace(op.result(0), op.arguments().front());
                    edit.erase(op);
                  }
                  if (!edit.commit(diagnostics)) {
                    return std::nullopt;
                  }
                  return fn;
                });

  std::size_t query_runs = 0;
  const auto compute_nodes = [&](const joggle::Fn& fn) {
    ++query_runs;
    return fn.ops().size();
  };

  compiler.bind(
      *cleanup_schema,
      [&compute_nodes](joggle::Fn fn,
                       joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
        static_cast<void>(compute_nodes(fn));
        const auto operations = fn.ops();
        const bool has_marker = std::any_of(
            operations.begin(), operations.end(), [](const joggle::Op& op) {
              return op.callee().referenced_fn()->name() == "marker";
            });
        if (!has_marker) {
          return fn;
        }
        auto edit = fn.edit();
        for (const joggle::Op& op : operations) {
          if (op.callee().referenced_fn()->name() != "marker") {
            continue;
          }
          edit.replace(op.result(0), op.arguments()[0]);
          edit.erase(op);
        }
        if (!edit.commit(diagnostics)) {
          return std::nullopt;
        }
        return fn;
      });

  const auto integer = compiler.make(*integer_schema, 8);
  auto fn = compiler.create_fn();
  if (!integer || !fn) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  {
    auto edit = fn->edit();
    const auto lhs = edit.argument(*integer);
    const auto rhs = edit.argument(*integer);
    const auto add = edit.call(*add_schema, {lhs, rhs});
    const auto cast = edit.call(*cast_schema, {add.result(0)});
    edit.call(*marker_schema, {cast.result(0)});
    joggle::Diag diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  bool ok = true;
  ok &= expect(compiler.verify(*fn), "bound Op verification");
  auto optimized = compiler.run<joggle::Fn>(*optimize_schema, *fn);
  ok &= expect(optimized.has_value(), "composed bound transformations run");
  if (optimized) {
    fn = std::move(optimized);
  }
  ok &= expect(fn->ops().size() == 1U,
               "a compiler fn transforms through Fn::Edit");
  ok &= expect(query_runs == 1U,
               "fn-local analysis executes explicitly without a side API");

  compiler.bind(
      *abort_schema,
      [marker_schema](joggle::Compiler&, joggle::Fn current,
                      joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
        const auto producer = current.ops().front();
        auto edit = current.edit();
        edit.call(*marker_schema, {producer.result(0)});
        if (!edit.commit(diagnostics)) {
          return std::nullopt;
        }
        return std::nullopt;
      });
  const auto aborted = compiler.run<joggle::Fn>(*abort_schema, *fn);
  ok &= expect(!aborted, "a failing compiler fn reports failure");
  ok &= expect(fn->ops().size() == 1U,
               "fn-level checkpoint restores committed inner edits");

  joggle::Compiler invalid;
  invalid.load(JOGGLE_TEST_MOD);
  if (!invalid.link()) {
    return EXIT_FAILURE;
  }
  const auto invalid_mod = invalid.mod("test_ir");
  const auto invalid_integer =
      invalid_mod ? invalid_mod->type("integer") : std::nullopt;
  if (!invalid_integer) {
    return EXIT_FAILURE;
  }
  invalid.verify(*invalid_integer, [](const joggle::Type& type, joggle::Diag&) {
    const auto width = type.get<std::int64_t>("width");
    return width && *width > 0;
  });
  ok &= expect(!invalid.make(*invalid_integer, 0) && !invalid.ok(),
               "type verifier rejection is diagnosed");

  joggle::Compiler reported;
  reported.load(JOGGLE_TEST_MOD);
  if (!reported.link()) {
    return EXIT_FAILURE;
  }
  const auto reported_mod = reported.mod("test_ir");
  const auto reported_integer =
      reported_mod ? reported_mod->type("integer") : std::nullopt;
  if (!reported_integer) {
    return EXIT_FAILURE;
  }
  reported.verify(*reported_integer,
                  [](const joggle::Type&, joggle::Diag& diagnostics) {
                    diagnostics.report("reported verifier failure");
                    return true;
                  });
  ok &= expect(
      !reported.make(*reported_integer, 8, false) && !reported.ok(),
      "a verifier diagnostic rejects construction even if it returns true");

  joggle::Compiler throwing;
  throwing.load(JOGGLE_TEST_MOD);
  throwing.add(R"(
joggle 1;
mod throwing@1.0.0 {
  import test_ir@1;
  type tag(value: int);
  fn use(input: test_ir.integer<8>) -> test_ir.integer<8> {
    return test_ir.cast(input);
  }
}
)",
               "throwing.joggle");
  const bool throwing_linked = throwing.link();
  const auto throwing_test_ir = throwing.mod("test_ir");
  const auto throwing_mod = throwing.mod("throwing");
  const auto throwing_integer =
      throwing_test_ir ? throwing_test_ir->type("integer") : std::nullopt;
  const auto throwing_cast =
      throwing_test_ir ? throwing_test_ir->fn("cast") : std::nullopt;
  const auto throwing_tag =
      throwing_mod ? throwing_mod->type("tag") : std::nullopt;
  const auto existing_integer = throwing_integer
                                    ? throwing.make(*throwing_integer, 8)
                                    : std::optional<joggle::Type>{};
  const auto throwing_fn = throwing.materialize("throwing.use");
  if (!throwing_linked || !throwing_integer || !throwing_cast ||
      !throwing_tag || !existing_integer || !throwing_fn) {
    throwing.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  throwing.verify(*throwing_integer, [](const joggle::Type&) -> bool {
    throw std::runtime_error("type verifier exception");
  });
  throwing.verify(*throwing_tag, [](const joggle::Type&) -> bool { throw 1; });
  throwing.verify(*throwing_cast, [](const joggle::Op&) -> bool {
    throw std::runtime_error("Op verifier exception");
  });
  const auto rejected_type = throwing.make(*throwing_integer, 16);
  const auto rejected_metadata = throwing.make(*throwing_tag, 1);
  const bool rejected_fn = throwing.verify(*throwing_fn);
  const auto& throwing_diagnostics = throwing.diag().issues();
  const auto thrown_diagnostics = std::count_if(
      throwing_diagnostics.begin(), throwing_diagnostics.end(),
      [](const joggle::Issue& diagnostic) {
        return diagnostic.message.find("semantic verifier for") !=
                   std::string::npos &&
               diagnostic.message.find("threw") != std::string::npos;
      });
  const bool reports_unknown_exception =
      std::any_of(throwing_diagnostics.begin(), throwing_diagnostics.end(),
                  [](const joggle::Issue& diagnostic) {
                    return diagnostic.message.find("unknown exception") !=
                           std::string::npos;
                  });
  const bool locates_op_exception =
      std::any_of(throwing_diagnostics.begin(), throwing_diagnostics.end(),
                  [](const joggle::Issue& diagnostic) {
                    return diagnostic.message.find("Op verifier exception") !=
                               std::string::npos &&
                           diagnostic.source.has_value();
                  });
  ok &= expect(!rejected_type && !rejected_metadata && !rejected_fn &&
                   thrown_diagnostics == 3 && reports_unknown_exception &&
                   locates_op_exception,
               "Type and Op verifier exceptions become "
               "ordinary diagnostics");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
