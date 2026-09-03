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
module module_defs@1.0.0 {
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
  compiler.add(source, "module-defs.joggle");
  const bool linked = compiler.link();
  auto main = linked ? compiler.function("module_defs.main") : std::nullopt;
  auto choose = linked ? compiler.function("module_defs.choose") : std::nullopt;
  const auto definitions = compiler.module("module_defs");
  const auto prelude = compiler.module("prelude");
  const auto callback_decl =
      definitions ? definitions->function("callback") : std::nullopt;
  const auto callable_decl = prelude ? prelude->type("callable") : std::nullopt;
  const auto i32 = compiler.make("i32");
  const auto callable =
      callable_decl && i32
          ? compiler.make(*callable_decl, std::vector<joggle::Type>{*i32},
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

  joggle::Module module("compiled_module", {2, 3, 4});
  joggle::Diagnostics diagnostics;
  if (!module.insert("main", std::move(*main), diagnostics) ||
      !module.insert("choose", std::move(*choose), diagnostics) ||
      !module.insert("callback_value", std::move(*callback_value),
                     diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto dependencies = module.dependencies();
  const auto materialized_main = module.function("main");
  const std::string text = joggle::format(module);
  joggle::Diagnostics parse_diagnostics;
  const auto parsed =
      joggle::parse_module(text, parse_diagnostics, "compiled-module.joggle");
  const std::string reparsed = parsed ? joggle::format(*parsed) : std::string{};
  if (parsed && reparsed != text) {
    std::cerr << "serialized:\n" << text << "reformatted:\n" << reparsed;
  }

  bool ok = true;
  ok &= expect(materialized_main &&
                   materialized_main->form() ==
                       joggle::Module::Function::Form::Body &&
                   materialized_main->inputs().empty() &&
                   materialized_main->results().size() == 1U &&
                   module.functions().size() == 3U,
               "a materialized member exposes the same canonical function "
               "signature instead of living in a second function table");
  ok &= expect(dependencies ==
                   std::vector<joggle::Module::Dependency>{
                       {"module_defs", {1, 0, 0}}, {"prelude", {2, 0, 0}}},
               "an executable Module reports exact schema dependencies");
  ok &= expect(parsed && reparsed == text,
               "a multi-Function Module serializes as canonical source");
  ok &= expect(text.find("import module_defs@1.0.0;") != std::string::npos &&
                   text.find("import prelude") == std::string::npos &&
                   text.find("fn choose") < text.find("fn main"),
               "serialization emits exact non-ambient imports and stable "
               "Function order");

  joggle::Compiler replay;
  replay.add(source, "module-defs.joggle");
  replay.add(text, "compiled-module.joggle");
  const bool replay_linked = replay.link();
  const auto replay_main =
      replay_linked ? replay.function("compiled_module.main") : std::nullopt;
  const auto replay_choose =
      replay_linked ? replay.function("compiled_module.choose") : std::nullopt;
  const auto replay_callback =
      replay_linked ? replay.function("compiled_module.callback_value")
                    : std::nullopt;
  if (!replay_main || !replay_choose || !replay_callback) {
    replay.diagnostics().print(std::cerr);
  }
  ok &= expect(replay_main && replay_main->instructions().size() == 1U &&
                   replay_choose && replay_choose->blocks().size() == 4U,
               "serialized data-flow and control-flow Functions link and "
               "instantiate again");
  const auto replay_returned =
      replay_callback ? replay_callback->entry().terminator().returned()
                      : std::vector<joggle::ir::Value>{};
  ok &= expect(replay_returned.size() == 1U &&
                   replay_returned.front().referenced_function() &&
                   replay_returned.front()
                           .referenced_function()
                           ->symbol()
                           .qualified_name() == "module_defs.callback",
               "function-reference dependencies serialize and replay");

  auto mixed_main = compiler.function("module_defs.main");
  joggle::Module mixed = *definitions;
  joggle::Diagnostics mixed_diagnostics;
  const bool mixed_inserted =
      mixed_main && mixed.insert("compiled_main", std::move(*mixed_main),
                                 mixed_diagnostics);
  const std::string mixed_text = mixed_inserted ? joggle::format(mixed) : "";
  joggle::Diagnostics mixed_parse_diagnostics;
  const auto mixed_reparsed =
      mixed_inserted
          ? joggle::parse_module(mixed_text, mixed_parse_diagnostics,
                                 "mixed-module.joggle")
          : std::nullopt;
  ok &= expect(mixed_inserted && mixed_diagnostics.ok() && mixed_reparsed &&
                   mixed_reparsed->function("source") &&
                   mixed_reparsed->function("compiled_main") &&
                   mixed_text.find("import module_defs") == std::string::npos,
               "one Module member table carries source declarations and "
               "materialized IR without a self-import or second container");

  auto duplicate = compiler.function("module_defs.main");
  ok &= expect(duplicate &&
                   !module.insert("main", std::move(*duplicate), diagnostics) &&
                   !diagnostics.ok(),
               "an executable Module rejects duplicate Function names");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
