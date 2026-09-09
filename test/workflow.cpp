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

  joggle::Mod types;
  constexpr std::string_view type_source =
      "module types\n"
      "use tensor\n"
      "fn id<E, S>(x: tensor<E, S>) -> tensor<E, S>;\n"
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
  CHECK(matmul.params().size() == 2);
  CHECK(matmul.blocks().size() == 3);

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
  joggle::Mod overload_execution;
  CHECK(joggle::parse(env, source.str(), overload_execution, argv[1]));
  CHECK(joggle::run(env, "script.overload_probe", overload_execution));
  joggle::Mod rolled_back;
  CHECK(joggle::parse(env, source.str(), rolled_back, argv[1]));
  const std::string before_failure = joggle::print(rolled_back);
  CHECK(!joggle::run(env, "script.fail_after_edit", rolled_back));
  CHECK(joggle::print(rolled_back) == before_failure);
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
