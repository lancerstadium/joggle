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
mod mod_defs@1.0.0 {
  pub type word();
  pub fn source() -> word;
  pub fn callback(input: i32) -> i32;
  pub fn apply(input: i32, body: (i32) -> i32) -> i32;

  pub fn main() -> word {
    value = source();
    return value;
  }

  pub fn forward() -> word {
    return main();
  }

  pub fn choose(condition: i1, lhs: word, rhs: word) -> word {
    return if condition { lhs } else { rhs };
  }

  pub fn inline_user(input: i32) -> i32 {
    return apply(input, (value: i32) => callback(value));
  }
}
)";

  joggle::Compiler compiler;
  compiler.add(source, "mod-defs.joggle");
  const bool linked = compiler.link();
  auto main = linked ? compiler.materialize("mod_defs.main") : std::nullopt;
  auto choose = linked ? compiler.materialize("mod_defs.choose") : std::nullopt;
  auto inline_user =
      linked ? compiler.materialize("mod_defs.inline_user") : std::nullopt;
  const auto definitions = compiler.mod("mod_defs");
  const auto prelude = compiler.mod("prelude");
  const auto callback_decl =
      definitions ? definitions->fn("callback") : std::nullopt;
  const auto callable_decl = prelude ? prelude->type("callable") : std::nullopt;
  const auto i32 = compiler.make("i32");
  const auto callable =
      callable_decl && i32
          ? compiler.make(*callable_decl, std::vector<joggle::Type>{*i32},
                          std::vector<joggle::Type>{*i32})
          : std::optional<joggle::Type>{};
  auto callback_value = compiler.create_fn();
  if (!main || !choose || !inline_user || !callback_decl || !callable ||
      !callback_value) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }

  {
    auto edit = callback_value->edit();
    const auto reference = edit.reference(*callback_decl, *callable);
    edit.ret(callback_value->entry(), {reference});
    joggle::Diag callback_diagnostics;
    if (!edit.commit(callback_diagnostics)) {
      callback_diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  joggle::Mod mod("compiled_mod", {2, 3, 4});
  joggle::Diag diagnostics;
  if (!mod.insert("main", std::move(*main), diagnostics) ||
      !mod.insert("choose", std::move(*choose), diagnostics) ||
      !mod.insert("inline_user", std::move(*inline_user), diagnostics) ||
      !mod.insert("callback_value", std::move(*callback_value), diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const std::string before_data(mod.digest());
  joggle::Bytes payload{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
  const std::string data_name = mod.store(payload);
  const std::string with_data(mod.digest());
  joggle::Mod data_copy = mod;
  const std::string duplicate_name = data_copy.store(payload);
  const std::string second_name =
      data_copy.store(joggle::Bytes{std::byte{0x40}});

  const auto dependencies = mod.dependencies();
  const auto materialized_main = mod.fn("main");
  const std::string text = joggle::format(mod);
  joggle::Diag parse_diagnostics;
  const auto parsed =
      joggle::parse_mod(text, parse_diagnostics, "compiled-mod.joggle");
  const std::string reparsed = parsed ? joggle::format(*parsed) : std::string{};
  if (parsed && reparsed != text) {
    std::cerr << "serialized:\n" << text << "reformatted:\n" << reparsed;
  }

  bool ok = true;
  const auto stored = mod.data(data_name);
  ok &= expect(data_name.starts_with("sha256:") &&
                   duplicate_name == data_name && second_name != data_name &&
                   before_data != with_data && mod.digest() == with_data &&
                   data_copy.digest() != mod.digest() && stored &&
                   std::vector<std::byte>(stored->begin(), stored->end()) ==
                       payload &&
                   mod.data() == std::vector<std::string>{data_name},
               "Mod owns content-addressed immutable data with copy-on-write "
               "identity");
  ok &= expect(
      materialized_main &&
          materialized_main->form() == joggle::Mod::FnDecl::Form::Body &&
          materialized_main->inputs().empty() &&
          materialized_main->results().size() == 1U && mod.fns().size() == 4U,
      "a materialized member exposes the same canonical fn "
      "signature instead of living in a second fn table");
  ok &= expect(dependencies ==
                   std::vector<joggle::Mod::Dependency>{{"mod_defs", {1, 0, 0}},
                                                        {"prelude", {6, 0, 0}}},
               "an executable Mod reports exact schema dependencies");
  ok &= expect(parsed && reparsed == text &&
                   parsed->declaration_digest() == mod.declaration_digest(),
               "a multi-Fn Mod serializes with stable artifact and "
               "declaration identities");
  ok &= expect(text.find("import mod_defs@1.0.0;") != std::string::npos &&
                   text.find("import prelude") == std::string::npos &&
                   text.find("fn choose") < text.find("fn main"),
               "serialization emits exact non-ambient imports and stable "
               "Fn order");

  joggle::Compiler object_replay;
  if (definitions && parsed) {
    object_replay.add(*definitions);
    object_replay.add(*parsed);
  }
  const bool object_replay_linked = object_replay.link();
  const auto object_replay_main =
      object_replay_linked ? object_replay.materialize("compiled_mod.main")
                           : std::optional<joggle::Fn>{};
  ok &= expect(object_replay_main && object_replay_main->ops().size() == 1U,
               "a parsed Mod object can enter a fresh compilation without "
               "a filesystem or text round trip");

  joggle::Compiler replay;
  replay.add(source, "mod-defs.joggle");
  replay.add(text, "compiled-mod.joggle");
  const bool replay_linked = replay.link();
  const auto replay_main =
      replay_linked ? replay.materialize("compiled_mod.main") : std::nullopt;
  const auto replay_choose =
      replay_linked ? replay.materialize("compiled_mod.choose") : std::nullopt;
  const auto replay_callback =
      replay_linked ? replay.materialize("compiled_mod.callback_value")
                    : std::nullopt;
  const auto replay_inline =
      replay_linked ? replay.materialize("compiled_mod.inline_user")
                    : std::nullopt;
  if (!replay_main || !replay_choose || !replay_callback || !replay_inline) {
    replay.diag().print(std::cerr);
  }
  ok &= expect(replay_main && replay_main->ops().size() == 1U &&
                   replay_choose && replay_choose->blks().size() == 4U &&
                   replay_inline && replay_inline->ops().size() == 1U &&
                   replay_inline->ops().front().arguments().back().inline_fn(),
               "serialized data-flow and control-flow Fns link and "
               "instantiate again, including inline callable bodies");
  const auto replay_returned =
      replay_callback ? replay_callback->entry().terminator().returned()
                      : std::vector<joggle::Val>{};
  ok &= expect(
      replay_returned.size() == 1U && replay_returned.front().referenced_fn() &&
          replay_returned.front().referenced_fn()->symbol().qualified_name() ==
              "mod_defs.callback",
      "fn-reference dependencies serialize and replay");

  auto local_callee = compiler.materialize("mod_defs.main");
  auto local_caller = compiler.materialize("mod_defs.forward");
  joggle::Mod local("local_link", {1, 0, 0});
  joggle::Diag local_diagnostics;
  if (!local_callee || !local_caller ||
      !local.insert("callee", std::move(*local_callee), local_diagnostics) ||
      !local.insert("caller", std::move(*local_caller), local_diagnostics)) {
    local_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto callee = local.fn("callee");
  const auto caller = local.fn("caller");
  joggle::Fn* caller_body = caller ? local.body(*caller) : nullptr;
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
  joggle::Diag local_parse_diagnostics;
  const auto local_reparsed = joggle::parse_mod(
      local_text, local_parse_diagnostics, "local-link.joggle");
  ok &= expect(compiler.verify(local) && local_reparsed &&
                   joggle::format(*local_reparsed) == local_text &&
                   caller_body->ops()
                           .front()
                           .callee()
                           .referenced_fn()
                           ->symbol()
                           .mod_name() == "local_link",
               "inserted Fns are immediately usable as typed local "
               "callees without a format-and-parse round trip");

  auto mixed_main = compiler.materialize("mod_defs.main");
  joggle::Mod mixed = *definitions;
  joggle::Diag mixed_diagnostics;
  const bool mixed_inserted =
      mixed_main &&
      mixed.insert("compiled_main", std::move(*mixed_main), mixed_diagnostics);
  const std::string mixed_text = mixed_inserted ? joggle::format(mixed) : "";
  joggle::Diag mixed_parse_diagnostics;
  const auto mixed_reparsed =
      mixed_inserted ? joggle::parse_mod(mixed_text, mixed_parse_diagnostics,
                                         "mixed-mod.joggle")
                     : std::nullopt;
  ok &= expect(mixed_inserted && mixed_diagnostics.ok() && mixed_reparsed &&
                   mixed_reparsed->fn("source") &&
                   mixed_reparsed->fn("compiled_main") &&
                   mixed_text.find("import mod_defs") == std::string::npos &&
                   compiler.verify(mixed),
               "one Mod member table carries source declarations and "
               "materialized IR through validation without a self-import or "
               "second container");

  auto overloaded_main = compiler.materialize("mod_defs.main");
  auto overloaded_choose = compiler.materialize("mod_defs.choose");
  joggle::Mod overloaded("overloaded", {1, 0, 0});
  joggle::Diag overloaded_diagnostics;
  if (!overloaded_main || !overloaded_choose ||
      !overloaded.insert("run", std::move(*overloaded_main),
                         overloaded_diagnostics) ||
      !overloaded.insert("run", std::move(*overloaded_choose),
                         overloaded_diagnostics)) {
    overloaded_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto run_overloads = overloaded.overloads("run");
  const auto no_argument = std::find_if(
      run_overloads.begin(), run_overloads.end(),
      [](const joggle::Mod::FnDecl& fn) { return fn.inputs().empty(); });
  const auto branched = std::find_if(
      run_overloads.begin(), run_overloads.end(),
      [](const joggle::Mod::FnDecl& fn) { return fn.inputs().size() == 3U; });
  const auto* no_argument_body = no_argument != run_overloads.end()
                                     ? overloaded.body(*no_argument)
                                     : nullptr;
  const auto* branched_body =
      branched != run_overloads.end() ? overloaded.body(*branched) : nullptr;
  const std::string overloaded_text = joggle::format(overloaded);
  joggle::Diag overloaded_parse_diagnostics;
  const auto overloaded_reparsed = joggle::parse_mod(
      overloaded_text, overloaded_parse_diagnostics, "overloaded.joggle");
  ok &= expect(
      run_overloads.size() == 2U && !overloaded.fn("run") &&
          no_argument != run_overloads.end() &&
          branched != run_overloads.end() && no_argument_body != nullptr &&
          no_argument_body->arguments().empty() && branched_body != nullptr &&
          branched_body->arguments().size() == 3U && overloaded_reparsed &&
          overloaded_reparsed->overloads("run").size() == 2U,
      "programmatic Mods preserve overloads and select mutable "
      "bodies by complete declaration across serialization");

  auto duplicate = compiler.materialize("mod_defs.main");
  ok &= expect(duplicate &&
                   !mod.insert("main", std::move(*duplicate), diagnostics) &&
                   !diagnostics.ok(),
               "an executable Mod rejects duplicate Fn signatures");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
