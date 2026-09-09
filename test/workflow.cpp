#include "joggle/joggle.h"

#include <cassert>

int main() {
  joggle::Env env;
  joggle::Mod mod;
  assert(joggle::parse(env, "module smoke\n", mod));
  assert(mod.name() == "smoke");
  assert(joggle::print(mod) == "module smoke\n");

  joggle::Mod reparsed;
  assert(joggle::parse(env, joggle::print(mod), reparsed));
  assert(reparsed.name() == mod.name());
  return 0;
}
