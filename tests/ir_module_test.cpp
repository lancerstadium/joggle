#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
  constexpr std::string_view source = R"(
joggle 1;
module program_defs@1.0.0 {
  type word();
  fn source() -> word;
  fn callback(input: i32) -> i32;

  fn main() -> word {
    value = source();
    return value;
  }

  fn choose(condition: i1, lhs: word, rhs: word) -> word {
    return if condition { lhs } else { rhs };
  }
}
)";

  joggle::Compiler compiler;
  compiler.add(source, "program-defs.joggle");
  const bool linked = compiler.link();
  auto main = linked ? compiler.function("program_defs.main") : std::nullopt;
  auto choose = linked ? compiler.function("program_defs.choose")
                       : std::nullopt;
  const auto definitions = compiler.module("program_defs");
  const auto prelude = compiler.module("prelude");
  const auto callback_decl =
      definitions ? definitions->function("callback") : std::nullopt;
  const auto callable_decl =
      prelude ? prelude->type("callable") : std::nullopt;
  const auto i32 = compiler.make("i32");
  const auto callable = callable_decl && i32
                            ? compiler.make(
                                  *callable_decl,
                                  std::vector<joggle::Type>{*i32},
                                  std::vector<joggle::Type>{*i32})
                            : std::optional<joggle::Type>{};
  auto callback_value = compiler.function();
  if (!main || !choose || !callback_decl || !callable || !callback_value) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  {
    auto edit = callback_value->edit();
    const auto reference = edit.reference(*callback_decl, *callable);
    edit.ret(callback_value->entry(), {reference});
    joggle::Diagnostics callback_diagnostics;
    if (!edit.commit(callback_diagnostics)) {
      callback_diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  joggle::ir::Module program;
  joggle::Diagnostics diagnostics;
  if (!program.insert("main", std::move(*main), diagnostics) ||
      !program.insert("choose", std::move(*choose), diagnostics) ||
      !program.insert("callback_value", std::move(*callback_value),
                      diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto dependencies = joggle::ir::dependencies(program);
  const std::string text =
      joggle::format(program, "compiled_program", {2, 3, 4});
  joggle::Diagnostics parse_diagnostics;
  const auto parsed = joggle::parse_module(
      text, parse_diagnostics, "compiled-program.joggle");
  const std::string reparsed = parsed ? joggle::format(*parsed) : std::string{};
  if (parsed && reparsed != text) {
    std::cerr << "serialized:\n" << text << "reformatted:\n" << reparsed;
  }

  bool ok = true;
  ok &= expect(
      dependencies ==
          std::vector<joggle::ir::Dependency>{{"prelude", {1, 1, 0}},
                                              {"program_defs", {1, 0, 0}}},
      "an executable Module reports exact schema dependencies");
  ok &= expect(parsed && reparsed == text,
               "a multi-Function IR Module serializes as canonical source");
  ok &= expect(text.find("import program_defs@1.0.0;") != std::string::npos &&
                   text.find("import prelude") == std::string::npos &&
                   text.find("fn choose") < text.find("fn main"),
               "serialization emits exact non-ambient imports and stable "
               "Function order");

  joggle::Compiler replay;
  replay.add(source, "program-defs.joggle");
  replay.add(text, "compiled-program.joggle");
  const bool replay_linked = replay.link();
  const auto replay_main =
      replay_linked ? replay.function("compiled_program.main") : std::nullopt;
  const auto replay_choose =
      replay_linked ? replay.function("compiled_program.choose")
                    : std::nullopt;
  const auto replay_callback =
      replay_linked ? replay.function("compiled_program.callback_value")
                    : std::nullopt;
  if (!replay_main || !replay_choose || !replay_callback) {
    replay.diagnostics().print(std::cerr);
  }
  ok &= expect(replay_main && replay_main->instructions().size() == 1U &&
                   replay_choose && replay_choose->blocks().size() == 4U,
               "serialized data-flow and control-flow Functions link and "
               "instantiate again");
  const auto replay_returned = replay_callback
                                   ? replay_callback->entry()
                                         .terminator()
                                         .returned()
                                   : std::vector<joggle::ir::Value>{};
  ok &= expect(replay_returned.size() == 1U &&
                   replay_returned.front().referenced_function() &&
                   replay_returned.front()
                           .referenced_function()
                           ->symbol()
                           .qualified_name() == "program_defs.callback",
               "function-reference dependencies serialize and replay");

  auto duplicate = compiler.function("program_defs.main");
  ok &= expect(duplicate &&
                   !program.insert("main", std::move(*duplicate), diagnostics) &&
                   !diagnostics.ok(),
               "an executable Module rejects duplicate Function names");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
