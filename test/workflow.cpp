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
  CHECK(env.load("sample"));
  CHECK(env.bound("sample.ping"));
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

  joggle::Mod attrs;
  constexpr std::string_view attr_source =
      "module attrs\nfn payload() -> dict {\n"
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
  joggle::Mod attrs_roundtrip;
  CHECK(joggle::parse(env, joggle::print(attrs), attrs_roundtrip,
                      "attrs-roundtrip.jog"));
  CHECK(joggle::structurally_equal(attrs, attrs_roundtrip));

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
