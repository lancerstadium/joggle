#include "joggle/joggle.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

bool fold_add_zero(joggle::Mod& mod, joggle::Op* removed = nullptr) {
  for (joggle::Fn fn : mod.fns()) {
    for (joggle::Blk blk : fn.blks()) {
      for (joggle::Op op : blk.ops()) {
        if (op.kind() != joggle::Op::Kind::call || op.callee() != "operator +")
          continue;
        const std::vector<joggle::Val> args = op.args();
        const std::vector<joggle::Val> outs = op.outs();
        if (args.size() != 2 || outs.size() != 1 || !args[1].is_const())
          continue;
        const auto value = args[1].constant().integer();
        if (!value || *value != 0)
          continue;
        if (removed)
          *removed = op;
        return mod.replace(outs[0], args[0]) && mod.erase(op);
      }
    }
  }
  return false;
}

bool rejects_type(joggle::Env& env, std::string_view source,
                  std::string_view message) {
  joggle::Mod mod;
  if (!joggle::parse(env, source, mod, "invalid-type.jog") || mod.verify(env))
    return false;
  return std::any_of(mod.diags().begin(), mod.diags().end(),
                     [&](const joggle::Diag& diag) {
                       return diag.message.find(message) != std::string::npos;
                     });
}

const joggle::Attr& field(const joggle::Attr& value, std::string_view key) {
  static const joggle::Attr missing;
  const auto* fields = value.dict();
  if (!fields)
    return missing;
  const auto found = fields->find(key);
  return found == fields->end() ? missing : found->second;
}

bool flag(const joggle::Attr& value, std::string_view key) {
  return field(value, key).boolean() == true;
}

std::int64_t number(const joggle::Attr& value, std::string_view key) {
  return field(value, key).integer().value_or(0);
}

std::string_view string_field(const joggle::Attr& value, std::string_view key) {
  return field(value, key).string().value_or(std::string_view{});
}

const joggle::Attr& item(const joggle::Attr& value, std::string_view key,
                         std::size_t index) {
  static const joggle::Attr missing;
  const auto* values = field(value, key).list();
  return values && index < values->size() ? values->at(index) : missing;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 4);
  const joggle::Ty tensor_type("tensor< f32, [2,N] >");
  CHECK(tensor_type.text() == "tensor<f32, [2, N]>");
  CHECK(tensor_type.name() == "tensor");
  CHECK(tensor_type.args().size() == 2);
  CHECK(tensor_type.args()[1].name() == "[]");
  CHECK(tensor_type.args()[1].args().size() == 2);
  CHECK(!joggle::Ty("tensor<i32,>").valid());
  const std::array<joggle::Ty, 2> shape_args{joggle::Ty("2"),
                                             joggle::Ty("N")};
  const joggle::Ty shape_type("[]", shape_args);
  const std::array<joggle::Ty, 2> tensor_args{joggle::Ty("f32"), shape_type};
  const joggle::Ty structured_tensor("tensor", tensor_args);
  CHECK(shape_type.valid() && shape_type.text() == "[2, N]");
  CHECK(structured_tensor == tensor_type);
  CHECK(joggle::Ty("[]", std::span<const joggle::Ty>{}).text() == "[]");
  CHECK(!joggle::Ty("tensor", std::span<const joggle::Ty>{}).valid());
  const std::array<joggle::Ty, 1> invalid_args{joggle::Ty("tensor<")};
  CHECK(!joggle::Ty("tensor", invalid_args).valid());

  std::ifstream input(argv[1]);
  CHECK(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[2]);
  env.path(argv[3]);

  joggle::Mod legacy_keyword;
  CHECK(!joggle::parse(env, "module legacy\n", legacy_keyword,
                       "legacy-keyword.jog"));
  CHECK(std::any_of(legacy_keyword.diags().begin(),
                    legacy_keyword.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.loc.line == 1 &&
                             diag.message.find("expected 'mod'") !=
                                 std::string::npos;
                    }));

  for (const std::string_view name : {
           std::string_view{}, std::string_view("../tensor"),
           std::string_view("tensor/child"),
           std::string_view("tensor\\child"),
           std::string_view(".tensor"), std::string_view("tensor."),
           std::string_view("tensor..child")}) {
    CHECK(!env.load(name));
    CHECK(!env.diags().empty());
    CHECK(env.diags().back().message.find("invalid module name") !=
          std::string::npos);
    env.clear_diags();
  }
  CHECK(env.modules().empty());

  joggle::Mod invalid_module_name;
  CHECK(!joggle::parse(env, "mod invalid.\n", invalid_module_name,
                       "invalid-module-name.jog"));
  CHECK(std::any_of(invalid_module_name.diags().begin(),
                    invalid_module_name.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.loc.line == 1 &&
                             diag.message.find("invalid module name") !=
                                 std::string::npos;
                    }));

  joggle::Mod invalid_use_name;
  CHECK(!joggle::parse(env, "mod valid\nuse invalid.\n", invalid_use_name,
                       "invalid-use-name.jog"));
  CHECK(std::any_of(invalid_use_name.diags().begin(),
                    invalid_use_name.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.loc.line == 2 &&
                             diag.message.find("invalid module name") !=
                                 std::string::npos;
                    }));

  joggle::Mod invalid_fn_name;
  CHECK(!joggle::parse(env,
                       "mod valid\nfn invalid.(x: i32) -> i32;\n",
                       invalid_fn_name, "invalid-fn-name.jog"));
  CHECK(std::any_of(invalid_fn_name.diags().begin(),
                    invalid_fn_name.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.loc.line == 2 &&
                             diag.message.find("invalid function name") !=
                                 std::string::npos;
                    }));

  CHECK(env.load("tensor"));
  CHECK(env.loaded("base"));
  CHECK(env.loaded("tensor"));
  CHECK((env.modules() == std::vector<std::string>{"base", "tensor"}));

  CHECK(env.load("prefix"));
  joggle::Mod short_prefix;
  CHECK(joggle::parse(env,
                      "mod short.prefix\n"
                      "use prefix\n"
                      "fn id(x: prefix.deep.Num) -> prefix.deep.Num {\n"
                      "  return x\n"
                      "}\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  return prefix.deep.pick(x)\n"
                      "}\n",
                      short_prefix, "short-prefix.jog"));
  CHECK(short_prefix.verify(env));
  const joggle::Op short_call =
      short_prefix.find_fn("main").body().ops().front();
  const joggle::Fn short_target = env.resolve(short_prefix, short_call);
  CHECK(short_target && short_target.module() == "prefix" &&
        short_target.name() == "deep.pick");
  CHECK(env.load("prefix.deep"));
  CHECK(short_prefix.verify(env));
  const joggle::Fn stable_short_target =
      env.resolve(short_prefix, short_call);
  CHECK(stable_short_target == short_target);
  const joggle::Fn global_prefix = env.find_fn("prefix.deep.pick");
  CHECK(global_prefix && global_prefix.module() == "prefix.deep");
  CHECK(global_prefix.params().front().type() == joggle::Ty("f32"));
  const joggle::Fn global_prefix_type = env.find_fn("prefix.deep.Num");
  CHECK(global_prefix_type && global_prefix_type.module() == "prefix.deep");

  joggle::Mod long_prefix;
  CHECK(joggle::parse(env,
                      "mod long.prefix\n"
                      "use prefix.deep\n"
                      "fn id(x: prefix.deep.Num) -> prefix.deep.Num {\n"
                      "  return x\n"
                      "}\n"
                      "fn main(x: f32) -> f32 {\n"
                      "  return prefix.deep.pick(x)\n"
                      "}\n",
                      long_prefix, "long-prefix.jog"));
  CHECK(long_prefix.verify(env));
  const joggle::Op long_call =
      long_prefix.find_fn("main").body().ops().front();
  const joggle::Fn long_target = env.resolve(long_prefix, long_call);
  CHECK(long_target && long_target.module() == "prefix.deep" &&
        long_target.name() == "pick");

  joggle::Mod parent_prefix;
  CHECK(joggle::parse(env,
                      "mod prefix\n"
                      "use prefix.deep\n"
                      "fn id(x: prefix.deep.Num) -> prefix.deep.Num {\n"
                      "  return x\n"
                      "}\n"
                      "fn main(x: f32) -> f32 {\n"
                      "  return prefix.deep.pick(x)\n"
                      "}\n",
                      parent_prefix, "parent-prefix.jog"));
  CHECK(parent_prefix.verify(env));
  const joggle::Op parent_call =
      parent_prefix.find_fn("main").body().ops().front();
  const joggle::Fn parent_target = env.resolve(parent_prefix, parent_call);
  CHECK(parent_target && parent_target.module() == "prefix.deep" &&
        parent_target.name() == "pick");

  const std::array<joggle::Source, 2> split_sources{
      joggle::Source{
          "mod split\n"
          "fn entry(x: i32) -> i32 { return helper(x) }\n",
          "module.jog"},
      joggle::Source{
          "local fn helper(x: i32) -> i32 { return x + 1 }\n",
          "lib/helper.jog"}};
  joggle::Mod split_mod;
  CHECK(joggle::parse(env, split_sources, split_mod));
  CHECK(split_mod.verify(env));
  CHECK(split_mod.find_fn("entry") && split_mod.find_fn("helper"));
  CHECK(split_mod.find_fn("entry").loc().file == "module.jog");
  CHECK(split_mod.find_fn("helper").loc().file == "lib/helper.jog");

  const std::array<joggle::Source, 2> invalid_split_sources{
      joggle::Source{"mod invalid_split\n", "module.jog"},
      joggle::Source{
          "fn wrong(x: i32) -> bool { return x }\n",
          "lib/wrong.jog"}};
  joggle::Mod invalid_split;
  CHECK(joggle::parse(env, invalid_split_sources, invalid_split));
  CHECK(!invalid_split.verify(env));
  CHECK(std::any_of(invalid_split.diags().begin(),
                    invalid_split.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.loc.file == "lib/wrong.jog" &&
                             diag.loc.line == 1 &&
                             diag.message.find("return type") !=
                                 std::string::npos;
                    }));

  joggle::Mod dynamic_overload;
  constexpr std::string_view dynamic_overload_source =
      "mod dynamic.overload\n"
      "use tensor\n"
      "fn equal(a: Attr, b: Attr) -> bool { return a == b }\n";
  CHECK(joggle::parse(env, dynamic_overload_source, dynamic_overload,
                      "dynamic-overload.jog"));
  CHECK(dynamic_overload.verify(env));
  const joggle::Op dynamic_equal =
      dynamic_overload.find_fn("equal").body().ops().front();
  const joggle::Fn dynamic_target = env.resolve(dynamic_overload, dynamic_equal);
  CHECK(dynamic_target && dynamic_target.module() == "base" &&
        dynamic_target.name() == "operator ==");

  std::ifstream base_input(std::string(argv[2]) + "/base/module.jog");
  CHECK(base_input);
  std::ostringstream base_source;
  base_source << base_input.rdbuf();
  joggle::Mod base;
  CHECK(joggle::parse(env, base_source.str(), base, "base.jog"));
  CHECK(base.verify(env));
  joggle::Mod base_roundtrip;
  CHECK(joggle::parse(env, joggle::print(base), base_roundtrip,
                      "base-roundtrip.jog"));
  CHECK(base_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(base, base_roundtrip));

  CHECK(env.load("nn"));
  joggle::Mod network;
  constexpr std::string_view network_source =
      "mod network\n"
      "use nn\n"
      "fn stage(x: tensor<f32, [4]>, skip: tensor<f32, [4]>) "
      "-> tensor<f32, [4]> {\n"
      "  return nn.relu(x + skip)\n}\n";
  CHECK(joggle::parse(env, network_source, network, "network.jog"));
  CHECK(network.verify(env));

  const joggle::Fn relu_template = env.find_fn("nn.relu");
  CHECK(relu_template && !relu_template.external());
  joggle::Mod generated_fn;
  constexpr std::string_view generated_source =
      "mod generated\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  return generated.relu(x)\n"
      "}\n";
  CHECK(joggle::parse(env, generated_source, generated_fn, "generated.jog"));
  generated_fn.clear_diags();
  const std::uint64_t before_fn_clone = generated_fn.revision();
  const joggle::Fn local_relu =
      generated_fn.clone(env, relu_template, "relu");
  CHECK(local_relu && local_relu.name() == "relu");
  CHECK(generated_fn.uses() == std::vector<std::string>{"nn"});
  CHECK(generated_fn.revision() == before_fn_clone + 1);
  CHECK(generated_fn.verify(env));
  CHECK(generated_fn.find_fns("relu").size() == 1);
  const std::string generated_text = joggle::print(generated_fn);
  CHECK(generated_text.find("fn relu<E: Ty, S: list<int>>") !=
        std::string::npos);
  CHECK(generated_text.find("tensor.tensor") == std::string::npos);
  generated_fn.clear_diags();
  const std::uint64_t duplicate_fn_revision = generated_fn.revision();
  CHECK(!generated_fn.clone(env, relu_template, "relu"));
  CHECK(joggle::print(generated_fn) == generated_text);
  CHECK(generated_fn.revision() == duplicate_fn_revision);
  generated_fn.clear_diags();
  CHECK(!generated_fn.clone(env, relu_template, "bad.name"));
  CHECK(joggle::print(generated_fn) == generated_text);
  CHECK(generated_fn.revision() == duplicate_fn_revision);
  generated_fn.clear_diags();
  const joggle::Fn leaky_template = env.find_fn("nn.leaky_relu");
  CHECK(leaky_template);
  const joggle::Fn local_leaky =
      generated_fn.clone(env, leaky_template, "relu");
  CHECK(local_leaky && generated_fn.revision() == duplicate_fn_revision + 1);
  const std::vector<joggle::Ty> relu4_args{joggle::Ty("f32"),
                                           joggle::Ty("[4]")};
  const std::uint64_t before_relu4 = generated_fn.revision();
  CHECK(!generated_fn.clone(env, relu_template, "main", relu4_args));
  CHECK(generated_fn.revision() == before_relu4);
  generated_fn.clear_diags();
  const joggle::Fn local_relu4 =
      generated_fn.clone(env, relu_template, "relu4", relu4_args);
  CHECK(local_relu4 && local_relu4.generics().empty());
  CHECK(local_relu4.params().size() == 1 &&
        local_relu4.params().front().type() ==
            joggle::Ty("tensor<f32, [4]>") &&
        local_relu4.returns() ==
            std::vector<joggle::Ty>{joggle::Ty("tensor<f32, [4]>")});
  CHECK(generated_fn.revision() == before_relu4 + 1);
  CHECK(generated_fn.verify(env));
  CHECK(generated_fn.find_fns("relu").size() == 2);
  const std::string specialized_text = joggle::print(generated_fn);
  CHECK(specialized_text.find("fn relu4(") != std::string::npos);
  CHECK(specialized_text.find("fn relu4<") == std::string::npos);
  const std::uint64_t rejected_specialization = generated_fn.revision();
  CHECK(!generated_fn.clone(
      env, relu_template, "bad_relu",
      std::vector<joggle::Ty>{joggle::Ty("f32")}));
  CHECK(generated_fn.revision() == rejected_specialization);
  generated_fn.clear_diags();
  CHECK(!generated_fn.clone(
      env, relu_template, "bad_relu",
      std::vector<joggle::Ty>{joggle::Ty("[4]"), joggle::Ty("[4]")}));
  CHECK(generated_fn.revision() == rejected_specialization);
  generated_fn.clear_diags();
  const joggle::Fn dynamic_relu = generated_fn.clone(
      env, relu_template, "dynamic_relu",
      std::vector<joggle::Ty>{joggle::Ty("f32"), joggle::Ty("[_, 4]")});
  CHECK(dynamic_relu && dynamic_relu.generics().empty() &&
        dynamic_relu.params().front().type() ==
            joggle::Ty("tensor<f32, [_, 4]>") &&
        generated_fn.revision() == rejected_specialization + 1);
  CHECK(generated_fn.verify(env));
  joggle::Mod generated_roundtrip;
  CHECK(joggle::parse(env, joggle::print(generated_fn), generated_roundtrip,
                      "generated-roundtrip.jog"));
  CHECK(generated_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(generated_fn, generated_roundtrip));

  joggle::Mod value_specialization;
  constexpr std::string_view value_specialization_source =
      "mod specialize\n"
      "use base\n"
      "fn repeat<N: int>(x: i32) -> i32 {\n"
      "  var y = x\n"
      "  for i in 0..N { y += 1 }\n"
      "  return y\n"
      "}\n"
      "fn shape<S: list<int>>(x: i32) -> i32 {\n"
      "  return use_shape(x, S)\n"
      "}\n"
      "fn choose<B: bool>(x: i32) -> i32 {\n"
      "  var y = x\n"
      "  if B { y += 1 }\n"
      "  return y\n"
      "}\n"
      "fn type_operand<T: Ty>(x: i32) -> i32 {\n"
      "  return opaque(x, T)\n"
      "}\n";
  CHECK(joggle::parse(env, value_specialization_source, value_specialization,
                      "specialize.jog"));
  CHECK(value_specialization.verify(env));
  const joggle::Fn repeat = value_specialization.find_fn("repeat");
  const joggle::Fn shape = value_specialization.find_fn("shape");
  const joggle::Fn choose_template = value_specialization.find_fn("choose");
  const joggle::Fn type_operand =
      value_specialization.find_fn("type_operand");
  CHECK(value_specialization.clone(
      env, repeat, "repeat4", std::vector<joggle::Ty>{joggle::Ty("4")}));
  CHECK(value_specialization.clone(
      env, shape, "shape23", std::vector<joggle::Ty>{joggle::Ty("[2, 3]")}));
  CHECK(value_specialization.clone(
      env, choose_template, "choose_true",
      std::vector<joggle::Ty>{joggle::Ty("true")}));
  const std::string before_type_operand = joggle::print(value_specialization);
  const std::uint64_t type_operand_revision = value_specialization.revision();
  CHECK(!value_specialization.clone(
      env, type_operand, "type_operand_f32",
      std::vector<joggle::Ty>{joggle::Ty("f32")}));
  CHECK(joggle::print(value_specialization) == before_type_operand);
  CHECK(value_specialization.revision() == type_operand_revision);
  value_specialization.clear_diags();
  CHECK(value_specialization.verify(env));
  const std::string value_specialization_text =
      joggle::print(value_specialization);
  CHECK(value_specialization_text.find("fn repeat4(x: i32)") !=
        std::string::npos);
  CHECK(value_specialization_text.find("for i in 0..4") != std::string::npos);
  CHECK(value_specialization_text.find("fn shape23(x: i32)") !=
        std::string::npos);
  CHECK(value_specialization_text.find("use_shape(x, [2, 3])") !=
        std::string::npos);
  CHECK(value_specialization_text.find("fn choose_true(x: i32)") !=
        std::string::npos);
  CHECK(value_specialization_text.find("if true") != std::string::npos);
  joggle::Mod value_specialization_roundtrip;
  CHECK(joggle::parse(env, value_specialization_text,
                      value_specialization_roundtrip,
                      "specialize-roundtrip.jog"));
  CHECK(value_specialization_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(value_specialization,
                                   value_specialization_roundtrip));

  joggle::Mod alpha_duplicate;
  constexpr std::string_view alpha_source =
      "mod alpha\n"
      "use tensor\n"
      "fn copy<A: Ty, D: list<int>>(\n"
      "  x: tensor<A, D>\n"
      ") -> tensor<A, D> { return x }\n";
  CHECK(joggle::parse(env, alpha_source, alpha_duplicate, "alpha.jog"));
  CHECK(alpha_duplicate.verify(env));
  const std::string alpha_text = joggle::print(alpha_duplicate);
  const std::uint64_t alpha_revision = alpha_duplicate.revision();
  CHECK(!alpha_duplicate.clone(env, relu_template, "copy"));
  CHECK(joggle::print(alpha_duplicate) == alpha_text);
  CHECK(alpha_duplicate.revision() == alpha_revision);
  CHECK(alpha_duplicate.uses() == std::vector<std::string>{"tensor"});
  alpha_duplicate.clear_diags();
  CHECK(alpha_duplicate.verify(env));

  joggle::Mod recursive_fn;
  constexpr std::string_view recursive_source =
      "mod recursive\n"
      "fn recur(n: int) -> int { return recur(n) }\n"
      "fn generic<N: int>(n: int) -> int { return generic<N>(n) }\n";
  CHECK(joggle::parse(env, recursive_source, recursive_fn, "recursive.jog"));
  CHECK(recursive_fn.verify(env));
  const joggle::Fn recur = recursive_fn.find_fn("recur");
  const joggle::Fn recur_copy = recursive_fn.clone(env, recur, "recur_copy");
  CHECK(recur_copy && recursive_fn.verify(env));
  const joggle::Fn generic_recur = recursive_fn.find_fn("generic");
  const joggle::Fn generic4 = recursive_fn.clone(
      env, generic_recur, "generic4",
      std::vector<joggle::Ty>{joggle::Ty("4")});
  CHECK(generic4 && generic4.generics().empty() && recursive_fn.verify(env));
  const std::string recursive_text = joggle::print(recursive_fn);
  CHECK(recursive_text.find("recursive.recur_copy(n)") != std::string::npos);
  CHECK(recursive_text.find("fn generic4(n: int)") != std::string::npos);
  CHECK(recursive_text.find("recursive.generic4(n)") != std::string::npos);
  CHECK(recursive_text.find("recursive.generic4<") == std::string::npos);
  joggle::Mod recursive_roundtrip;
  CHECK(joggle::parse(env, recursive_text, recursive_roundtrip,
                      "recursive-roundtrip.jog"));
  CHECK(recursive_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(recursive_fn, recursive_roundtrip));

  joggle::Mod function_edit;
  constexpr std::string_view function_edit_source =
      "mod function_edit\n"
      "fn helper<T: Ty>(x: T) -> T { return helper<T>(x) }\n"
      "fn renamed(x: i32, y: i32) -> i32 { return x }\n"
      "fn duplicate<U: Ty>(x: U) -> U { return x }\n"
      "fn main(x: i32) -> i32 { return helper<i32>(x) }\n";
  CHECK(joggle::parse(env, function_edit_source, function_edit,
                      "function-edit.jog"));
  CHECK(function_edit.verify(env));
  joggle::Fn helper = function_edit.find_fn("helper");
  joggle::Fn edit_main = function_edit.find_fn("main");
  const joggle::Val helper_param = helper.params().front();
  const joggle::Op helper_call = helper.body().ops().front();
  const joggle::Op main_call = edit_main.body().ops().front();
  const std::string before_function_rename = joggle::print(function_edit);
  const std::uint64_t before_function_rename_revision =
      function_edit.revision();
  CHECK(!function_edit.rename(env, helper, "duplicate"));
  CHECK(joggle::print(function_edit) == before_function_rename);
  CHECK(function_edit.revision() == before_function_rename_revision);
  function_edit.clear_diags();
  CHECK(function_edit.rename(env, helper, "renamed"));
  CHECK(function_edit.revision() == before_function_rename_revision + 1);
  CHECK(helper.name() == "renamed" && !function_edit.find_fn("helper"));
  CHECK(function_edit.find_fns("renamed").size() == 2);
  CHECK(function_edit.verify(env));
  const std::string renamed_function_text = joggle::print(function_edit);
  CHECK(renamed_function_text.find("renamed<T>(x)") !=
        std::string::npos);
  CHECK(renamed_function_text.find("renamed<i32>(x)") !=
        std::string::npos);
  const std::uint64_t before_live_erase = function_edit.revision();
  CHECK(!function_edit.erase(env, helper));
  CHECK(function_edit.revision() == before_live_erase);
  function_edit.clear_diags();
  CHECK(function_edit.erase(env, edit_main));
  CHECK(!edit_main && !main_call);
  CHECK(function_edit.erase(env, helper));
  CHECK(!helper && !helper_param && !helper_call);
  CHECK(function_edit.find_fns("renamed").size() == 1);
  CHECK(function_edit.verify(env));
  joggle::Mod function_edit_roundtrip;
  CHECK(joggle::parse(env, joggle::print(function_edit),
                      function_edit_roundtrip, "function-edit-roundtrip.jog"));
  CHECK(function_edit_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(function_edit, function_edit_roundtrip));

  joggle::Mod deferred_caller;
  CHECK(joggle::parse(env,
                      "mod deferred.caller\n"
                      "fn choose(x: i32) -> i32 { return x }\n"
                      "fn choose(x: f32) -> f32 { return x }\n"
                      "fn apply<T: Ty>(x: T) -> T { return choose(x) }\n"
                      "fn exact(x: f32) -> f32 { return choose(x) }\n",
                      deferred_caller, "deferred-caller.jog"));
  CHECK(deferred_caller.verify(env));
  joggle::Fn integer_choice;
  for (const joggle::Fn choice : deferred_caller.find_fns("choose"))
    if (choice.params().front().type() == joggle::Ty("i32"))
      integer_choice = choice;
  CHECK(integer_choice);
  const std::string deferred_before = joggle::print(deferred_caller);
  const std::uint64_t deferred_revision = deferred_caller.revision();
  CHECK(!deferred_caller.rename(env, integer_choice, "integer_choice"));
  CHECK(integer_choice.name() == "choose" &&
        joggle::print(deferred_caller) == deferred_before);
  CHECK(deferred_caller.revision() == deferred_revision);
  CHECK(!deferred_caller.diags().empty() &&
        deferred_caller.diags().back().message.find("deferred caller") !=
            std::string::npos);
  deferred_caller.clear_diags();
  CHECK(!deferred_caller.erase(env, integer_choice));
  CHECK(integer_choice && joggle::print(deferred_caller) == deferred_before);
  CHECK(deferred_caller.revision() == deferred_revision);
  CHECK(!deferred_caller.diags().empty() &&
        deferred_caller.diags().back().message.find("deferred caller") !=
            std::string::npos);
  deferred_caller.clear_diags();
  CHECK(deferred_caller.erase(env, deferred_caller.find_fn("apply")));
  CHECK(deferred_caller.erase(env, integer_choice));
  CHECK(!integer_choice && deferred_caller.verify(env));

  joggle::Mod nested_return;
  constexpr std::string_view nested_return_source =
      "mod nested_return\n"
      "fn bad(x: i32) -> i32 {\n"
      "  if true { return true }\n"
      "  return x\n"
      "}\n";
  CHECK(joggle::parse(env, nested_return_source, nested_return,
                      "nested-return.jog"));
  CHECK(!nested_return.verify(env));
  CHECK(std::any_of(nested_return.diags().begin(), nested_return.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.message.find("return type 'bool'") !=
                             std::string::npos;
                    }));

  joggle::Mod return_edit;
  CHECK(joggle::parse(env,
                      "mod return_edit\n"
                      "fn identity(x: i32) -> i32 { return x }\n",
                      return_edit, "return-edit.jog"));
  CHECK(return_edit.verify(env));
  const joggle::Fn identity = return_edit.find_fn("identity");
  const std::uint64_t before_return_edit = return_edit.revision();
  CHECK(return_edit.type(identity.params().front(), joggle::Ty("i64")));
  CHECK(return_edit.returns(
      identity, std::vector<joggle::Ty>{joggle::Ty("i64")}));
  CHECK(return_edit.revision() == before_return_edit + 2);
  CHECK(identity.returns() ==
        std::vector<joggle::Ty>{joggle::Ty("i64")});
  CHECK(return_edit.verify(env));
  const std::uint64_t stable_return_edit = return_edit.revision();
  CHECK(return_edit.returns(
      identity, std::vector<joggle::Ty>{joggle::Ty("i64")}));
  CHECK(return_edit.revision() == stable_return_edit);
  CHECK(!return_edit.returns(identity,
                             std::vector<joggle::Ty>{joggle::Ty{}}));
  CHECK(return_edit.revision() == stable_return_edit);
  return_edit.clear_diags();

  joggle::Mod generic_edit;
  constexpr std::string_view generic_edit_source =
      "mod generic_edit\n"
      "fn helper<N: int>(x: i32) -> i32 { return x }\n"
      "fn main(x: i32) -> i32 { return helper<4>(x) }\n";
  CHECK(joggle::parse(env, generic_edit_source, generic_edit,
                      "generic-edit.jog"));
  CHECK(generic_edit.verify(env));
  const joggle::Op generic_call =
      generic_edit.find_fn("main").body().ops().front();
  CHECK(generic_call.generics() ==
        std::vector<joggle::Ty>{joggle::Ty("4")});
  const std::uint64_t before_generic_edit = generic_edit.revision();
  CHECK(generic_edit.generics(
      env, generic_call, std::vector<joggle::Ty>{joggle::Ty("6")}));
  CHECK(generic_call.callee() == "helper<6>" &&
        generic_call.generics() ==
            std::vector<joggle::Ty>{joggle::Ty("6")});
  CHECK(generic_edit.revision() == before_generic_edit + 1);
  CHECK(generic_edit.verify(env));
  const std::uint64_t rejected_generic_edit = generic_edit.revision();
  CHECK(!generic_edit.generics(
      env, generic_call, std::vector<joggle::Ty>{joggle::Ty("i32")}));
  CHECK(generic_call.callee() == "helper<6>" &&
        generic_edit.revision() == rejected_generic_edit);
  generic_edit.clear_diags();

  joggle::Mod derived_generic;
  constexpr std::string_view derived_generic_source =
      "mod derived_generic\n"
      "fn vec<N: int>() -> Ty;\n"
      "fn vec<N: int>(x: i32) -> vec<N>;\n"
      "fn build<S: list<int>>(x: i32) -> vec<len<S>> {\n"
      "  return vec<len<S>>(x)\n"
      "}\n"
      "fn main(x: i32) -> vec<3> { return build<[2, 3, 5]>(x) }\n";
  CHECK(joggle::parse(env, derived_generic_source, derived_generic,
                      "derived-generic.jog"));
  CHECK(derived_generic.verify(env));
  const joggle::Fn build = derived_generic.find_fn("build");
  const joggle::Fn build3 = derived_generic.clone(
      env, build, "build3", std::vector<joggle::Ty>{joggle::Ty("[2, 3, 5]")});
  CHECK(build3 && build3.generics().empty() &&
        build3.returns() == std::vector<joggle::Ty>{joggle::Ty("vec<3>")});
  CHECK(build3.body().ops().front().callee() == "derived_generic.vec<3>");
  CHECK(derived_generic.verify(env));
  joggle::Mod derived_generic_roundtrip;
  CHECK(joggle::parse(env, joggle::print(derived_generic),
                      derived_generic_roundtrip,
                      "derived-generic-roundtrip.jog"));
  CHECK(derived_generic_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(derived_generic,
                                   derived_generic_roundtrip));

  joggle::Op tensor_add;
  joggle::Op relu_call;
  for (joggle::Op op : network.find_fn("stage").ops())
    if (op.callee() == "operator +")
      tensor_add = op;
    else if (op.callee() == "nn.relu")
      relu_call = op;
  CHECK(tensor_add && env.resolve(network, tensor_add).module() == "tensor");
  const joggle::Fn relu = env.find_fn("nn.relu");
  CHECK(relu && !relu.external());
  const std::vector<joggle::Ty> matched_generics =
      env.match(relu_call, relu);
  CHECK((matched_generics ==
         std::vector<joggle::Ty>{joggle::Ty("f32"), joggle::Ty("[4]")}));
  bool relu_loop = false;
  bool relu_branch = false;
  for (joggle::Op op : relu.ops()) {
    relu_loop = relu_loop || op.kind() == joggle::Op::Kind::loop;
    relu_branch = relu_branch || op.kind() == joggle::Op::Kind::branch;
  }
  CHECK(relu_loop && relu_branch);
  joggle::Mod network_roundtrip;
  CHECK(joggle::parse(env, joggle::print(network), network_roundtrip,
                      "network-roundtrip.jog"));
  CHECK(network_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(network, network_roundtrip));
  joggle::Mod network_cpp;
  CHECK(joggle::parse(env, network_source, network_cpp, "network-cpp.jog"));
  for (joggle::Op op : network_cpp.ops()) {
    if (op.kind() != joggle::Op::Kind::call)
      continue;
    if (op.callee() != "operator +" && op.callee() != "nn.relu")
      continue;
    const joggle::Fn callee = env.resolve(network_cpp, op);
    CHECK(callee && env.expand(network_cpp, op, callee));
  }
  CHECK(network_cpp.verify(env));
  CHECK(env.load("script"));
  for (joggle::Fn fn : env.fns("script"))
    CHECK(fn.name() != "hidden" && !fn.local());
  bool nested_path = false;
  const joggle::Fn expanded_stage = network_cpp.find_fn("stage");
  for (const joggle::Op op : expanded_stage.ops()) {
    const std::vector<std::size_t> path = network_cpp.path(op);
    CHECK(!path.empty());
    CHECK(network_cpp.at(expanded_stage, path) == op);
    nested_path = nested_path || path.size() > 1;
  }
  CHECK(nested_path);
  joggle::Mod path_copy;
  CHECK(joggle::parse(env, joggle::print(network_cpp), path_copy,
                      "network-path-copy.jog"));
  CHECK(path_copy.verify(env));
  const joggle::Fn copied_stage = path_copy.find_fn("stage");
  for (const joggle::Op op : expanded_stage.ops()) {
    const std::vector<std::size_t> path = network_cpp.path(op);
    const joggle::Op copied = path_copy.at(copied_stage, path);
    CHECK(copied && copied.kind() == op.kind() && copied.form() == op.form());
  }
  const std::array<joggle::Attr, 1> stage_arg{joggle::Attr("stage")};
  joggle::Attr path_roundtrip;
  CHECK(joggle::query(env, "script.path_roundtrip", network_cpp,
                      path_roundtrip, stage_arg));
  CHECK(path_roundtrip.boolean() && *path_roundtrip.boolean());
  std::int64_t call_count = 0;
  std::int64_t control_count = 0;
  for (joggle::Op op : network_cpp.ops()) {
    call_count += op.kind() == joggle::Op::Kind::call;
    control_count += op.kind() == joggle::Op::Kind::loop ||
                     op.kind() == joggle::Op::Kind::branch;
  }
  joggle::Attr kind_counts;
  CHECK(joggle::query(env, "script.op_kind_counts", network_cpp,
                      kind_counts));
  const auto* kind_count_list = kind_counts.list();
  CHECK(kind_count_list && kind_count_list->size() == 3);
  CHECK(kind_count_list->at(0).integer() &&
        *kind_count_list->at(0).integer() == call_count);
  CHECK(kind_count_list->at(1).integer() &&
        *kind_count_list->at(1).integer() == control_count);
  CHECK(kind_count_list->at(2).integer() &&
        *kind_count_list->at(2).integer() == 0);
  joggle::Mod unused_subject;
  CHECK(joggle::parse(
      env,
      "mod unused.subject\n"
      "fn graph(x: int) -> int {\n"
      "  let one: int = 1\n"
      "  let used: int = x + one\n"
      "  let dead: int = 2\n"
      "  return used\n"
      "}\n",
      unused_subject, "unused-subject.jog"));
  CHECK(unused_subject.verify(env));
  const std::array<joggle::Attr, 1> unused_args{joggle::Attr("graph")};
  joggle::Attr unused_counts;
  CHECK(joggle::query(env, "script.unused_probe", unused_subject,
                      unused_counts, unused_args));
  const auto* unused_count_list = unused_counts.list();
  CHECK(unused_count_list && unused_count_list->size() == 3);
  CHECK(unused_count_list->at(0).integer() &&
        *unused_count_list->at(0).integer() == 4);
  CHECK(unused_count_list->at(1).integer() &&
        *unused_count_list->at(1).integer() == 3);
  CHECK(unused_count_list->at(2).integer() &&
        *unused_count_list->at(2).integer() == 1);
  joggle::Attr call_filter_counts;
  CHECK(joggle::query(env, "script.call_filter_counts", unused_subject,
                      call_filter_counts, unused_args));
  const auto* call_filter_count_list = call_filter_counts.list();
  CHECK(call_filter_count_list && call_filter_count_list->size() == 3);
  CHECK(call_filter_count_list->at(0).integer() &&
        *call_filter_count_list->at(0).integer() == 1);
  CHECK(call_filter_count_list->at(1).integer() &&
        *call_filter_count_list->at(1).integer() == 1);
  CHECK(call_filter_count_list->at(2).integer() &&
        *call_filter_count_list->at(2).integer() == 0);
  CHECK(env.declared("script.hidden"));
  CHECK(!env.declared("script.missing"));
  CHECK(!env.find_fn("script.hidden"));
  constexpr std::string_view reactive_schedule_source =
      "mod schedule.subject\n"
      "fn left() -> int {\n"
      "  let left_value: int = 1\n"
      "  return left_value\n"
      "}\n"
      "fn right() -> int {\n"
      "  let right_value: int = 2\n"
      "  return right_value\n"
      "}\n";
  joggle::Mod reactive_scheduled;
  CHECK(joggle::parse(env, reactive_schedule_source, reactive_scheduled,
                      "schedule.jog"));
  CHECK(reactive_scheduled.verify(env));
  joggle::ReactiveSchedule schedule(
      {"script.schedule_mark_a", "script.schedule_mark_b"});
  const std::array<joggle::Attr, 1> left_args{joggle::Attr("left")};
  joggle::Attr schedule_report;
  CHECK(schedule.run(env, reactive_scheduled, left_args, &schedule_report));
  CHECK(flag(schedule_report, "succeeded") && flag(schedule_report, "cold") &&
        number(schedule_report, "executed_stages") == 2 &&
        number(schedule_report, "reused_stages") == 0 &&
        field(schedule_report, "select_ns").integer().has_value() &&
        field(schedule_report, "total_ns").integer().has_value() &&
        number(schedule_report, "total_ns") >=
            number(schedule_report, "select_ns") &&
        field(field(schedule_report, "execution"), "steps").list()->size() == 2);
  CHECK(number(item(schedule_report, "stages", 0), "observed_functions") == 1 &&
        number(item(schedule_report, "stages", 1), "observed_functions") == 1 &&
        flag(item(schedule_report, "stages", 0), "observed_structure") &&
        !flag(item(schedule_report, "stages", 0), "observed_whole_mod"));
  CHECK(schedule.run(env, reactive_scheduled, left_args, &schedule_report));
  CHECK(flag(schedule_report, "succeeded") && !flag(schedule_report, "cold") &&
        number(schedule_report, "executed_stages") == 0 &&
        number(schedule_report, "reused_stages") == 2 &&
        field(field(schedule_report, "execution"), "steps").list()->empty());
  const auto replace_constant = [&](std::string_view function,
                                    std::int64_t value) {
    for (joggle::Op op : reactive_scheduled.find_fn(function).ops())
      if (op.kind() == joggle::Op::Kind::constant)
        return reactive_scheduled.replace(op, joggle::Attr(value));
    return false;
  };
  CHECK(replace_constant("right", 20));
  CHECK(schedule.run(env, reactive_scheduled, left_args, &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 0 &&
        number(schedule_report, "reused_stages") == 2);
  CHECK(replace_constant("left", 10));
  CHECK(schedule.run(env, reactive_scheduled, left_args, &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 2 &&
        number(schedule_report, "reused_stages") == 0 &&
        string_field(item(schedule_report, "stages", 0), "miss") ==
            "function_revision" &&
        string_field(item(schedule_report, "stages", 1), "miss") ==
            "function_revision");
  const std::array<joggle::Attr, 1> right_args{joggle::Attr("right")};
  CHECK(schedule.run(env, reactive_scheduled, right_args, &schedule_report));
  CHECK(flag(schedule_report, "cold") &&
        number(schedule_report, "executed_stages") == 2 &&
        string_field(item(schedule_report, "stages", 0), "miss") == "arguments");
  constexpr std::string_view cone_schedule_source =
      "mod schedule.cone\n"
      "fn graph() -> int {\n"
      "  let left_root: int = 1\n"
      "  let right_root: int = 2\n"
      "  let left_result: int = pure(left_root)\n"
      "  let right_result: int = pure(right_root)\n"
      "  return left_result\n"
      "}\n";
  joggle::Mod cone_scheduled;
  CHECK(joggle::parse(env, cone_schedule_source, cone_scheduled,
                      "schedule-cone.jog"));
  CHECK(cone_scheduled.verify(env));
  joggle::ReactiveSchedule cone_schedule(
      {"script.schedule_cone_a", "script.schedule_cone_b"});
  const std::array<joggle::Attr, 2> cone_args{
      joggle::Attr("graph"), joggle::Attr("left_root")};
  CHECK(cone_schedule.run(env, cone_scheduled, cone_args, &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 2 &&
        number(item(schedule_report, "stages", 0), "observed_functions") == 0 &&
        number(item(schedule_report, "stages", 0), "observed_collections") == 1 &&
        number(item(schedule_report, "stages", 0), "observed_operations") != 0 &&
        number(item(schedule_report, "stages", 0), "observed_values") != 0);
  const auto replace_named_constant = [&](std::string_view name,
                                           std::int64_t value) {
    for (joggle::Val candidate : cone_scheduled.find_fn("graph").vals())
      if (candidate.name() == name && candidate.is_const())
        return cone_scheduled.replace(candidate.def(), joggle::Attr(value));
    return false;
  };
  CHECK(replace_named_constant("right_root", 20));
  CHECK(cone_schedule.run(env, cone_scheduled, cone_args, &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 0 &&
        number(schedule_report, "reused_stages") == 2);
  CHECK(replace_named_constant("left_root", 10));
  CHECK(cone_schedule.run(env, cone_scheduled, cone_args, &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 2 &&
        string_field(item(schedule_report, "stages", 0), "miss") ==
            "operation_revision" &&
        string_field(item(schedule_report, "stages", 1), "miss") ==
            "operation_revision");
  joggle::Mod constant_scheduled;
  CHECK(joggle::parse(env, cone_schedule_source, constant_scheduled,
                      "schedule-constant.jog"));
  CHECK(constant_scheduled.verify(env));
  const std::vector<joggle::Op> constant_operations =
      constant_scheduled.find_fn("graph").ops();
  std::size_t left_constant_index = constant_operations.size();
  joggle::Op scheduled_left_constant;
  joggle::Op scheduled_right_constant;
  for (std::size_t index = 0; index < constant_operations.size(); ++index) {
    const joggle::Op operation = constant_operations[index];
    if (operation.kind() != joggle::Op::Kind::constant ||
        operation.outs().size() != 1)
      continue;
    if (operation.outs().front().name() == "left_root") {
      scheduled_left_constant = operation;
      left_constant_index = index;
    } else if (operation.outs().front().name() == "right_root") {
      scheduled_right_constant = operation;
    }
  }
  CHECK(scheduled_left_constant && scheduled_right_constant &&
        left_constant_index < constant_operations.size());
  joggle::ReactiveSchedule constant_schedule({"script.schedule_constant"});
  const std::array<joggle::Attr, 2> constant_args{
      joggle::Attr("graph"),
      joggle::Attr(static_cast<std::int64_t>(left_constant_index))};
  CHECK(constant_schedule.run(env, constant_scheduled, constant_args,
                              &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 1 &&
        number(item(schedule_report, "stages", 0), "observed_operations") == 1);
  CHECK(constant_scheduled.replace(scheduled_right_constant,
                                   joggle::Attr(std::int64_t{20})));
  CHECK(constant_schedule.run(env, constant_scheduled, constant_args,
                              &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 0 &&
        number(schedule_report, "reused_stages") == 1);
  CHECK(constant_scheduled.replace(scheduled_left_constant,
                                   joggle::Attr(std::int64_t{10})));
  CHECK(constant_schedule.run(env, constant_scheduled, constant_args,
                              &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 1 &&
        string_field(item(schedule_report, "stages", 0), "miss") ==
            "operation_revision");
  joggle::Mod range_scheduled;
  CHECK(joggle::parse(env, cone_schedule_source, range_scheduled,
                      "schedule-range.jog"));
  CHECK(range_scheduled.verify(env));
  const std::vector<joggle::Op> range_operations =
      range_scheduled.find_fn("graph").body().ops();
  std::size_t range_left_index = range_operations.size();
  joggle::Op range_left;
  joggle::Op range_right;
  for (std::size_t index = 0; index < range_operations.size(); ++index) {
    const joggle::Op operation = range_operations[index];
    if (operation.kind() != joggle::Op::Kind::constant ||
        operation.outs().size() != 1)
      continue;
    if (operation.outs().front().name() == "left_root") {
      range_left = operation;
      range_left_index = index;
    } else if (operation.outs().front().name() == "right_root") {
      range_right = operation;
    }
  }
  CHECK(range_left && range_right && range_left_index < range_operations.size());
  joggle::ReactiveSchedule range_schedule({"script.schedule_range"});
  const std::array<joggle::Attr, 2> range_schedule_args{
      joggle::Attr("graph"),
      joggle::Attr(static_cast<std::int64_t>(range_left_index))};
  CHECK(range_schedule.run(env, range_scheduled, range_schedule_args,
                           &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 1 &&
        number(item(schedule_report, "stages", 0), "observed_functions") == 1 &&
        number(item(schedule_report, "stages", 0), "observed_collections") == 0 &&
        number(item(schedule_report, "stages", 0), "observed_operations") == 1);
  CHECK(range_scheduled.replace(range_right, joggle::Attr(std::int64_t{20})));
  CHECK(range_schedule.run(env, range_scheduled, range_schedule_args,
                           &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 0 &&
        number(schedule_report, "reused_stages") == 1);
  CHECK(range_scheduled.replace(range_left, joggle::Attr(std::int64_t{10})));
  CHECK(range_schedule.run(env, range_scheduled, range_schedule_args,
                           &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 1 &&
        string_field(item(schedule_report, "stages", 0), "miss") ==
            "operation_revision");
  CHECK(range_scheduled.type(range_right.outs().front(), joggle::Ty("_")));
  CHECK(range_schedule.run(env, range_scheduled, range_schedule_args,
                           &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 0 &&
        number(schedule_report, "reused_stages") == 1);
  CHECK(range_scheduled.type(range_left.outs().front(), joggle::Ty("_")));
  CHECK(range_schedule.run(env, range_scheduled, range_schedule_args,
                           &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 1 &&
        string_field(item(schedule_report, "stages", 0), "miss") ==
            "value_revision");
  joggle::ReactiveSchedule propagated_schedule(
      {"script.schedule_forward", "script.schedule_consume"});
  const std::array<joggle::Attr, 2> edge_args{joggle::Attr("left"),
                                              joggle::Attr("right")};
  CHECK(propagated_schedule.run(env, reactive_scheduled, edge_args,
                                &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 2 &&
        number(item(schedule_report, "stages", 0), "changed_functions") == 1 &&
        number(item(schedule_report, "stages", 1), "observed_functions") == 1);
  CHECK(replace_constant("left", 11));
  CHECK(propagated_schedule.run(env, reactive_scheduled, edge_args,
                                &schedule_report));
  CHECK(number(schedule_report, "executed_stages") == 2 &&
        string_field(item(schedule_report, "stages", 0), "miss") ==
            "function_revision" &&
        string_field(item(schedule_report, "stages", 1), "miss") == "upstream");
  const std::string before_failed_schedule = joggle::print(reactive_scheduled);
  joggle::ReactiveSchedule failing_schedule(
      {"script.schedule_mark_a", "script.schedule_fail"});
  CHECK(!failing_schedule.run(env, reactive_scheduled, left_args,
                              &schedule_report));
  CHECK(joggle::print(reactive_scheduled) == before_failed_schedule);
  env.clear_diags();
  constexpr std::string_view local_client_source =
      "mod local.client\n"
      "use script\n"
      "local fn hidden() -> bool {\n"
      "  return true\n"
      "}\n"
      "fn main() -> bool {\n"
      "  return hidden()\n"
      "}\n";
  joggle::Mod local_client;
  CHECK(joggle::parse(env, local_client_source, local_client,
                      "local-client.jog"));
  CHECK(local_client.verify(env));
  CHECK(local_client.find_fn("hidden").local());
  CHECK(env.resolve(local_client, "hidden") == local_client.find_fn("hidden"));
  CHECK(joggle::print(local_client).find("local fn hidden") !=
        std::string::npos);
  CHECK(!env.resolve(local_client, "script.hidden"));
  CHECK(env.resolve(local_client, "script.local_probe"));
  CHECK(env.declared("script.hidden_type"));
  CHECK(!env.find_fn("script.hidden_type"));
  CHECK(rejects_type(
      env,
      "mod local.type.client\nuse script\n"
      "fn bad(x: script.hidden_type<i32>) -> script.hidden_type<i32> { "
      "return x }\n",
      "names a local function in another module"));
  joggle::Attr local_result;
  CHECK(joggle::query(env, "script.local_probe", network_cpp, local_result));
  CHECK(local_result.boolean() && *local_result.boolean());
  CHECK(joggle::query(env, "script.own_local_visible", network_cpp,
                      local_result));
  CHECK(local_result.boolean() && *local_result.boolean());
  CHECK(joggle::query(env, "script.imported_locals_hidden", network_cpp,
                      local_result));
  CHECK(local_result.boolean() && *local_result.boolean());

  joggle::Mod private_call;
  CHECK(joggle::parse(env,
                      "mod private_call\n"
                      "use script\n"
                      "fn main(m: Mod) -> bool { return script.hidden(m) }\n",
                      private_call, "private-call.jog"));
  CHECK(!private_call.verify(env));
  CHECK(std::any_of(private_call.diags().begin(), private_call.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.message.find("local function in another "
                                               "module") !=
                             std::string::npos;
                    }));

  joggle::Mod open_transport;
  CHECK(joggle::parse(
      env,
      "mod open_transport\n"
      "use onnx\n"
      "fn main(x: opaque) -> opaque { return onnx.NotDeclared(x) }\n",
      open_transport, "open-transport.jog"));
  CHECK(open_transport.verify(env));
  joggle::Mod scripted_generic_edit;
  CHECK(joggle::parse(env, generic_edit_source, scripted_generic_edit,
                      "scripted-generic-edit.jog"));
  CHECK(joggle::run(env, "script.rewrite_call_generics",
                    scripted_generic_edit));
  CHECK(scripted_generic_edit.verify(env));
  const joggle::Op scripted_generic_call =
      scripted_generic_edit.find_fn("main").body().ops().front();
  CHECK(scripted_generic_call.callee() == "helper<5>" &&
        scripted_generic_call.generics() ==
            std::vector<joggle::Ty>{joggle::Ty("5")});
  joggle::Mod matched_fn;
  CHECK(joggle::parse(env, network_source, matched_fn, "matched-fn.jog"));
  joggle::Attr matched_report;
  joggle::Attr matched_timing;
  CHECK(joggle::run(env, "script.clone_matched", matched_fn, {},
                    &matched_report, &matched_timing));
  CHECK(flag(matched_timing, "succeeded") &&
        field(matched_timing, "steps").list()->size() == 1 &&
        flag(item(matched_timing, "steps", 0), "succeeded") &&
        string_field(item(matched_timing, "steps", 0), "function") ==
            "script.clone_matched" &&
        number(item(matched_timing, "steps", 0), "after") >=
            number(item(matched_timing, "steps", 0), "before"));
  CHECK(matched_fn.verify(env));
  const joggle::Fn matched_relu = matched_fn.find_fn("relu_matched");
  CHECK(matched_relu && matched_relu.generics().empty());
  CHECK(matched_relu.params().front().type() ==
        joggle::Ty("tensor<f32, [4]>") &&
        matched_relu.returns().front() == joggle::Ty("tensor<f32, [4]>"));
  bool retargeted_relu = false;
  for (const joggle::Op op : matched_fn.find_fn("stage").ops())
    retargeted_relu =
        op.callee() == "network.relu_matched" || retargeted_relu;
  CHECK(retargeted_relu);
  joggle::Mod failed_matched_fn;
  CHECK(joggle::parse(env, network_source, failed_matched_fn,
                      "failed-matched-fn.jog"));
  const std::string before_failed_match = joggle::print(failed_matched_fn);
  const std::uint64_t before_failed_match_revision =
      failed_matched_fn.revision();
  CHECK(!joggle::run(env, "script.clone_then_fail", failed_matched_fn));
  CHECK(joggle::print(failed_matched_fn) == before_failed_match);
  CHECK(failed_matched_fn.revision() == before_failed_match_revision);
  CHECK(!failed_matched_fn.find_fn("orphan"));
  joggle::Mod scripted_function_edit;
  constexpr std::string_view scripted_function_edit_source =
      "mod scripted_function_edit\n"
      "fn helper(x: i32) -> i32 { return x }\n"
      "fn main(x: i32) -> i32 { return helper(x) }\n";
  CHECK(joggle::parse(env, scripted_function_edit_source,
                      scripted_function_edit, "scripted-function-edit.jog"));
  CHECK(joggle::run(env, "script.rename_helper", scripted_function_edit));
  CHECK(scripted_function_edit.verify(env));
  CHECK(scripted_function_edit.find_fn("renamed"));
  CHECK(!scripted_function_edit.find_fn("helper"));
  CHECK(joggle::print(scripted_function_edit)
            .find("renamed(x)") != std::string::npos);
  CHECK(joggle::run(env, "script.erase_main", scripted_function_edit));
  CHECK(joggle::run(env, "script.erase_renamed", scripted_function_edit));
  CHECK(scripted_function_edit.fns().empty());
  CHECK(scripted_function_edit.verify(env));
  joggle::Mod scripted_return_edit;
  CHECK(joggle::parse(env,
                      "mod scripted_return_edit\n"
                      "fn identity(x: i32) -> i32 { return x }\n",
                      scripted_return_edit, "scripted-return-edit.jog"));
  CHECK(joggle::run(env, "script.retype_identity", scripted_return_edit));
  CHECK(scripted_return_edit.verify(env));
  CHECK(scripted_return_edit.find_fn("identity").params().front().type() ==
        joggle::Ty("i64"));
  CHECK(scripted_return_edit.find_fn("identity").returns() ==
        std::vector<joggle::Ty>{joggle::Ty("i64")});
  const std::string before_bad_return = joggle::print(scripted_return_edit);
  const std::uint64_t before_bad_return_revision =
      scripted_return_edit.revision();
  CHECK(!joggle::run(env, "script.break_identity_return",
                     scripted_return_edit));
  CHECK(joggle::print(scripted_return_edit) == before_bad_return);
  CHECK(scripted_return_edit.revision() == before_bad_return_revision);
  joggle::Mod scripted_fn;
  constexpr std::string_view scripted_fn_source =
      "mod scripted.generated\n"
      "use tensor\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  return scripted.generated.relu(x)\n"
      "}\n";
  CHECK(joggle::parse(env, scripted_fn_source, scripted_fn,
                      "scripted-generated.jog"));
  joggle::Attr fn_clone_report;
  CHECK(joggle::run(env, "script.clone_relu", scripted_fn, fn_clone_report));
  CHECK(scripted_fn.verify(env));
  CHECK((scripted_fn.uses() == std::vector<std::string>{"tensor", "nn"}));
  CHECK(scripted_fn.find_fns("relu").size() == 1);
  const joggle::Attr::Dict* fn_clone_summary = fn_clone_report.dict();
  CHECK(fn_clone_summary);
  const joggle::Attr::List* fn_clone_steps =
      fn_clone_summary->at("steps").list();
  CHECK(fn_clone_steps && fn_clone_steps->size() == 1);
  const joggle::Attr::Dict* fn_clone_event = fn_clone_steps->front().dict();
  CHECK(fn_clone_event && fn_clone_event->at("kind").string() == "clone");
  CHECK(fn_clone_event->at("source").string() == "nn.relu");
  CHECK(fn_clone_event->at("copy").string() == "scripted.generated.relu");
  CHECK(fn_clone_event->at("edits").integer() == 1);
  CHECK(fn_clone_event->at("params").list() &&
        fn_clone_event->at("params").list()->size() == 1);
  CHECK(fn_clone_event->at("returns").list() &&
        fn_clone_event->at("returns").list()->size() == 1);
  CHECK(fn_clone_event->at("generics").list() &&
        fn_clone_event->at("generics").list()->empty());
  joggle::Attr fn_specialize_report;
  CHECK(joggle::run(env, "script.clone_relu4", scripted_fn,
                    fn_specialize_report));
  CHECK(scripted_fn.verify(env));
  const joggle::Fn scripted_relu4 = scripted_fn.find_fn("relu4");
  CHECK(scripted_relu4 && scripted_relu4.generics().empty());
  CHECK(scripted_relu4.params().front().type() ==
        joggle::Ty("tensor<f32, [4]>") &&
        scripted_relu4.returns().front() ==
            joggle::Ty("tensor<f32, [4]>"));
  const joggle::Attr::Dict* fn_specialize_summary =
      fn_specialize_report.dict();
  CHECK(fn_specialize_summary);
  const joggle::Attr::List* fn_specialize_steps =
      fn_specialize_summary->at("steps").list();
  CHECK(fn_specialize_steps && fn_specialize_steps->size() == 1);
  const joggle::Attr::Dict* fn_specialize_event =
      fn_specialize_steps->front().dict();
  CHECK(fn_specialize_event &&
        fn_specialize_event->at("kind").string() == "clone");
  CHECK(fn_specialize_event->at("copy").string() ==
        "scripted.generated.relu4");
  const joggle::Attr::List* specialized_params =
      fn_specialize_event->at("params").list();
  CHECK(specialized_params && specialized_params->size() == 1 &&
        specialized_params->front().string() == "tensor<f32, [4]>");
  const joggle::Attr::List* specialized_generics =
      fn_specialize_event->at("generics").list();
  CHECK(specialized_generics && specialized_generics->size() == 2 &&
        (*specialized_generics)[0].string() == "f32" &&
        (*specialized_generics)[1].string() == "[4]");
  joggle::Mod scripted_fn_roundtrip;
  CHECK(joggle::parse(env, joggle::print(scripted_fn), scripted_fn_roundtrip,
                      "scripted-generated-roundtrip.jog"));
  CHECK(scripted_fn_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(scripted_fn, scripted_fn_roundtrip));
  constexpr std::string_view relation_source =
      "mod relation\n"
      "[mark: [\"fn\", \"entry\"]]\n"
      "fn main([mark: \"param\"] x: i32) -> i32 {\n"
      "  [mark: \"op\"]\n"
      "  let [mark: \"val\"] y: i32 = opaque(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod relation;
  CHECK(joggle::parse(env, relation_source, relation, "relation.jog"));
  CHECK(relation.verify(env));
  CHECK(joggle::run(env, "script.where_probe", relation));
  CHECK(joggle::run(env, "script.apply_rules", relation));
  CHECK(relation.verify(env));
  bool matched = false;
  for (joggle::Op op : relation.ops()) {
    if (op.callee() == "opaque") {
      const joggle::Attr* value = op.meta("matched");
      matched = value && value->boolean().value_or(false);
    }
  }
  CHECK(matched);
  CHECK(joggle::run(env, "script.apply_configured_rule", relation));
  bool configured = false;
  for (joggle::Op op : relation.ops()) {
    if (op.callee() == "opaque") {
      const joggle::Attr* value = op.meta("configured");
      configured = value && value->integer() == 7;
    }
  }
  CHECK(configured);
  const std::string before_bad_config = joggle::print(relation);
  const std::uint64_t before_bad_config_revision = relation.revision();
  CHECK(!joggle::run(env, "script.reject_configured_rule", relation));
  CHECK(joggle::print(relation) == before_bad_config);
  CHECK(relation.revision() == before_bad_config_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  const std::vector<joggle::Attr> accepted_callee{joggle::Attr("opaque")};
  joggle::Attr configured_frontier;
  CHECK(joggle::query(env, "script.configured_frontier", relation,
                      configured_frontier, accepted_callee));
  CHECK(configured_frontier.list() && configured_frontier.list()->empty());
  const std::vector<joggle::Attr> rejected_callee{joggle::Attr("other")};
  CHECK(joggle::query(env, "script.configured_frontier", relation,
                      configured_frontier, rejected_callee));
  CHECK(configured_frontier.list() &&
        configured_frontier.list()->size() == 1 &&
        configured_frontier.list()->front().string() == "opaque");
  std::int64_t expected_cost = 0;
  for (joggle::Op op : relation.ops())
    expected_cost += op.kind() == joggle::Op::Kind::call ? 2 : 1;
  joggle::Attr model_cost;
  CHECK(joggle::query(env, "script.model_cost", relation, model_cost));
  CHECK(model_cost.integer() == expected_cost);
  const std::string before_bad_cost = joggle::print(relation);
  const std::uint64_t before_bad_cost_revision = relation.revision();
  CHECK(!joggle::query(env, "script.reject_bad_cost", relation, model_cost));
  CHECK(joggle::print(relation) == before_bad_cost);
  CHECK(relation.revision() == before_bad_cost_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  const std::string relation_before = joggle::print(relation);
  const std::uint64_t relation_revision = relation.revision();
  CHECK(!joggle::run(env, "script.reject_bad_rule", relation));
  CHECK(joggle::print(relation) == relation_before);
  CHECK(relation.revision() == relation_revision);
  env.clear_diags();
  CHECK(env.load("tflite.nn"));
  constexpr std::string_view tflite_relation_source =
      "mod tflite.relation\n"
      "use tflite\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3]>, right: tensor<f32, [2, 1]>\n"
      ") -> tensor<f32, [2, 3]> {\n"
      "  [tflite: {options: {fused_activation_function: \"NONE\"}}]\n"
      "  let out: tensor<f32, [2, 3]> = tflite.ADD(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod tflite_relation;
  CHECK(joggle::parse(env, tflite_relation_source, tflite_relation,
                      "tflite-relation.jog"));
  CHECK(tflite_relation.verify(env));
  CHECK(joggle::run(env, "tflite.nn.convert", tflite_relation));
  CHECK(tflite_relation.verify(env));
  std::size_t tflite_adds = 0;
  std::size_t nn_adds = 0;
  for (joggle::Op op : tflite_relation.ops()) {
    tflite_adds += op.callee() == "tflite.ADD" ? 1 : 0;
    nn_adds += op.callee() == "nn.add" ? 1 : 0;
  }
  CHECK(tflite_adds == 0 && nn_adds == 1);
  constexpr std::string_view custom_onnx_source =
      "mod custom.onnx\n"
      "use onnx\n"
      "fn main(x: i32) -> _ {\n"
      "  [onnx: {}]\n"
      "  let y = onnx.Custom(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod custom_onnx;
  CHECK(joggle::parse(env, custom_onnx_source, custom_onnx,
                      "custom-onnx.jog"));
  CHECK(custom_onnx.verify(env));
  CHECK(joggle::run(env, "script.extend_onnx", custom_onnx));
  CHECK(custom_onnx.verify(env));
  std::size_t custom_calls = 0;
  std::size_t copies = 0;
  for (joggle::Op op : custom_onnx.ops()) {
    custom_calls += op.callee() == "onnx.Custom" ? 1 : 0;
    if (op.callee() == "base.copy") {
      copies += 1;
      CHECK(op.outs().size() == 1 &&
            op.outs()[0].type() == joggle::Ty("i32"));
    }
  }
  CHECK(custom_calls == 0 && copies == 1);
  CHECK(joggle::run(env, "script.tensor_type_probe", network_cpp));
  CHECK(joggle::run(env, "script.text_probe", network_cpp));
  CHECK(joggle::run(env, "script.partial_generic_probe", network_cpp));
  CHECK(joggle::run(env, "script.generic_invoke_probe", network_cpp));
  {
    joggle::Mod compiler;
    CHECK(joggle::parse(env,
                        "mod compiler.demo\n"
                        "use script\n"
                        "use opt\n"
                        "fn inert(m: Mod) -> bool { return true }\n",
                        compiler, "compiler-demo.jog"));
    const joggle::Fn source = env.find_fn("script.emit_bytes");
    CHECK(source);
    const joggle::Fn generated =
        compiler.clone(env, source, "derived_emitter");
    CHECK(generated && compiler.verify(env));
    joggle::Mod program;
    CHECK(joggle::parse(env,
                        "mod compiler.input\n"
                        "fn main(x: i32) -> i32 { let y = x + 0 return y }\n",
                        program, "compiler-input.jog"));
    CHECK(program.verify(env));
    const std::string original_program = joggle::print(program);
    joggle::Attr output;
    CHECK(joggle::query(env, generated, program, output));
    CHECK(output == joggle::Attr(joggle::Attr::Bytes{0, 65, 10, 255}));
    for (joggle::Op op : generated.ops()) {
      if (op.kind() != joggle::Op::Kind::ret)
        continue;
      const joggle::Val next =
          compiler.constant(op, joggle::Attr(joggle::Attr::Bytes{254}),
                            joggle::Ty("bytes"));
      CHECK(next && compiler.replace(op.args().front(), next, op));
    }
    CHECK(compiler.verify(env));
    CHECK(joggle::query(env, generated, program, output));
    CHECK(output == joggle::Attr(joggle::Attr::Bytes{254}));
    CHECK(joggle::query(env, "script.emit_bytes", program, output));
    CHECK(output == joggle::Attr(joggle::Attr::Bytes{0, 65, 10, 255}));
    CHECK(joggle::print(program) == original_program);

    const joggle::Fn optimization =
        compiler.clone(env, env.find_fn("opt.fold_add_zero"),
                       "fold_add_zero");
    CHECK(optimization && compiler.verify(env));
    const std::string compiler_source = joggle::print(compiler);
    joggle::Attr report;
    CHECK(!joggle::query(env, optimization, program, output));
    CHECK(joggle::print(program) == original_program);
    CHECK(joggle::run(env, optimization, program, report));
    CHECK(report.dict() && report.dict()->at("reported").boolean() == true);
    CHECK(program.verify(env));
    CHECK(joggle::print(program) != original_program);
    CHECK(joggle::print(compiler) == compiler_source);
    CHECK(!program.find_fn("derived_emitter"));
    CHECK(!program.find_fn("fold_add_zero"));
    CHECK(env.load("c"));
    CHECK(joggle::run(env, "c.prepare", program));
    CHECK(program.verify(env));
  }
  {
    // A copied public procedure must capture its private lexical helper
    // closure without making the source module's private API public.
    joggle::Mod code;
    CHECK(joggle::parse(env,
                        "mod compiler.boundary\n"
                        "use opt\n"
                        "fn inert(m: Mod) -> bool { return true }\n",
                        code, "compiler-boundary.jog"));
    joggle::Fn source;
    for (joggle::Fn candidate : env.find_fns("opt.expose")) {
      if (candidate.params().size() == 3 &&
          candidate.params()[1].type() == joggle::Ty("Fn"))
        source = candidate;
    }
    CHECK(source);
    CHECK(code.clone(env, source, "derived_expose"));
    CHECK(code.find_fn("derived_expose_expose_with"));
    CHECK(code.find_fn("derived_expose_expand_with"));
    CHECK(code.verify(env));

    joggle::Mod c_code;
    CHECK(joggle::parse(env,
                        "mod compiler.c\n"
                        "use c\n"
                        "fn inert(m: Mod) -> bool { return true }\n",
                        c_code, "compiler-c.jog"));
    joggle::Fn c_source;
    for (joggle::Fn candidate : env.find_fns("c.prepare"))
      if (candidate.params().size() == 1 &&
          candidate.params().front().type() == joggle::Ty("Mod"))
        c_source = candidate;
    CHECK(c_source);
    const joggle::Fn c_derived =
        c_code.clone(env, c_source, "derived_prepare");
    CHECK(c_derived && c_code.verify(env));
    CHECK(c_code.find_fn("derived_prepare_prepare_with"));

    joggle::Mod original, target;
    const std::string_view source_model =
        "mod compiler.c_input\n"
        "fn main(x: i32) -> i32 { let y = x + 0 return y }\n";
    CHECK(joggle::parse(env, source_model, original, "compiler-c-input.jog"));
    CHECK(joggle::parse(env, source_model, target, "compiler-c-target.jog"));
    joggle::Attr report;
    CHECK(joggle::run(env, "c.prepare", original));
    CHECK(joggle::run(env, c_derived, target, report));
    CHECK(joggle::print(target) == joggle::print(original));
    CHECK(!target.find_fn("derived_prepare"));
    CHECK(target.verify(env));

    const std::string original_code = joggle::print(c_code);
    std::size_t legalize_calls = 0;
    bool specialized = false;
    for (joggle::Op op :
         c_code.find_fn("derived_prepare_prepare_with").ops()) {
      if (op.kind() != joggle::Op::Kind::call ||
          op.callee() != "opt.legalize")
        continue;
      if (++legalize_calls == 2)
        specialized = c_code.replace(op, joggle::Attr(false));
    }
    CHECK(legalize_calls == 2 && specialized && c_code.verify(env));
    CHECK(joggle::print(c_code) != original_code);
    joggle::Mod optimized_target;
    CHECK(joggle::parse(env, source_model, optimized_target,
                        "compiler-c-optimized-target.jog"));
    CHECK(joggle::run(env, c_derived, optimized_target, report));
    CHECK(joggle::print(optimized_target) == joggle::print(original));
    joggle::Attr original_source, derived_source;
    CHECK(joggle::query(env, "c.source", original, original_source));
    CHECK(joggle::query(env, "c.source", optimized_target,
                        derived_source));
    CHECK(derived_source == original_source);
    joggle::Mod original_after;
    CHECK(joggle::parse(env, source_model, original_after,
                        "compiler-c-source-after.jog"));
    CHECK(joggle::run(env, "c.prepare", original_after));
    CHECK(joggle::print(original_after) == joggle::print(original));
  }
  {
    // A private opaque dependency cannot be copied as an editable body;
    // failing that closure must leave the destination exactly unchanged.
    joggle::Mod source, destination;
    CHECK(joggle::parse(env,
                        "mod compiler.private\n"
                        "local fn helper(m: Mod) -> bool;\n"
                        "fn entry(m: Mod) -> bool { return helper(m) }\n",
                        source, "compiler-private.jog"));
    CHECK(source.verify(env));
    CHECK(joggle::parse(env,
                        "mod compiler.private_target\n"
                        "fn inert(m: Mod) -> bool { return true }\n",
                        destination, "compiler-private-target.jog"));
    const std::string before = joggle::print(destination);
    CHECK(!destination.clone(env, source.find_fn("entry"),
                             "derived_entry"));
    CHECK(joggle::print(destination) == before);
    CHECK(destination.verify(env));
    joggle::Mod cyclic;
    CHECK(joggle::parse(env,
                        "mod compiler.cyclic\n"
                        "local fn a(m: Mod) -> bool { return b(m) }\n"
                        "local fn b(m: Mod) -> bool { return a(m) }\n"
                        "fn entry(m: Mod) -> bool { return a(m) }\n",
                        cyclic, "compiler-cyclic.jog"));
    CHECK(cyclic.verify(env));
    CHECK(!destination.clone(env, cyclic.find_fn("entry"),
                             "cyclic_entry"));
    CHECK(joggle::print(destination) == before);
  }
  {
    joggle::Mod derived;
    CHECK(joggle::parse(env,
                        "mod derived\n"
                        "fn matmul() -> i32 { return 1 }\n",
                        derived, "derived.jog"));
    CHECK(derived.verify(env));
    const std::string initial = joggle::print(derived);
    const auto revision = derived.revision();
    joggle::Attr direct, invoked;
    CHECK(joggle::query(env, "script.generic_invoke_probe", derived, invoked));
    CHECK(invoked.boolean() == true);
    CHECK(joggle::query(env, "stat.summary", derived, direct));
    CHECK(joggle::query(env, "script.invoke_summary", derived, invoked));
    CHECK(direct == invoked);
    CHECK(joggle::query(env, "script.invoke_name", derived, invoked));
    CHECK(invoked == joggle::Attr("matmul"));
    CHECK(joggle::print(derived) == initial && derived.revision() == revision);

    // Nested invocation cannot turn a read-only query into a mutating entry.
    CHECK(!joggle::query(env, "script.derive_emitter", derived, invoked));
    CHECK(joggle::print(derived) == initial && derived.revision() == revision);
    derived.clear_diags();
    joggle::Attr derivation;
    CHECK(joggle::run(env, "script.derive_emitter", derived, derivation));
    CHECK(derivation.dict() &&
          derivation.dict()->at("reported").boolean() == true);
    CHECK(derived.find_fn("derived"));
    CHECK(derived.verify(env));
    joggle::Mod roundtrip;
    CHECK(joggle::parse(env, joggle::print(derived), roundtrip,
                        "derived-roundtrip.jog"));
    CHECK(roundtrip.verify(env));
    CHECK(joggle::structurally_equal(derived, roundtrip));

    // Signature failures must undo the clone and its dependency edits too.
    for (const auto* entry :
         {"script.reject_invoke_arity", "script.reject_invoke_result"}) {
      joggle::Mod rejected;
      CHECK(joggle::parse(env, initial, rejected, "rejected.jog"));
      const auto before = rejected.revision();
      CHECK(!joggle::run(env, entry, rejected));
      CHECK(joggle::print(rejected) == initial);
      CHECK(rejected.revision() == before);
      CHECK(!rejected.find_fn("rejected"));
      CHECK(!env.diags().empty());
      CHECK(env.diags().back().message.find("ir.invoke callback") !=
            std::string::npos);
    }
  }
  CHECK(joggle::run(env, "script.byte_probe", network_cpp));
  CHECK(joggle::run(env, "script.def_probe", network_cpp));
  CHECK(joggle::run(env, "script.expand_network", network));
  CHECK(network.verify(env));
  bool expanded_loop = false;
  bool expanded_branch = false;
  for (joggle::Op op : network.find_fn("stage").ops()) {
    CHECK(op.callee() != "nn.relu");
    expanded_loop = expanded_loop || op.kind() == joggle::Op::Kind::loop;
    expanded_branch =
        expanded_branch || op.kind() == joggle::Op::Kind::branch;
  }
  CHECK(expanded_loop && expanded_branch);
  joggle::Mod expanded_roundtrip;
  CHECK(joggle::parse(env, joggle::print(network), expanded_roundtrip,
                      "expanded-roundtrip.jog"));
  CHECK(expanded_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(network, expanded_roundtrip));
  CHECK(joggle::structurally_equal(network, network_cpp));

  constexpr std::string_view logical_source =
      "mod logical\n"
      "fn choose(a: bool, b: bool, c: bool) -> bool {\n"
      "  let out: bool = a && (b || c)\n"
      "  return out\n"
      "}\n";
  joggle::Mod logical;
  CHECK(joggle::parse(env, logical_source, logical, "logical.jog"));
  CHECK(logical.verify(env));
  std::size_t logical_branches = 0;
  for (joggle::Op op : logical.ops())
    logical_branches += op.kind() == joggle::Op::Kind::branch ? 1 : 0;
  CHECK(logical_branches == 2);
  const std::string logical_text = joggle::print(logical);
  CHECK(logical_text.find("a && (b || c)") != std::string::npos);
  joggle::Mod logical_roundtrip;
  CHECK(joggle::parse(env, logical_text, logical_roundtrip,
                      "logical-roundtrip.jog"));
  CHECK(logical_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(logical, logical_roundtrip));
  joggle::Mod invalid_logical;
  CHECK(joggle::parse(
      env, "mod invalid.logical\n"
           "fn bad(a: bool) -> bool { return a && 1 }\n",
      invalid_logical, "invalid-logical.jog"));
  CHECK(!invalid_logical.verify(env));
  CHECK(!invalid_logical.diags().empty());
  CHECK(invalid_logical.diags().front().message.find("yield type") !=
        std::string::npos);

  joggle::Mod generic_matmul;
  constexpr std::string_view generic_matmul_source =
      "mod generic.matmul\n"
      "use tensor\n"
      "fn main(a: tensor<f32, [2, 3]>, b: tensor<f32, [3, 4]>) "
      "-> tensor<f32, [2, 4]> {\n"
      "  return tensor.matmul(a, b)\n}\n";
  CHECK(joggle::parse(env, generic_matmul_source, generic_matmul,
                      "generic-matmul.jog"));
  joggle::Op matmul_call;
  for (joggle::Op op : generic_matmul.ops())
    if (op.callee() == "tensor.matmul")
      matmul_call = op;
  CHECK(matmul_call);
  const joggle::Fn matmul_fn = env.resolve(generic_matmul, matmul_call);
  CHECK(matmul_fn && env.expand(generic_matmul, matmul_call, matmul_fn));
  CHECK(generic_matmul.verify(env));
  std::size_t matmul_loops = 0;
  for (joggle::Op op : generic_matmul.ops()) {
    CHECK(op.callee() != "tensor.matmul");
    matmul_loops += op.kind() == joggle::Op::Kind::loop ? 1 : 0;
  }
  CHECK(matmul_loops == 1);
  joggle::Mod generic_matmul_roundtrip;
  CHECK(joggle::parse(env, joggle::print(generic_matmul),
                      generic_matmul_roundtrip,
                      "generic-matmul-roundtrip.jog"));
  CHECK(generic_matmul_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(generic_matmul,
                                   generic_matmul_roundtrip));

  joggle::Mod conv_network;
  constexpr std::string_view conv_network_source =
      "mod conv.network\n"
      "use nn\n"
      "fn main(\n"
      "  x: tensor<f32, [1, 3, 5, 5]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>,\n"
      "  stride: list<int>, pad: list<int>, dilation: list<int>\n"
      ") -> tensor<f32, [1, 4, 3, 3]> {\n"
      "  let y: tensor<f32, [1, 4, 3, 3]> = nn.conv2d(\n"
      "    x, weight, stride, pad, dilation, 1\n"
      "  )\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, conv_network_source, conv_network,
                      "conv-network.jog"));
  CHECK(conv_network.verify(env));
  joggle::Op conv_call;
  for (joggle::Op op : conv_network.ops())
    if (op.callee() == "nn.conv2d")
      conv_call = op;
  CHECK(conv_call);
  const joggle::Fn conv_fn = env.resolve(conv_network, conv_call);
  CHECK(conv_fn && env.expand(conv_network, conv_call, conv_fn));
  CHECK(conv_network.verify(env));
  joggle::Op layout_conv;
  for (joggle::Op op : conv_network.ops())
    if (op.callee() == "conv2d" || op.callee() == "nn.conv2d")
      layout_conv = op;
  CHECK(layout_conv);
  const joggle::Fn layout_conv_fn = env.resolve(conv_network, layout_conv);
  CHECK(layout_conv_fn &&
        env.expand(conv_network, layout_conv, layout_conv_fn));
  CHECK(conv_network.verify(env));
  std::size_t conv_loops = 0;
  for (joggle::Op op : conv_network.ops()) {
    CHECK(op.callee() != "nn.conv2d");
    if (op.kind() == joggle::Op::Kind::loop) {
      ++conv_loops;
      CHECK(op.args().size() == op.outs().size() + 7);
    }
  }
  CHECK(conv_loops == 1);
  joggle::Mod conv_roundtrip;
  CHECK(joggle::parse(env, joggle::print(conv_network), conv_roundtrip,
                      "conv-network-roundtrip.jog"));
  CHECK(conv_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(conv_network, conv_roundtrip));

  joggle::Mod pool_network;
  constexpr std::string_view pool_network_source =
      "mod pool.network\n"
      "use nn\n"
      "fn main(x: tensor<f32, [1, 8, 4, 4]>) "
      "-> tensor<f32, [1, 8, 1, 1]> {\n"
      "  return nn.global_avg_pool2d(x)\n"
      "}\n";
  CHECK(joggle::parse(env, pool_network_source, pool_network,
                      "pool-network.jog"));
  CHECK(pool_network.verify(env));
  joggle::Op pool_call;
  for (joggle::Op op : pool_network.ops())
    if (op.callee() == "nn.global_avg_pool2d")
      pool_call = op;
  CHECK(pool_call);
  const joggle::Fn pool_fn = env.resolve(pool_network, pool_call);
  CHECK(pool_fn && env.expand(pool_network, pool_call, pool_fn));
  CHECK(pool_network.verify(env));
  for (joggle::Op op : pool_network.ops())
    CHECK(op.callee() != "nn.global_avg_pool2d");
  joggle::Mod pool_roundtrip;
  CHECK(joggle::parse(env, joggle::print(pool_network), pool_roundtrip,
                      "pool-network-roundtrip.jog"));
  CHECK(pool_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(pool_network, pool_roundtrip));

  joggle::Mod norm_network;
  constexpr std::string_view norm_network_source =
      "mod norm.network\n"
      "use nn\n"
      "fn main(\n"
      "  x: tensor<f32, [1, 8, 4, 4]>,\n"
      "  scale: tensor<f32, [8]>, bias: tensor<f32, [8]>,\n"
      "  mean: tensor<f32, [8]>, variance: tensor<f32, [8]>, epsilon: f64\n"
      ") -> tensor<f32, [1, 8, 4, 4]> {\n"
      "  return nn.batch_norm(x, scale, bias, mean, variance, epsilon)\n"
      "}\n";
  CHECK(joggle::parse(env, norm_network_source, norm_network,
                      "norm-network.jog"));
  CHECK(norm_network.verify(env));
  joggle::Op norm_call;
  for (joggle::Op op : norm_network.ops())
    if (op.callee() == "nn.batch_norm")
      norm_call = op;
  CHECK(norm_call);
  const joggle::Fn norm_fn = env.resolve(norm_network, norm_call);
  CHECK(norm_fn && env.expand(norm_network, norm_call, norm_fn));
  std::size_t sqrt_calls = 0;
  for (joggle::Op op : norm_network.ops()) {
    CHECK(op.callee() != "nn.batch_norm");
    const joggle::Fn target = env.resolve(norm_network, op);
    if (target && target.module() == "math" && target.name() == "sqrt") {
      CHECK(op.outs().size() == 1 &&
            op.outs().front().type() == joggle::Ty("f32"));
      ++sqrt_calls;
    }
  }
  CHECK(sqrt_calls == 1);
  CHECK(norm_network.verify(env));
  joggle::Mod norm_roundtrip;
  CHECK(joggle::parse(env, joggle::print(norm_network), norm_roundtrip,
                      "norm-network-roundtrip.jog"));
  CHECK(norm_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(norm_network, norm_roundtrip));

  joggle::Mod reshape_network;
  constexpr std::string_view reshape_network_source =
      "mod reshape.network\n"
      "use tensor\n"
      "fn main(x: tensor<f32, [1, 2, 3]>) -> tensor<f32, [1, 6]> {\n"
      "  let y: tensor<f32, [1, 6]> = tensor.reshape(x)\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, reshape_network_source, reshape_network,
                      "reshape-network.jog"));
  CHECK(reshape_network.verify(env));
  joggle::Op reshape_call;
  for (joggle::Op op : reshape_network.ops())
    if (op.callee() == "tensor.reshape")
      reshape_call = op;
  CHECK(reshape_call);
  const joggle::Fn reshape_fn = env.resolve(reshape_network, reshape_call);
  CHECK(reshape_fn && env.expand(reshape_network, reshape_call, reshape_fn));
  CHECK(reshape_network.verify(env));
  for (joggle::Op op : reshape_network.ops())
    CHECK(op.callee() != "tensor.reshape");
  joggle::Mod reshape_roundtrip;
  CHECK(joggle::parse(env, joggle::print(reshape_network), reshape_roundtrip,
                      "reshape-network-roundtrip.jog"));
  CHECK(reshape_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(reshape_network, reshape_roundtrip));

  joggle::Mod linear_network;
  constexpr std::string_view linear_network_source =
      "mod linear.network\n"
      "use nn\n"
      "fn stage(\n"
      "  x: tensor<f32, [2, 3]>,\n"
      "  weight: tensor<f32, [3, 4]>,\n"
      "  bias: tensor<f32, [4]>\n"
      ") -> tensor<f32, [2, 4]> {\n"
      "  return nn.relu(nn.linear(x, weight, bias))\n}\n";
  CHECK(joggle::parse(env, linear_network_source, linear_network,
                      "linear-network.jog"));
  CHECK(linear_network.verify(env));
  for (joggle::Op op : linear_network.ops()) {
    if (op.callee() != "nn.linear" && op.callee() != "nn.relu")
      continue;
    const joggle::Fn callee = env.resolve(linear_network, op);
    CHECK(callee && env.expand(linear_network, op, callee));
  }
  CHECK(linear_network.verify(env));
  joggle::Op exposed_matmul;
  for (joggle::Op op : linear_network.ops()) {
    CHECK(op.callee() != "nn.linear" && op.callee() != "nn.relu");
    if (op.callee() == "tensor.matmul")
      exposed_matmul = op;
  }
  CHECK(exposed_matmul);
  const joggle::Fn exposed_matmul_fn =
      env.resolve(linear_network, exposed_matmul);
  CHECK(exposed_matmul_fn &&
        env.expand(linear_network, exposed_matmul, exposed_matmul_fn));
  CHECK(linear_network.verify(env));
  for (joggle::Op op : linear_network.ops())
    CHECK(op.callee() != "tensor.matmul");
  joggle::Mod linear_network_roundtrip;
  const std::string linear_network_text = joggle::print(linear_network);
  CHECK(joggle::parse(env, linear_network_text,
                      linear_network_roundtrip,
                      "linear-network-roundtrip.jog"));
  CHECK(linear_network_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(linear_network,
                                   linear_network_roundtrip));

  joggle::Mod local_expand;
  constexpr std::string_view local_expand_source =
      "mod local.expand\n"
      "fn pair(x: i32) -> (i32, i32) {\n"
      "  return x, x + 1\n}\n"
      "fn main(x: i32) -> i32 {\n"
      "  let first, second = pair(x)\n"
      "  return first + second\n}\n";
  CHECK(joggle::parse(env, local_expand_source, local_expand,
                      "local-expand.jog"));
  CHECK(local_expand.verify(env));
  joggle::Op pair_call;
  for (joggle::Op op : local_expand.find_fn("main").ops())
    if (op.callee() == "pair")
      pair_call = op;
  const joggle::Fn pair_fn = env.resolve(local_expand, pair_call);
  CHECK(pair_call && pair_fn && env.expand(local_expand, pair_call, pair_fn));
  CHECK(local_expand.verify(env));
  CHECK(joggle::print(local_expand).find("pair(x)") == std::string::npos);
  joggle::Mod local_expand_roundtrip;
  CHECK(joggle::parse(env, joggle::print(local_expand),
                      local_expand_roundtrip, "local-expand-roundtrip.jog"));
  CHECK(local_expand_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(local_expand, local_expand_roundtrip));

  joggle::Mod expression_expand;
  constexpr std::string_view expression_expand_source =
      "mod expression.expand\n"
      "fn helper(x: i32) -> bool {\n"
      "  var out = false\n"
      "  if x > 0 { out = true }\n"
      "  return out\n"
      "}\n"
      "fn main(x: i32) -> bool { return x > 0 && helper(x) }\n";
  CHECK(joggle::parse(env, expression_expand_source, expression_expand,
                      "expression-expand.jog"));
  CHECK(expression_expand.verify(env));
  joggle::Op expression_call;
  for (joggle::Op op : expression_expand.find_fn("main").ops())
    if (op.callee() == "helper")
      expression_call = op;
  const joggle::Fn expression_body = env.resolve(expression_expand,
                                                  expression_call);
  const std::string expression_text = joggle::print(expression_expand);
  const std::uint64_t expression_revision = expression_expand.revision();
  CHECK(expression_call && expression_body &&
        !env.expand(expression_expand, expression_call, expression_body));
  CHECK(expression_expand.revision() == expression_revision);
  CHECK(joggle::print(expression_expand) == expression_text);
  CHECK(std::any_of(
      expression_expand.diags().begin(), expression_expand.diags().end(),
      [](const joggle::Diag& diag) {
        return diag.message.find("stateful body in an expression region") !=
               std::string::npos;
      }));
  expression_expand.clear_diags();
  CHECK(expression_expand.verify(env));

  joggle::Mod imported_expand;
  constexpr std::string_view imported_expand_source =
      "mod imported.expand\n"
      "use tensor\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  let y: tensor<f32, [4]> = relu(x)\n"
      "  return y\n}\n";
  CHECK(joggle::parse(env, imported_expand_source, imported_expand,
                      "imported-expand.jog"));
  CHECK(imported_expand.verify(env));
  joggle::Op imported_relu;
  for (joggle::Op op : imported_expand.ops())
    if (op.callee() == "relu")
      imported_relu = op;
  const std::uint64_t imported_revision = imported_expand.revision();
  CHECK(imported_relu && env.expand(imported_expand, imported_relu, relu));
  CHECK(imported_expand.revision() == imported_revision + 1);
  CHECK(imported_expand.uses() ==
        std::vector<std::string>({"tensor", "nn"}));
  CHECK(imported_expand.verify(env));

  joggle::Mod lexical_expand;
  constexpr std::string_view lexical_expand_source =
      "mod lexical.expand\n"
      "use script\n"
      "local fn hidden(m: Mod) -> bool { return false }\n"
      "fn main(m: Mod) -> bool { return script.local_probe(m) }\n";
  CHECK(joggle::parse(env, lexical_expand_source, lexical_expand,
                      "lexical-expand.jog"));
  CHECK(lexical_expand.verify(env));
  joggle::Op local_probe;
  for (joggle::Op op : lexical_expand.find_fn("main").ops())
    if (op.callee() == "script.local_probe")
      local_probe = op;
  const std::uint64_t lexical_revision = lexical_expand.revision();
  const joggle::Fn local_probe_fn = env.resolve(lexical_expand, local_probe);
  CHECK(local_probe && local_probe_fn &&
        env.expand(lexical_expand, local_probe, local_probe_fn));
  CHECK(lexical_expand.revision() == lexical_revision + 1);
  CHECK(lexical_expand.verify(env));
  CHECK(lexical_expand.find_fns("hidden").size() == 1);
  CHECK(lexical_expand.find_fns("script_hidden").empty());
  const std::string lexical_text = joggle::print(lexical_expand);
  CHECK(lexical_text.find("ir.revision(m)") != std::string::npos);
  CHECK(lexical_text.find("local_probe") == std::string::npos);
  joggle::Mod lexical_roundtrip;
  CHECK(joggle::parse(env, lexical_text, lexical_roundtrip,
                      "lexical-expand-roundtrip.jog"));
  CHECK(lexical_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(lexical_expand, lexical_roundtrip));

  joggle::Mod recursive_closure;
  constexpr std::string_view recursive_closure_source =
      "mod recursive.closure\n"
      "use script\n"
      "fn main(x: i32) -> i32 { return script.recursive_local_probe(x) }\n";
  CHECK(joggle::parse(env, recursive_closure_source, recursive_closure,
                      "recursive-closure.jog"));
  CHECK(recursive_closure.verify(env));
  joggle::Op recursive_call;
  for (joggle::Op op : recursive_closure.ops())
    if (op.callee() == "script.recursive_local_probe")
      recursive_call = op;
  const std::string recursive_closure_text = joggle::print(recursive_closure);
  const std::uint64_t recursive_revision = recursive_closure.revision();
  const joggle::Fn recursive_target = env.resolve(recursive_closure,
                                                   recursive_call);
  CHECK(recursive_call && recursive_target &&
        !env.expand(recursive_closure, recursive_call, recursive_target));
  CHECK(recursive_closure.revision() == recursive_revision);
  CHECK(joggle::print(recursive_closure) == recursive_closure_text);
  CHECK(std::any_of(
      recursive_closure.diags().begin(), recursive_closure.diags().end(),
      [](const joggle::Diag& diag) {
        return diag.message.find("recursive local function dependency") !=
               std::string::npos;
      }));
  recursive_closure.clear_diags();
  CHECK(recursive_closure.verify(env));

  joggle::Mod batch_expand;
  constexpr std::string_view batch_expand_source =
      "mod batch.expand\n"
      "use nn\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  let first = nn.relu(x)\n"
      "  let second = nn.relu(first)\n"
      "  return second\n}\n";
  CHECK(joggle::parse(env, batch_expand_source, batch_expand,
                      "batch-expand.jog"));
  CHECK(batch_expand.verify(env));
  std::vector<joggle::Op> batch_calls;
  for (joggle::Op op : batch_expand.ops())
    if (op.callee() == "nn.relu")
      batch_calls.push_back(op);
  CHECK(batch_calls.size() == 2);
  const std::vector<joggle::Fn> batch_impls{relu, relu};
  const std::uint64_t batch_revision = batch_expand.revision();
  CHECK(env.expand(batch_expand, batch_calls, batch_impls));
  CHECK(batch_expand.revision() == batch_revision + batch_calls.size());
  CHECK(batch_expand.verify(env));
  for (joggle::Op op : batch_expand.ops())
    CHECK(op.callee() != "nn.relu");

  joggle::Mod batch_rollback;
  constexpr std::string_view batch_rollback_source =
      "mod batch.rollback\n"
      "use nn\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  let first = nn.relu(x)\n"
      "  [keep]\n"
      "  let second = nn.relu(first)\n"
      "  return second\n}\n";
  CHECK(joggle::parse(env, batch_rollback_source, batch_rollback,
                      "batch-rollback.jog"));
  CHECK(batch_rollback.verify(env));
  std::vector<joggle::Op> rollback_calls;
  for (joggle::Op op : batch_rollback.ops())
    if (op.callee() == "nn.relu")
      rollback_calls.push_back(op);
  CHECK(rollback_calls.size() == 2);
  const std::string rollback_text = joggle::print(batch_rollback);
  const std::uint64_t rollback_revision = batch_rollback.revision();
  CHECK(!env.expand(batch_rollback, rollback_calls, batch_impls));
  CHECK(joggle::print(batch_rollback) == rollback_text);
  CHECK(batch_rollback.revision() == rollback_revision);
  batch_rollback.clear_diags();
  CHECK(batch_rollback.verify(env));
  const std::array<joggle::Fn, 1> missing_impl{relu};
  CHECK(!env.expand(batch_rollback, rollback_calls, missing_impl));
  CHECK(joggle::print(batch_rollback) == rollback_text);
  CHECK(batch_rollback.revision() == rollback_revision);
  batch_rollback.clear_diags();
  CHECK(batch_rollback.verify(env));

  joggle::Mod detached_implementation;
  CHECK(joggle::parse(env,
                      "mod detached.impl\n"
                      "fn source(x: i32) -> i32 { return x }\n",
                      detached_implementation, "detached-impl.jog"));
  CHECK(detached_implementation.verify(env));
  joggle::Mod detached_user;
  CHECK(joggle::parse(env,
                      "mod detached.user\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      detached_user, "detached-user.jog"));
  CHECK(detached_user.verify(env));
  const joggle::Op detached_call =
      detached_user.find_fn("main").body().ops()[0];
  const std::string detached_before = joggle::print(detached_user);
  const std::uint64_t detached_revision = detached_user.revision();
  CHECK(!env.expand(detached_user, detached_call,
                    detached_implementation.find_fn("source")));
  CHECK(joggle::print(detached_user) == detached_before);
  CHECK(detached_user.revision() == detached_revision);
  CHECK(detached_user.uses().empty());
  detached_user.clear_diags();
  CHECK(detached_user.verify(env));

  joggle::Mod rejected_expand;
  constexpr std::string_view rejected_expand_source =
      "mod rejected.expand\n"
      "use tensor\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  [keep]\n"
      "  let y: tensor<f32, [4]> = relu(x)\n"
      "  return y\n}\n";
  CHECK(joggle::parse(env, rejected_expand_source, rejected_expand,
                      "rejected-expand.jog"));
  joggle::Op rejected_relu;
  for (joggle::Op op : rejected_expand.ops())
    if (op.callee() == "relu")
      rejected_relu = op;
  CHECK(rejected_relu && rejected_relu.meta("keep"));
  const std::string rejected_text = joggle::print(rejected_expand);
  const std::uint64_t rejected_revision = rejected_expand.revision();
  CHECK(!env.expand(rejected_expand, rejected_relu, relu));
  CHECK(joggle::print(rejected_expand) == rejected_text);
  CHECK(rejected_expand.revision() == rejected_revision);
  CHECK(rejected_expand.uses() == std::vector<std::string>{"tensor"});
  rejected_expand.clear_diags();
  CHECK(rejected_expand.verify(env));

  CHECK(env.load("onnx"));
  joggle::Mod bridged;
  constexpr std::string_view bridged_source =
      "mod bridged\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  let y: tensor<f32, [4]> = onnx.Relu(x)\n"
      "  return y\n}\n";
  CHECK(joggle::parse(env, bridged_source, bridged, "bridged.jog"));
  CHECK(bridged.verify(env));
  CHECK(joggle::run(env, "script.bridge_relu", bridged));
  CHECK(bridged.verify(env));
  bool uses_nn = false;
  for (const std::string& module : bridged.uses())
    uses_nn = uses_nn || module == "nn";
  CHECK(uses_nn);
  joggle::Attr has_nn;
  const std::vector<joggle::Attr> nn_query{joggle::Attr("nn")};
  CHECK(joggle::query(env, "script.has_use", bridged, has_nn, nn_query));
  CHECK(has_nn.boolean() && *has_nn.boolean());
  joggle::Op bridged_relu;
  for (joggle::Op op : bridged.ops())
    if (op.callee() == "nn.relu")
      bridged_relu = op;
  CHECK(bridged_relu && env.resolve(bridged, bridged_relu) == relu);
  const std::uint64_t bridged_revision = bridged.revision();
  CHECK(joggle::run(env, "script.bridge_relu", bridged));
  CHECK(bridged.revision() == bridged_revision);
  joggle::Mod bridged_roundtrip;
  CHECK(joggle::parse(env, joggle::print(bridged), bridged_roundtrip,
                      "bridged-roundtrip.jog"));
  CHECK(bridged_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(bridged, bridged_roundtrip));

  joggle::Mod dependencies;
  CHECK(joggle::parse(env, "mod dependencies\nfn main() -> int { "
                           "return 0 }\n",
                      dependencies, "dependencies.jog"));
  const std::uint64_t dependencies_revision = dependencies.revision();
  CHECK(dependencies.use(env, "nn"));
  CHECK(dependencies.revision() == dependencies_revision + 1);
  CHECK(dependencies.use(env, "nn"));
  CHECK(dependencies.revision() == dependencies_revision + 1);
  const std::string dependencies_text = joggle::print(dependencies);
  CHECK(!dependencies.use(env, "not-a-module"));
  CHECK(joggle::print(dependencies) == dependencies_text);
  CHECK(dependencies.revision() == dependencies_revision + 1);
  CHECK(!dependencies.use(env, "missing"));
  CHECK(joggle::print(dependencies) == dependencies_text);
  CHECK(dependencies.revision() == dependencies_revision + 1);
  dependencies.clear_diags();
  CHECK(dependencies.verify(env));

  joggle::Mod indirect_cycle;
  CHECK(joggle::parse(env,
                      "mod opt\n"
                      "fn main() -> int { return 0 }\n",
                      indirect_cycle, "indirect-cycle.jog"));
  const std::string indirect_cycle_text = joggle::print(indirect_cycle);
  const std::uint64_t indirect_cycle_revision = indirect_cycle.revision();
  CHECK(!indirect_cycle.use(env, "script"));
  CHECK(joggle::print(indirect_cycle) == indirect_cycle_text);
  CHECK(indirect_cycle.uses().empty());
  CHECK(indirect_cycle.revision() == indirect_cycle_revision);
  CHECK(!indirect_cycle.diags().empty());
  CHECK(indirect_cycle.diags().back().message.find("dependency cycle") !=
        std::string::npos);

  joggle::Mod parsed_indirect_cycle;
  CHECK(joggle::parse(env,
                      "mod opt\n"
                      "use script\n"
                      "fn main() -> int { return 0 }\n",
                      parsed_indirect_cycle, "parsed-indirect-cycle.jog"));
  CHECK(!parsed_indirect_cycle.verify(env));
  CHECK(std::any_of(
      parsed_indirect_cycle.diags().begin(),
      parsed_indirect_cycle.diags().end(), [](const joggle::Diag& diag) {
        return diag.message.find("dependency cycle through: script") !=
               std::string::npos;
      }));

  joggle::Mod missing_dependency;
  CHECK(joggle::parse(env,
                      "mod missing_dependency\n"
                      "use missing\n"
                      "fn main() -> int { return 0 }\n",
                      missing_dependency, "missing-dependency.jog"));
  CHECK(!missing_dependency.verify(env));
  CHECK(!missing_dependency.diags().empty());
  CHECK(missing_dependency.diags().front().message.find(
            "dependency module is not loaded") != std::string::npos);

  joggle::Mod self_dependency;
  CHECK(joggle::parse(env,
                      "mod self_dependency\n"
                      "use self_dependency\n"
                      "fn main() -> int { return 0 }\n",
                      self_dependency, "self-dependency.jog"));
  CHECK(!self_dependency.verify(env));
  CHECK(std::any_of(
      self_dependency.diags().begin(), self_dependency.diags().end(),
      [](const joggle::Diag& diag) {
        return diag.message.find("cannot depend on itself") !=
               std::string::npos;
      }));

  joggle::Mod duplicate_dependency;
  CHECK(joggle::parse(env,
                      "mod duplicate_dependency\n"
                      "use base\n"
                      "use base\n"
                      "fn main() -> int { return 0 }\n",
                      duplicate_dependency, "duplicate-dependency.jog"));
  CHECK(!duplicate_dependency.verify(env));
  CHECK(std::any_of(
      duplicate_dependency.diags().begin(), duplicate_dependency.diags().end(),
      [](const joggle::Diag& diag) {
        return diag.message.find("duplicate module dependency") !=
               std::string::npos;
      }));

  constexpr std::string_view inferred_source =
      "mod inferred\n"
      "use tensor\n"
      "fn main(x: i32) -> tensor<f32, [2, 3]> {\n"
      "  var y = opaque(x)\n"
      "  for i in 0..1 {\n"
      "    observe(y)\n"
      "  }\n"
      "  return y\n}\n";
  joggle::Mod inferred;
  joggle::Mod inferred_cpp;
  CHECK(joggle::parse(env, inferred_source, inferred, "inferred.jog"));
  CHECK(joggle::parse(env, inferred_source, inferred_cpp,
                      "inferred-cpp.jog"));
  CHECK(inferred.verify(env) && inferred_cpp.verify(env));
  CHECK(joggle::run(env, "script.type_opaque", inferred));
  joggle::Op opaque_cpp;
  for (joggle::Op op : inferred_cpp.ops())
    if (op.callee() == "opaque")
      opaque_cpp = op;
  const joggle::Ty inferred_type("tensor<f32, [2, 3]>");
  CHECK(opaque_cpp && inferred_cpp.type(opaque_cpp.outs()[0], inferred_type));
  CHECK(inferred.verify(env) && inferred_cpp.verify(env));
  CHECK(joggle::structurally_equal(inferred, inferred_cpp));
  joggle::Attr elements;
  const std::vector<joggle::Attr> opaque_query{joggle::Attr("opaque")};
  CHECK(joggle::query(env, "script.elements", inferred, elements,
                      opaque_query));
  CHECK(elements.integer() == 6);
  const std::string inferred_text = joggle::print(inferred);
  CHECK(inferred_text.find("var y: tensor<f32, [2, 3]> = opaque(x)") !=
        std::string::npos);
  joggle::Mod inferred_roundtrip;
  CHECK(joggle::parse(env, inferred_text, inferred_roundtrip,
                      "inferred-roundtrip.jog"));
  CHECK(inferred_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(inferred, inferred_roundtrip));
  const std::uint64_t inferred_revision = inferred_cpp.revision();
  const std::string inferred_cpp_text = joggle::print(inferred_cpp);
  CHECK(!inferred_cpp.type(opaque_cpp.outs()[0], joggle::Ty("tensor<")));
  CHECK(inferred_cpp.revision() == inferred_revision);
  CHECK(joggle::print(inferred_cpp) == inferred_cpp_text);
  inferred_cpp.clear_diags();
  CHECK(inferred_cpp.verify(env));

  constexpr std::string_view batch_type_source =
      "mod batch_type\n"
      "fn main(n: i32) -> i32 {\n"
      "  var x = seed_x(n)\n"
      "  var y = seed_y(n)\n"
      "  for i in 0..2 { x = step_x(x) }\n"
      "  if n > 0 { y = step_y(y) }\n"
      "  return n\n"
      "}\n";
  joggle::Mod batch_type_cpp;
  joggle::Mod batch_type_script;
  CHECK(joggle::parse(env, batch_type_source, batch_type_cpp,
                      "batch-type-cpp.jog"));
  CHECK(joggle::parse(env, batch_type_source, batch_type_script,
                      "batch-type-script.jog"));
  CHECK(batch_type_cpp.verify(env) && batch_type_script.verify(env));
  joggle::Val seed_x;
  joggle::Val seed_y;
  for (joggle::Op op : batch_type_cpp.ops()) {
    if (op.callee() == "seed_x")
      seed_x = op.outs().front();
    if (op.callee() == "seed_y")
      seed_y = op.outs().front();
  }
  CHECK(seed_x && seed_y);
  const std::vector<joggle::Val> type_values{seed_x, seed_y};
  const std::vector<joggle::Ty> type_targets{joggle::Ty("i32"),
                                              joggle::Ty("f32")};
  const std::uint64_t before_batch_type = batch_type_cpp.revision();
  CHECK(batch_type_cpp.type(type_values, type_targets));
  CHECK(batch_type_cpp.revision() == before_batch_type + 1);
  for (joggle::Val value : batch_type_cpp.vals()) {
    if (value.name() == "x")
      CHECK(value.type() == joggle::Ty("i32"));
    if (value.name() == "y")
      CHECK(value.type() == joggle::Ty("f32"));
  }
  CHECK(joggle::run(env, "script.type_batch", batch_type_script));
  CHECK(batch_type_cpp.verify(env) && batch_type_script.verify(env));
  CHECK(joggle::structurally_equal(batch_type_cpp, batch_type_script));
  const std::string before_conflicting_type = joggle::print(batch_type_cpp);
  const std::uint64_t before_conflicting_type_revision =
      batch_type_cpp.revision();
  const std::vector<joggle::Val> conflicting_values{seed_x, seed_x};
  const std::vector<joggle::Ty> conflicting_types{joggle::Ty("i32"),
                                                   joggle::Ty("i64")};
  CHECK(!batch_type_cpp.type(conflicting_values, conflicting_types));
  CHECK(batch_type_cpp.revision() == before_conflicting_type_revision);
  CHECK(joggle::print(batch_type_cpp) == before_conflicting_type);
  batch_type_cpp.clear_diags();
  const std::vector<joggle::Ty> missing_type{joggle::Ty("i32")};
  CHECK(!batch_type_cpp.type(type_values, missing_type));
  CHECK(batch_type_cpp.revision() == before_conflicting_type_revision);
  CHECK(joggle::print(batch_type_cpp) == before_conflicting_type);
  batch_type_cpp.clear_diags();

  joggle::Mod explicit_type;
  CHECK(joggle::parse(env,
                      "mod explicit_type\n"
                      "fn f(x: i32) -> i32 {\n"
                      "  let y = x + i32(1)\n"
                      "  return y\n"
                      "}\n",
                      explicit_type, "explicit-type.jog"));
  CHECK(explicit_type.verify(env));
  joggle::Val inferred_y;
  for (joggle::Val value : explicit_type.find_fn("f").vals())
    if (value.name() == "y")
      inferred_y = value;
  CHECK(inferred_y);
  CHECK(inferred_y.type() == joggle::Ty("i32"));
  const std::uint64_t before_explicit_type = explicit_type.revision();
  CHECK(explicit_type.type(inferred_y, joggle::Ty("i32")));
  CHECK(explicit_type.revision() == before_explicit_type + 1);
  CHECK(joggle::print(explicit_type).find("let y: i32 = x + i32(1)") !=
        std::string::npos);

  joggle::Mod precedence;
  constexpr std::string_view precedence_source =
      "mod precedence\n"
      "use base\n"
      "fn logic(a: bool, b: bool, c: bool) -> bool {\n"
      "  return a && (b || c)\n}\n"
      "fn math(a: int, b: int, c: int) -> int {\n"
      "  return (a + b) * (c - (a - b))\n}\n";
  CHECK(joggle::parse(env, precedence_source, precedence, "precedence.jog"));
  CHECK(precedence.verify(env));
  const std::string precedence_text = joggle::print(precedence);
  CHECK(precedence_text.find("a && (b || c)") != std::string::npos);
  CHECK(precedence_text.find("(a + b) * (c - (a - b))") !=
        std::string::npos);
  joggle::Mod precedence_roundtrip;
  CHECK(joggle::parse(env, precedence_text, precedence_roundtrip,
                      "precedence-roundtrip.jog"));
  CHECK(precedence_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(precedence, precedence_roundtrip));

  joggle::Mod types;
  constexpr std::string_view type_source =
      "mod types\n"
      "use tensor\n"
      "fn id<E: Ty, S: list<int>>(x: tensor<E, S>) -> tensor<E, S>;\n"
      "fn make<T: Ty>(x: i32) -> T;\n"
      "fn apply(x: tensor<f32, [2, 3]>) -> tensor<f32, [2, 3]> {\n"
      "  return id(x)\n"
      "}\n"
      "fn choose(x: i32) -> f32 {\n"
      "  let y: f32 = make(x)\n"
      "  return y\n"
      "}\n"
      "fn last(xs: list<int>) -> int {\n"
      "  var out = 0\n"
      "  for x in xs { out = x }\n"
      "  return out\n"
      "}\n";
  CHECK(joggle::parse(env, type_source, types, "types.jog"));
  CHECK(types.verify(env));
  const joggle::Val applied =
      types.find_fn("apply").body().ops().back().args().front();
  CHECK(applied.type() == joggle::Ty("tensor<f32, [2, 3]>"));
  const joggle::Op make = types.find_fn("choose").body().ops().front();
  CHECK(make.callee() == "make");
  CHECK(env.resolve(types, make));
  CHECK(make.outs().front().type() == joggle::Ty("f32"));
  const std::vector<joggle::Blk> last_blks = types.find_fn("last").blks();
  CHECK(last_blks.size() == 2);
  CHECK(last_blks[1].args().front().type() == joggle::Ty("int"));
  joggle::Mod types_roundtrip;
  CHECK(joggle::parse(env, joggle::print(types), types_roundtrip,
                      "types-roundtrip.jog"));
  CHECK(types_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(types, types_roundtrip));

  joggle::Mod type_composition;
  constexpr std::string_view type_composition_source =
      "mod type_composition\n"
      "use tensor\n"
      "fn tensor(x: i32) -> i32;\n"
      "fn scalar(x: i32) -> i32 { return tensor(x) }\n"
      "fn shaped(x: tensor<f32, [4]>) -> tensor<f32, [4]> { return x }\n";
  CHECK(joggle::parse(env, type_composition_source, type_composition,
                      "type-composition.jog"));
  CHECK(type_composition.verify(env));
  const joggle::Op scalar_call =
      type_composition.find_fn("scalar").body().ops().front();
  CHECK(env.resolve(type_composition, scalar_call).module() ==
        "type_composition");
  CHECK(type_composition.find_fn("shaped").params().front().type() ==
        joggle::Ty("tensor<f32, [4]>"));

  joggle::Mod structural_arity;
  CHECK(joggle::parse(
      env,
      "mod structural_arity\n"
      "fn keep(x: opaque) -> opaque;\n"
      "fn bad(x: opaque<i32>) -> opaque { return keep(x) }\n",
      structural_arity, "structural-arity.jog"));
  CHECK(!structural_arity.verify(env));
  CHECK(std::any_of(structural_arity.diags().begin(),
                    structural_arity.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.message.find("has type 'opaque<i32>', "
                                               "expected 'opaque'") !=
                             std::string::npos;
                    }));

  joggle::Mod structural_exact;
  CHECK(joggle::parse(
      env,
      "mod structural_exact\n"
      "fn keep(x: opaque<i32>) -> opaque<i32>;\n"
      "fn good(x: opaque<i32>) -> opaque<i32> { return keep(x) }\n",
      structural_exact, "structural-exact.jog"));
  CHECK(structural_exact.verify(env));

  joggle::Mod wrong_shape;
  CHECK(joggle::parse(
      env,
      "mod wrong_shape\nuse tensor\n"
      "fn bad(x: tensor<f32, 4>) -> tensor<f32, 4> { return x }\n",
      wrong_shape, "wrong-shape.jog"));
  CHECK(!wrong_shape.verify(env));
  CHECK(!wrong_shape.diags().empty());
  CHECK(wrong_shape.diags().front().message.find("expected 'list<int>'") !=
        std::string::npos);

  CHECK(rejects_type(
      env, "mod invalid\nfn bad(x: i32<f32>) -> i32<f32> { return x }\n",
      "does not accept type arguments"));
  CHECK(rejects_type(
      env,
      "mod invalid\nfn bad<T: Ty>(x: T<i32>) -> T<i32> { return x }\n",
      "cannot be used as a type constructor"));
  CHECK(rejects_type(
      env, "mod invalid\nfn bad<N: int>(x: N) -> N { return x }\n",
      "has type 'int', expected 'Ty'"));
  CHECK(rejects_type(
      env, "mod invalid\nfn bad(x: [i32]) -> [i32] { return x }\n",
      "has type 'list<Ty>', expected 'Ty'"));
  CHECK(rejects_type(
      env, "mod invalid\nfn bad(x: list) -> list { return x }\n",
      "type 'list' expects 1 argument"));

  joggle::Mod multi;
  constexpr std::string_view multi_source =
      "mod multi\n"
      "fn split(x: i32) -> (i32, bool);\n"
      "fn first(x: i32) -> i32 {\n"
      "  let value: i32, valid: bool = split(x)\n"
      "  return value\n"
      "}\n";
  CHECK(joggle::parse(env, multi_source, multi, "multi.jog"));
  CHECK(multi.verify(env));
  const joggle::Op split = multi.find_fn("first").body().ops().front();
  CHECK(split.outs().size() == 2);
  CHECK(split.outs()[0].type() == joggle::Ty("i32"));
  CHECK(split.outs()[1].type() == joggle::Ty("bool"));
  const std::string multi_text = joggle::print(multi);
  CHECK(multi_text.find("let value: i32, valid: bool = split(x)") !=
        std::string::npos);
  joggle::Mod multi_roundtrip;
  CHECK(joggle::parse(env, multi_text, multi_roundtrip, "multi-roundtrip.jog"));
  CHECK(multi_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(multi, multi_roundtrip));

  joggle::Mod annotated;
  constexpr std::string_view annotated_source =
      "mod annotated\n"
      "fn add(x: i32) -> i32 {\n"
      "  [cost: 3, place: \"edge\"]\n"
      "  let y = x + 1\n"
      "  [trace]\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, annotated_source, annotated, "annotated.jog"));
  CHECK(annotated.verify(env));
  const joggle::Fn annotated_fn = annotated.find_fn("add");
  const std::vector<joggle::Op> annotated_ops = annotated_fn.body().ops();
  CHECK(annotated_ops.size() == 3);
  CHECK(annotated_ops[1].meta("place") &&
        annotated_ops[1].meta("place")->string() == "edge");
  CHECK(annotated_ops.back().meta("trace") &&
        annotated_ops.back().meta("trace")->boolean() == true);
  const std::string before_invalid_meta = joggle::print(annotated);
  const std::uint64_t before_invalid_meta_revision = annotated.revision();
  CHECK(!annotated.set(annotated_fn, "bad key", joggle::Attr(true)));
  CHECK(joggle::print(annotated) == before_invalid_meta);
  CHECK(annotated.revision() == before_invalid_meta_revision);
  annotated.clear_diags();
  CHECK(annotated.set(annotated_ops[1], "layout", joggle::Attr("packed")));
  CHECK(annotated.unset(annotated_ops[1], "cost"));
  CHECK(!annotated_ops[1].meta("cost"));
  CHECK(annotated.set(annotated_fn, "pipeline", joggle::Attr("fast")));
  CHECK(annotated_fn.meta("pipeline") &&
        annotated_fn.meta("pipeline")->string() == "fast");
  const std::string annotated_text = joggle::print(annotated);
  CHECK(annotated_text.find("[layout: \"packed\", place: \"edge\"]") !=
        std::string::npos);
  joggle::Mod annotated_roundtrip;
  CHECK(joggle::parse(env, annotated_text, annotated_roundtrip,
                      "annotated-roundtrip.jog"));
  CHECK(annotated_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(annotated, annotated_roundtrip));

  joggle::Mod invalid_meta_name;
  CHECK(!joggle::parse(env,
                       "mod invalid.meta\n[bad.] fn f() -> ();\n",
                       invalid_meta_name, "invalid-meta-name.jog"));
  CHECK(std::any_of(invalid_meta_name.diags().begin(),
                    invalid_meta_name.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.loc.line == 2 &&
                             diag.message.find("invalid metadata name") !=
                                 std::string::npos;
                    }));

  joggle::Mod selective;
  constexpr std::string_view selective_source =
      "mod selective\n"
      "fn choose(x: i32) -> i32 {\n"
      "  let shared = x + 0\n"
      "  let left = shared + 1\n"
      "  let right = shared + 2\n"
      "  return left + right\n"
      "}\n";
  CHECK(joggle::parse(env, selective_source, selective, "selective.jog"));
  CHECK(selective.verify(env));
  const joggle::Fn choose = selective.find_fn("choose");
  CHECK(choose.ops().size() == choose.body().ops().size());
  CHECK(selective.ops().size() == choose.ops().size());
  const joggle::Val shared = choose.body().ops()[1].outs().front();
  const std::vector<joggle::Op> shared_users = shared.users();
  CHECK(shared_users.size() == 2);
  const std::uint64_t before_replace = selective.revision();
  CHECK(selective.replace(shared, choose.params().front(), shared_users[0]));
  CHECK(selective.revision() == before_replace + 1);
  CHECK(selective.verify(env));
  const std::string selective_text = joggle::print(selective);
  CHECK(selective_text.find("let left = x + 1") != std::string::npos);
  CHECK(selective_text.find("let right = shared + 2") != std::string::npos);

  joggle::Mod duplicate_uses;
  constexpr std::string_view duplicate_uses_source =
      "mod duplicate_uses\n"
      "fn sum(x: i32, y: i32) -> i32 {\n"
      "  let result = x + x\n"
      "  return result\n"
      "}\n";
  CHECK(joggle::parse(env, duplicate_uses_source, duplicate_uses,
                      "duplicate-uses.jog"));
  CHECK(duplicate_uses.verify(env));
  const joggle::Fn duplicate_sum = duplicate_uses.find_fn("sum");
  const joggle::Op addition = duplicate_sum.body().ops()[0];
  const joggle::Op sum_return = duplicate_sum.body().ops()[1];
  CHECK(duplicate_sum.params()[0].users().size() == 2);
  CHECK(duplicate_uses.replace(duplicate_sum.params()[0],
                               duplicate_sum.params()[1]));
  CHECK(duplicate_sum.params()[0].users().empty());
  CHECK(duplicate_sum.params()[1].users().size() == 2);
  CHECK(duplicate_uses.replace(addition.outs()[0],
                               duplicate_sum.params()[1]));
  CHECK(duplicate_uses.erase(addition));
  const std::vector<joggle::Op> remaining_users =
      duplicate_sum.params()[1].users();
  CHECK(remaining_users.size() == 1);
  CHECK(remaining_users.front() == sum_return);
  CHECK(duplicate_uses.verify(env));

  joggle::Mod cloned_loop;
  constexpr std::string_view loop_source =
      "mod looped\n"
      "fn sum(n: int) -> int {\n"
      "  var total = 0\n"
      "  for i in 0..n {\n"
      "    total += i\n"
      "  }\n"
      "  return total\n"
      "}\n";
  CHECK(joggle::parse(env, loop_source, cloned_loop, "looped.jog"));
  CHECK(cloned_loop.verify(env));
  joggle::Op old_loop;
  for (joggle::Op op : cloned_loop.ops())
    if (op.kind() == joggle::Op::Kind::loop)
      old_loop = op;
  CHECK(old_loop && old_loop.outs().size() == 1);
  const joggle::Blk old_body = old_loop.blks().front();
  const joggle::Op copied_loop = cloned_loop.clone(old_loop, old_loop);
  CHECK(copied_loop && copied_loop.blks().size() == 1);
  CHECK(copied_loop.outs().front().name() == old_loop.outs().front().name());
  CHECK(cloned_loop.verify(env));
  joggle::Mod coexisting_loop;
  CHECK(joggle::parse(env, joggle::print(cloned_loop), coexisting_loop,
                      "coexisting-loop.jog"));
  CHECK(coexisting_loop.verify(env));
  CHECK(joggle::structurally_equal(cloned_loop, coexisting_loop));
  CHECK(cloned_loop.replace(old_loop.outs()[0], copied_loop.outs()[0]));
  CHECK(cloned_loop.erase(old_loop));
  CHECK(!old_loop.valid() && !old_body.valid());
  CHECK(cloned_loop.verify(env));
  joggle::Mod cloned_roundtrip;
  CHECK(joggle::parse(env, joggle::print(cloned_loop), cloned_roundtrip,
                      "cloned-roundtrip.jog"));
  CHECK(cloned_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(cloned_loop, cloned_roundtrip));

  joggle::Mod cloned_sequence;
  constexpr std::string_view sequence_source =
      "mod sequence\n"
      "fn twice(x: int) -> int {\n"
      "  let next = x + 1\n"
      "  let result = next * 2\n"
      "  return result\n"
      "}\n";
  CHECK(joggle::parse(env, sequence_source, cloned_sequence, "sequence.jog"));
  CHECK(cloned_sequence.verify(env));
  const std::vector<joggle::Op> sequence_ops =
      cloned_sequence.find_fn("twice").ops();
  CHECK(sequence_ops.size() == 5);
  const std::array sequence_body{sequence_ops[1], sequence_ops[3]};
  const std::uint64_t before_sequence_clone = cloned_sequence.revision();
  const std::vector<joggle::Op> sequence_copy =
      cloned_sequence.clone(sequence_body, sequence_ops.back());
  CHECK(sequence_copy.size() == 2);
  CHECK(cloned_sequence.revision() == before_sequence_clone + 1);
  CHECK(sequence_copy[1].args().front() == sequence_copy[0].outs().front());
  CHECK(cloned_sequence.verify(env));
  const std::array duplicate_sequence{sequence_ops[1], sequence_ops[1]};
  const std::uint64_t before_duplicate_clone = cloned_sequence.revision();
  CHECK(cloned_sequence.clone(duplicate_sequence, sequence_ops.back()).empty());
  CHECK(cloned_sequence.revision() == before_duplicate_clone);
  cloned_sequence.clear_diags();

  joggle::Mod remapped_loop;
  constexpr std::string_view remapped_loop_source =
      "mod remapped\n"
      "fn sum(n: int, left: int, right: int) -> int {\n"
      "  var total = 0\n"
      "  for i in 0..n { total += left + i }\n"
      "  return total + right\n"
      "}\n";
  CHECK(joggle::parse(env, remapped_loop_source, remapped_loop,
                      "remapped-loop.jog"));
  CHECK(remapped_loop.verify(env));
  const joggle::Fn remapped_sum = remapped_loop.find_fn("sum");
  const std::vector<joggle::Val> remapped_params = remapped_sum.params();
  CHECK(remapped_params.size() == 3);
  joggle::Op loop_to_remap;
  for (joggle::Op op : remapped_loop.ops())
    if (op.kind() == joggle::Op::Kind::loop)
      loop_to_remap = op;
  CHECK(loop_to_remap && !loop_to_remap.blks().empty());

  const std::array old_capture{remapped_params[1]};
  const std::array new_capture{remapped_params[2]};
  const std::uint64_t before_remapped_clone = remapped_loop.revision();
  const joggle::Op remapped_copy = remapped_loop.clone(
      loop_to_remap, loop_to_remap, old_capture, new_capture);
  CHECK(remapped_copy);
  CHECK(remapped_loop.revision() == before_remapped_clone + 1);
  bool copy_uses_right = false;
  const auto find_argument = [&](const auto& self, joggle::Op op,
                                 joggle::Val value) -> bool {
    const std::vector<joggle::Val> args = op.args();
    if (std::find(args.begin(), args.end(), value) != args.end())
      return true;
    for (joggle::Blk blk : op.blks())
      for (joggle::Op child : blk.ops())
        if (self(self, child, value))
          return true;
    return false;
  };
  copy_uses_right = find_argument(find_argument, remapped_copy,
                                  remapped_params[2]);
  CHECK(copy_uses_right);
  CHECK(!find_argument(find_argument, remapped_copy, remapped_params[1]));
  CHECK(find_argument(find_argument, loop_to_remap, remapped_params[1]));
  CHECK(remapped_loop.verify(env));

  const std::uint64_t before_rejected_clone = remapped_loop.revision();
  CHECK(!remapped_loop.clone(loop_to_remap, loop_to_remap, old_capture,
                             std::span<const joggle::Val>{}));
  CHECK(remapped_loop.revision() == before_rejected_clone);
  remapped_loop.clear_diags();
  const std::array internal_value{loop_to_remap.blks().front().args().front()};
  CHECK(!remapped_loop.clone(loop_to_remap, loop_to_remap, internal_value,
                             new_capture));
  CHECK(remapped_loop.revision() == before_rejected_clone);
  remapped_loop.clear_diags();
  CHECK(joggle::run(env, "script.reject_internal_clone", remapped_loop));
  CHECK(remapped_loop.revision() == before_rejected_clone);
  remapped_loop.clear_diags();
  const std::array unused_value{remapped_params[0]};
  const joggle::Op extra_mapping_copy = remapped_loop.clone(
      loop_to_remap, loop_to_remap, unused_value, new_capture);
  CHECK(extra_mapping_copy);
  CHECK(remapped_loop.revision() == before_rejected_clone + 1);

  joggle::Mod remapped_roundtrip;
  CHECK(joggle::parse(env, joggle::print(remapped_loop), remapped_roundtrip,
                      "remapped-roundtrip.jog"));
  CHECK(remapped_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(remapped_loop, remapped_roundtrip));

  joggle::Mod sparse_control;
  constexpr std::string_view sparse_control_source =
      "mod sparse.control\n"
      "fn run(n: int, flag: bool) -> int {\n"
      "  var acc = 0\n"
      "  var seen = n\n"
      "  var idle = n\n"
      "  for i in 0..n { acc += seen + i }\n"
      "  if flag { acc += seen }\n"
      "  return acc + idle\n"
      "}\n";
  CHECK(joggle::parse(env, sparse_control_source, sparse_control,
                      "sparse-control.jog"));
  CHECK(sparse_control.verify(env));
  joggle::Op sparse_loop;
  joggle::Op sparse_branch;
  for (joggle::Op op : sparse_control.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      sparse_loop = op;
    if (op.kind() == joggle::Op::Kind::branch)
      sparse_branch = op;
  }
  CHECK(sparse_loop && sparse_loop.args().size() == 2 &&
        sparse_loop.outs().size() == 1 &&
        sparse_loop.blks()[0].args().size() == 2 &&
        sparse_loop.blks()[0].args()[1].name() == "acc");
  CHECK(sparse_branch && sparse_branch.args().size() == 2 &&
        sparse_branch.outs().size() == 1 &&
        sparse_branch.blks()[0].args().size() == 1 &&
        sparse_branch.blks()[1].args().size() == 1 &&
        sparse_branch.blks()[0].args()[0].name() == "acc" &&
        sparse_branch.blks()[1].args()[0].name() == "acc");
  joggle::Mod sparse_roundtrip;
  CHECK(joggle::parse(env, joggle::print(sparse_control), sparse_roundtrip,
                      "sparse-control-roundtrip.jog"));
  CHECK(sparse_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(sparse_control, sparse_roundtrip));

  joggle::Mod cloned_binding;
  constexpr std::string_view binding_source =
      "mod binding\n"
      "fn compute(x: i32) -> i32 {\n"
      "  let value = first(x)\n"
      "  return value\n"
      "}\n";
  CHECK(joggle::parse(env, binding_source, cloned_binding, "binding.jog"));
  CHECK(cloned_binding.verify(env));
  const joggle::Op old_binding =
      cloned_binding.find_fn("compute").body().ops()[0];
  const joggle::Op copied_binding =
      cloned_binding.clone(old_binding, old_binding);
  CHECK(copied_binding && copied_binding.outs().size() == 1);
  CHECK(copied_binding.outs().front().name() !=
        old_binding.outs().front().name());
  CHECK(cloned_binding.verify(env));
  joggle::Mod binding_roundtrip;
  CHECK(joggle::parse(env, joggle::print(cloned_binding), binding_roundtrip,
                      "binding-roundtrip.jog"));
  CHECK(binding_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(cloned_binding, binding_roundtrip));

  joggle::Mod scheduled;
  constexpr std::string_view schedule_source =
      "mod scheduled\n"
      "fn schedule(x: i32) -> i32 {\n"
      "  let a = first(x)\n"
      "  let b = second(x)\n"
      "  return a + b\n"
      "}\n";
  CHECK(joggle::parse(env, schedule_source, scheduled, "scheduled.jog"));
  CHECK(scheduled.verify(env));
  const std::vector<joggle::Op> schedule_ops =
      scheduled.find_fn("schedule").body().ops();
  const std::uint64_t before_move = scheduled.revision();
  CHECK(scheduled.move(schedule_ops[1], schedule_ops[0]));
  CHECK(scheduled.revision() == before_move + 1);
  CHECK(scheduled.verify(env));
  const std::string scheduled_text = joggle::print(scheduled);
  CHECK(scheduled_text.find("let b = second(x)") <
        scheduled_text.find("let a = first(x)"));
  const std::uint64_t before_bad_move = scheduled.revision();
  CHECK(!scheduled.move(schedule_ops[2], schedule_ops[0]));
  CHECK(scheduled.revision() == before_bad_move);
  scheduled.clear_diags();
  CHECK(scheduled.verify(env));

  joggle::Mod loop_motion;
  constexpr std::string_view loop_motion_source =
      "mod loop_motion\n"
      "fn compute(x: i32) -> i32 {\n"
      "  var total: i32 = 0\n"
      "  for i in 0..4 {\n"
      "    let invariant: i32 = pure(x)\n"
      "    total += invariant\n"
      "  }\n"
      "  return total\n"
      "}\n";
  CHECK(joggle::parse(env, loop_motion_source, loop_motion,
                      "loop-motion.jog"));
  CHECK(loop_motion.verify(env));
  joggle::Op motion_loop;
  joggle::Op invariant;
  for (joggle::Op op : loop_motion.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      motion_loop = op;
    if (op.callee() == "pure")
      invariant = op;
  }
  CHECK(motion_loop && invariant);
  CHECK(invariant.blk() != motion_loop.blk());
  CHECK(loop_motion.move(invariant, motion_loop));
  CHECK(invariant.blk() == motion_loop.blk());
  CHECK(loop_motion.verify(env));
  const std::string moved_text = joggle::print(loop_motion);
  CHECK(moved_text.find("let invariant: i32 = pure(x)") <
        moved_text.find("for i in 0..4"));
  joggle::Op inside;
  for (joggle::Op op : motion_loop.blks().front().ops())
    if (op.kind() != joggle::Op::Kind::yield)
      inside = op;
  CHECK(inside);
  const std::uint64_t before_cyclic_move = loop_motion.revision();
  CHECK(!loop_motion.move(motion_loop, inside));
  CHECK(loop_motion.revision() == before_cyclic_move);
  loop_motion.clear_diags();
  CHECK(loop_motion.verify(env));

  joggle::Mod colliding_motion;
  constexpr std::string_view colliding_motion_source =
      "mod colliding_motion\n"
      "fn compute(x: i32) -> i32 {\n"
      "  let invariant: i32 = pure(x)\n"
      "  var total: i32 = invariant\n"
      "  for i in 0..4 {\n"
      "    let invariant: i32 = pure(x)\n"
      "    total += invariant\n"
      "  }\n"
      "  return total\n"
      "}\n";
  CHECK(joggle::parse(env, colliding_motion_source, colliding_motion,
                      "colliding-motion.jog"));
  joggle::Op colliding_loop;
  joggle::Op colliding_inner;
  const joggle::Blk colliding_root =
      colliding_motion.find_fn("compute").body();
  for (joggle::Op op : colliding_motion.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      colliding_loop = op;
    if (op.callee() == "pure" && op.blk() != colliding_root)
      colliding_inner = op;
  }
  CHECK(colliding_loop && colliding_inner);
  const std::uint64_t before_name_collision = colliding_motion.revision();
  CHECK(!colliding_motion.move(colliding_inner, colliding_loop));
  CHECK(colliding_motion.revision() == before_name_collision);
  colliding_motion.clear_diags();
  joggle::Attr colliding_report;
  CHECK(joggle::run(env, "script.hoist_pure", colliding_motion,
                    colliding_report));
  const joggle::Attr::Dict* colliding_summary = colliding_report.dict();
  CHECK(colliding_summary &&
        colliding_summary->at("changed").boolean() == false);
  CHECK(colliding_motion.verify(env));

  joggle::Mod hoisted_motion;
  constexpr std::string_view hoisted_motion_source =
      "mod hoisted_motion\n"
      "fn compute(x: i32) -> i32 {\n"
      "  var total: i32 = 0\n"
      "  for i in 0..4 {\n"
      "    let first: i32 = pure(x)\n"
      "    let second: i32 = pure(first)\n"
      "    let kept: i32 = effect(x)\n"
      "    total += second + kept\n"
      "  }\n"
      "  return total\n"
      "}\n";
  CHECK(joggle::parse(env, hoisted_motion_source, hoisted_motion,
                      "hoisted-motion.jog"));
  joggle::Attr hoist_report;
  CHECK(joggle::run(env, "script.hoist_pure", hoisted_motion,
                    hoist_report));
  CHECK(hoisted_motion.verify(env));
  const joggle::Attr::Dict* hoist_summary = hoist_report.dict();
  CHECK(hoist_summary && hoist_summary->at("changed").boolean() == true);
  joggle::Op hoisted_loop;
  std::vector<joggle::Op> pure_ops;
  joggle::Op effect_op;
  for (joggle::Op op : hoisted_motion.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      hoisted_loop = op;
    if (op.callee() == "pure")
      pure_ops.push_back(op);
    if (op.callee() == "effect")
      effect_op = op;
  }
  CHECK(hoisted_loop && pure_ops.size() == 2 && effect_op);
  CHECK(std::all_of(pure_ops.begin(), pure_ops.end(),
                    [hoisted_loop](joggle::Op op) {
                      return op.blk() == hoisted_loop.blk();
                    }));
  CHECK(effect_op.blk() != hoisted_loop.blk());
  joggle::Attr stable_hoist_report;
  CHECK(joggle::run(env, "script.hoist_pure", hoisted_motion,
                    stable_hoist_report));
  const joggle::Attr::Dict* stable_hoist_summary =
      stable_hoist_report.dict();
  CHECK(stable_hoist_summary &&
        stable_hoist_summary->at("changed").boolean() == false);

  joggle::Mod named_motion;
  CHECK(joggle::parse(env, hoisted_motion_source, named_motion,
                      "named-motion.jog"));
  joggle::Attr named_hoist_report;
  CHECK(joggle::run(env, "script.hoist_named", named_motion,
                    named_hoist_report));
  CHECK(named_motion.verify(env));
  const joggle::Attr::Dict* named_hoist_summary =
      named_hoist_report.dict();
  CHECK(named_hoist_summary &&
        named_hoist_summary->at("changed").boolean() == true);
  for (joggle::Op op : named_motion.ops()) {
    if (op.callee() == "pure")
      CHECK(op.blk() == named_motion.find_fn("compute").body());
    if (op.callee() == "effect")
      CHECK(op.blk() != named_motion.find_fn("compute").body());
  }

  joggle::Mod cloned_branch;
  constexpr std::string_view branch_source =
      "mod branched\n"
      "fn choose(x: i32, flag: bool) -> i32 {\n"
      "  var result = x\n"
      "  if flag {\n"
      "    result += 1\n"
      "  } else {\n"
      "    result += 2\n"
      "  }\n"
      "  return result\n"
      "}\n";
  CHECK(joggle::parse(env, branch_source, cloned_branch, "branched.jog"));
  CHECK(cloned_branch.verify(env));
  joggle::Op old_branch;
  for (joggle::Op op : cloned_branch.ops())
    if (op.kind() == joggle::Op::Kind::branch)
      old_branch = op;
  CHECK(old_branch && old_branch.blks().size() == 2);
  const joggle::Op copied_branch = cloned_branch.clone(old_branch, old_branch);
  CHECK(copied_branch && copied_branch.blks().size() == 2);
  CHECK(copied_branch.outs().front().name() ==
        old_branch.outs().front().name());
  CHECK(cloned_branch.verify(env));
  CHECK(cloned_branch.replace(old_branch.outs()[0],
                              copied_branch.outs()[0]));
  CHECK(cloned_branch.erase(old_branch));
  CHECK(cloned_branch.verify(env));

  joggle::Mod built_control;
  constexpr std::string_view control_seed =
      "mod control\n"
      "fn build(n: int, flag: bool) -> int { return n }\n";
  CHECK(joggle::parse(env, control_seed, built_control, "control.jog"));
  CHECK(built_control.verify(env));
  const joggle::Fn build_fn = built_control.find_fn("build");
  const joggle::Op build_ret = build_fn.body().ops().back();
  const joggle::Val zero =
      built_control.constant(build_ret, joggle::Attr(std::int64_t{0}),
                             joggle::Ty("int"));
  CHECK(zero && built_control.rename(zero, "total"));
  const std::vector<joggle::Val> range_args{zero, build_fn.params().front()};
  const joggle::Val range = built_control.call(
      build_ret, "operator ..", range_args, joggle::Ty("range"));
  CHECK(range);
  const std::vector<std::string> iter_names{"i"};
  const std::vector<joggle::Val> sources{range};
  const std::vector<joggle::Val> carried{zero};
  const joggle::Op built_loop =
      built_control.loop(build_ret, iter_names, sources, carried);
  CHECK(built_loop && built_loop.blks().size() == 1);
  const joggle::Blk built_body = built_loop.blks().front();
  CHECK(built_body.args().size() == 2);
  const joggle::Op built_yield = built_body.ops().back();
  const std::vector<joggle::Val> inner_range_args{zero,
                                                  built_body.args()[0]};
  const joggle::Val inner_range = built_control.call(
      built_yield, "operator ..", inner_range_args, joggle::Ty("range"));
  CHECK(inner_range);
  const std::vector<std::string> inner_names{"j"};
  const std::vector<joggle::Val> inner_sources{inner_range};
  const std::vector<joggle::Val> inner_carried{built_body.args()[1]};
  const joggle::Op inner_loop = built_control.loop(
      built_yield, inner_names, inner_sources, inner_carried);
  CHECK(inner_loop && inner_loop.blks().size() == 1);
  const joggle::Blk inner_body = inner_loop.blks().front();
  const joggle::Op inner_yield = inner_body.ops().back();
  const std::vector<joggle::Val> sum_args{inner_body.args()[1],
                                          inner_body.args()[0]};
  const joggle::Val sum_value = built_control.call(
      inner_yield, "operator +", sum_args, joggle::Ty("int"));
  CHECK(sum_value);
  const joggle::Val sum = built_control.assign(
      inner_yield, inner_body.args()[1], sum_value);
  CHECK(sum && sum.name() == "total" &&
        sum.def().form() == joggle::Op::Form::assign);
  const std::vector<joggle::Val> inner_values{sum};
  CHECK(built_control.args(env, inner_yield, inner_values));
  CHECK(built_control.args(env, built_yield, inner_loop.outs()));
  const joggle::Op built_branch = built_control.branch(
      build_ret, build_fn.params()[1], built_loop.outs());
  CHECK(built_branch && built_branch.blks().size() == 2);
  const joggle::Blk then_blk = built_branch.blks().front();
  const joggle::Op then_yield = then_blk.ops().back();
  const joggle::Val one =
      built_control.constant(then_yield, joggle::Attr(std::int64_t{1}),
                             joggle::Ty("int"));
  CHECK(one && built_control.rename(one, "one"));
  const std::vector<joggle::Val> then_args{then_blk.args().front(), one};
  const joggle::Val increment_value = built_control.call(
      then_yield, "operator +", then_args, joggle::Ty("int"));
  CHECK(increment_value);
  const joggle::Val increment = built_control.assign(
      then_yield, then_blk.args().front(), increment_value);
  CHECK(increment && increment.name() == "total" &&
        increment.def().form() == joggle::Op::Form::assign);
  const std::vector<joggle::Val> then_values{increment};
  CHECK(built_control.args(env, then_yield, then_values));
  CHECK(built_control.args(env, build_ret, built_branch.outs()));
  CHECK(built_control.verify(env));
  const std::string built_control_text = joggle::print(built_control);
  CHECK(built_control_text.find("for i in total..n") != std::string::npos);
  CHECK(built_control_text.find("for j in total..i") != std::string::npos);
  CHECK(built_control_text.find("if flag") != std::string::npos);
  CHECK(built_control_text.find("total = total + j") != std::string::npos);
  joggle::Mod control_roundtrip;
  CHECK(joggle::parse(env, built_control_text, control_roundtrip,
                      "control-roundtrip.jog"));
  CHECK(control_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(built_control, control_roundtrip));

  const std::uint64_t assign_revision = built_control.revision();
  CHECK(!built_control.assign(build_ret, build_fn.params()[0],
                              built_branch.outs()[0]));
  CHECK(built_control.revision() == assign_revision);
  CHECK(joggle::print(built_control) == built_control_text);
  CHECK(!built_control.diags().empty());
  CHECK(built_control.diags().back().message.find("not a mutable binding") !=
        std::string::npos);
  built_control.clear_diags();
  CHECK(!built_control.assign(build_ret, built_branch.outs()[0],
                              build_fn.params()[1]));
  CHECK(built_control.revision() == assign_revision);
  CHECK(joggle::print(built_control) == built_control_text);
  CHECK(!built_control.diags().empty());
  CHECK(built_control.diags().back().message.find("equal target and value") !=
        std::string::npos);
  built_control.clear_diags();
  CHECK(built_control.verify(env));

  joggle::Mod failed_structure;
  CHECK(joggle::parse(env,
                      "mod failed.structure\n"
                      "fn seed(x: int) -> int;\n"
                      "fn main(n: int, flag: bool) -> int {\n"
                      "  let total = seed(n)\n"
                      "  return total\n"
                      "}\n",
                      failed_structure, "failed-structure.jog"));
  CHECK(failed_structure.verify(env));
  const joggle::Fn failed_main = failed_structure.find_fn("main");
  const joggle::Op failed_ret = failed_main.body().ops().back();
  const joggle::Val unnamed = failed_structure.constant(
      failed_ret, joggle::Attr(std::int64_t{0}), joggle::Ty("int"));
  CHECK(unnamed);
  const std::vector<joggle::Val> failed_range_args{
      unnamed, failed_main.params().front()};
  const joggle::Val failed_range = failed_structure.call(
      failed_ret, "operator ..", failed_range_args, joggle::Ty("range"));
  CHECK(failed_range);
  const joggle::Val total = failed_main.body().ops().front().outs().front();
  const std::vector<joggle::Val> invalid_carried{total, unnamed};
  const std::string failed_structure_text = joggle::print(failed_structure);
  const std::uint64_t failed_structure_revision =
      failed_structure.revision();
  const std::vector<std::string> failed_names{"i"};
  const std::vector<joggle::Val> failed_sources{failed_range};
  CHECK(!failed_structure.loop(failed_ret, failed_names, failed_sources,
                               invalid_carried));
  CHECK(joggle::print(failed_structure) == failed_structure_text);
  CHECK(failed_structure.revision() == failed_structure_revision);
  failed_structure.clear_diags();
  CHECK(!failed_structure.branch(failed_ret, failed_main.params()[1],
                                 invalid_carried));
  CHECK(joggle::print(failed_structure) == failed_structure_text);
  CHECK(failed_structure.revision() == failed_structure_revision);
  failed_structure.clear_diags();
  CHECK(failed_structure.verify(env));

  joggle::Mod renamed_control;
  CHECK(joggle::parse(env, built_control_text, renamed_control,
                      "renamed-control.jog"));
  std::vector<joggle::Op> renamed_loops;
  joggle::Op renamed_branch;
  for (joggle::Op op : renamed_control.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      renamed_loops.push_back(op);
    else if (op.kind() == joggle::Op::Kind::branch)
      renamed_branch = op;
  }
  CHECK(renamed_loops.size() == 2 && renamed_branch);
  const std::uint64_t rename_revision = renamed_control.revision();
  CHECK(!renamed_control.rename(renamed_loops[0].blks()[0].args()[0],
                                "return"));
  CHECK(renamed_control.revision() == rename_revision);
  renamed_control.clear_diags();
  CHECK(!renamed_control.rename(renamed_loops[0].blks()[0].args()[0],
                                "total"));
  CHECK(renamed_control.revision() == rename_revision);
  renamed_control.clear_diags();
  CHECK(renamed_control.rename(renamed_loops[0].blks()[0].args()[0],
                               "outer"));
  CHECK(renamed_control.rename(renamed_loops[1].blks()[0].args()[0],
                               "inner"));
  CHECK(renamed_control.rename(renamed_branch.blks()[0].args()[0], "acc"));
  CHECK(renamed_control.verify(env));
  const std::string renamed_control_text = joggle::print(renamed_control);
  CHECK(renamed_control_text.find("var acc: int = 0") != std::string::npos);
  CHECK(renamed_control_text.find("for outer in acc..n") != std::string::npos);
  CHECK(renamed_control_text.find("for inner in acc..outer") !=
        std::string::npos);
  CHECK(renamed_control_text.find("acc + inner") != std::string::npos);
  joggle::Mod renamed_roundtrip;
  CHECK(joggle::parse(env, renamed_control_text, renamed_roundtrip,
                      "renamed-control-roundtrip.jog"));
  CHECK(renamed_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(renamed_control, renamed_roundtrip));

  joggle::Mod wrong_result_type;
  CHECK(joggle::parse(env,
                      "mod wrong_result\n"
                      "fn split(x: i32) -> (i32, bool);\n"
                      "fn bad(x: i32) -> str {\n"
                      "  let value: str, ok: bool = split(x)\n"
                      "  return value\n}\n",
                      wrong_result_type, "wrong-result.jog"));
  CHECK(!wrong_result_type.verify(env));
  CHECK(!wrong_result_type.diags().empty());
  CHECK(wrong_result_type.diags().front().message.find("requires type 'str'") !=
        std::string::npos);

  constexpr std::string_view failed_inference_source =
      "mod failed.inference\n"
      "use base\n"
      "fn main(x: i32) -> i64 {\n"
      "  let y = base.copy(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod failed_inference;
  CHECK(joggle::parse(env, failed_inference_source, failed_inference,
                      "failed-inference.jog"));
  joggle::Val inferred_on_failure;
  for (joggle::Op op : failed_inference.ops()) {
    if (op.kind() == joggle::Op::Kind::call &&
        op.callee() == "base.copy")
      inferred_on_failure = op.outs().front();
  }
  CHECK(inferred_on_failure && inferred_on_failure.type() == joggle::Ty("_"));
  const std::string failed_inference_before = joggle::print(failed_inference);
  const std::uint64_t failed_inference_revision = failed_inference.revision();
  CHECK(!failed_inference.verify(env));
  CHECK(inferred_on_failure.type() == joggle::Ty("_"));
  CHECK(joggle::print(failed_inference) == failed_inference_before);
  CHECK(failed_inference.revision() == failed_inference_revision);
  CHECK(!failed_inference.diags().empty());

  joggle::Mod built_multi;
  CHECK(joggle::parse(env,
                      "mod built\n"
                      "fn split(x: i32) -> (i32, bool);\n"
                      "fn observe(x: i32) -> ();\n"
                      "fn first(x: i32) -> i32 { return x }\n",
                      built_multi, "built-multi.jog"));
  const joggle::Fn built_first = built_multi.find_fn("first");
  const joggle::Op before = built_first.body().ops().back();
  const std::vector<joggle::Val> split_args{built_first.params().front()};
  const std::vector<joggle::Ty> split_types{joggle::Ty("i32"),
                                            joggle::Ty("bool")};
  const std::uint64_t before_calls = built_multi.revision();
  const joggle::Op built_split =
      built_multi.call(before, "split", split_args, split_types);
  CHECK(built_split && built_split.outs().size() == 2);
  CHECK(built_multi.revision() == before_calls + 1);
  const std::vector<joggle::Ty> no_types;
  const joggle::Op observe =
      built_multi.call(before, "observe", split_args, no_types);
  CHECK(observe && observe.outs().empty());
  CHECK(built_multi.rename(built_split.outs()[0], "value"));
  CHECK(built_multi.rename(built_split.outs()[1], "valid"));
  CHECK(built_multi.verify(env));
  CHECK(joggle::print(built_multi)
            .find("let value: i32, valid: bool = split(x)") !=
        std::string::npos);
  CHECK(joggle::print(built_multi).find("observe(x)") != std::string::npos);

  joggle::Fn base_add;
  for (const joggle::Fn fn : env.find_fns("base.operator +"))
    if (fn.params().size() == 2)
      base_add = fn;
  CHECK(base_add);
  joggle::Mod exact_call;
  CHECK(joggle::parse(env,
                      "mod exact.call\n"
                      "use base\n"
                      "fn +(left: i64, right: i64) -> i64 {\n"
                      "  return left - right\n"
                      "}\n"
                      "fn custom(x: i64, y: i64) -> i64 { return x + y }\n"
                      "fn main(x: i64, y: i64) -> i64 { return x }\n",
                      exact_call, "exact-call.jog"));
  CHECK(exact_call.verify(env));
  const joggle::Fn exact_main = exact_call.find_fn("main");
  const joggle::Op exact_return = exact_main.body().ops().back();
  const std::vector<joggle::Val> exact_args{exact_main.params()[0],
                                            exact_main.params()[1]};
  const std::string before_bad_exact = joggle::print(exact_call);
  const std::uint64_t before_bad_exact_revision = exact_call.revision();
  CHECK(!exact_call.call(env, exact_return, base_add, exact_args,
                         joggle::Ty("bool")));
  CHECK(joggle::print(exact_call) == before_bad_exact);
  CHECK(exact_call.revision() == before_bad_exact_revision);
  exact_call.clear_diags();
  const joggle::Val exact_sum = exact_call.call(
      env, exact_return, base_add, exact_args, joggle::Ty("i64"));
  CHECK(exact_sum && env.resolve(exact_call, exact_sum.def()) == base_add);
  CHECK(exact_call.replace(exact_main.params().front(), exact_sum,
                           exact_return));
  CHECK(exact_call.verify(env));
  const std::string exact_text = joggle::print(exact_call);
  CHECK(exact_text.find("base.+(x, y)") != std::string::npos);
  joggle::Mod exact_roundtrip;
  CHECK(joggle::parse(env, exact_text, exact_roundtrip,
                      "exact-call-roundtrip.jog"));
  CHECK(exact_roundtrip.verify(env));
  bool exact_roundtrip_target = false;
  for (const joggle::Op op : exact_roundtrip.ops())
    if (op.callee() == "base.operator +")
      exact_roundtrip_target = env.resolve(exact_roundtrip, op) == base_add;
  CHECK(exact_roundtrip_target);

  joggle::Mod exact_vm;
  CHECK(joggle::parse(env, exact_text, exact_vm, "exact-vm.jog"));
  CHECK(exact_vm.verify(env));
  CHECK(env.load("vm"));
  joggle::Attr exact_vm_prepare;
  CHECK(joggle::run(env, "vm.prepare", exact_vm, exact_vm_prepare));
  joggle::Attr exact_image;
  CHECK(joggle::query(env, "vm.image", exact_vm, exact_image));
  CHECK(exact_image.string() &&
        exact_image.string()->find("add ") != std::string::npos &&
        exact_image.string()->find("sub ") != std::string::npos);

  CHECK(env.load("c"));
  joggle::Attr exact_prepare;
  CHECK(joggle::run(env, "c.prepare", exact_roundtrip, exact_prepare));
  joggle::Attr exact_c;
  CHECK(joggle::query(env, "c.source", exact_roundtrip, exact_c));
  CHECK(exact_c.string() &&
        exact_c.string()->find("(x + y)") != std::string::npos &&
        exact_c.string()->find(
            "return exact_call_operatorZX20ZX2b(x, y);") !=
            std::string::npos);
  joggle::Attr exact_definition;
  joggle::Attr exact_definition_report;
  const std::vector<joggle::Attr> exact_definition_args{
      joggle::Attr("custom")};
  CHECK(joggle::query(env, "c.definition", exact_roundtrip,
                      exact_definition, exact_definition_args,
                      &exact_definition_report));
  CHECK(exact_definition.string() &&
        exact_definition.string()->find("exact_call_custom") !=
            std::string::npos);
  CHECK(!flag(exact_definition_report, "cached") &&
        number(exact_definition_report, "observed_functions") >= 1 &&
        !flag(exact_definition_report, "observed_whole_mod"));
  joggle::Attr exact_definition_cached;
  CHECK(joggle::query(env, "c.definition", exact_roundtrip,
                      exact_definition_cached, exact_definition_args,
                      &exact_definition_report));
  CHECK(flag(exact_definition_report, "cached") &&
        exact_definition_cached == exact_definition);
  joggle::Attr exact_preamble;
  CHECK(joggle::query(env, "c.preamble", exact_roundtrip,
                      exact_preamble));
  CHECK(exact_preamble.string());
  std::string exact_fragments(*exact_preamble.string());
  std::size_t exact_definition_index = 0;
  for (const joggle::Fn fn : exact_roundtrip.fns()) {
    if (!fn.body())
      continue;
    joggle::Attr fragment;
    const std::vector<joggle::Attr> args{
        joggle::Attr(static_cast<std::int64_t>(exact_definition_index++))};
    CHECK(joggle::query(env, "c.definition", exact_roundtrip,
                        fragment, args));
    CHECK(fragment.string());
    exact_fragments += *fragment.string();
  }
  CHECK(exact_fragments == *exact_c.string());

  joggle::Attr exact_head;
  joggle::Attr exact_storage;
  joggle::Attr exact_body;
  joggle::Attr exact_tail;
  CHECK(joggle::query(env, "c.definition_head", exact_roundtrip,
                      exact_head, exact_definition_args));
  CHECK(joggle::query(env, "c.definition_storage", exact_roundtrip,
                      exact_storage, exact_definition_args));
  CHECK(joggle::query(env, "c.definition_body", exact_roundtrip,
                      exact_body, exact_definition_args));
  CHECK(joggle::query(env, "c.definition_tail", exact_roundtrip,
                      exact_tail, exact_definition_args));
  CHECK(exact_head.string() && exact_storage.string() &&
        exact_body.string() && exact_tail.string());
  const std::string exact_segmented_definition =
      std::string(*exact_head.string()) +
      std::string(*exact_storage.string()) +
      std::string(*exact_body.string()) + std::string(*exact_tail.string());
  CHECK(exact_segmented_definition == *exact_definition.string());

  const std::vector<joggle::Attr> exact_binding_args{
      joggle::Attr("custom"), joggle::Attr("bound_custom")};
  CHECK(joggle::run(env, "script.bind_c_target", exact_roundtrip,
                    exact_binding_args));
  joggle::Attr bound_head;
  CHECK(joggle::query(env, "c.definition_head", exact_roundtrip,
                      bound_head, exact_definition_args));
  CHECK(bound_head.string() &&
        bound_head.string()->find("bound_custom(") != std::string::npos);
  joggle::Attr bound_preamble;
  CHECK(joggle::query(env, "c.preamble", exact_roundtrip, bound_preamble));
  CHECK(bound_preamble.string() &&
        bound_preamble.string()->find("bound_custom(") !=
            std::string::npos);
  joggle::Attr bound_fragments;
  CHECK(joggle::query(env, "c.definition_binding", exact_roundtrip,
                      bound_fragments, exact_definition_args));
  CHECK(bound_fragments.list() && bound_fragments.list()->size() == 2 &&
        bound_fragments.list()->at(0).string() &&
        bound_fragments.list()->at(1).string());
  const std::string bound_declaration(
      *bound_fragments.list()->at(0).string());
  const std::string bound_definition_head(
      *bound_fragments.list()->at(1).string());
  CHECK(bound_declaration.ends_with(";\n") &&
        bound_definition_head.ends_with(" {\n") &&
        bound_declaration.substr(0, bound_declaration.size() - 2) ==
            bound_definition_head.substr(0,
                                         bound_definition_head.size() - 3));

  joggle::Attr exact_direct;
  joggle::Attr exact_results;
  CHECK(joggle::query(env, "c.definition_direct", exact_roundtrip,
                      exact_direct, exact_definition_args));
  CHECK(joggle::query(env, "c.definition_results", exact_roundtrip,
                      exact_results, exact_definition_args));
  CHECK(exact_direct.list() && exact_results.list());
  std::string exact_chunked_body;
  const std::size_t exact_body_ops =
      exact_roundtrip.find_fn("custom").body().ops().size();
  for (std::size_t index = 0; index < exact_body_ops; ++index) {
    const std::vector<joggle::Attr> chunk_args{
        joggle::Attr("custom"),
        joggle::Attr(static_cast<std::int64_t>(index)),
        joggle::Attr(std::int64_t{1}), exact_direct, exact_results};
    joggle::Attr chunk;
    CHECK(joggle::query(env, "c.definition_body_chunk", exact_roundtrip,
                        chunk, chunk_args));
    CHECK(chunk.string());
    exact_chunked_body += *chunk.string();
  }
  CHECK(exact_chunked_body == *exact_body.string());

  joggle::Mod nested_emission;
  CHECK(joggle::parse(
      env,
      "mod nested_emission\n"
      "use base\n"
      "fn main() -> i64 {\n"
      "  var total: i64 = i64(0)\n"
      "  for i in 0..4 {\n"
      "    if i >= 2 { total += i64(2) } else { total += i64(1) }\n"
      "  }\n"
      "  return total\n"
      "}\n",
      nested_emission, "nested-emission.jog"));
  CHECK(nested_emission.verify(env));
  const joggle::Fn nested_main = nested_emission.find_fn("main");
  CHECK(nested_main && nested_main.body());
  const std::vector<joggle::Attr> nested_name_args{joggle::Attr("main")};
  joggle::Attr nested_direct;
  joggle::Attr nested_results;
  joggle::Attr nested_body;
  CHECK(joggle::query(env, "c.definition_direct", nested_emission,
                      nested_direct, nested_name_args));
  CHECK(joggle::query(env, "c.definition_results", nested_emission,
                      nested_results, nested_name_args));
  CHECK(joggle::query(env, "c.definition_body", nested_emission,
                      nested_body, nested_name_args));
  CHECK(nested_direct.list() && nested_results.list() && nested_body.string());
  std::size_t nested_blocks_checked = 0;
  for (const joggle::Op owner : nested_main.ops()) {
    if (owner.blks().empty())
      continue;
    const std::vector<std::size_t> owner_path = nested_emission.path(owner);
    CHECK(!owner_path.empty());
    joggle::Attr::List encoded_path;
    for (const std::size_t index : owner_path)
      encoded_path.emplace_back(static_cast<std::int64_t>(index));
    for (std::size_t child = 0; child < owner.blks().size(); ++child) {
      const std::size_t count = owner.blks()[child].ops().size();
      if (count == 0)
        continue;
      const std::vector<joggle::Attr> nested_args{
          joggle::Attr("main"), joggle::Attr(encoded_path),
          joggle::Attr(static_cast<std::int64_t>(child)),
          joggle::Attr(std::int64_t{0}),
          joggle::Attr(static_cast<std::int64_t>(count)), nested_direct,
          nested_results};
      joggle::Attr fragment;
      joggle::Attr fragment_report;
      CHECK(joggle::query(env, "c.definition_nested_chunk", nested_emission,
                          fragment, nested_args, &fragment_report));
      CHECK(fragment.string() && !fragment.string()->empty());
      CHECK(nested_body.string()->find(*fragment.string()) !=
            std::string_view::npos);
      CHECK(!flag(fragment_report, "observed_whole_mod"));
      const std::vector<joggle::Attr> partition_args{
          joggle::Attr("main"), joggle::Attr(std::int64_t{0}),
          joggle::Attr(static_cast<std::int64_t>(
              nested_main.body().ops().size())),
          joggle::Attr(encoded_path),
          joggle::Attr(static_cast<std::int64_t>(child)),
          joggle::Attr(std::int64_t{0}),
          joggle::Attr(static_cast<std::int64_t>(count)), nested_direct,
          nested_results};
      joggle::Attr partition;
      joggle::Attr partition_report;
      CHECK(joggle::query(env, "c.definition_nested_partition",
                          nested_emission, partition, partition_args,
                          &partition_report));
      CHECK(partition.list() && partition.list()->size() == 3);
      std::string partitioned;
      for (const joggle::Attr& piece : *partition.list()) {
        CHECK(piece.string());
        partitioned += *piece.string();
      }
      CHECK(partitioned == *nested_body.string());
      CHECK(!flag(partition_report, "observed_whole_mod"));
      ++nested_blocks_checked;
    }
  }
  CHECK(nested_blocks_checked >= 1);

  // Function-granular C definitions must invalidate at the same boundary as
  // their owning IR function. An unrelated body edit retains the cached
  // definition; an edit to the observed function rebuilds it.
  joggle::Mod definition_scope;
  CHECK(joggle::parse(env,
                      "mod definition_scope\n"
                      "fn left() -> int { return 1 }\n"
                      "fn right() -> int { return 2 }\n",
                      definition_scope, "definition-scope.jog"));
  CHECK(definition_scope.verify(env));
  const std::vector<joggle::Attr> definition_left_args{
      joggle::Attr("left")};
  joggle::Attr left_definition;
  joggle::Attr definition_report;
  CHECK(joggle::query(env, "c.definition", definition_scope,
                      left_definition, definition_left_args,
                      &definition_report));
  CHECK(!flag(definition_report, "cached") &&
        number(definition_report, "observed_functions") == 1 &&
        flag(definition_report, "observed_structure") &&
        !flag(definition_report, "observed_whole_mod"));
  joggle::Op definition_left_constant;
  joggle::Op definition_right_constant;
  for (const joggle::Op op : definition_scope.ops()) {
    if (op.kind() != joggle::Op::Kind::constant)
      continue;
    const joggle::Fn owner = op.blk().fn();
    if (owner.name() == "left")
      definition_left_constant = op;
    else if (owner.name() == "right")
      definition_right_constant = op;
  }
  CHECK(definition_left_constant && definition_right_constant);
  CHECK(definition_scope.replace(definition_right_constant,
                                 joggle::Attr(std::int64_t{3})));
  joggle::Attr retained_left_definition;
  CHECK(joggle::query(env, "c.definition", definition_scope,
                      retained_left_definition, definition_left_args,
                      &definition_report));
  CHECK(flag(definition_report, "cached") &&
        retained_left_definition == left_definition);
  CHECK(definition_scope.replace(definition_left_constant,
                                 joggle::Attr(std::int64_t{4})));
  joggle::Attr rebuilt_left_definition;
  CHECK(joggle::query(env, "c.definition", definition_scope,
                      rebuilt_left_definition, definition_left_args,
                      &definition_report));
  CHECK(!flag(definition_report, "cached") &&
        string_field(definition_report, "miss") == "function_revision" &&
        rebuilt_left_definition != left_definition);

  joggle::Mod overloaded;
  constexpr std::string_view overload_source =
      "mod overloads\n"
      "fn choose(x: int) -> str;\n"
      "fn choose(x: str) -> int;\n"
      "fn select<T>(x: T) -> int;\n"
      "fn select(x: str) -> str;\n"
      "fn +<T>(a: T, b: T) -> T;\n"
      "fn from_int(x: int) -> str { return choose(x) }\n"
      "fn from_str(x: str) -> int { return choose(x) }\n"
      "fn specific(x: str) -> str { return select(x) }\n"
      "fn plus(a: i32, b: i32) -> i32 { return a + b }\n";
  CHECK(joggle::parse(env, overload_source, overloaded, "overloads.jog"));
  CHECK(overloaded.verify(env));
  CHECK(!overloaded.find_fn("choose"));
  CHECK(overloaded.find_fns("choose").size() == 2);
  CHECK(overloaded.find_fn("from_int")
            .body()
            .ops()
            .back()
            .args()
            .front()
            .type() == joggle::Ty("str"));
  CHECK(overloaded.find_fn("specific")
            .body()
            .ops()
            .back()
            .args()
            .front()
            .type() == joggle::Ty("str"));
  joggle::Mod overload_roundtrip;
  CHECK(joggle::parse(env, joggle::print(overloaded), overload_roundtrip,
                      "overloads-roundtrip.jog"));
  CHECK(overload_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(overloaded, overload_roundtrip));

  joggle::Mod dependent_overload;
  constexpr std::string_view dependent_overload_source =
      "mod dependent.overload\n"
      "fn root(x: f32) -> f32;\n"
      "fn root(x: f64) -> f64;\n"
      "fn apply<E: Ty>(x: E) -> E { return root(x) }\n"
      "fn main(x: f32) -> f32 { return apply(x) }\n";
  CHECK(joggle::parse(env, dependent_overload_source, dependent_overload,
                      "dependent-overload.jog"));
  CHECK(dependent_overload.verify(env));
  const joggle::Fn dependent_apply = dependent_overload.find_fn("apply");
  CHECK(dependent_apply &&
        !env.resolve(dependent_overload,
                     dependent_apply.body().ops().front()));
  const joggle::Fn apply_f32 = dependent_overload.clone(
      env, dependent_apply, "apply_f32",
      std::vector<joggle::Ty>{joggle::Ty("f32")});
  CHECK(apply_f32 && apply_f32.generics().empty());
  CHECK(apply_f32.body().ops().front().outs().front().type() ==
        joggle::Ty("f32"));
  const joggle::Fn selected_root = env.resolve(
      dependent_overload, apply_f32.body().ops().front());
  CHECK(selected_root &&
        selected_root.params().front().type() == joggle::Ty("f32"));
  CHECK(dependent_overload.verify(env));
  joggle::Mod dependent_roundtrip;
  CHECK(joggle::parse(env, joggle::print(dependent_overload),
                      dependent_roundtrip, "dependent-roundtrip.jog"));
  CHECK(dependent_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(dependent_overload,
                                   dependent_roundtrip));

  CHECK(env.load("number"));
  CHECK(rejects_type(
      env,
      "mod missing.number\n"
      "fn bad(x: qreal<8>) -> qreal<8> { return x }\n",
      "requires 'use number'"));
  CHECK(rejects_type(
      env,
      "mod wrong.number\nuse number\n"
      "fn bad(x: qreal<f32>) -> qreal<f32> { return x }\n",
      "expected 'int'"));
  CHECK(rejects_type(
      env,
      "mod wrong.number\nuse number\n"
      "fn bad(x: qreal<8, 16>) -> qreal<8, 16> { return x }\n",
      "expects 1 type argument"));
  joggle::Mod trim_number;
  CHECK(joggle::parse(env,
                      "mod trim.number\n"
                      "use math\n"
                      "use number\n"
                      "fn keep(x: qreal<8>) -> qreal<8> { return x }\n",
                      trim_number, "trim-number.jog"));
  CHECK(trim_number.verify(env));
  const std::uint64_t before_trim_number = trim_number.revision();
  CHECK(trim_number.trim(env));
  CHECK(trim_number.revision() == before_trim_number + 1);
  CHECK(trim_number.uses() == std::vector<std::string>{"number"});
  CHECK(trim_number.verify(env));
  joggle::Mod custom_number;
  constexpr std::string_view custom_number_source =
      "mod custom.number\n"
      "use math\n"
      "use number\n"
      "fn root<E: Ty>(x: E) -> E { return sqrt(x) }\n"
      "fn main(x: qreal<8>) -> qreal<8> { return root(x) }\n";
  CHECK(joggle::parse(env, custom_number_source, custom_number,
                      "custom-number.jog"));
  CHECK(custom_number.verify(env));
  const joggle::Fn generic_root = custom_number.find_fn("root");
  CHECK(generic_root &&
        !env.resolve(custom_number, generic_root.body().ops().front()));
  const joggle::Fn qreal_root = custom_number.clone(
      env, generic_root, "qreal_root",
      std::vector<joggle::Ty>{joggle::Ty("qreal<8>")});
  CHECK(qreal_root && custom_number.verify(env));
  const joggle::Fn number_sqrt =
      env.resolve(custom_number, qreal_root.body().ops().front());
  CHECK(number_sqrt && number_sqrt.module() == "number" &&
        number_sqrt.name() == "sqrt");

  joggle::Mod invalid_dependent;
  CHECK(joggle::parse(env,
                      "mod invalid.dependent\n"
                      "fn root(x: f32) -> f32;\n"
                      "fn root(x: f64) -> f64;\n"
                      "fn bad(x: i32) -> i32 { return root(x) }\n",
                      invalid_dependent, "invalid-dependent.jog"));
  CHECK(!invalid_dependent.verify(env));
  CHECK(!invalid_dependent.diags().empty());
  CHECK(invalid_dependent.diags().front().message.find("no overload") !=
        std::string::npos);

  joggle::Mod intrinsic_casts;
  constexpr std::string_view intrinsic_cast_source =
      "mod intrinsic.casts\n"
      "use tensor\n"
      "fn average(x: tensor<f32, [1]>) -> tensor<f32, [1]> {\n"
      "  var out = tensor<f32, [1]>(f32(0))\n"
      "  for i in 0..1 {\n"
      "    var sum = f32(0)\n"
      "    sum += x[i]\n"
      "    out[i] = sum / f32(i32(1))\n"
      "  }\n"
      "  return out\n"
      "}\n";
  CHECK(joggle::parse(env, intrinsic_cast_source, intrinsic_casts,
                      "intrinsic-casts.jog"));
  CHECK(intrinsic_casts.verify(env));
  for (joggle::Op op : intrinsic_casts.ops()) {
    if (op.callee() == "f32")
      CHECK(op.outs().size() == 1 &&
            op.outs().front().type() == joggle::Ty("f32"));
    if (op.callee() == "i32")
      CHECK(op.outs().size() == 1 &&
            op.outs().front().type() == joggle::Ty("i32"));
  }
  joggle::Mod intrinsic_casts_roundtrip;
  CHECK(joggle::parse(env, joggle::print(intrinsic_casts),
                      intrinsic_casts_roundtrip,
                      "intrinsic-casts-roundtrip.jog"));
  CHECK(intrinsic_casts_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(intrinsic_casts, intrinsic_casts_roundtrip));

  constexpr std::string_view result_context_source =
      "mod result_context\n"
      "fn choose<T: Ty>(x: i32) -> T;\n"
      "fn choose(x: i32) -> i32;\n"
      "fn use(x: i32) -> f32 { return choose(x) }\n";
  constexpr std::string_view reversed_result_context_source =
      "mod reversed_result_context\n"
      "fn choose(x: i32) -> i32;\n"
      "fn choose<T: Ty>(x: i32) -> T;\n"
      "fn use(x: i32) -> f32 { return choose(x) }\n";
  for (const auto& [source, file] :
       {std::pair{result_context_source, "result-context.jog"},
        std::pair{reversed_result_context_source,
                  "reversed-result-context.jog"}}) {
    joggle::Mod result_context;
    CHECK(joggle::parse(env, source, result_context, file));
    CHECK(result_context.verify(env));
    const joggle::Val returned =
        result_context.find_fn("use").body().ops().back().args().front();
    CHECK(returned.type() == joggle::Ty("f32"));
  }

  joggle::Mod ambiguous;
  constexpr std::string_view ambiguous_source =
      "mod ambiguous\n"
      "fn pick<A>(x: pair<A, i32>) -> int;\n"
      "fn pick<B>(x: pair<i32, B>) -> int;\n"
      "fn use(x: pair<i32, i32>) -> int { return pick(x) }\n";
  CHECK(joggle::parse(env, ambiguous_source, ambiguous, "ambiguous.jog"));
  CHECK(!ambiguous.verify(env));
  CHECK(!ambiguous.diags().empty());
  CHECK(ambiguous.diags().front().message.find("ambiguous") !=
        std::string::npos);

  joggle::Mod duplicate_overload;
  CHECK(!joggle::parse(env,
                       "mod duplicate\n"
                       "fn same(x: int) -> int;\n"
                       "fn same(x: int) -> str;\n",
                       duplicate_overload, "duplicate-overload.jog"));
  CHECK(!duplicate_overload.diags().empty());

  joggle::Mod duplicate_generic;
  CHECK(!joggle::parse(env,
                       "mod duplicate\n"
                       "fn same<T>(x: T) -> int;\n"
                       "fn same<U>(x: U) -> int;\n",
                       duplicate_generic, "duplicate-generic.jog"));
  CHECK(!duplicate_generic.diags().empty());

  joggle::Mod intrinsic_generic;
  CHECK(!joggle::parse(env,
                       "mod intrinsic.generic\n"
                       "fn bad<i32: Ty>(x: i32) -> i32 { return x }\n",
                       intrinsic_generic, "intrinsic-generic.jog"));
  CHECK(std::any_of(intrinsic_generic.diags().begin(),
                    intrinsic_generic.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.message.find(
                                 "conflicts with an intrinsic type") !=
                             std::string::npos;
                    }));

  joggle::Mod qualified_binding;
  CHECK(!joggle::parse(env,
                       "mod invalid.binding\n"
                       "fn bad(x.y: i32) -> i32 { return x.y }\n",
                       qualified_binding, "qualified-binding.jog"));
  CHECK(std::any_of(qualified_binding.diags().begin(),
                    qualified_binding.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.message.find("invalid parameter name") !=
                             std::string::npos;
                    }));

  joggle::Mod reserved_binding;
  CHECK(!joggle::parse(env,
                       "mod invalid.binding\n"
                       "fn bad(x: i32) -> i32 { let true = x return x }\n",
                       reserved_binding, "reserved-binding.jog"));
  CHECK(std::any_of(reserved_binding.diags().begin(),
                    reserved_binding.diags().end(),
                    [](const joggle::Diag& diag) {
                      return diag.message.find("invalid binding name") !=
                             std::string::npos;
                    }));

  joggle::Mod stringly_builder;
  CHECK(joggle::parse(env,
                      "mod stringly.builder\n"
                      "use ir\n"
                      "fn build(m: Mod, before: Op, x: Val) -> Val {\n"
                      "  return ir.call(m, before, \"copy\", [x], \"i32\")\n"
                      "}\n",
                      stringly_builder, "stringly-builder.jog"));
  CHECK(!stringly_builder.verify(env));
  CHECK(!stringly_builder.diags().empty());
  CHECK(stringly_builder.diags().front().message.find("no overload") !=
        std::string::npos);

  joggle::Mod duplicate_parameter;
  CHECK(!joggle::parse(env,
                       "mod duplicate\n"
                       "fn same<T, T>(x: int) -> int;\n",
                       duplicate_parameter, "duplicate-parameter.jog"));
  CHECK(!duplicate_parameter.diags().empty());

  joggle::Mod wrong_generic_kind;
  CHECK(joggle::parse(env,
                      "mod kinds\n"
                      "fn width<W: int>(x: int) -> int;\n"
                      "fn bad(x: int) -> int { return width<f32>(x) }\n",
                      wrong_generic_kind, "wrong-generic-kind.jog"));
  CHECK(!wrong_generic_kind.verify(env));
  CHECK(!wrong_generic_kind.diags().empty());
  CHECK(wrong_generic_kind.diags().front().message.find("expected 'int'") !=
        std::string::npos);

  joggle::Mod wrong_result;
  CHECK(joggle::parse(env,
                      "mod wrong_result\n"
                      "fn make<T: Ty>() -> T;\n"
                      "fn bad() -> i32 { return make<f32>() }\n",
                      wrong_result, "wrong-result.jog"));
  CHECK(!wrong_result.verify(env));
  CHECK(!wrong_result.diags().empty());
  CHECK(wrong_result.diags().front().message.find(
            "requires type 'i32'") != std::string::npos);

  joggle::Mod wrong_result_count;
  CHECK(joggle::parse(env,
                      "mod wrong_result_count\n"
                      "fn split() -> (i32, bool);\n"
                      "fn bad() -> i32 { return split() }\n",
                      wrong_result_count, "wrong-result-count.jog"));
  CHECK(!wrong_result_count.verify(env));
  CHECK(!wrong_result_count.diags().empty());
  CHECK(wrong_result_count.diags().front().message.find(
            "expects 2 results, got 1") != std::string::npos);

  joggle::Mod statement_calls;
  constexpr std::string_view statement_call_source =
      "mod statement_calls\n"
      "fn observe(x: i32) -> ();\n"
      "fn value(x: i32) -> i32;\n"
      "fn run(x: i32) -> () {\n"
      "  observe(x)\n"
      "  value(x)\n"
      "  return\n"
      "}\n";
  CHECK(joggle::parse(env, statement_call_source, statement_calls,
                      "statement-calls.jog"));
  const std::uint64_t statement_revision = statement_calls.revision();
  const std::vector<joggle::Attr> value_query{joggle::Attr("value")};
  joggle::Attr statement_count;
  joggle::Attr statement_query;
  CHECK(joggle::query(env, "opt.count", statement_calls, statement_count,
                      value_query, &statement_query));
  CHECK(!flag(statement_query, "cached") &&
        string_field(statement_query, "miss") == "cold" &&
        number(statement_query, "observed_functions") == statement_calls.fns().size() &&
        flag(statement_query, "observed_structure") &&
        !flag(statement_query, "observed_whole_mod") &&
        number(statement_query, "execute_ns") > 0 &&
        statement_count.integer() == 1);
  CHECK(joggle::query(env, "opt.count", statement_calls, statement_count,
                      value_query, &statement_query));
  CHECK(flag(statement_query, "cached") &&
        string_field(statement_query, "miss") == "none" &&
        number(statement_query, "execute_ns") == 0);
  CHECK(statement_calls.verify(env));

  joggle::Mod built_statement;
  CHECK(joggle::parse(env,
                      "mod built.statement\n"
                      "fn observe(x: i32) -> ();\n"
                      "fn run(x: i32) -> () { return }\n",
                      built_statement, "built-statement.jog"));
  CHECK(built_statement.verify(env));
  const std::uint64_t built_statement_revision = built_statement.revision();
  CHECK(joggle::run(env, "script.insert_observe", built_statement));
  CHECK(built_statement.revision() == built_statement_revision + 1);
  CHECK(built_statement.verify(env));
  const std::vector<joggle::Op> built_statement_ops =
      built_statement.find_fn("run").body().ops();
  CHECK(built_statement_ops.size() == 2);
  CHECK(built_statement_ops.front().callee() == "observe");
  CHECK(built_statement_ops.front().outs().empty());
  CHECK(statement_calls.revision() == statement_revision + 1);
  CHECK(joggle::query(env, "opt.count", statement_calls, statement_count,
                      value_query, &statement_query));
  CHECK(!flag(statement_query, "cached") &&
        string_field(statement_query, "miss") == "structure_revision" &&
        statement_count.integer() == 1);
  const std::uint64_t verified_statement_revision =
      statement_calls.revision();
  CHECK(statement_calls.verify(env));
  CHECK(statement_calls.revision() == verified_statement_revision);
  const std::vector<joggle::Op> statement_ops =
      statement_calls.find_fn("run").body().ops();
  CHECK(statement_ops.size() == 3);
  CHECK(statement_ops[0].callee() == "observe");
  CHECK(statement_ops[0].outs().empty());
  CHECK(statement_ops[1].callee() == "value");
  CHECK(statement_ops[1].outs().size() == 1);
  CHECK(statement_ops[1].outs().front().type() == joggle::Ty("i32"));
  joggle::Mod statement_roundtrip;
  CHECK(joggle::parse(env, joggle::print(statement_calls), statement_roundtrip,
                      "statement-roundtrip.jog"));
  CHECK(statement_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(statement_calls, statement_roundtrip));

  joggle::Mod incompatible_result;
  CHECK(joggle::parse(env,
                      "mod incompatible_result\n"
                      "fn implementation(x: i32) -> f32;\n"
                      "fn apply(x: i32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      incompatible_result, "incompatible-result.jog"));
  CHECK(incompatible_result.verify(env));
  const joggle::Op source_call =
      incompatible_result.find_fn("apply").body().ops().front();
  CHECK(!env.accepts(source_call,
                     incompatible_result.find_fn("implementation")));
  const std::string before_retarget = joggle::print(incompatible_result);
  const std::uint64_t before_retarget_revision =
      incompatible_result.revision();
  CHECK(!incompatible_result.retarget(env, source_call, "implementation",
                                      source_call.args()));
  CHECK(joggle::print(incompatible_result) == before_retarget);
  CHECK(incompatible_result.revision() == before_retarget_revision);
  CHECK(!incompatible_result.diags().empty());
  CHECK(incompatible_result.diags().back().message.find(
            "does not accept the call signature") != std::string::npos);

  joggle::Mod explicit_match;
  CHECK(joggle::parse(env,
                      "mod explicit_match\n"
                      "fn candidate<T: Ty>(x: T) -> T;\n"
                      "fn incompatible(x: i32) -> i32 {\n"
                      "  let y: i32 = source<f32>(x)\n"
                      "  return y\n"
                      "}\n"
                      "fn compatible(x: i32) -> i32 {\n"
                      "  let y: i32 = source<i32>(x)\n"
                      "  return y\n"
                      "}\n",
                      explicit_match, "explicit-match.jog"));
  CHECK(explicit_match.verify(env));
  const joggle::Fn explicit_candidate = explicit_match.find_fn("candidate");
  CHECK(!env.accepts(explicit_match.find_fn("incompatible").body().ops()[0],
                     explicit_candidate));
  CHECK(env.accepts(explicit_match.find_fn("compatible").body().ops()[0],
                    explicit_candidate));

  joggle::Mod rename_safety;
  CHECK(joggle::parse(env,
                      "mod rename_safety\n"
                      "fn incompatible(x: i32) -> f32;\n"
                      "fn compatible(x: i32) -> i32;\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      rename_safety, "rename-safety.jog"));
  CHECK(rename_safety.verify(env));
  const joggle::Op renamed_call =
      rename_safety.find_fn("main").body().ops()[0];
  const std::string rename_before = joggle::print(rename_safety);
  const std::uint64_t rename_safety_revision = rename_safety.revision();
  CHECK(!rename_safety.rename(env, renamed_call, "bad callee"));
  CHECK(joggle::print(rename_safety) == rename_before);
  CHECK(rename_safety.revision() == rename_safety_revision);
  CHECK(!rename_safety.rename(env, renamed_call, "incompatible"));
  CHECK(joggle::print(rename_safety) == rename_before);
  CHECK(rename_safety.revision() == rename_safety_revision);
  CHECK(rename_safety.rename(env, renamed_call, "compatible"));
  CHECK(rename_safety.verify(env));

  joggle::Mod constant_safety;
  CHECK(joggle::parse(env,
                      "mod constant_safety\n"
                      "fn main() -> int { return 0 }\n",
                      constant_safety, "constant-safety.jog"));
  CHECK(constant_safety.verify(env));
  const joggle::Op constant_ret =
      constant_safety.find_fn("main").body().ops().back();
  const std::string constant_before = joggle::print(constant_safety);
  const std::uint64_t constant_revision = constant_safety.revision();
  CHECK(!constant_safety.call(constant_ret, "bad callee", {},
                              joggle::Ty("int")));
  CHECK(joggle::print(constant_safety) == constant_before);
  CHECK(constant_safety.revision() == constant_revision);
  CHECK(!constant_safety.constant(constant_ret, joggle::Attr("not an int"),
                                  joggle::Ty("i32")));
  CHECK(joggle::print(constant_safety) == constant_before);
  CHECK(constant_safety.revision() == constant_revision);
  const joggle::Attr::List mixed{joggle::Attr(std::int64_t{1}),
                                 joggle::Attr("two")};
  CHECK(!constant_safety.constant(constant_ret, joggle::Attr(mixed),
                                  joggle::Ty("list<int>")));
  CHECK(joggle::print(constant_safety) == constant_before);
  CHECK(constant_safety.revision() == constant_revision);
  CHECK(constant_safety.constant(constant_ret, joggle::Attr("encoded"),
                                 joggle::Ty("format<8>")));
  CHECK(constant_safety.verify(env));

  joggle::Mod replaced_constant;
  CHECK(joggle::parse(env,
                      "mod replaced_constant\n"
                      "use tensor\n"
                      "fn tensor_source() -> tensor<f32, [2]>;\n"
                      "fn main() -> tensor<f32, [2]> {\n"
                      "  let weight: tensor<f32, [2]> = tensor_source()\n"
                      "  return weight\n"
                      "}\n",
                      replaced_constant, "replaced-constant.jog"));
  CHECK(replaced_constant.verify(env));
  const joggle::Op tensor_source_call =
      replaced_constant.find_fn("main").body().ops().front();
  const joggle::Val source_result = tensor_source_call.outs().front();
  const std::uint64_t before_op_replace = replaced_constant.revision();
  const joggle::Attr::Bytes weight_bytes{0, 0, 128, 63, 0, 0, 0, 64};
  CHECK(replaced_constant.replace(tensor_source_call,
                                  joggle::Attr(weight_bytes)));
  CHECK(replaced_constant.revision() == before_op_replace + 1);
  CHECK(tensor_source_call.kind() == joggle::Op::Kind::constant);
  CHECK(tensor_source_call.args().empty() &&
        tensor_source_call.outs().size() == 1);
  CHECK(source_result && source_result.is_const());
  CHECK(source_result.name() == "weight");
  CHECK(source_result.type() == joggle::Ty("tensor<f32, [2]>"));
  const joggle::Attr replaced_payload = source_result.constant();
  CHECK(replaced_payload.bytes() && *replaced_payload.bytes() == weight_bytes);
  CHECK(replaced_constant.verify(env));
  joggle::Mod replaced_constant_roundtrip;
  CHECK(joggle::parse(env, joggle::print(replaced_constant),
                      replaced_constant_roundtrip,
                      "replaced-constant-roundtrip.jog"));
  CHECK(replaced_constant_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(replaced_constant,
                                   replaced_constant_roundtrip));

  joggle::Mod args_safety;
  CHECK(joggle::parse(env,
                      "mod args_safety\n"
                      "fn take(x: i32) -> i32 { return x }\n"
                      "fn apply(x: i32, other: f32) -> i32 {\n"
                      "  return take(x)\n"
                      "}\n"
                      "fn open(x: i32, other: f32) -> i32 {\n"
                      "  let y: i32 = source(x)\n"
                      "  return y\n"
                      "}\n",
                      args_safety, "args-safety.jog"));
  CHECK(args_safety.verify(env));
  const joggle::Fn args_apply = args_safety.find_fn("apply");
  const joggle::Op take_call =
      args_apply.body().ops().back().args().front().def();
  const std::string before_args = joggle::print(args_safety);
  const std::uint64_t before_args_revision = args_safety.revision();
  const std::vector<joggle::Val> no_args;
  const std::vector<joggle::Val> wrong_args{args_apply.params()[1]};
  const std::vector<joggle::Val> same_args{args_apply.params()[0]};
  CHECK(!args_safety.args(env, take_call, no_args));
  CHECK(!args_safety.args(env, take_call, wrong_args));
  CHECK(joggle::print(args_safety) == before_args);
  CHECK(args_safety.revision() == before_args_revision);
  CHECK(args_safety.args(env, take_call, same_args));
  CHECK(args_safety.revision() == before_args_revision);
  const joggle::Fn open_args = args_safety.find_fn("open");
  const joggle::Op open_call = open_args.body().ops().front();
  const std::vector<joggle::Val> open_values{open_args.params()[1]};
  CHECK(args_safety.args(env, open_call, open_values));
  CHECK(args_safety.revision() == before_args_revision + 1);
  CHECK(args_safety.verify(env));

  joggle::Mod replace_safety;
  CHECK(joggle::parse(env,
                      "mod replace_safety\n"
                      "fn consume(x: i32) -> i32;\n"
                      "fn apply() -> i32 {\n"
                      "  let open = source()\n"
                      "  let concrete: f32 = value()\n"
                      "  return consume(open)\n"
                      "}\n",
                      replace_safety, "replace-safety.jog"));
  CHECK(replace_safety.verify(env));
  const std::vector<joggle::Op> replace_ops =
      replace_safety.find_fn("apply").body().ops();
  const joggle::Val open_value = replace_ops[0].outs().front();
  const joggle::Val concrete_value = replace_ops[1].outs().front();
  CHECK(open_value.type() == joggle::Ty("_"));
  CHECK(concrete_value.type() == joggle::Ty("f32"));
  const std::string before_unsafe_replace = joggle::print(replace_safety);
  const std::uint64_t before_unsafe_replace_revision =
      replace_safety.revision();
  CHECK(!replace_safety.replace(open_value, concrete_value));
  CHECK(joggle::print(replace_safety) == before_unsafe_replace);
  CHECK(replace_safety.revision() == before_unsafe_replace_revision);

  joggle::Attr load_count;
  const std::vector<joggle::Attr> choose_query{joggle::Attr("choose")};
  joggle::Attr load_query;
  CHECK(joggle::query(env, "opt.count", overloaded, load_count,
                      choose_query, &load_query));
  CHECK(!flag(load_query, "cached") && load_count.integer() == 2);
  CHECK(joggle::query(env, "opt.count", overloaded, load_count,
                      choose_query, &load_query));
  CHECK(flag(load_query, "cached"));

  CHECK(!env.loaded("sample"));
  CHECK(!env.load("bad"));
  CHECK(!env.loaded("bad"));
  CHECK(!env.loaded("sample"));
  CHECK(!env.bound("sample.ping"));
  CHECK(!env.diags().empty());
  env.clear_diags();
  CHECK(joggle::query(env, "opt.count", overloaded, load_count,
                      choose_query, &load_query));
  CHECK(flag(load_query, "cached") && load_count.integer() == 2);

  CHECK(env.load("sample"));
  CHECK(env.bound("sample.ping"));
  const joggle::Fn ping = env.find_fn("sample.ping");
  CHECK(ping && ping.external());
  CHECK(ping.module() == "sample");
  CHECK(!ping.meta("host"));
  CHECK(ping.meta("role") && ping.meta("role")->string() == "test");
  const std::vector<joggle::Fn> echoes = env.find_fns("sample.echo");
  CHECK(echoes.size() == 2);
  CHECK(!env.find_fn("sample.echo"));
  CHECK(echoes[0].external() && echoes[1].external());
  CHECK(echoes[0].meta().empty() && echoes[1].meta().empty());
  CHECK(env.bound("sample.echo"));
  const std::vector<joggle::Attr> arguments{joggle::Attr(std::int64_t{41})};
  std::vector<joggle::Attr> returns;
  CHECK(env.call("sample.ping", arguments, returns));
  CHECK(returns.size() == 1);
  CHECK(returns[0].integer() == 42);
  CHECK(env.call("sample.wide", arguments, returns));
  CHECK(returns.size() == 1 && returns[0].integer() == 42);
  const std::vector<joggle::Attr> bytes{
      joggle::Attr(joggle::Attr::Bytes{0, 127, 255})};
  CHECK(env.call("sample.echo", bytes, returns));
  CHECK(returns.size() == 1);
  CHECK(returns[0].bytes() && *returns[0].bytes() == *bytes[0].bytes());
  const std::vector<joggle::Attr> text{joggle::Attr("hello")};
  CHECK(env.call("sample.echo", text, returns));
  CHECK(returns.size() == 1 && returns[0].string() == "hello");
  CHECK(env.call("sample.empty", {}, returns));
  CHECK(returns.size() == 1 && returns[0].string() == "");
  const std::vector<joggle::Attr> nothing{joggle::Attr{}};
  CHECK(env.call("sample.nothing", nothing, returns));
  CHECK(returns.size() == 1 && returns[0].empty());
  const std::vector<joggle::Attr> committed_returns = returns;
  CHECK(!env.call("sample.partial", {}, returns));
  CHECK(returns == committed_returns);
  CHECK(!env.diags().empty() &&
        env.diags().back().message.find("did not write every return") !=
            std::string::npos);
  env.clear_diags();
  CHECK(!env.call("sample.produce", arguments, returns));
  CHECK(returns == committed_returns);
  CHECK(!env.diags().empty() &&
        env.diags().back().message.find(
            "return type does not match native declaration") !=
            std::string::npos);
  env.clear_diags();
  joggle::Mod mod;
  CHECK(joggle::parse(env, source.str(), mod, argv[1]));
  CHECK(mod.verify(env));
  CHECK(mod.name() == "test.linear");
  CHECK(mod.uses() == std::vector<std::string>{"tensor"});
  CHECK(mod.fns().size() == 3);

  joggle::Fn matmul = mod.find_fn("matmul");
  CHECK(matmul);
  CHECK(!env.loaded("test.linear"));
  CHECK(env.resolve(matmul, "add_zero") == mod.find_fn("add_zero"));
  CHECK(env.resolve(matmul, "test.linear.add_zero") ==
        mod.find_fn("add_zero"));
  const joggle::Fn tensor_valid = env.resolve(matmul, "tensor.valid");
  CHECK(tensor_valid && tensor_valid.module() == "tensor");
  CHECK(matmul.generics().size() == 4);
  CHECK(matmul.generics()[0].type() == joggle::Ty("Ty"));
  CHECK(matmul.generics()[1].type() == joggle::Ty("int"));
  CHECK(matmul.params().size() == 2);
  CHECK(matmul.blks().size() == 3);
  CHECK(matmul.ops().size() > matmul.body().ops().size());
  CHECK(!matmul.body().op());
  for (joggle::Blk blk : matmul.blks()) {
    if (blk != matmul.body()) {
      const joggle::Op owner = blk.op();
      CHECK(owner);
      CHECK(owner.kind() == joggle::Op::Kind::loop);
      const std::vector<joggle::Blk> children = owner.blks();
      CHECK(std::find(children.begin(), children.end(), blk) != children.end());
    }
  }
  std::size_t matmul_values = matmul.params().size();
  for (joggle::Blk blk : matmul.blks())
    matmul_values += blk.args().size();
  for (joggle::Op op : matmul.ops())
    matmul_values += op.outs().size();
  const std::vector<joggle::Val> reflected_values = matmul.vals();
  const std::vector<joggle::Val> reflected_generics = matmul.generics();
  CHECK(reflected_values.size() == matmul_values);
  CHECK(std::none_of(reflected_values.begin(), reflected_values.end(),
                     [&](joggle::Val value) {
                       return std::find(reflected_generics.begin(),
                                        reflected_generics.end(), value) !=
                              reflected_generics.end();
                     }));
  std::size_t module_values = 0;
  for (joggle::Fn fn : mod.fns())
    module_values += fn.vals().size();
  CHECK(mod.vals().size() == module_values);

  const std::string canonical = joggle::print(mod);
  CHECK(canonical.find("for i in 0..M, j in 0..N") != std::string::npos);
  CHECK(canonical.find("for k in 0..K") != std::string::npos);
  CHECK(canonical.find("sum += a[i, k] * b[k, j]") != std::string::npos);
  CHECK(canonical.find("if flag") != std::string::npos);

  joggle::Mod reparsed;
  CHECK(joggle::parse(env, canonical, reparsed, "canonical.jog"));
  CHECK(reparsed.verify(env));
  CHECK(joggle::structurally_equal(mod, reparsed));

  joggle::Op removed;
  CHECK(fold_add_zero(mod, &removed));
  CHECK(!removed.valid());
  CHECK(mod.verify(env));
  const std::string folded = joggle::print(mod);
  CHECK(folded.find("let y = x + 0") == std::string::npos);
  CHECK(folded.find("return x") != std::string::npos);

  CHECK(env.load("opt"));
  CHECK(env.loaded("ir"));
  joggle::Mod scripted;
  CHECK(joggle::parse(env, source.str(), scripted, argv[1]));
  if (!joggle::run(env, "opt.fold_add_zero", scripted))
    return env.print_diags(stderr);
  const std::string scripted_text = joggle::print(scripted);
  CHECK(scripted_text.find("let y = x + 0") == std::string::npos);
  CHECK(scripted_text.find("return x") != std::string::npos);

  CHECK(env.load("script"));
  joggle::Mod guarded_exposure;
  CHECK(joggle::parse(env, source.str(), guarded_exposure,
                      "guarded-exposure.jog"));
  const std::string guarded_before = joggle::print(guarded_exposure);
  const std::uint64_t guarded_revision = guarded_exposure.revision();
  CHECK(!joggle::run(env, "script.expose_with_mutating_cap",
                     guarded_exposure));
  CHECK(joggle::print(guarded_exposure) == guarded_before);
  CHECK(guarded_exposure.revision() == guarded_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  joggle::Mod reflected;
  CHECK(joggle::parse(env, source.str(), reflected, "matmul-reflection.jog"));
  CHECK(reflected.verify(env));
  joggle::Attr owner_result;
  CHECK(joggle::query(env, "script.owner_probe", reflected, owner_result));
  CHECK(owner_result.boolean() && *owner_result.boolean());
  joggle::Attr reflected_result;
  CHECK(joggle::query(env, "script.reflect_fn", reflected, reflected_result));
  CHECK(reflected_result.boolean() == true);
  joggle::Mod embedded_sequence;
  joggle::Mod untimed_sequence;
  joggle::Mod source_sequence;
  CHECK(joggle::parse(env, source.str(), embedded_sequence, argv[1]));
  CHECK(joggle::parse(env, source.str(), untimed_sequence, argv[1]));
  CHECK(joggle::parse(env, source.str(), source_sequence, argv[1]));
  constexpr std::string_view sequence[]{"opt.fold_add_zero",
                                        "script.mark_add"};
  joggle::Attr sequence_report;
  joggle::Attr sequence_timing;
  CHECK(joggle::run(env, sequence, embedded_sequence, {}, &sequence_report,
                    &sequence_timing));
  const auto* timed_steps = field(sequence_timing, "steps").list();
  CHECK(flag(sequence_timing, "succeeded") &&
        flag(sequence_timing, "structural_snapshot") && timed_steps &&
        timed_steps->size() == std::size(sequence));
  CHECK(std::all_of(timed_steps->begin(), timed_steps->end(),
                    [](const joggle::Attr& step) {
                      const bool counters_ok =
                          !flag(step, "counters_enabled") ||
                          (number(step, "evaluated_ops") != 0 &&
                           ((number(step, "plan_compiles") +
                                     number(step, "plan_hits") != 0 &&
                             number(step, "plan_window_hits") +
                                     number(step, "plan_window_misses") !=
                                 0) ||
                            (number(step, "frame_lookups") != 0 &&
                             number(step, "frame_probes") >=
                                 number(step, "frame_lookups") &&
                             number(step, "frame_writes") != 0 &&
                             number(step, "frame_pool_hits") +
                                     number(step, "frame_pool_misses") !=
                                 0 &&
                             number(step, "frame_growths") != 0 &&
                             number(step, "frame_peak_capacity") != 0)) &&
                           number(step, "dispatch_hits") +
                                   number(step, "dispatch_misses") +
                                   number(step, "plan_direct_operator_links") !=
                               0);
                      return flag(step, "succeeded") && counters_ok &&
                             number(step, "total_ns") >= 0;
                    }));
  joggle::Attr untimed_sequence_report;
  joggle::Attr reused_sequence_timing;
  CHECK(joggle::run(env, sequence, untimed_sequence, {},
                    &untimed_sequence_report, &reused_sequence_timing));
  const auto* reused_steps = field(reused_sequence_timing, "steps").list();
  CHECK(flag(reused_sequence_timing, "succeeded") && reused_steps &&
        reused_steps->size() == std::size(sequence));
  if (flag(reused_steps->front(), "counters_enabled")) {
    std::uint64_t persistent_hits = 0;
    std::uint64_t cache_resets = 0;
    std::uint64_t plan_compiles = 0;
    for (const joggle::Attr& step : *reused_steps) {
      persistent_hits += number(step, "plan_persistent_hits");
      cache_resets += number(step, "plan_cache_resets");
      plan_compiles += number(step, "plan_compiles");
    }
    CHECK(persistent_hits != 0);
    CHECK(cache_resets == 0);
    CHECK(plan_compiles == 0);
  }
  CHECK(joggle::print(untimed_sequence) == joggle::print(embedded_sequence));
  CHECK(untimed_sequence_report == sequence_report);

  // Loading a new mod changes the environment epoch. The next evaluator must
  // discard persistent plans before reuse, then share the rebuilt cache with
  // later steps in the same sequence.
  CHECK(!env.loaded("sat"));
  CHECK(env.load("sat"));
  joggle::Mod epoch_sequence;
  CHECK(joggle::parse(env, source.str(), epoch_sequence, argv[1]));
  joggle::Attr epoch_sequence_report;
  joggle::Attr epoch_sequence_timing;
  CHECK(joggle::run(env, sequence, epoch_sequence, {},
                    &epoch_sequence_report, &epoch_sequence_timing));
  CHECK(joggle::print(epoch_sequence) == joggle::print(embedded_sequence));
  CHECK(epoch_sequence_report == sequence_report);
  const auto* epoch_steps = field(epoch_sequence_timing, "steps").list();
  CHECK(epoch_steps && epoch_steps->size() == std::size(sequence));
  if (flag(epoch_steps->front(), "counters_enabled")) {
    std::uint64_t cache_resets = 0;
    std::uint64_t plan_compiles = 0;
    for (const joggle::Attr& step : *epoch_steps) {
      cache_resets += number(step, "plan_cache_resets");
      plan_compiles += number(step, "plan_compiles");
    }
    CHECK(cache_resets == 1);
    CHECK(plan_compiles != 0);
  }
  CHECK(joggle::run(env, "script.prepare", source_sequence));
  CHECK(joggle::print(embedded_sequence) == joggle::print(source_sequence));
  const joggle::Attr::Dict* sequence_summary = sequence_report.dict();
  CHECK(sequence_summary &&
        sequence_summary->at("changed").boolean() == true);
  const joggle::Attr::List* sequence_steps =
      sequence_summary->at("steps").list();
  CHECK(sequence_steps && sequence_steps->size() == 2);
  const joggle::Attr::Dict* fold_step = sequence_steps->at(0).dict();
  const joggle::Attr::Dict* mark_step = sequence_steps->at(1).dict();
  CHECK(fold_step && mark_step);
  const joggle::Attr::Dict* fold_calls = fold_step->at("calls").dict();
  const joggle::Attr::Dict* mark_calls = mark_step->at("calls").dict();
  CHECK(fold_calls && fold_calls->at("opt.fold_add_zero").integer() >= 1);
  CHECK(mark_calls && mark_calls->at("script.mark_add").integer() >= 1);
  joggle::Mod memo_module;
  CHECK(joggle::parse(env, source.str(), memo_module, argv[1]));
  joggle::Attr memo_report;
  CHECK(joggle::run(env, "script.memo_probe", memo_module, memo_report));
  const joggle::Attr::Dict* memo_summary = memo_report.dict();
  CHECK(memo_summary);
  const joggle::Attr::Dict* memo_cached =
      memo_summary->at("cached").dict();
  CHECK(memo_cached &&
        memo_cached->at("script.memo_sum").integer() == 1);
  joggle::Mod memo_handle_module;
  CHECK(joggle::parse(env, source.str(), memo_handle_module, argv[1]));
  joggle::Attr memo_handle_report;
  CHECK(joggle::run(env, "script.memo_handle_probe", memo_handle_module,
                    memo_handle_report));
  const joggle::Attr::Dict* memo_handle_summary = memo_handle_report.dict();
  CHECK(memo_handle_summary);
  const joggle::Attr::Dict* memo_handle_cached =
      memo_handle_summary->at("cached").dict();
  CHECK(memo_handle_cached &&
        memo_handle_cached->at("script.memo_name").integer() == 1);
  joggle::Mod fresh_rename_module;
  CHECK(joggle::parse(env, source.str(), fresh_rename_module, argv[1]));
  CHECK(joggle::run(env, "script.fresh_rename_probe",
                    fresh_rename_module));
  CHECK(fresh_rename_module.verify(env));
  joggle::Mod failed_sequence;
  CHECK(joggle::parse(env, source.str(), failed_sequence, argv[1]));
  const std::string before_sequence = joggle::print(failed_sequence);
  const std::uint64_t before_sequence_revision = failed_sequence.revision();
  constexpr std::string_view invalid_sequence[]{"opt.fold_add_zero",
                                                "script.bad_entry"};
  joggle::Attr failed_sequence_report;
  joggle::Attr failed_sequence_timing;
  CHECK(!joggle::run(env, invalid_sequence, failed_sequence, {},
                     &failed_sequence_report, &failed_sequence_timing));
  CHECK(failed_sequence_report.empty());
  CHECK(!flag(failed_sequence_timing, "succeeded") &&
        flag(failed_sequence_timing, "structural_snapshot") &&
        field(failed_sequence_timing, "steps").list()->size() == 2 &&
        flag(item(failed_sequence_timing, "steps", 0), "succeeded") &&
        !flag(item(failed_sequence_timing, "steps", 1), "succeeded"));
  CHECK(joggle::print(failed_sequence) == before_sequence);
  CHECK(failed_sequence.revision() == before_sequence_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();

  // A transaction may start with metadata-only edits and discover a
  // structural edit later. The delayed full snapshot must still represent
  // the state before the first step, not the partially edited state.
  joggle::Mod mixed_rollback;
  CHECK(joggle::parse(env, source.str(), mixed_rollback, argv[1]));
  const std::string mixed_before = joggle::print(mixed_rollback);
  const std::uint64_t mixed_revision = mixed_rollback.revision();
  constexpr std::string_view mixed_sequence[]{"script.mark_add",
                                               "opt.fold_add_zero",
                                               "script.bad_entry"};
  joggle::Attr mixed_report;
  joggle::Attr mixed_timing;
  CHECK(!joggle::run(env, mixed_sequence, mixed_rollback, {}, &mixed_report,
                     &mixed_timing));
  CHECK(flag(mixed_timing, "structural_snapshot") &&
        joggle::print(mixed_rollback) == mixed_before &&
        mixed_rollback.revision() == mixed_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();

  // Function revisions are the cache key for executable bodies. A local IR
  // edit advances only its owner; a dependency-set edit is conservatively
  // visible to every function because it can change overload resolution.
  joggle::Mod revision_scope;
  constexpr std::string_view revision_scope_source =
      "mod revision_scope\n"
      "fn left() -> int { return 1 }\n"
      "fn right() -> int { return 2 }\n";
  CHECK(joggle::parse(env, revision_scope_source, revision_scope,
                      "revision-scope.jog"));
  CHECK(revision_scope.verify(env));
  joggle::Fn left = revision_scope.find_fn("left");
  joggle::Fn right = revision_scope.find_fn("right");
  CHECK(left && right);
  const std::uint64_t left_before = left.revision();
  const std::uint64_t right_before = right.revision();
  const std::uint64_t mod_before = revision_scope.revision();
  joggle::Op left_constant;
  for (joggle::Op op : left.ops())
    if (op.kind() == joggle::Op::Kind::constant)
      left_constant = op;
  CHECK(left_constant);
  CHECK(revision_scope.replace(left_constant, joggle::Attr(std::int64_t{3})));
  CHECK(revision_scope.revision() == mod_before + 1);
  CHECK(left.revision() == left_before + 1);
  CHECK(right.revision() == right_before);
  const std::vector<joggle::Attr> left_name{joggle::Attr("left")};
  joggle::Attr observed_fn_revision;
  CHECK(joggle::query(env, "script.fn_revision", revision_scope,
                      observed_fn_revision, left_name));
  CHECK(observed_fn_revision.integer() ==
        static_cast<std::int64_t>(left.revision()));

  // Replacing one expression with a constant removes exactly its operand
  // edges, including duplicate uses, without rebuilding unrelated use lists.
  joggle::Mod constant_replacement;
  CHECK(joggle::parse(
      env,
      "mod constant_replacement\n"
      "fn duplicate(x: int) -> int {\n"
      "  let y: int = pure(x, x)\n"
      "  return y\n"
      "}\n",
      constant_replacement, "constant-replacement.jog"));
  CHECK(constant_replacement.verify(env));
  const joggle::Fn duplicate = constant_replacement.find_fn("duplicate");
  CHECK(duplicate && duplicate.params().size() == 1);
  const joggle::Val duplicate_param = duplicate.params().front();
  joggle::Op duplicate_call;
  for (joggle::Op op : duplicate.ops())
    if (op.kind() == joggle::Op::Kind::call)
      duplicate_call = op;
  CHECK(duplicate_call && duplicate_call.args().size() == 2 &&
        duplicate_param.users().size() == 2);
  CHECK(constant_replacement.replace(
      duplicate_call, joggle::Attr(std::int64_t{7})));
  CHECK(duplicate_call.args().empty() && duplicate_param.users().empty());
  CHECK(constant_replacement.verify(env));

  // A query that only enumerates a function's operations records the exact
  // collection shape. Literal edits in either the same or another function
  // preserve the cached count; a module-wide dependency boundary invalidates
  // the lookup through ir.find.
  joggle::Attr left_op_count;
  joggle::Attr dependency_query;
  CHECK(joggle::query(env, "script.fn_op_count", revision_scope,
                      left_op_count, left_name, &dependency_query));
  CHECK(!flag(dependency_query, "cached") &&
        string_field(dependency_query, "miss") == "cold" &&
        number(dependency_query, "observed_functions") == 0 &&
        number(dependency_query, "observed_collections") == 1 &&
        flag(dependency_query, "observed_structure") &&
        !flag(dependency_query, "observed_whole_mod") &&
        left_op_count.integer() ==
            static_cast<std::int64_t>(left.ops().size()));
  CHECK(joggle::query(env, "script.fn_op_count", revision_scope,
                      left_op_count, left_name, &dependency_query));
  CHECK(flag(dependency_query, "cached"));

  joggle::Attr whole_result;
  joggle::Attr whole_query;
  CHECK(joggle::query(env, "script.local_probe", revision_scope,
                      whole_result, {}, &whole_query));
  CHECK(!flag(whole_query, "cached") && flag(whole_query, "observed_whole_mod") &&
        !flag(whole_query, "observed_structure") &&
        number(whole_query, "observed_functions") == 0);
  CHECK(joggle::query(env, "script.local_probe", revision_scope,
                      whole_result, {}, &whole_query));
  CHECK(flag(whole_query, "cached"));
  joggle::Op right_constant;
  for (joggle::Op op : right.ops())
    if (op.kind() == joggle::Op::Kind::constant)
      right_constant = op;
  CHECK(right_constant);
  const std::uint64_t left_before_meta = left.revision();
  const std::uint64_t right_before_meta = right.revision();
  CHECK(revision_scope.set(right, "test.mark", joggle::Attr(true)));
  CHECK(left.revision() == left_before_meta &&
        right.revision() == right_before_meta + 1);
  CHECK(joggle::query(env, "script.fn_op_count", revision_scope,
                      left_op_count, left_name, &dependency_query));
  CHECK(flag(dependency_query, "cached"));
  CHECK(joggle::query(env, "script.local_probe", revision_scope,
                      whole_result, {}, &whole_query));
  CHECK(!flag(whole_query, "cached") && flag(whole_query, "verification_cached") &&
        string_field(whole_query, "miss") == "whole_revision");
  CHECK(joggle::query(env, "script.local_probe", revision_scope,
                      whole_result, {}, &whole_query));
  CHECK(flag(whole_query, "cached"));
  CHECK(revision_scope.replace(right_constant,
                               joggle::Attr(std::int64_t{5})));
  CHECK(joggle::query(env, "script.fn_op_count", revision_scope,
                      left_op_count, left_name, &dependency_query));
  CHECK(flag(dependency_query, "cached"));
  CHECK(joggle::query(env, "script.local_probe", revision_scope,
                      whole_result, {}, &whole_query));
  CHECK(!flag(whole_query, "cached") &&
        string_field(whole_query, "miss") == "whole_revision");
  CHECK(revision_scope.replace(left_constant, joggle::Attr(std::int64_t{4})));
  CHECK(joggle::query(env, "script.fn_op_count", revision_scope,
                      left_op_count, left_name, &dependency_query));
  CHECK(flag(dependency_query, "cached") &&
        string_field(dependency_query, "miss") == "none");

  // Package enumeration is its own dependency class: it neither observes the
  // whole mod nor aliases every structural edit, and a dependency-set change
  // reports the exact invalidation reason.
  joggle::Attr package_count;
  joggle::Attr package_query;
  CHECK(joggle::query(env, "script.package_count", revision_scope,
                      package_count, {}, &package_query));
  CHECK(!flag(package_query, "cached") && package_count.integer() == 0 &&
        number(package_query, "observed_packages") == 1 &&
        number(package_query, "observed_intrinsics") >= 1 &&
        !flag(package_query, "observed_structure") &&
        !flag(package_query, "observed_whole_mod"));
  CHECK(joggle::query(env, "script.package_count", revision_scope,
                      package_count, {}, &package_query));
  CHECK(flag(package_query, "cached"));
  joggle::ReactiveSchedule package_schedule({"script.observe_packages"});
  joggle::Attr package_schedule_report;
  CHECK(package_schedule.run(env, revision_scope, {},
                             &package_schedule_report));
  CHECK(number(package_schedule_report, "executed_stages") == 1 &&
        field(package_schedule_report, "stages").list()->size() == 1 &&
        number(item(package_schedule_report, "stages", 0),
               "observed_packages") == 1 &&
        number(item(package_schedule_report, "stages", 0),
               "observed_intrinsics") >= 1 &&
        !flag(item(package_schedule_report, "stages", 0),
              "observed_structure") &&
        !flag(item(package_schedule_report, "stages", 0),
              "observed_whole_mod"));
  CHECK(package_schedule.run(env, revision_scope, {},
                             &package_schedule_report));
  CHECK(number(package_schedule_report, "executed_stages") == 0 &&
        number(package_schedule_report, "reused_stages") == 1);
  const std::uint64_t left_local = left.revision();
  const std::uint64_t right_local = right.revision();
  CHECK(revision_scope.use(env, "base"));
  CHECK(left.revision() == left_local + 1);
  CHECK(right.revision() == right_local + 1);
  CHECK(revision_scope.verify(env));
  CHECK(joggle::query(env, "script.fn_op_count", revision_scope,
                      left_op_count, left_name, &dependency_query));
  CHECK(!flag(dependency_query, "cached") &&
        string_field(dependency_query, "miss") == "structure_revision");
  CHECK(joggle::query(env, "script.package_count", revision_scope,
                      package_count, {}, &package_query));
  CHECK(!flag(package_query, "cached") && package_count.integer() == 1 &&
        string_field(package_query, "miss") == "package_dependencies" &&
        number(package_query, "observed_packages") == 1 &&
        !flag(package_query, "observed_structure") &&
        !flag(package_query, "observed_whole_mod"));
  CHECK(package_schedule.run(env, revision_scope, {},
                             &package_schedule_report));
  CHECK(number(package_schedule_report, "executed_stages") == 1 &&
        string_field(item(package_schedule_report, "stages", 0), "miss") ==
            "package_dependencies");

  // Package outputs also propagate within one schedule run. The first stage
  // is selected by target metadata, adds a different dependency, and causes a
  // package-only downstream reader to execute as an upstream invalidation.
  joggle::Mod package_pipeline;
  CHECK(joggle::parse(env, revision_scope_source, package_pipeline,
                      "package-pipeline.jog"));
  CHECK(package_pipeline.verify(env));
  joggle::ReactiveSchedule package_pipeline_schedule(
      {"script.sync_package", "script.observe_packages_named"});
  const std::array<joggle::Attr, 1> package_args{joggle::Attr("left")};
  CHECK(package_pipeline_schedule.run(env, package_pipeline, package_args,
                                      &package_schedule_report));
  CHECK(package_pipeline.uses() == std::vector<std::string>{"alternate"} &&
        number(package_schedule_report, "executed_stages") == 2);
  CHECK(package_pipeline.set(package_pipeline.find_fn("left"),
                             "package.base", joggle::Attr(true)));
  CHECK(package_pipeline_schedule.run(env, package_pipeline, package_args,
                                      &package_schedule_report));
  CHECK(package_pipeline.uses() ==
            (std::vector<std::string>{"alternate", "base"}) &&
        number(package_schedule_report, "executed_stages") == 2 &&
        string_field(item(package_schedule_report, "stages", 0), "miss") ==
            "function_revision" &&
        string_field(item(package_schedule_report, "stages", 1), "miss") ==
            "upstream");

  joggle::Env second_env;
  second_env.path(argv[2]);
  second_env.path(argv[3]);
  CHECK(second_env.load("script"));
  joggle::Attr environment_query;
  CHECK(joggle::query(second_env, "script.fn_op_count", revision_scope,
                      left_op_count, left_name, &environment_query));
  CHECK(!flag(environment_query, "cached") &&
        string_field(environment_query, "miss") == "environment");

  joggle::Mod cleaned;
  constexpr std::string_view clean_source =
      "mod clean\n"
      "fn work(x: i32) -> i32 {\n"
      "  let unused: int = 7\n"
      "  let first: i32 = pure(x)\n"
      "  let same: i32 = pure(x)\n"
      "  let dead: i32 = pure(first)\n"
      "  return same\n}\n";
  CHECK(joggle::parse(env, clean_source, cleaned, "clean.jog"));
  CHECK(cleaned.verify(env));
  const std::vector<joggle::Attr> pure_query{joggle::Attr("pure")};
  joggle::Attr count;
  joggle::Attr query_report;
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query,
                      &query_report));
  CHECK(!flag(query_report, "cached") && count.integer() == 3);
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query,
                      &query_report));
  CHECK(flag(query_report, "cached") && count.integer() == 3);
  const std::string before_bad_query = joggle::print(cleaned);
  const std::uint64_t before_bad_query_revision = cleaned.revision();
  CHECK(!joggle::query(env, "script.mutating_query", cleaned, count,
                       pure_query, &query_report));
  CHECK(joggle::print(cleaned) == before_bad_query);
  CHECK(cleaned.revision() == before_bad_query_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  CHECK(!joggle::query(env, "script.handle_query", cleaned, count));
  CHECK(joggle::print(cleaned) == before_bad_query);
  CHECK(cleaned.revision() == before_bad_query_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  const joggle::Attr::Dict attr_map{
      {"axis", joggle::Attr(std::int64_t{2})},
      {"mode", joggle::Attr("nearest")},
      {"values", joggle::Attr(joggle::Attr::List{
                     joggle::Attr(std::int64_t{2}),
                     joggle::Attr(std::int64_t{3}),
                     joggle::Attr(std::int64_t{5})})}};
  joggle::Attr parsed_attr;
  CHECK(joggle::parse(
      env, R"({"axis": 2, "mode": "nearest", "values": [2, 3, 5]})",
      parsed_attr, "<test-argument>"));
  CHECK(parsed_attr == joggle::Attr(attr_map));
  CHECK(joggle::print(parsed_attr) ==
        R"({"axis": 2, "mode": "nearest", "values": [2, 3, 5]})");
  CHECK(!joggle::parse(env, "{broken", parsed_attr, "<test-argument>"));
  CHECK(parsed_attr.empty() && !env.diags().empty());
  env.clear_diags();
  const std::vector<joggle::Attr> attr_args{joggle::Attr(attr_map)};
  CHECK(joggle::query(env, "script.attr_query", cleaned, count, attr_args));
  CHECK(count.integer() == 2);
  joggle::Attr mode;
  CHECK(joggle::query(env, "script.attr_mode", cleaned, mode, attr_args));
  CHECK(mode.string() == "nearest");
  joggle::Attr keys;
  CHECK(joggle::query(env, "script.attr_keys", cleaned, keys, attr_args));
  CHECK(keys.list() && keys.list()->size() == 3);
  CHECK(joggle::query(env, "script.attr_sum", cleaned, count, attr_args));
  CHECK(count.integer() == 10);
  const std::vector<joggle::Attr> missing_attr{
      joggle::Attr(joggle::Attr::Dict{})};
  CHECK(joggle::query(env, "script.attr_query", cleaned, count,
                       missing_attr));
  CHECK(count.integer() == -1);
  constexpr std::string_view affected_source =
      "mod affected\n"
      "fn work(x: i32) -> i32 {\n"
      "  let a: i32 = pure(x)\n"
      "  let b: i32 = pure(a)\n"
      "  let side: i32 = pure(x)\n"
      "  return join(b, side)\n"
      "}\n";
  joggle::Mod affected;
  CHECK(joggle::parse(env, affected_source, affected, "affected.jog"));
  CHECK(affected.verify(env));
  joggle::Val affected_root;
  joggle::Op unrelated;
  for (joggle::Op op : affected.ops()) {
    const std::vector<joggle::Val> outputs = op.outs();
    if (!outputs.empty() && outputs.front().name() == "a")
      affected_root = outputs.front();
    if (!outputs.empty() && outputs.front().name() == "side")
      unrelated = op;
  }
  CHECK(affected_root && unrelated);
  const std::vector<joggle::Op> affected_ops =
      affected.affected(std::span<const joggle::Val>(&affected_root, 1));
  CHECK(affected_ops.size() == 3 &&
        std::find(affected_ops.begin(), affected_ops.end(), unrelated) ==
            affected_ops.end());
  CHECK(affected.affected(std::span<const joggle::Val>(&affected_root, 1)) ==
        affected_ops);
  CHECK(affected.affected({}).empty());
  const joggle::Val invalid_affected_root;
  CHECK(affected
            .affected(std::span<const joggle::Val>(&invalid_affected_root, 1))
            .empty());
  joggle::Mod foreign_affected;
  CHECK(joggle::parse(env, affected_source, foreign_affected,
                      "foreign-affected.jog"));
  const joggle::Val foreign_affected_root =
      foreign_affected.find_fn("work").params().front();
  CHECK(affected
            .affected(std::span<const joggle::Val>(&foreign_affected_root, 1))
            .empty());
  CHECK(joggle::query(env, "script.affected_count", affected, count));
  CHECK(count.integer() == 3);
  joggle::Attr affected_report;
  joggle::Attr affected_timing;
  CHECK(joggle::run(env, "script.mark_affected", affected, {},
                    &affected_report, &affected_timing));
  CHECK(flag(affected_timing, "succeeded") &&
        field(affected_timing, "steps").list()->size() == 1 &&
        !flag(affected_timing, "structural_snapshot") &&
        flag(item(affected_timing, "steps", 0), "verification_cached"));
  std::size_t affected_marked = 0;
  for (joggle::Val value : affected.vals())
    affected_marked += value.meta("affected") &&
                       value.meta("affected")->boolean() == true;
  const joggle::Attr::Dict* affected_summary = affected_report.dict();
  const joggle::Attr::Dict* affected_calls =
      affected_summary ? affected_summary->at("calls").dict() : nullptr;
  CHECK(affected_marked == 1 && !unrelated.outs().front().meta("affected") &&
        affected_calls &&
        affected_calls->at("script.mark_affected_op").integer() == 3);
  const std::string affected_before_reject = joggle::print(affected);
  const std::uint64_t affected_revision = affected.revision();
  joggle::Attr rejected_affected_timing;
  joggle::Attr rejected_affected_report;
  CHECK(!joggle::run(env, "script.reject_affected", affected, {},
                     &rejected_affected_report, &rejected_affected_timing));
  CHECK(joggle::print(affected) == affected_before_reject &&
        affected.revision() == affected_revision &&
        !flag(rejected_affected_timing, "structural_snapshot") &&
        !env.diags().empty());
  CHECK(affected.affected(std::span<const joggle::Val>(&affected_root, 1)) ==
        affected_ops);
  env.clear_diags();
  constexpr std::string_view nested_affected_source =
      "mod nested_affected\n"
      "fn work(n: int) -> int {\n"
      "  var total = 0\n"
      "  for i in 0..n { total += i }\n"
      "  return total\n"
      "}\n";
  joggle::Mod nested_affected;
  CHECK(joggle::parse(env, nested_affected_source, nested_affected,
                      "nested-affected.jog"));
  CHECK(nested_affected.verify(env));
  const joggle::Fn nested_work = nested_affected.find_fn("work");
  const joggle::Val nested_root = nested_work.params().front();
  joggle::Op affected_loop;
  joggle::Op affected_return;
  for (joggle::Op op : nested_work.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      affected_loop = op;
    if (op.kind() == joggle::Op::Kind::ret)
      affected_return = op;
  }
  CHECK(affected_loop && affected_return);
  const std::vector<joggle::Op> nested_affected_ops =
      nested_affected.affected(std::span<const joggle::Val>(&nested_root, 1));
  CHECK(std::find(nested_affected_ops.begin(), nested_affected_ops.end(),
                  affected_loop) != nested_affected_ops.end());
  for (joggle::Op nested : affected_loop.blks().front().ops())
    CHECK(std::find(nested_affected_ops.begin(), nested_affected_ops.end(),
                    nested) != nested_affected_ops.end());
  CHECK(std::find(nested_affected_ops.begin(), nested_affected_ops.end(),
                  affected_return) != nested_affected_ops.end());
  const joggle::Val grown_root = nested_affected.constant(
      affected_return, joggle::Attr(std::int64_t{7}), joggle::Ty("int"));
  CHECK(grown_root);
  CHECK(nested_affected.replace(affected_return.args().front(), grown_root,
                                affected_return));
  CHECK(nested_affected.verify(env));
  const std::vector<joggle::Op> grown_affected_ops =
      nested_affected.affected(std::span<const joggle::Val>(&grown_root, 1));
  CHECK(grown_affected_ops.size() == 1 &&
        grown_affected_ops.front() == affected_return);
  constexpr std::string_view deep_dead_source =
      "mod deep.dead\n"
      "fn work(x: i32) -> i32 {\n"
      "  let a: i32 = pure(x)\n"
      "  let b: i32 = pure(a)\n"
      "  let c: i32 = pure(b)\n"
      "  let d: i32 = pure(c)\n"
      "  return x\n"
      "}\n";
  joggle::Mod deep_dead;
  CHECK(joggle::parse(env, deep_dead_source, deep_dead, "deep-dead.jog"));
  CHECK(deep_dead.verify(env));
  const std::string deep_before = joggle::print(deep_dead);
  const std::uint64_t deep_revision = deep_dead.revision();
  CHECK(!joggle::run(env, "script.clean_tight", deep_dead));
  CHECK(joggle::print(deep_dead) == deep_before);
  CHECK(deep_dead.revision() == deep_revision);
  CHECK(!env.diags().empty());
  CHECK(env.diags().front().message.find("did not converge") !=
        std::string::npos);
  env.clear_diags();
  CHECK(joggle::run(env, "script.clean_pure", deep_dead));
  CHECK(deep_dead.verify(env));
  CHECK(joggle::query(env, "opt.count", deep_dead, count, pure_query));
  CHECK(count.integer() == 0);

  constexpr std::string_view partial_source =
      "mod partial\n"
      "use base\n"
      "fn work(x: int) -> int {\n"
      "  let first: int = base.copy(x)\n"
      "  let second: int = base.copy(first)\n"
      "  let extent: int = len([2, 3, 5])\n"
      "  return second + extent\n"
      "}\n";
  joggle::Mod partial;
  CHECK(joggle::parse(env, partial_source, partial, "partial.jog"));
  CHECK(partial.verify(env));
  CHECK(joggle::run(env, "opt.fold", partial));
  CHECK(joggle::run(env, "opt.copy", partial));
  CHECK(partial.verify(env));
  const std::vector<joggle::Attr> copy_query{joggle::Attr("base.copy")};
  CHECK(joggle::query(env, "opt.count", partial, count, copy_query));
  CHECK(count.integer() == 0);
  const std::vector<joggle::Attr> len_query{joggle::Attr("len")};
  CHECK(joggle::query(env, "opt.count", partial, count, len_query));
  CHECK(count.integer() == 0);
  const std::string partial_text = joggle::print(partial);
  CHECK(partial_text.find("let extent: int = 3") != std::string::npos);
  CHECK(partial_text.find("return x + extent") != std::string::npos);

  constexpr std::string_view redundant_use_source =
      "mod redundant.use\n"
      "use tensor\n"
      "use nn\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> { return x }\n";
  joggle::Mod redundant_use;
  CHECK(joggle::parse(env, redundant_use_source, redundant_use,
                      "redundant-use.jog"));
  CHECK(redundant_use.verify(env));
  CHECK(joggle::run(env, "script.trim", redundant_use));
  CHECK(redundant_use.verify(env));
  CHECK(redundant_use.uses() == std::vector<std::string>{"tensor"});
  const std::uint64_t trimmed_revision = redundant_use.revision();
  CHECK(joggle::run(env, "script.trim", redundant_use));
  CHECK(redundant_use.revision() == trimmed_revision);

  constexpr std::string_view folded_assignment_source =
      "mod folded_assignment\n"
      "use base\n"
      "fn work() -> int {\n"
      "  var value: int = 1\n"
      "  value = base.copy(2)\n"
      "  value += 3\n"
      "  return value\n"
      "}\n";
  joggle::Mod folded_assignment;
  CHECK(joggle::parse(env, folded_assignment_source, folded_assignment,
                      "folded-assignment.jog"));
  CHECK(folded_assignment.verify(env));
  CHECK(joggle::run(env, "opt.fold", folded_assignment));
  CHECK(folded_assignment.verify(env));
  const std::string folded_assignment_text = joggle::print(folded_assignment);
  CHECK(folded_assignment_text.find("value = 2") != std::string::npos);
  CHECK(folded_assignment_text.find("value += 3") != std::string::npos);
  bool saw_var_form = false;
  bool saw_assign_form = false;
  bool saw_compound_form = false;
  for (joggle::Op op : folded_assignment.ops()) {
    saw_var_form = saw_var_form || op.form() == joggle::Op::Form::var;
    saw_assign_form =
        saw_assign_form || op.form() == joggle::Op::Form::assign;
    saw_compound_form =
        saw_compound_form || op.form() == joggle::Op::Form::compound;
  }
  CHECK(saw_var_form && saw_assign_form && saw_compound_form);
  joggle::Mod folded_assignment_roundtrip;
  CHECK(joggle::parse(env, folded_assignment_text,
                      folded_assignment_roundtrip,
                      "folded-assignment-roundtrip.jog"));
  CHECK(folded_assignment_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(folded_assignment,
                                   folded_assignment_roundtrip));

  constexpr std::string_view folded_controls_source =
      "mod folded_controls\n"
      "use base\n"
      "fn work() -> int {\n"
      "  var total: int = 0\n"
      "  for i in 0..2 {\n"
      "    for j in 0..3 { total += 1 }\n"
      "  }\n"
      "  if true { total += 4 } else { total += 100 }\n"
      "  return total\n"
      "}\n";
  joggle::Mod folded_controls;
  CHECK(joggle::parse(env, folded_controls_source, folded_controls,
                      "folded-controls.jog"));
  CHECK(folded_controls.verify(env));
  const std::string controls_before = joggle::print(folded_controls);
  CHECK(joggle::run(env, "script.fold_controls", folded_controls));
  CHECK(folded_controls.verify(env));
  for (joggle::Op op : folded_controls.ops()) {
    CHECK(op.kind() != joggle::Op::Kind::loop);
    CHECK(op.kind() != joggle::Op::Kind::branch);
  }
  CHECK(joggle::print(folded_controls).find("var total: int = 10") !=
        std::string::npos);

  joggle::Mod rejected_controls;
  CHECK(joggle::parse(env, folded_controls_source, rejected_controls,
                      "rejected-controls.jog"));
  CHECK(!joggle::run(env, "script.reject_mixed_fold", rejected_controls));
  CHECK(joggle::print(rejected_controls) == controls_before);
  CHECK(!env.diags().empty());
  CHECK(env.diags().front().message.find("homogeneous control list") !=
        std::string::npos);
  env.clear_diags();

  constexpr std::string_view specialized_source =
      "mod specialized\n"
      "fn work(x: int, y: int) -> int {\n"
      "  var out = 0\n"
      "  [stage: \"shape\"]\n"
      "  for i in 0..2 {\n"
      "    let values = [x, y]\n"
      "    let enabled = [true, true]\n"
      "    if enabled[i] {\n"
      "      out += values[i]\n"
      "    }\n"
      "  }\n"
      "  return out\n"
      "}\n";
  joggle::Mod specialized;
  CHECK(joggle::parse(env, specialized_source, specialized,
                      "specialized.jog"));
  CHECK(specialized.verify(env));
  const std::vector<joggle::Attr> specialize_args{
      joggle::Attr("stage"), joggle::Attr("shape")};
  CHECK(joggle::run(env, "opt.specialize", specialized, specialize_args));
  CHECK(specialized.verify(env));
  for (joggle::Op op : specialized.ops()) {
    CHECK(op.kind() != joggle::Op::Kind::loop);
    CHECK(op.kind() != joggle::Op::Kind::branch);
    CHECK(op.callee() != "operator []");
  }
  const std::string staged_text = joggle::print(specialized);
  CHECK(staged_text.find("out += x") != std::string::npos);
  CHECK(staged_text.find("out += y") != std::string::npos);
  joggle::Mod specialized_roundtrip;
  CHECK(joggle::parse(env, staged_text, specialized_roundtrip,
                      "specialized-roundtrip.jog"));
  CHECK(specialized_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(specialized, specialized_roundtrip));

  constexpr std::string_view dynamic_specialized_source =
      "mod dynamic_specialized\n"
      "fn work(n: int) -> int {\n"
      "  var out = 0\n"
      "  let values = [n, n]\n"
      "  out += len(values)\n"
      "  [stage: \"shape\"]\n"
      "  for i in 0..n {\n"
      "    out += i\n"
      "  }\n"
      "  return out\n"
      "}\n"
      "fn large() -> int {\n"
      "  var out = 0\n"
      "  [stage: \"shape\"]\n"
      "  for i in 0..257 {\n"
      "    out += i\n"
      "  }\n"
      "  return out\n"
      "}\n";
  joggle::Mod dynamic_specialized;
  CHECK(joggle::parse(env, dynamic_specialized_source, dynamic_specialized,
                      "dynamic-specialized.jog"));
  CHECK(dynamic_specialized.verify(env));
  CHECK(joggle::run(env, "opt.specialize", dynamic_specialized,
                    specialize_args));
  CHECK(dynamic_specialized.verify(env));
  const std::string retained_specialization_text =
      joggle::print(dynamic_specialized);
  CHECK(retained_specialization_text.find("for i in 0..n") !=
        std::string::npos);
  CHECK(retained_specialization_text.find("for i_1 in 0..257") !=
            std::string::npos ||
        retained_specialization_text.find("for i in 0..257") !=
            std::string::npos);
  CHECK(joggle::run(env, "c.prepare", dynamic_specialized));
  CHECK(dynamic_specialized.verify(env));
  joggle::Attr dynamic_specialized_c;
  CHECK(joggle::query(env, "c.source", dynamic_specialized,
                      dynamic_specialized_c));
  CHECK(dynamic_specialized_c.string() &&
        dynamic_specialized_c.string()->find("for (") != std::string::npos);

  constexpr std::string_view generic_entry_source =
      "mod generic_entry\n"
      "use tensor\n"
      "[entry]\n"
      "fn main<N: int>(x: tensor<f32, [N, 2]>) -> "
      "tensor<f32, [N, 2]> {\n"
      "  return x\n"
      "}\n";
  joggle::Mod generic_entry;
  CHECK(joggle::parse(env, generic_entry_source, generic_entry,
                      "generic-entry.jog"));
  CHECK(generic_entry.verify(env));
  const joggle::Attr::List batch_one{joggle::Attr("1")};
  const std::vector<joggle::Attr> instantiate_args{
      joggle::Attr("main"), joggle::Attr(batch_one)};
  CHECK(joggle::run(env, "opt.instantiate", generic_entry,
                    instantiate_args));
  CHECK(generic_entry.verify(env));
  const joggle::Fn entry_instance = generic_entry.find_fn("main");
  CHECK(entry_instance && entry_instance.generics().empty());
  CHECK(entry_instance.params().size() == 1);
  CHECK(entry_instance.params()[0].type() ==
        joggle::Ty("tensor<f32, [1, 2]>"));
  CHECK(entry_instance.returns() ==
        std::vector<joggle::Ty>{joggle::Ty("tensor<f32, [1, 2]>")});
  CHECK(!generic_entry.find_fn("main_template"));

  joggle::Attr clean_report;
  CHECK(joggle::run(env, "script.clean_pure", cleaned, clean_report));
  CHECK(cleaned.verify(env));
  const joggle::Attr::Dict* clean_summary = clean_report.dict();
  CHECK(clean_summary && clean_summary->at("ok").boolean() == true);
  CHECK(clean_summary->at("fn").string() == "script.clean_pure");
  CHECK(clean_summary->at("changed").boolean() == true);
  CHECK(clean_summary->at("reported").boolean() == true);
  CHECK(clean_summary->at("edits").integer() &&
        *clean_summary->at("edits").integer() > 0);
  CHECK(clean_summary->at("steps").list() &&
        !clean_summary->at("steps").list()->empty());
  std::size_t pure_calls = 0;
  std::size_t unused_constants = 0;
  for (joggle::Op op : cleaned.ops())
    pure_calls += op.callee() == "pure" ? 1 : 0;
  for (joggle::Op op : cleaned.ops())
    if (op.kind() == joggle::Op::Kind::constant && !op.outs().empty())
      unused_constants += op.outs().front().name() == "unused" ? 1 : 0;
  CHECK(pure_calls == 1);
  CHECK(unused_constants == 0);
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query,
                      &query_report));
  CHECK(!flag(query_report, "cached") && count.integer() == 1);
  CHECK(joggle::query(env, "opt.count", cleaned, count, pure_query,
                      &query_report));
  CHECK(flag(query_report, "cached") && count.integer() == 1);
  const std::vector<joggle::Attr> join_query{joggle::Attr("join")};
  CHECK(joggle::query(env, "opt.count", cleaned, count, join_query,
                      &query_report));
  CHECK(!flag(query_report, "cached") && count.integer() == 0);
  const std::uint64_t clean_revision = cleaned.revision();
  joggle::Attr stable_report;
  CHECK(joggle::run(env, "script.clean_pure", cleaned, stable_report));
  CHECK(cleaned.revision() == clean_revision);
  const joggle::Attr::Dict* stable_summary = stable_report.dict();
  CHECK(stable_summary && stable_summary->at("changed").boolean() == false);
  CHECK(stable_summary->at("reported").boolean() == false);
  CHECK(stable_summary->at("edits").integer() == 0);
  CHECK(joggle::run(env, "script.selected_entry", cleaned));
  CHECK(!joggle::run(env, "script.bad_entry", cleaned));
  CHECK(!env.diags().empty());
  env.clear_diags();

  joggle::Mod distinct_meta;
  constexpr std::string_view distinct_meta_source =
      "mod distinct\n"
      "fn work(x: i32) -> i32 {\n"
      "  [variant: 0]\n"
      "  let first: i32 = pure(x)\n"
      "  [variant: 1]\n"
      "  let second: i32 = pure(x)\n"
      "  return join(first, second)\n}\n";
  CHECK(joggle::parse(env, distinct_meta_source, distinct_meta,
                      "distinct-meta.jog"));
  CHECK(distinct_meta.verify(env));
  CHECK(joggle::run(env, "script.clean_pure", distinct_meta));
  pure_calls = 0;
  for (joggle::Op op : distinct_meta.ops())
    pure_calls += op.callee() == "pure" ? 1 : 0;
  CHECK(pure_calls == 2);

  joggle::Mod overload_execution;
  CHECK(joggle::parse(env, source.str(), overload_execution, argv[1]));
  CHECK(joggle::run(env, "script.overload_probe", overload_execution));
  CHECK(joggle::run(env, "script.short_circuit_probe", overload_execution));
  CHECK(joggle::run(env, "script.short_circuit_edit", overload_execution));
  const std::vector<std::string> short_uses = overload_execution.uses();
  CHECK(std::find(short_uses.begin(), short_uses.end(), "skipped") ==
        short_uses.end());
  CHECK(joggle::run(env, "script.generic_probe", overload_execution));
  CHECK(joggle::run(env, "script.multi_probe", overload_execution));
  CHECK(joggle::run(env, "script.compound_probe", overload_execution));
  CHECK(joggle::run(env, "script.collection_update_probe",
                    overload_execution));
  CHECK(joggle::run(env, "script.value_alias_probe", overload_execution));
  CHECK(joggle::run(env, "script.numel_probe", overload_execution));
  CHECK(joggle::run(env, "script.make_pair", overload_execution));
  CHECK(joggle::print(overload_execution)
            .find("let left: i32, right: i32 = test.pair(x, 0)") !=
        std::string::npos);
  joggle::Mod marked;
  CHECK(joggle::parse(env, source.str(), marked, argv[1]));
  CHECK(joggle::run(env, "script.mark_add", marked));
  const joggle::Op marked_add = marked.find_fn("add_zero").body().ops()[1];
  CHECK(marked_add.meta("place") &&
        marked_add.meta("place")->string() == "edge");
  CHECK(marked_add.meta("tile") && marked_add.meta("tile")->list() &&
        marked_add.meta("tile")->list()->size() == 2);
  joggle::Mod scripted_selective;
  CHECK(joggle::parse(env, selective_source, scripted_selective,
                      "scripted-selective.jog"));
  CHECK(joggle::run(env, "script.replace_first_use", scripted_selective));
  CHECK(joggle::print(scripted_selective).find("let left = x + 1") !=
        std::string::npos);
  CHECK(joggle::print(scripted_selective)
            .find("let right = shared + 2") != std::string::npos);
  joggle::Mod scripted_loop;
  CHECK(joggle::parse(env, loop_source, scripted_loop, "scripted-loop.jog"));
  CHECK(joggle::run(env, "script.clone_loop", scripted_loop));
  CHECK(scripted_loop.verify(env));
  CHECK(joggle::print(scripted_loop) == joggle::print(cloned_loop));
  joggle::Mod scripted_remapped_loop;
  CHECK(joggle::parse(env, remapped_loop_source, scripted_remapped_loop,
                      "scripted-remapped-loop.jog"));
  CHECK(joggle::run(env, "script.clone_remapped_loop",
                    scripted_remapped_loop));
  CHECK(scripted_remapped_loop.verify(env));
  const std::vector<joggle::Val> scripted_remapped_params =
      scripted_remapped_loop.find_fn("sum").params();
  std::size_t right_capturing_loops = 0;
  for (joggle::Op op : scripted_remapped_loop.ops())
    if (op.kind() == joggle::Op::Kind::loop &&
        find_argument(find_argument, op, scripted_remapped_params[2]))
      ++right_capturing_loops;
  CHECK(right_capturing_loops == 1);
  joggle::Mod tiled_loop;
  CHECK(joggle::parse(env, loop_source, tiled_loop, "tiled-loop.jog"));
  const std::vector<joggle::Attr> tile_factor{
      joggle::Attr(std::int64_t{4})};
  const bool tiled =
      joggle::run(env, "script.split_first_loop", tiled_loop, tile_factor);
  if (!tiled) {
    tiled_loop.print_diags(stderr);
    env.print_diags(stderr);
  }
  CHECK(tiled);
  CHECK(tiled_loop.verify(env));
  std::size_t tiled_loops = 0;
  std::size_t tail_guards = 0;
  for (joggle::Op op : tiled_loop.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      ++tiled_loops;
    if (op.kind() == joggle::Op::Kind::branch)
      ++tail_guards;
  }
  CHECK(tiled_loops == 1);
  CHECK(tail_guards == 1);
  const std::string tiled_text = joggle::print(tiled_loop);
  CHECK(tiled_text.find("for i_tile in") != std::string::npos);
  CHECK(tiled_text.find(", i in") != std::string::npos);
  joggle::Mod tiled_roundtrip;
  CHECK(joggle::parse(env, joggle::print(tiled_loop), tiled_roundtrip,
                      "tiled-roundtrip.jog"));
  CHECK(tiled_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(tiled_loop, tiled_roundtrip));
  joggle::Mod returned_constant;
  CHECK(joggle::parse(env,
                      "mod returned\n"
                      "fn value(x: i32) -> i32 { return x }\n",
                      returned_constant, "returned.jog"));
  CHECK(joggle::run(env, "script.return_three", returned_constant));
  CHECK(joggle::print(returned_constant).find("let three: i32 = 3") !=
        std::string::npos);
  CHECK(joggle::print(returned_constant).find("return three") !=
        std::string::npos);
  joggle::Mod scripted_schedule;
  CHECK(joggle::parse(env, schedule_source, scripted_schedule,
                      "scripted-schedule.jog"));
  CHECK(joggle::run(env, "script.move_second_first", scripted_schedule));
  CHECK(joggle::print(scripted_schedule) == scheduled_text);
  joggle::Mod scripted_control;
  CHECK(joggle::parse(env, control_seed, scripted_control,
                      "scripted-control.jog"));
  CHECK(joggle::run(env, "script.build_control", scripted_control));
  CHECK(scripted_control.verify(env));
  CHECK(joggle::print(scripted_control) == built_control_text);
  joggle::Mod control_rollback;
  CHECK(joggle::parse(env, built_control_text, control_rollback,
                      "control-rollback.jog"));
  const std::uint64_t control_revision = control_rollback.revision();
  CHECK(!joggle::run(env, "script.fail_after_control_rename",
                     control_rollback));
  CHECK(joggle::print(control_rollback) == built_control_text);
  CHECK(control_rollback.revision() == control_revision);
  env.clear_diags();
  CHECK(!joggle::run(env, "script.hide_loop_input", control_rollback));
  CHECK(joggle::print(control_rollback) == built_control_text);
  CHECK(control_rollback.revision() == control_revision);
  bool kept_loop_scope_detail = false;
  for (const joggle::Diag& diag : env.diags())
    kept_loop_scope_detail =
        kept_loop_scope_detail ||
        diag.message.find("loop variable 'n' is already visible") !=
            std::string::npos;
  CHECK(kept_loop_scope_detail);
  control_rollback.clear_diags();
  CHECK(joggle::run(env, "script.rename_control", scripted_control));
  CHECK(scripted_control.verify(env));
  CHECK(joggle::print(scripted_control) == renamed_control_text);
  joggle::Mod rolled_back;
  CHECK(joggle::parse(env, source.str(), rolled_back, argv[1]));
  const std::string before_failure = joggle::print(rolled_back);
  const std::uint64_t before_failure_revision = rolled_back.revision();
  CHECK(!joggle::run(env, "script.fail_after_edit", rolled_back));
  CHECK(joggle::print(rolled_back) == before_failure);
  CHECK(rolled_back.revision() == before_failure_revision);
  CHECK(!env.diags().empty());
  env.clear_diags();
  joggle::Mod invalid_run;
  CHECK(joggle::parse(env,
                      "mod invalid.run\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  let y: i32 = pure(x)\n"
                      "  return y\n"
                      "}\n",
                      invalid_run, "invalid-run.jog"));
  CHECK(invalid_run.verify(env));
  const std::string before_invalid_run = joggle::print(invalid_run);
  const std::uint64_t before_invalid_run_revision = invalid_run.revision();
  CHECK(!joggle::run(env, "script.invalidate_call", invalid_run));
  CHECK(joggle::print(invalid_run) == before_invalid_run);
  CHECK(invalid_run.revision() == before_invalid_run_revision);
  bool kept_verify_detail = false;
  for (const joggle::Diag& diag : env.diags())
    kept_verify_detail = kept_verify_detail ||
                         diag.message.find("base.len") != std::string::npos;
  CHECK(kept_verify_detail);
  env.clear_diags();

  joggle::Mod duplicate_edit;
  CHECK(joggle::parse(env,
                      "mod duplicate.edit\n"
                      "fn main(x: i32) -> i32 {\n"
                      "  let left = x + 1\n"
                      "  let right = left + 1\n"
                      "  return right\n"
                      "}\n",
                      duplicate_edit, "duplicate-edit.jog"));
  CHECK(duplicate_edit.verify(env));
  const std::string before_duplicate_edit = joggle::print(duplicate_edit);
  const std::uint64_t before_duplicate_edit_revision =
      duplicate_edit.revision();
  CHECK(!joggle::run(env, "script.duplicate_local", duplicate_edit));
  CHECK(joggle::print(duplicate_edit) == before_duplicate_edit);
  CHECK(duplicate_edit.revision() == before_duplicate_edit_revision);
  bool kept_duplicate_detail = false;
  for (const joggle::Diag& diag : env.diags())
    kept_duplicate_detail =
        kept_duplicate_detail ||
        diag.message.find("already declared in this scope") !=
            std::string::npos;
  CHECK(kept_duplicate_detail);

  joggle::Mod nested_shadow;
  CHECK(joggle::parse(env,
                      "mod nested.shadow\n"
                      "fn main(x: i32, flag: bool) -> i32 {\n"
                      "  let value = x + 1\n"
                      "  if flag {\n"
                      "    let value = x + 2\n"
                      "  }\n"
                      "  return value\n"
                      "}\n",
                      nested_shadow, "nested-shadow.jog"));
  CHECK(nested_shadow.verify(env));
  joggle::Mod nested_shadow_roundtrip;
  CHECK(joggle::parse(env, joggle::print(nested_shadow),
                      nested_shadow_roundtrip,
                      "nested-shadow-roundtrip.jog"));
  CHECK(nested_shadow_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(nested_shadow,
                                   nested_shadow_roundtrip));

  joggle::Mod immutable;
  CHECK(!joggle::parse(env,
                       "mod bad\nfn f(x: i32) -> i32 {\n"
                       "  let y = x\n  y = 1\n  return y\n}\n",
                       immutable, "immutable.jog"));
  CHECK(!immutable.diags().empty());
  CHECK(immutable.diags().front().loc.line == 4);

  joggle::Mod invalid_number;
  CHECK(!joggle::parse(
      env, "mod bad\nfn f() -> int { return 999999999999999999999999 }\n",
      invalid_number, "number.jog"));
  CHECK(!invalid_number.diags().empty());

  joggle::Mod invalid_type;
  CHECK(!joggle::parse(
      env, "mod bad\nfn f(x: tensor<i32,>) -> i32 { return 0 }\n",
      invalid_type, "type.jog"));
  CHECK(!invalid_type.diags().empty());

  joggle::Mod attrs;
  constexpr std::string_view attr_source =
      "mod attrs\n"
      "[entry]\n"
      "[policy: {name: \"roundtrip\", levels: [1, 2]}]\n"
      "fn payload() -> dict {\n"
      "  return {axis: 1, epsilon: 9.9999997473787516e-06, "
      "pads: [0, -1], raw: hex\"007fff\", "
      "words: [\"-\", \"[\", \"]\"]}\n}\n";
  CHECK(joggle::parse(env, attr_source, attrs, "attrs.jog"));
  CHECK(attrs.verify(env));
  const joggle::Val payload =
      attrs.find_fn("payload").body().ops().back().args()[0];
  const joggle::Attr payload_attr = payload.constant();
  const joggle::Attr::Dict* dict = payload_attr.dict();
  CHECK(dict && dict->size() == 5);
  CHECK(dict->at("axis").integer() == 1);
  CHECK(dict->at("epsilon").real() == 9.9999997473787516e-06);
  CHECK(dict->at("pads").list() && dict->at("pads").list()->size() == 2);
  CHECK(dict->at("raw").bytes() && dict->at("raw").bytes()->size() == 3);
  CHECK(dict->at("words").list() && dict->at("words").list()->size() == 3);
  const joggle::Fn payload_fn = attrs.find_fn("payload");
  CHECK(payload_fn.meta().size() == 2);
  CHECK(payload_fn.meta("entry") &&
        payload_fn.meta("entry")->boolean() == true);
  CHECK(payload_fn.meta("policy") && payload_fn.meta("policy")->dict());
  joggle::Mod attrs_roundtrip;
  CHECK(joggle::parse(env, joggle::print(attrs), attrs_roundtrip,
                      "attrs-roundtrip.jog"));
  CHECK(joggle::structurally_equal(attrs, attrs_roundtrip));

  joggle::Mod value_attrs;
  constexpr std::string_view value_attr_source =
      "mod value_attrs\n"
      "use base\n"
      "fn carry<[role: \"extent\"] N: int>(\n"
      "  [range: {min: 0}] seed: i32\n"
      ") -> i32 {\n"
      "  [place: \"edge\"]\n"
      "  var [format: \"q8\"] value: i32 = seed\n"
      "  for i in 0..N {\n"
      "    value += 1\n"
      "  }\n"
      "  let [range: {min: 0, max: 255}] output: i32 = value\n"
      "  return output\n"
      "}\n"
      "fn inner(x: i32) -> i32 {\n"
      "  let [space: \"body\"] y: i32 = x\n"
      "  return y\n"
      "}\n"
      "fn conflict(x: i32) -> i32 {\n"
      "  let [space: \"call\"] y: i32 = inner(x)\n"
      "  return y\n"
      "}\n";
  CHECK(joggle::parse(env, value_attr_source, value_attrs,
                      "value-attrs.jog"));
  CHECK(value_attrs.verify(env));
  const joggle::Fn carry = value_attrs.find_fn("carry");
  CHECK(carry && carry.generics().size() == 1 && carry.params().size() == 1);
  CHECK(carry.generics()[0].meta("role") &&
        carry.generics()[0].meta("role")->string() == "extent");
  CHECK(carry.params()[0].meta("range") &&
        carry.params()[0].meta("range")->dict());
  joggle::Op value_loop;
  joggle::Val value;
  joggle::Val output;
  for (joggle::Op op : carry.ops()) {
    if (op.kind() == joggle::Op::Kind::loop)
      value_loop = op;
    for (joggle::Val out : op.outs()) {
      if (out.name() == "value" && out.meta("format"))
        value = out;
      if (out.name() == "output")
        output = out;
    }
  }
  CHECK(value_loop && value && output);
  CHECK(output.meta("range") && output.meta("range")->dict());
  const joggle::Op conflict_call =
      value_attrs.find_fn("conflict").body().ops().front();
  const std::string before_conflict = joggle::print(value_attrs);
  CHECK(!env.expand(value_attrs, conflict_call,
                    env.resolve(value_attrs, conflict_call)));
  CHECK(joggle::print(value_attrs) == before_conflict);
  value_attrs.clear_diags();
  CHECK(value_attrs.set(value_loop.blks()[0].args()[1], "bank",
                        joggle::Attr(std::int64_t{2})));
  for (joggle::Val member : value_loop.blks()[0].args())
    if (member.name() == "value")
      CHECK(member.meta("bank") && member.meta("bank")->integer() == 2);
  CHECK(value_attrs.unset(value, "bank"));
  const std::array batch_values{carry.generics()[0], carry.params()[0]};
  const std::array batch_meta{joggle::Attr("compile"),
                              joggle::Attr("runtime")};
  CHECK(value_attrs.set(batch_values, "stage", batch_meta));
  CHECK(carry.generics()[0].meta("stage") &&
        carry.generics()[0].meta("stage")->string() == "compile");
  CHECK(carry.params()[0].meta("stage") &&
        carry.params()[0].meta("stage")->string() == "runtime");
  const std::array conflict_values{value,
                                   value_loop.blks()[0].args()[1]};
  const std::array conflict_meta{joggle::Attr(std::int64_t{1}),
                                 joggle::Attr(std::int64_t{2})};
  CHECK(!value_attrs.set(conflict_values, "bank", conflict_meta));
  value_attrs.clear_diags();
  CHECK(joggle::run(env, "script.mark_first_param", value_attrs));
  CHECK(carry.params()[0].meta("layout") &&
        carry.params()[0].meta("layout")->string() == "packed");
  joggle::Mod value_attrs_roundtrip;
  const std::string value_attrs_text = joggle::print(value_attrs);
  if (!joggle::parse(env, value_attrs_text, value_attrs_roundtrip,
                     "value-attrs-roundtrip.jog")) {
    std::fwrite(value_attrs_text.data(), 1, value_attrs_text.size(), stderr);
    value_attrs_roundtrip.print_diags(stderr);
    return 1;
  }
  CHECK(value_attrs_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(value_attrs, value_attrs_roundtrip));

  joggle::Mod tagged;
  constexpr std::string_view tagged_source =
      "mod tagged\n"
      "[entry, rename: \"custom.add\"]\n"
      "fn add(x: i32, y: i32) -> i32 { return x + y }\n"
      "fn plain(x: i32, y: i32) -> i32 { return x + y }\n";
  CHECK(joggle::parse(env, tagged_source, tagged, "tagged.jog"));
  CHECK(tagged.verify(env));
  CHECK(joggle::run(env, "script.rebuild_tagged", tagged));
  const joggle::Op renamed =
      tagged.find_fn("add").body().ops().back().args().front().def();
  const joggle::Op plain =
      tagged.find_fn("plain").body().ops().back().args().front().def();
  CHECK(renamed.callee() == "custom.add");
  CHECK(plain.callee() == "operator +");

  joggle::Mod unsafe_fusion;
  constexpr std::string_view unsafe_source =
      "mod unsafe\n"
      "fn main(x: i32) -> i32 {\n"
      "  let first = test.first(x)\n"
      "  let unrelated = test.unrelated(x)\n"
      "  let last = test.last(first)\n"
      "  return last\n}\n";
  CHECK(joggle::parse(env, unsafe_source, unsafe_fusion, "unsafe.jog"));
  CHECK(unsafe_fusion.verify(env));
  const std::string unsafe_before = joggle::print(unsafe_fusion);
  std::vector<joggle::Op> unsafe_group;
  for (joggle::Op op : unsafe_fusion.find_fn("main").body().ops()) {
    if (op.callee() == "test.first" || op.callee() == "test.last")
      unsafe_group.push_back(op);
  }
  CHECK(unsafe_group.size() == 2);
  CHECK(!unsafe_fusion.fuse(env, unsafe_group, "test.fused"));
  CHECK(joggle::print(unsafe_fusion) == unsafe_before);
  CHECK(!unsafe_fusion.diags().empty());

  joggle::Mod typed_fusion;
  constexpr std::string_view typed_fusion_source =
      "mod typed_fusion\n"
      "fn incompatible(x: i32) -> f32;\n"
      "fn compatible(x: i32) -> i32;\n"
      "fn main(x: i32) -> i32 {\n"
      "  let first: i32 = experiment.first(x)\n"
      "  let last: i32 = experiment.last(first)\n"
      "  return last\n}\n";
  CHECK(joggle::parse(env, typed_fusion_source, typed_fusion,
                      "typed-fusion.jog"));
  CHECK(typed_fusion.verify(env));
  std::vector<joggle::Op> typed_group;
  for (joggle::Op op : typed_fusion.find_fn("main").body().ops())
    if (op.kind() == joggle::Op::Kind::call)
      typed_group.push_back(op);
  CHECK(typed_group.size() == 2);
  const std::string typed_before = joggle::print(typed_fusion);
  const std::uint64_t typed_revision = typed_fusion.revision();
  CHECK(!typed_fusion.fuse(env, typed_group, "incompatible"));
  CHECK(joggle::print(typed_fusion) == typed_before);
  CHECK(typed_fusion.revision() == typed_revision);

  typed_group.clear();
  for (joggle::Op op : typed_fusion.find_fn("main").body().ops())
    if (op.kind() == joggle::Op::Kind::call)
      typed_group.push_back(op);
  CHECK(typed_fusion.fuse(env, typed_group, "compatible"));
  CHECK(typed_fusion.verify(env));
  CHECK(joggle::print(typed_fusion).find("compatible(x)") !=
        std::string::npos);

  joggle::Mod duplicate_meta;
  CHECK(!joggle::parse(env,
                       "mod bad\n[a, a: 1]\n"
                       "fn f() -> int { return 0 }\n",
                       duplicate_meta, "duplicate-meta.jog"));
  CHECK(!duplicate_meta.diags().empty());

  joggle::Mod duplicate_binding;
  CHECK(!joggle::parse(env,
                       "mod bad\nfn f(x: i64) -> i64 {\n"
                       "  let y = x + i64(1)\n"
                       "  let y = y + i64(1)\n"
                       "  return y\n}\n",
                       duplicate_binding, "duplicate-binding.jog"));
  CHECK(!duplicate_binding.diags().empty());

  joggle::Mod shadowed_iterator;
  CHECK(!joggle::parse(env,
                       "mod bad\nfn f(i: i64) -> i64 {\n"
                       "  for i in 0..2 {}\n"
                       "  return i\n}\n",
                       shadowed_iterator, "shadowed-iterator.jog"));
  CHECK(!shadowed_iterator.diags().empty());

  joggle::Mod scoped_bindings;
  CHECK(joggle::parse(env,
                      "mod scoped\nfn f(x: i64, flag: bool) -> i64 {\n"
                      "  if flag { let x = x + i64(1) }\n"
                      "  for i in 0..2 {}\n"
                      "  for i in 0..2 {}\n"
                      "  return x\n}\n",
                      scoped_bindings, "scoped-bindings.jog"));
  CHECK(scoped_bindings.verify(env));

  joggle::Mod missing_return;
  CHECK(joggle::parse(env, "mod bad\nfn f(x: i32) -> i32 { x + 1 }\n",
                      missing_return, "return.jog"));
  CHECK(!missing_return.verify(env));
  CHECK(!missing_return.diags().empty());

  env.clear_diags();
  const std::vector<joggle::Attr> wrong{joggle::Attr("not an integer")};
  CHECK(!env.call("sample.ping", wrong, returns));
  CHECK(!env.diags().empty());
  return 0;
}
