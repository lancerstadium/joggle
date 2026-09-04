#include <algorithm>
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

  fn forward() -> word {
    return main();
  }

  fn choose(condition: i1, lhs: word, rhs: word) -> word {
    return if condition { lhs } else { rhs };
  }
}
)";

  joggle::Compiler compiler;
  compiler.add(source, "module-defs.joggle");
  const bool linked = compiler.link();
  auto main = linked ? compiler.materialize("module_defs.main") : std::nullopt;
  auto choose =
      linked ? compiler.materialize("module_defs.choose") : std::nullopt;
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
  auto callback_value = compiler.create_function();
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

  const std::string before_data(module.digest());
  joggle::Bytes payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
  const std::string data_name = module.store(payload);
  const std::string with_data(module.digest());
  joggle::Module data_copy = module;
  const std::string duplicate_name = data_copy.store(payload);
  const std::string second_name =
      data_copy.store(joggle::Bytes{std::byte{0x40}});

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
  const auto stored = module.data(data_name);
  ok &= expect(data_name.starts_with("sha256:") &&
                   duplicate_name == data_name && second_name != data_name &&
                   before_data != with_data && module.digest() == with_data &&
                   data_copy.digest() != module.digest() && stored &&
                   std::vector<std::byte>(stored->begin(), stored->end()) ==
                       payload &&
                   module.data() == std::vector<std::string>{data_name},
               "Module owns content-addressed immutable data with copy-on-write "
               "identity");
  ok &= expect(materialized_main &&
                   materialized_main->form() ==
                       joggle::Module::FunctionDecl::Form::Body &&
                   materialized_main->inputs().empty() &&
                   materialized_main->results().size() == 1U &&
                   module.functions().size() == 3U,
               "a materialized member exposes the same canonical function "
               "signature instead of living in a second function table");
  ok &= expect(dependencies ==
                   std::vector<joggle::Module::Dependency>{
                       {"module_defs", {1, 0, 0}}, {"prelude", {3, 0, 0}}},
               "an executable Module reports exact schema dependencies");
  ok &= expect(parsed && reparsed == text &&
                   parsed->declaration_digest() == module.declaration_digest(),
               "a multi-Function Module serializes with stable artifact and "
               "declaration identities");
  ok &= expect(text.find("import module_defs@1.0.0;") != std::string::npos &&
                   text.find("import prelude") == std::string::npos &&
                   text.find("fn choose") < text.find("fn main"),
               "serialization emits exact non-ambient imports and stable "
               "Function order");

  joggle::Compiler object_replay;
  if (definitions && parsed) {
    object_replay.add(*definitions);
    object_replay.add(*parsed);
  }
  const bool object_replay_linked = object_replay.link();
  const auto object_replay_main =
      object_replay_linked
          ? object_replay.materialize("compiled_module.main")
          : std::optional<joggle::Function>{};
  ok &= expect(object_replay_main && object_replay_main->ops().size() == 1U,
               "a parsed Module object can enter a fresh compilation without "
               "a filesystem or text round trip");

  joggle::Compiler replay;
  replay.add(source, "module-defs.joggle");
  replay.add(text, "compiled-module.joggle");
  const bool replay_linked = replay.link();
  const auto replay_main =
      replay_linked ? replay.materialize("compiled_module.main") : std::nullopt;
  const auto replay_choose = replay_linked
                                 ? replay.materialize("compiled_module.choose")
                                 : std::nullopt;
  const auto replay_callback =
      replay_linked ? replay.materialize("compiled_module.callback_value")
                    : std::nullopt;
  if (!replay_main || !replay_choose || !replay_callback) {
    replay.diagnostics().print(std::cerr);
  }
  ok &= expect(replay_main && replay_main->ops().size() == 1U &&
                   replay_choose && replay_choose->blocks().size() == 4U,
               "serialized data-flow and control-flow Functions link and "
               "instantiate again");
  const auto replay_returned =
      replay_callback ? replay_callback->entry().terminator().returned()
                      : std::vector<joggle::Value>{};
  ok &= expect(replay_returned.size() == 1U &&
                   replay_returned.front().referenced_function() &&
                   replay_returned.front()
                           .referenced_function()
                           ->symbol()
                           .qualified_name() == "module_defs.callback",
               "function-reference dependencies serialize and replay");

  auto local_callee = compiler.materialize("module_defs.main");
  auto local_caller = compiler.materialize("module_defs.forward");
  joggle::Module local("local_link", {1, 0, 0});
  joggle::Diagnostics local_diagnostics;
  if (!local_callee || !local_caller ||
      !local.insert("callee", std::move(*local_callee), local_diagnostics) ||
      !local.insert("caller", std::move(*local_caller), local_diagnostics)) {
    local_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto callee = local.function("callee");
  const auto caller = local.function("caller");
  joggle::Function* caller_body = caller ? local.body(*caller) : nullptr;
  if (!callee || caller_body == nullptr || caller_body->ops().size() != 1U) {
    return EXIT_FAILURE;
  }
  {
    const joggle::Op call = caller_body->ops().front();
    auto edit = caller_body->edit();
    edit.replace(call, *callee);
    if (!edit.commit(local_diagnostics)) {
      local_diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }
  const std::string local_text = joggle::format(local);
  joggle::Diagnostics local_parse_diagnostics;
  const auto local_reparsed = joggle::parse_module(
      local_text, local_parse_diagnostics, "local-link.joggle");
  ok &= expect(compiler.verify(local) && local_reparsed &&
                   joggle::format(*local_reparsed) == local_text &&
                   caller_body->ops().front().callee().symbol().module_name() ==
                       "local_link",
               "inserted Functions are immediately usable as typed local "
               "callees without a format-and-parse round trip");

  auto mixed_main = compiler.materialize("module_defs.main");
  joggle::Module mixed = *definitions;
  joggle::Diagnostics mixed_diagnostics;
  const bool mixed_inserted =
      mixed_main &&
      mixed.insert("compiled_main", std::move(*mixed_main), mixed_diagnostics);
  const std::string mixed_text = mixed_inserted ? joggle::format(mixed) : "";
  joggle::Diagnostics mixed_parse_diagnostics;
  const auto mixed_reparsed =
      mixed_inserted ? joggle::parse_module(mixed_text, mixed_parse_diagnostics,
                                            "mixed-module.joggle")
                     : std::nullopt;
  ok &= expect(mixed_inserted && mixed_diagnostics.ok() && mixed_reparsed &&
                   mixed_reparsed->function("source") &&
                   mixed_reparsed->function("compiled_main") &&
                   mixed_text.find("import module_defs") == std::string::npos &&
                   compiler.verify(mixed),
               "one Module member table carries source declarations and "
               "materialized IR through validation without a self-import or "
               "second container");

  auto overloaded_main = compiler.materialize("module_defs.main");
  auto overloaded_choose = compiler.materialize("module_defs.choose");
  joggle::Module overloaded("overloaded", {1, 0, 0});
  joggle::Diagnostics overloaded_diagnostics;
  if (!overloaded_main || !overloaded_choose ||
      !overloaded.insert("run", std::move(*overloaded_main),
                         overloaded_diagnostics) ||
      !overloaded.insert("run", std::move(*overloaded_choose),
                         overloaded_diagnostics)) {
    overloaded_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto run_overloads = overloaded.overloads("run");
  const auto no_argument =
      std::find_if(run_overloads.begin(), run_overloads.end(),
                   [](const joggle::Module::FunctionDecl& function) {
                     return function.inputs().empty();
                   });
  const auto branched =
      std::find_if(run_overloads.begin(), run_overloads.end(),
                   [](const joggle::Module::FunctionDecl& function) {
                     return function.inputs().size() == 3U;
                   });
  const auto* no_argument_body = no_argument != run_overloads.end()
                                     ? overloaded.body(*no_argument)
                                     : nullptr;
  const auto* branched_body =
      branched != run_overloads.end() ? overloaded.body(*branched) : nullptr;
  const std::string overloaded_text = joggle::format(overloaded);
  joggle::Diagnostics overloaded_parse_diagnostics;
  const auto overloaded_reparsed = joggle::parse_module(
      overloaded_text, overloaded_parse_diagnostics, "overloaded.joggle");
  ok &= expect(
      run_overloads.size() == 2U && !overloaded.function("run") &&
          no_argument != run_overloads.end() &&
          branched != run_overloads.end() && no_argument_body != nullptr &&
          no_argument_body->arguments().empty() && branched_body != nullptr &&
          branched_body->arguments().size() == 3U && overloaded_reparsed &&
          overloaded_reparsed->overloads("run").size() == 2U,
      "programmatic Modules preserve overloads and select mutable "
      "bodies by complete declaration across serialization");

  auto duplicate = compiler.materialize("module_defs.main");
  ok &= expect(duplicate &&
                   !module.insert("main", std::move(*duplicate), diagnostics) &&
                   !diagnostics.ok(),
               "an executable Module rejects duplicate Function signatures");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
