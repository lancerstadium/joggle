#include "joggle/joggle.h"

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: joggle <file.jog>\n";
    return 2;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "joggle: cannot open " << argv[1] << '\n';
    return 1;
  }
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  joggle::Mod mod;
  if (!joggle::parse(env, source.str(), mod, argv[1]))
    return mod.print_diags(stderr);
  if (!mod.verify(env))
    return mod.print_diags(stderr);
  return joggle::print(stdout, mod) ? 0 : 1;
}
