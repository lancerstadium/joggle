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

  compiler.bind(*integer_schema,
                [](const joggle::Type& type, joggle::Diagnostics& diagnostics) {
                  const auto width = type.get<std::int64_t>("width");
                  if (!width || *width <= 0) {
                    diagnostics.report("integer width must be positive");
                    return false;
                  }
                  return true;
                });
  const auto same_type = [](const joggle::ir::Instruction& instruction,
                            joggle::Diagnostics& diagnostics) {
    const auto arguments = instruction.arguments();
    const auto results = instruction.results();
    if (results.empty() ||
        std::any_of(arguments.begin(), arguments.end(), [&](const auto& value) {
          return value.type() != results[0].type();
        })) {
      diagnostics.report("integer Instruction types must agree");
      return false;
    }
    return true;
  };
  compiler.bind(*add_schema, same_type);
  compiler.bind(*cast_schema, same_type);
  compiler.bind(*marker_schema, same_type);
  compiler.bind(
      *canonicalize_schema,
      [cast_schema](joggle::ir::Function& function,
                    joggle::Diagnostics& diagnostics) {
        auto edit = function.edit();
        for (const joggle::ir::Instruction& instruction : function.instructions()) {
          if (instruction.callee() != *cast_schema) {
            continue;
          }
          edit.replace(instruction.result(0), instruction.arguments().front());
          edit.erase(instruction);
        }
        return edit.commit(diagnostics);
      });

  std::size_t query_runs = 0;
  const auto compute_nodes = [&](const joggle::ir::Function& function) {
    ++query_runs;
    return function.instructions().size();
  };

  compiler.bind(
      *cleanup_schema,
      [&compute_nodes](joggle::ir::Function& function,
                       joggle::Diagnostics& diagnostics) {
        static_cast<void>(compute_nodes(function));
        const auto operations = function.instructions();
        const bool has_marker =
            std::any_of(operations.begin(), operations.end(),
                        [](const joggle::ir::Instruction& instruction) {
                          return instruction.callee().name() == "marker";
                        });
        if (!has_marker) {
          return true;
        }
        auto edit = function.edit();
        for (const joggle::ir::Instruction& instruction : operations) {
          if (instruction.callee().name() != "marker") {
            continue;
          }
          edit.replace(instruction.result(0), instruction.arguments()[0]);
          edit.erase(instruction);
        }
        return edit.commit(diagnostics);
      });

  const auto integer = compiler.make(*integer_schema, 8);
  auto function = compiler.function();
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
  ok &= expect(compiler.verify(*function), "native Instruction verification");
  ok &= expect(compiler.run(*function, *optimize_schema),
               "composed native transformations run");
  ok &= expect(function->instructions().size() == 1U,
               "a compiler function transforms through Function::Edit");
  ok &= expect(query_runs == 1U,
               "function-local analysis executes explicitly without a side API");

  compiler.bind(*abort_schema, [marker_schema,
                                integer](joggle::Compiler&, joggle::ir::Function& current,
                                      joggle::Diagnostics& diagnostics) {
    const auto producer = current.instructions().front();
    auto edit = current.edit();
    edit.append(*marker_schema, {producer.result(0)});
    if (!edit.commit(diagnostics)) {
      return false;
    }
    return false;
  });
  ok &= expect(!compiler.run(*function, *abort_schema),
               "a failing compiler function reports failure");
  ok &= expect(function->instructions().size() == 1U,
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
