#include "joggle/joggle.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int usage() {
  std::cerr << "usage:\n"
               "  joggle check <file.jog>\n"
               "  joggle run <module.fn> <file.jog> [-M <module-dir>]...\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3)
    return usage();

  const std::string command = argv[1];
  const bool execute = command == "run";
  if (command != "check" && !execute)
    return usage();
  if ((!execute && argc != 3) || (execute && argc < 4))
    return usage();

  const std::string function = execute ? argv[2] : "";
  const std::string file = execute ? argv[3] : argv[2];
  std::vector<std::string> paths;
  for (int index = execute ? 4 : 3; index < argc; index += 2) {
    if (std::string_view(argv[index]) != "-M" || index + 1 >= argc)
      return usage();
    paths.emplace_back(argv[index + 1]);
  }

  std::ifstream input(file);
  if (!input) {
    std::cerr << "joggle: cannot open " << file << '\n';
    return 1;
  }
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  for (std::string& path : paths)
    env.path(std::move(path));
  if (execute) {
    const std::size_t dot = function.rfind('.');
    if (dot == std::string::npos || !env.load(function.substr(0, dot))) {
      env.print_diags(stderr);
      return 1;
    }
  }

  joggle::Mod mod;
  if (!joggle::parse(env, source.str(), mod, file))
    return mod.print_diags(stderr);
  if (!mod.verify(env))
    return mod.print_diags(stderr);
  if (execute && !joggle::run(env, function, mod)) {
    mod.print_diags(stderr);
    env.print_diags(stderr);
    return 1;
  }
  return joggle::print(stdout, mod) ? 0 : 1;
}
