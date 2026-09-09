#include "joggle/joggle.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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
    for (joggle::Blk block : fn.blocks()) {
      for (joggle::Op op : block.ops()) {
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

  std::ifstream input(argv[1]);
  CHECK(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[2]);
  env.path(argv[3]);
  CHECK(env.load("tensor"));
  CHECK(env.loaded("base"));
  CHECK(env.loaded("tensor"));
  CHECK((env.modules() == std::vector<std::string>{"base", "tensor"}));

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

  joggle::Mod precedence;
  constexpr std::string_view precedence_source =
      "module precedence\n"
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
      "module types\n"
      "use tensor\n"
      "fn id<E: Ty, S: list<int>>(x: tensor<E, S>) -> tensor<E, S>;\n"
      "fn apply(x: tensor<f32, [2, 3]>) -> tensor<f32, [2, 3]> {\n"
      "  return id(x)\n"
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
  const std::vector<joggle::Blk> last_blocks = types.find_fn("last").blocks();
  CHECK(last_blocks.size() == 2);
  CHECK(last_blocks[1].args().front().type() == joggle::Ty("int"));
  joggle::Mod types_roundtrip;
  CHECK(joggle::parse(env, joggle::print(types), types_roundtrip,
                      "types-roundtrip.jog"));
  CHECK(types_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(types, types_roundtrip));

  joggle::Mod wrong_shape;
  CHECK(joggle::parse(
      env,
      "module wrong_shape\nuse tensor\n"
      "fn bad(x: tensor<f32, 4>) -> tensor<f32, 4> { return x }\n",
      wrong_shape, "wrong-shape.jog"));
  CHECK(!wrong_shape.verify(env));
  CHECK(!wrong_shape.diags().empty());
  CHECK(wrong_shape.diags().front().message.find("expected 'list<int>'") !=
        std::string::npos);

  joggle::Mod multi;
  constexpr std::string_view multi_source =
      "module multi\n"
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
      "module annotated\n"
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

  joggle::Mod selective;
  constexpr std::string_view selective_source =
      "module selective\n"
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

  joggle::Mod cloned_loop;
  constexpr std::string_view loop_source =
      "module looped\n"
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
  const joggle::Blk old_body = old_loop.blocks().front();
  const joggle::Op copied_loop = cloned_loop.clone(old_loop, old_loop);
  CHECK(copied_loop && copied_loop.blocks().size() == 1);
  CHECK(cloned_loop.replace(old_loop.outs()[0], copied_loop.outs()[0]));
  CHECK(cloned_loop.erase(old_loop));
  CHECK(!old_loop.valid() && !old_body.valid());
  CHECK(cloned_loop.verify(env));
  joggle::Mod cloned_roundtrip;
  CHECK(joggle::parse(env, joggle::print(cloned_loop), cloned_roundtrip,
                      "cloned-roundtrip.jog"));
  CHECK(cloned_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(cloned_loop, cloned_roundtrip));

  joggle::Mod scheduled;
  constexpr std::string_view schedule_source =
      "module scheduled\n"
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

  joggle::Mod cloned_branch;
  constexpr std::string_view branch_source =
      "module branched\n"
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
  CHECK(old_branch && old_branch.blocks().size() == 2);
  const joggle::Op copied_branch = cloned_branch.clone(old_branch, old_branch);
  CHECK(copied_branch && copied_branch.blocks().size() == 2);
  CHECK(cloned_branch.replace(old_branch.outs()[0],
                              copied_branch.outs()[0]));
  CHECK(cloned_branch.erase(old_branch));
  CHECK(cloned_branch.verify(env));

  joggle::Mod built_control;
  constexpr std::string_view control_seed =
      "module control\n"
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
  CHECK(built_loop && built_loop.blocks().size() == 1);
  const joggle::Blk built_body = built_loop.blocks().front();
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
  CHECK(inner_loop && inner_loop.blocks().size() == 1);
  const joggle::Blk inner_body = inner_loop.blocks().front();
  const joggle::Op inner_yield = inner_body.ops().back();
  const std::vector<joggle::Val> sum_args{inner_body.args()[1],
                                          inner_body.args()[0]};
  const joggle::Val sum = built_control.call(
      inner_yield, "operator +", sum_args, joggle::Ty("int"));
  CHECK(sum && built_control.rename(sum, "total"));
  const std::vector<joggle::Val> inner_values{sum};
  CHECK(built_control.args(inner_yield, inner_values));
  CHECK(built_control.args(built_yield, inner_loop.outs()));
  const joggle::Op built_branch = built_control.branch(
      build_ret, build_fn.params()[1], built_loop.outs());
  CHECK(built_branch && built_branch.blocks().size() == 2);
  const joggle::Blk then_block = built_branch.blocks().front();
  const joggle::Op then_yield = then_block.ops().back();
  const joggle::Val one =
      built_control.constant(then_yield, joggle::Attr(std::int64_t{1}),
                             joggle::Ty("int"));
  CHECK(one && built_control.rename(one, "one"));
  const std::vector<joggle::Val> then_args{then_block.args().front(), one};
  const joggle::Val increment = built_control.call(
      then_yield, "operator +", then_args, joggle::Ty("int"));
  CHECK(increment && built_control.rename(increment, "total"));
  const std::vector<joggle::Val> then_values{increment};
  CHECK(built_control.args(then_yield, then_values));
  CHECK(built_control.args(build_ret, built_branch.outs()));
  CHECK(built_control.verify(env));
  const std::string built_control_text = joggle::print(built_control);
  CHECK(built_control_text.find("for i in total..n") != std::string::npos);
  CHECK(built_control_text.find("for j in total..i") != std::string::npos);
  CHECK(built_control_text.find("if flag") != std::string::npos);
  joggle::Mod control_roundtrip;
  CHECK(joggle::parse(env, built_control_text, control_roundtrip,
                      "control-roundtrip.jog"));
  CHECK(control_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(built_control, control_roundtrip));

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
  CHECK(!renamed_control.rename(renamed_loops[0].blocks()[0].args()[0],
                                "return"));
  CHECK(renamed_control.revision() == rename_revision);
  renamed_control.clear_diags();
  CHECK(!renamed_control.rename(renamed_loops[0].blocks()[0].args()[0],
                                "total"));
  CHECK(renamed_control.revision() == rename_revision);
  renamed_control.clear_diags();
  CHECK(renamed_control.rename(renamed_loops[0].blocks()[0].args()[0],
                               "outer"));
  CHECK(renamed_control.rename(renamed_loops[1].blocks()[0].args()[0],
                               "inner"));
  CHECK(renamed_control.rename(renamed_branch.blocks()[0].args()[0], "acc"));
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
                      "module wrong_result\n"
                      "fn split(x: i32) -> (i32, bool);\n"
                      "fn bad(x: i32) -> str {\n"
                      "  let value: str, ok: bool = split(x)\n"
                      "  return value\n}\n",
                      wrong_result_type, "wrong-result.jog"));
  CHECK(!wrong_result_type.verify(env));
  CHECK(!wrong_result_type.diags().empty());
  CHECK(wrong_result_type.diags().front().message.find("expected 'i32'") !=
        std::string::npos);

  joggle::Mod built_multi;
  CHECK(joggle::parse(env,
                      "module built\n"
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

  joggle::Mod overloaded;
  constexpr std::string_view overload_source =
      "module overloads\n"
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

  joggle::Mod ambiguous;
  constexpr std::string_view ambiguous_source =
      "module ambiguous\n"
      "fn pick<A>(x: pair<A, i32>) -> int;\n"
      "fn pick<B>(x: pair<i32, B>) -> str;\n"
      "fn use(x: pair<i32, i32>) -> int { return pick(x) }\n";
  CHECK(joggle::parse(env, ambiguous_source, ambiguous, "ambiguous.jog"));
  CHECK(!ambiguous.verify(env));
  CHECK(!ambiguous.diags().empty());
  CHECK(ambiguous.diags().front().message.find("ambiguous") !=
        std::string::npos);

  joggle::Mod duplicate_overload;
  CHECK(!joggle::parse(env,
                       "module duplicate\n"
                       "fn same(x: int) -> int;\n"
                       "fn same(x: int) -> str;\n",
                       duplicate_overload, "duplicate-overload.jog"));
  CHECK(!duplicate_overload.diags().empty());

  joggle::Mod duplicate_generic;
  CHECK(!joggle::parse(env,
                       "module duplicate\n"
                       "fn same<T>(x: T) -> int;\n"
                       "fn same<U>(x: U) -> int;\n",
                       duplicate_generic, "duplicate-generic.jog"));
  CHECK(!duplicate_generic.diags().empty());

  joggle::Mod duplicate_parameter;
  CHECK(!joggle::parse(env,
                       "module duplicate\n"
                       "fn same<T, T>(x: int) -> int;\n",
                       duplicate_parameter, "duplicate-parameter.jog"));
  CHECK(!duplicate_parameter.diags().empty());

  joggle::Mod wrong_generic_kind;
  CHECK(joggle::parse(env,
                      "module kinds\n"
                      "fn width<W: int>(x: int) -> int;\n"
                      "fn bad(x: int) -> int { return width<f32>(x) }\n",
                      wrong_generic_kind, "wrong-generic-kind.jog"));
  CHECK(!wrong_generic_kind.verify(env));
  CHECK(!wrong_generic_kind.diags().empty());
  CHECK(wrong_generic_kind.diags().front().message.find("expected 'int'") !=
        std::string::npos);

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
  const std::vector<joggle::Attr> bytes{
      joggle::Attr(joggle::Attr::Bytes{0, 127, 255})};
  CHECK(env.call("sample.echo", bytes, returns));
  CHECK(returns.size() == 1);
  CHECK(returns[0].bytes() && *returns[0].bytes() == *bytes[0].bytes());
  const std::vector<joggle::Attr> text{joggle::Attr("hello")};
  CHECK(env.call("sample.echo", text, returns));
  CHECK(returns.size() == 1 && returns[0].string() == "hello");
  CHECK(!env.load("bad"));
  CHECK(!env.loaded("bad"));
  CHECK(!env.diags().empty());
  env.clear_diags();
  joggle::Mod mod;
  CHECK(joggle::parse(env, source.str(), mod, argv[1]));
  CHECK(mod.verify(env));
  CHECK(mod.name() == "test.linear");
  CHECK(mod.uses() == std::vector<std::string>{"tensor"});
  CHECK(mod.fns().size() == 3);

  joggle::Fn matmul = mod.find_fn("matmul");
  CHECK(matmul);
  CHECK(matmul.generics().size() == 4);
  CHECK(matmul.generics()[0].type() == joggle::Ty("Ty"));
  CHECK(matmul.generics()[1].type() == joggle::Ty("int"));
  CHECK(matmul.params().size() == 2);
  CHECK(matmul.blocks().size() == 3);
  CHECK(matmul.ops().size() > matmul.body().ops().size());

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
  joggle::Mod cleaned;
  constexpr std::string_view clean_source =
      "module clean\n"
      "fn work(x: i32) -> i32 {\n"
      "  let first: i32 = pure(x)\n"
      "  let same: i32 = pure(x)\n"
      "  let dead: i32 = pure(first)\n"
      "  return same\n}\n";
  CHECK(joggle::parse(env, clean_source, cleaned, "clean.jog"));
  CHECK(cleaned.verify(env));
  CHECK(joggle::run(env, "script.clean_pure", cleaned));
  CHECK(cleaned.verify(env));
  std::size_t pure_calls = 0;
  for (joggle::Op op : cleaned.ops())
    pure_calls += op.callee() == "pure" ? 1 : 0;
  CHECK(pure_calls == 1);
  const std::uint64_t clean_revision = cleaned.revision();
  CHECK(joggle::run(env, "script.clean_pure", cleaned));
  CHECK(cleaned.revision() == clean_revision);

  joggle::Mod distinct_meta;
  constexpr std::string_view distinct_meta_source =
      "module distinct\n"
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
  CHECK(joggle::run(env, "script.generic_probe", overload_execution));
  CHECK(joggle::run(env, "script.multi_probe", overload_execution));
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
  joggle::Mod returned_constant;
  CHECK(joggle::parse(env,
                      "module returned\n"
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

  joggle::Mod immutable;
  CHECK(!joggle::parse(env,
                       "module bad\nfn f(x: i32) -> i32 {\n"
                       "  let y = x\n  y = 1\n  return y\n}\n",
                       immutable, "immutable.jog"));
  CHECK(!immutable.diags().empty());
  CHECK(immutable.diags().front().loc.line == 4);

  joggle::Mod invalid_number;
  CHECK(!joggle::parse(
      env, "module bad\nfn f() -> int { return 999999999999999999999999 }\n",
      invalid_number, "number.jog"));
  CHECK(!invalid_number.diags().empty());

  joggle::Mod invalid_type;
  CHECK(!joggle::parse(
      env, "module bad\nfn f(x: tensor<i32,>) -> i32 { return 0 }\n",
      invalid_type, "type.jog"));
  CHECK(!invalid_type.diags().empty());

  joggle::Mod attrs;
  constexpr std::string_view attr_source =
      "module attrs\n"
      "[entry]\n"
      "[policy: {name: \"roundtrip\", levels: [1, 2]}]\n"
      "fn payload() -> dict {\n"
      "  return {axis: 1, epsilon: 9.9999997473787516e-06, "
      "pads: [0, -1], raw: hex\"007fff\"}\n}\n";
  CHECK(joggle::parse(env, attr_source, attrs, "attrs.jog"));
  CHECK(attrs.verify(env));
  const joggle::Val payload =
      attrs.find_fn("payload").body().ops().back().args()[0];
  const joggle::Attr payload_attr = payload.constant();
  const joggle::Attr::Dict* dict = payload_attr.dict();
  CHECK(dict && dict->size() == 4);
  CHECK(dict->at("axis").integer() == 1);
  CHECK(dict->at("epsilon").real() == 9.9999997473787516e-06);
  CHECK(dict->at("pads").list() && dict->at("pads").list()->size() == 2);
  CHECK(dict->at("raw").bytes() && dict->at("raw").bytes()->size() == 3);
  const joggle::Fn payload_fn = attrs.find_fn("payload");
  CHECK(payload_fn.meta().size() == 2);
  CHECK(payload_fn.meta("entry") &&
        payload_fn.meta("entry")->boolean() == true);
  CHECK(payload_fn.meta("policy") && payload_fn.meta("policy")->dict());
  joggle::Mod attrs_roundtrip;
  CHECK(joggle::parse(env, joggle::print(attrs), attrs_roundtrip,
                      "attrs-roundtrip.jog"));
  CHECK(joggle::structurally_equal(attrs, attrs_roundtrip));

  joggle::Mod tagged;
  constexpr std::string_view tagged_source =
      "module tagged\n"
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
      "module unsafe\n"
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
  CHECK(!unsafe_fusion.fuse(unsafe_group, "test.fused"));
  CHECK(joggle::print(unsafe_fusion) == unsafe_before);
  CHECK(!unsafe_fusion.diags().empty());

  joggle::Mod duplicate_meta;
  CHECK(!joggle::parse(env,
                       "module bad\n[a, a: 1]\n"
                       "fn f() -> int { return 0 }\n",
                       duplicate_meta, "duplicate-meta.jog"));
  CHECK(!duplicate_meta.diags().empty());

  joggle::Mod missing_return;
  CHECK(joggle::parse(env, "module bad\nfn f(x: i32) -> i32 { x + 1 }\n",
                      missing_return, "return.jog"));
  CHECK(!missing_return.verify(env));
  CHECK(!missing_return.diags().empty());

  env.clear_diags();
  const std::vector<joggle::Attr> wrong{joggle::Attr("not an integer")};
  CHECK(!env.call("sample.ping", wrong, returns));
  CHECK(!env.diags().empty());
  return 0;
}
