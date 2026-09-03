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
  compiler.load(JOGGLE_TEST_MODULE);
  compiler.add(R"(
    joggle 1;
    module testing@1.0.0 {
      import test_ir@1;
      fn marker<T: type>(input: T) -> T;
      fn cleanup(input: function) -> function;
      fn optimize(input: function) -> function {
        return cleanup(test_ir.canonicalize(input));
      }
      fn abort(input: function) -> function;
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
  const auto canonicalize_schema = test_ir->function("canonicalize");
  const auto marker_schema = testing->function("marker");
  const auto cleanup_schema = testing->function("cleanup");
  const auto optimize_schema = testing->function("optimize");
  const auto abort_schema = testing->function("abort");
  if (!integer_schema || !add_schema || !cast_schema || !canonicalize_schema ||
      !marker_schema || !cleanup_schema || !optimize_schema || !abort_schema) {
    return EXIT_FAILURE;
  }

  compiler.verify(*integer_schema, [](const joggle::Type& type,
                                      joggle::Diagnostics& diagnostics) {
    const auto width = type.get<std::int64_t>("width");
    if (!width || *width <= 0) {
      diagnostics.report("integer width must be positive");
      return false;
    }
    return true;
  });
  const auto same_type = [](const joggle::Op& op,
                            joggle::Diagnostics& diagnostics) {
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
  compiler.bind(
      *canonicalize_schema,
      [cast_schema](joggle::Function function, joggle::Diagnostics& diagnostics)
          -> std::optional<joggle::Function> {
        auto edit = function.edit();
        for (const joggle::Op& op : function.ops()) {
          if (op.callee() != *cast_schema) {
            continue;
          }
          edit.replace(op.result(0), op.arguments().front());
          edit.erase(op);
        }
        if (!edit.commit(diagnostics)) {
          return std::nullopt;
        }
        return function;
      });

  std::size_t query_runs = 0;
  const auto compute_nodes = [&](const joggle::Function& function) {
    ++query_runs;
    return function.ops().size();
  };

  compiler.bind(
      *cleanup_schema,
      [&compute_nodes](
          joggle::Function function,
          joggle::Diagnostics& diagnostics) -> std::optional<joggle::Function> {
        static_cast<void>(compute_nodes(function));
        const auto operations = function.ops();
        const bool has_marker =
            std::any_of(operations.begin(), operations.end(),
                        [](const joggle::Op& op) {
                          return op.callee().name() == "marker";
                        });
        if (!has_marker) {
          return function;
        }
        auto edit = function.edit();
        for (const joggle::Op& op : operations) {
          if (op.callee().name() != "marker") {
            continue;
          }
          edit.replace(op.result(0), op.arguments()[0]);
          edit.erase(op);
        }
        if (!edit.commit(diagnostics)) {
          return std::nullopt;
        }
        return function;
      });

  const auto integer = compiler.make(*integer_schema, 8);
  auto function = compiler.create_function();
  if (!integer || !function) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  {
    auto edit = function->edit();
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
  ok &= expect(compiler.verify(*function), "native Op verification");
  auto optimized = compiler.run<joggle::Function>(*optimize_schema, *function);
  ok &= expect(optimized.has_value(), "composed native transformations run");
  if (optimized) {
    function = std::move(optimized);
  }
  ok &= expect(function->ops().size() == 1U,
               "a compiler function transforms through Function::Edit");
  ok &=
      expect(query_runs == 1U,
             "function-local analysis executes explicitly without a side API");

  compiler.bind(*abort_schema,
                [marker_schema](joggle::Compiler&, joggle::Function current,
                                joggle::Diagnostics& diagnostics)
                    -> std::optional<joggle::Function> {
                  const auto producer = current.ops().front();
                  auto edit = current.edit();
                  edit.append(*marker_schema, {producer.result(0)});
                  if (!edit.commit(diagnostics)) {
                    return std::nullopt;
                  }
                  return std::nullopt;
                });
  const auto aborted = compiler.run<joggle::Function>(*abort_schema, *function);
  ok &= expect(!aborted, "a failing compiler function reports failure");
  ok &= expect(function->ops().size() == 1U,
               "function-level checkpoint restores committed inner edits");

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
  invalid.verify(*invalid_integer,
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
  reported.verify(*reported_integer,
                  [](const joggle::Type&, joggle::Diagnostics& diagnostics) {
                    diagnostics.report("reported verifier failure");
                    return true;
                  });
  ok &= expect(
      !reported.make(*reported_integer, 8, false) && !reported.ok(),
      "a verifier diagnostic rejects construction even if it returns true");

  joggle::Compiler throwing;
  throwing.load(JOGGLE_TEST_MODULE);
  throwing.add(R"(
joggle 1;
module throwing@1.0.0 {
  import test_ir@1;
  attr tag(value: int);
  fn use(input: test_ir.integer<8>) -> test_ir.integer<8> {
    return test_ir.cast(input);
  }
}
)",
               "throwing.joggle");
  const bool throwing_linked = throwing.link();
  const auto throwing_test_ir = throwing.module("test_ir");
  const auto throwing_module = throwing.module("throwing");
  const auto throwing_integer =
      throwing_test_ir ? throwing_test_ir->type("integer") : std::nullopt;
  const auto throwing_cast =
      throwing_test_ir ? throwing_test_ir->function("cast") : std::nullopt;
  const auto throwing_tag =
      throwing_module ? throwing_module->attribute("tag") : std::nullopt;
  const auto existing_integer =
      throwing_integer ? throwing.make(*throwing_integer, 8)
                       : std::optional<joggle::Type>{};
  const auto throwing_function = throwing.materialize("throwing.use");
  if (!throwing_linked || !throwing_integer || !throwing_cast ||
      !throwing_tag || !existing_integer || !throwing_function) {
    throwing.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  throwing.verify(*throwing_integer,
                  [](const joggle::Type&) -> bool {
                    throw std::runtime_error("type verifier exception");
                  });
  throwing.verify(*throwing_tag,
                  [](const joggle::Attribute&) -> bool {
                    throw 1;
                  });
  throwing.verify(*throwing_cast,
                  [](const joggle::Op&) -> bool {
                    throw std::runtime_error("Op verifier exception");
                  });
  const auto rejected_type = throwing.make(*throwing_integer, 16);
  const auto rejected_attribute = throwing.make(*throwing_tag, 1);
  const bool rejected_function = throwing.verify(*throwing_function);
  const auto& throwing_diagnostics = throwing.diagnostics().entries();
  const auto thrown_diagnostics = std::count_if(
      throwing_diagnostics.begin(), throwing_diagnostics.end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("semantic verifier for") !=
                   std::string::npos &&
               diagnostic.message.find("threw") != std::string::npos;
      });
  const bool reports_unknown_exception = std::any_of(
      throwing_diagnostics.begin(), throwing_diagnostics.end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("unknown exception") !=
               std::string::npos;
      });
  const bool locates_op_exception = std::any_of(
      throwing_diagnostics.begin(), throwing_diagnostics.end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find("Op verifier exception") !=
                   std::string::npos &&
               diagnostic.source.has_value();
      });
  ok &= expect(!rejected_type && !rejected_attribute && !rejected_function &&
                   thrown_diagnostics == 3 && reports_unknown_exception &&
                   locates_op_exception,
               "Type, Attribute, and Op verifier exceptions become "
               "ordinary diagnostics");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
