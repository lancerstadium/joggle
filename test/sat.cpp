#include "joggle/joggle.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

bool call(joggle::Env& env, std::string_view name,
          std::vector<joggle::Attr> args, std::vector<joggle::Attr>& returns) {
  env.clear_diags();
  return env.call(name, args, returns);
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 3);
  std::ifstream input(argv[1]);
  CHECK(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[2]);
  CHECK(env.load("sat"));

  joggle::Mod mod;
  CHECK(joggle::parse(env, source.str(), mod, argv[1]));
  CHECK(mod.verify(env));
  CHECK(joggle::run(env, "sat.select", mod));
  CHECK(mod.verify(env));

  const joggle::Op selected =
      mod.find_fn("add8").body().ops().back().args().front().def();
  const joggle::Op untouched =
      mod.find_fn("add32").body().ops().back().args().front().def();
  CHECK(selected.callee() == "sat.add");
  CHECK(untouched.callee() == "operator +");
  const std::string once = joggle::print(mod);
  CHECK(joggle::run(env, "sat.select", mod));
  CHECK(joggle::print(mod) == once);

  std::vector<joggle::Attr> returns;
  CHECK(call(env, "sat.supports", {joggle::Attr("sat<8>")}, returns));
  CHECK(returns.size() == 1 && returns[0].boolean() == true);
  CHECK(call(env, "sat.supports", {joggle::Attr("i32")}, returns));
  CHECK(returns.size() == 1 && returns[0].boolean() == false);

  CHECK(call(env, "sat.sim",
             {joggle::Attr(std::int64_t{8}), joggle::Attr(std::int64_t{100}),
              joggle::Attr(std::int64_t{100})},
             returns));
  CHECK(returns.size() == 1 && returns[0].integer() == 127);
  CHECK(call(env, "sat.sim",
             {joggle::Attr(std::int64_t{8}), joggle::Attr(std::int64_t{-100}),
              joggle::Attr(std::int64_t{-100})},
             returns));
  CHECK(returns.size() == 1 && returns[0].integer() == -128);

  CHECK(call(env, "sat.emit", {joggle::Attr(std::int64_t{8})}, returns));
  CHECK(returns.size() == 1 && returns[0].string());
  CHECK(returns[0].string()->find("module sat_add_8") !=
        std::string_view::npos);
  CHECK(returns[0].string()->find("[7:0]") != std::string_view::npos);
  CHECK(returns[0].string()->find("{a[7], a} + {b[7], b}") !=
        std::string_view::npos);
  return 0;
}
