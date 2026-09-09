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
               "  joggle read <module.fn> <file> [-M <module-dir>]...\n"
               "  joggle run <module.fn> <file.jog> [-M <module-dir>]...\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3)
    return usage();

  const std::string command = argv[1];
  const bool execute = command == "run";
  const bool decode = command == "read";
  if (command != "check" && !execute && !decode)
    return usage();
  if ((!execute && !decode && argc != 3) || ((execute || decode) && argc < 4))
    return usage();

  const std::string function = execute || decode ? argv[2] : "";
  const std::string file = execute || decode ? argv[3] : argv[2];
  std::vector<std::string> paths;
  for (int index = execute || decode ? 4 : 3; index < argc; index += 2) {
    if (std::string_view(argv[index]) != "-M" || index + 1 >= argc)
      return usage();
    paths.emplace_back(argv[index + 1]);
  }

  std::ifstream input(file, std::ios::binary);
  if (!input) {
    std::cerr << "joggle: cannot open " << file << '\n';
    return 1;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  std::string source = contents.str();

  joggle::Env env;
  for (std::string& path : paths)
    env.path(std::move(path));
  if (execute || decode) {
    const std::size_t dot = function.rfind('.');
    if (dot == std::string::npos || !env.load(function.substr(0, dot))) {
      env.print_diags(stderr);
      return 1;
    }
  }

  if (decode) {
    joggle::Attr::Bytes bytes;
    bytes.reserve(source.size());
    for (const unsigned char byte : source)
      bytes.push_back(byte);
    const std::vector<joggle::Attr> args{joggle::Attr(std::move(bytes))};
    std::vector<joggle::Attr> returns;
    if (!env.call(function, args, returns)) {
      env.print_diags(stderr);
      return 1;
    }
    if (returns.size() != 1 || !returns.front().string()) {
      std::cerr << "joggle: read function must return one str\n";
      return 1;
    }
    source = std::string(*returns.front().string());
  }

  joggle::Mod mod;
  if (!joggle::parse(env, source, mod, file))
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
