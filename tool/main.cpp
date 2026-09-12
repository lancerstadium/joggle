#include "module.h"

#include "joggle/joggle.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

int usage() {
  std::cerr << "usage:\n"
               "  joggle check <file.jog> [-M <module-dir>]...\n"
               "  joggle read <module.fn> <file> [-M <module-dir>]...\n"
               "  joggle run <module.fn>... <file.jog> "
               "[--arg <Attr>]... [--report <file>] [-M <module-dir>]...\n"
               "  joggle query <module.fn> <file.jog> "
               "[--arg <Attr>]... [-M <module-dir>]...\n"
               "  joggle emit <module.fn> <file.jog> "
               "[--arg <Attr>]... [-M <module-dir>]...\n"
               "  joggle module list [-M <module-dir>]...\n"
               "  joggle module info <name> [-M <module-dir>]...\n"
               "  joggle module check <name> [-M <module-dir>]...\n"
               "  joggle module install <directory> <module-dir> "
               "[-M <dependency-dir>]...\n"
               "  joggle module upgrade <directory> <module-dir> "
               "[-M <dependency-dir>]...\n"
               "  joggle module uninstall <name> <module-dir>\n";
  return 2;
}

bool options(int argc, char** argv, int first, bool allow_report,
             bool allow_args, std::vector<fs::path>& roots,
             std::optional<fs::path>& report,
             std::vector<std::string>& args) {
  for (int index = first; index < argc; index += 2) {
    if (index + 1 >= argc)
      return false;
    const std::string_view option = argv[index];
    if (option == "-M") {
      roots.emplace_back(argv[index + 1]);
    } else if (allow_report && option == "--report" && !report) {
      report.emplace(argv[index + 1]);
    } else if (allow_args && option == "--arg") {
      args.emplace_back(argv[index + 1]);
    } else {
      return false;
    }
  }
  return true;
}

bool load_uses(joggle::Env& env, const joggle::Mod& mod) {
  for (const std::string& dependency : mod.uses()) {
    if (!env.load(dependency)) {
      env.print_diags(stderr);
      return false;
    }
  }
  return true;
}

int process(int argc, char** argv) {
  if (argc < 3)
    return usage();

  const std::string command = argv[1];
  const bool execute = command == "run";
  const bool decode = command == "read";
  const bool inspect = command == "query";
  const bool emit = command == "emit";
  if (command != "check" && !execute && !decode && !inspect && !emit)
    return usage();
  if ((!execute && !decode && !inspect && !emit && argc < 3) ||
      ((execute || decode || inspect || emit) && argc < 4))
    return usage();

  const bool selects_function = execute || decode || inspect || emit;
  std::vector<std::string> functions;
  std::string file;
  int first_option = 0;
  if (execute) {
    int positional_end = 2;
    while (positional_end < argc &&
           std::string_view(argv[positional_end]) != "-M" &&
           std::string_view(argv[positional_end]) != "--report" &&
           std::string_view(argv[positional_end]) != "--arg")
      ++positional_end;
    if (positional_end < 4)
      return usage();
    file = argv[positional_end - 1];
    for (int index = 2; index + 1 < positional_end; ++index)
      functions.emplace_back(argv[index]);
    first_option = positional_end;
  } else if (selects_function) {
    functions.emplace_back(argv[2]);
    file = argv[3];
    first_option = 4;
  } else {
    file = argv[2];
    first_option = 3;
  }
  std::vector<fs::path> roots;
  std::optional<fs::path> report_file;
  std::vector<std::string> argument_sources;
  if (!options(argc, argv, first_option, execute,
               execute || inspect || emit, roots, report_file,
               argument_sources))
    return usage();

  std::ifstream input(file, std::ios::binary);
  if (!input) {
    std::cerr << "joggle: cannot open " << file << '\n';
    return 1;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  std::string source = contents.str();

  joggle::Env env;
  for (const fs::path& root : roots)
    env.path(root.string());

  std::vector<joggle::Attr> arguments;
  arguments.reserve(argument_sources.size());
  for (const std::string& text : argument_sources) {
    joggle::Attr argument;
    if (!joggle::parse(env, text, argument, "<argument>")) {
      env.print_diags(stderr);
      return 1;
    }
    arguments.push_back(std::move(argument));
  }
  if (selects_function) {
    for (const std::string& function : functions) {
      const std::size_t dot = function.rfind('.');
      if (dot == std::string::npos || !env.load(function.substr(0, dot))) {
        env.print_diags(stderr);
        return 1;
      }
    }
  }

  const std::string& function = functions.empty() ? file : functions.front();

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
  if (!load_uses(env, mod))
    return 1;
  if (!mod.verify(env))
    return mod.print_diags(stderr);
  if (inspect || emit) {
    joggle::Attr result;
    if (!joggle::query(env, function, mod, result, arguments)) {
      env.print_diags(stderr);
      return 1;
    }
    if (inspect) {
      if (!joggle::print(stdout, result) || std::fputc('\n', stdout) == EOF)
        return 1;
    } else if (const auto value = result.string()) {
      if (std::fwrite(value->data(), 1, value->size(), stdout) != value->size())
        return 1;
    } else if (const auto* value = result.bytes()) {
      if (std::fwrite(value->data(), 1, value->size(), stdout) != value->size())
        return 1;
    } else {
      std::cerr << "joggle: emit function must return str or bytes\n";
      return 1;
    }
    return 0;
  }
  joggle::Attr report;
  if (execute) {
    bool ran = false;
    if (functions.size() == 1) {
      ran = report_file ? joggle::run(env, function, mod, report, arguments)
                        : joggle::run(env, function, mod, arguments);
    } else {
      std::vector<std::string_view> names;
      names.reserve(functions.size());
      for (const std::string& name : functions)
        names.emplace_back(name);
      ran = report_file ? joggle::run(env, names, mod, report, arguments)
                        : joggle::run(env, names, mod, arguments);
    }
    if (!ran) {
      mod.print_diags(stderr);
      env.print_diags(stderr);
      return 1;
    }
  }
  if (report_file) {
    std::ofstream output(*report_file, std::ios::binary | std::ios::trunc);
    if (!output || !(output << joggle::print(report) << '\n')) {
      std::cerr << "joggle: cannot write report " << *report_file << '\n';
      return 1;
    }
  }
  return joggle::print(stdout, mod) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::string_view(argv[1]) == "module") {
    const int result = joggle::tool::module(argc, argv);
    return result == 2 ? usage() : result;
  }
  return process(argc, argv);
}
