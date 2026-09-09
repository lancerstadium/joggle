#include "joggle/joggle.h"

#include <cassert>
#include <fstream>
#include <sstream>
#include <string>

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
  assert(argc == 4);
  std::ifstream input(argv[1]);
  assert(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[2]);
  env.path(argv[3]);
  assert(env.load("tensor"));
  assert(env.loaded("base"));
  assert(env.loaded("tensor"));
  assert((env.modules() == std::vector<std::string>{"base", "tensor"}));
  assert(env.load("sample"));
  assert(env.bound("sample.ping"));
  const std::vector<joggle::Attr> arguments{joggle::Attr(std::int64_t{41})};
  std::vector<joggle::Attr> returns;
  assert(env.call("sample.ping", arguments, returns));
  assert(returns.size() == 1);
  assert(returns[0].integer() == 42);
  assert(!env.load("bad"));
  assert(!env.loaded("bad"));
  assert(!env.diags().empty());
  env.clear_diags();
  joggle::Mod mod;
  assert(joggle::parse(env, source.str(), mod, argv[1]));
  assert(mod.verify(env));
  assert(mod.name() == "test.linear");
  assert(mod.uses() == std::vector<std::string>{"tensor"});
  assert(mod.fns().size() == 3);

  joggle::Fn matmul = mod.find_fn("matmul");
  assert(matmul);
  assert(matmul.generics().size() == 4);
  assert(matmul.params().size() == 2);
  assert(matmul.blocks().size() == 3);

  const std::string canonical = joggle::print(mod);
  assert(canonical.find("for i in 0..M, j in 0..N") != std::string::npos);
  assert(canonical.find("for k in 0..K") != std::string::npos);
  assert(canonical.find("sum += a[i, k] * b[k, j]") != std::string::npos);
  assert(canonical.find("if flag") != std::string::npos);

  joggle::Mod reparsed;
  assert(joggle::parse(env, canonical, reparsed, "canonical.jog"));
  assert(reparsed.verify(env));
  assert(joggle::structurally_equal(mod, reparsed));

  joggle::Op removed;
  assert(fold_add_zero(mod, &removed));
  assert(!removed.valid());
  assert(mod.verify(env));
  const std::string folded = joggle::print(mod);
  assert(folded.find("let y = x + 0") == std::string::npos);
  assert(folded.find("return x") != std::string::npos);

  joggle::Mod immutable;
  assert(!joggle::parse(env,
                        "module bad\nfn f(x: i32) -> i32 {\n"
                        "  let y = x\n  y = 1\n  return y\n}\n",
                        immutable, "immutable.jog"));
  assert(!immutable.diags().empty());
  assert(immutable.diags().front().loc.line == 4);

  joggle::Mod missing_return;
  assert(joggle::parse(env, "module bad\nfn f(x: i32) -> i32 { x + 1 }\n",
                       missing_return, "return.jog"));
  assert(!missing_return.verify(env));
  assert(!missing_return.diags().empty());

  env.clear_diags();
  const std::vector<joggle::Attr> wrong{joggle::Attr("not an integer")};
  assert(!env.call("sample.ping", wrong, returns));
  assert(!env.diags().empty());
  return 0;
}
