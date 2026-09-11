#include "module.h"

#include "joggle/joggle.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace joggle::tool {
namespace {

namespace fs = std::filesystem;

bool paths(int argc, char** argv, int first, std::vector<fs::path>& out) {
  for (int index = first; index < argc; index += 2) {
    if (std::string_view(argv[index]) != "-M" || index + 1 >= argc)
      return false;
    out.emplace_back(argv[index + 1]);
  }
  return true;
}

void add_paths(Env& env, const std::vector<fs::path>& roots) {
  for (const fs::path& root : roots)
    env.path(root.string());
}

std::vector<fs::path> source_files(const fs::path& directory) {
  const fs::path declaration = directory / "module.jog";
  if (!fs::is_regular_file(declaration))
    return {};

  std::vector<fs::path> files{declaration};
  const fs::path library = directory / "lib";
  if (fs::is_directory(library)) {
    for (const fs::directory_entry& item : fs::directory_iterator(library)) {
      if (item.is_regular_file() && item.path().extension() == ".jog")
        files.push_back(item.path());
    }
    std::sort(files.begin() + 1, files.end());
  }
  return files;
}

bool read(const fs::path& directory, Mod& mod,
          std::vector<fs::path>& files) {
  files = source_files(directory);
  if (files.empty()) {
    std::cerr << "joggle: no module.jog in " << directory << '\n';
    return false;
  }

  std::ostringstream source;
  for (const fs::path& file : files) {
    std::ifstream input(file);
    if (!input) {
      std::cerr << "joggle: cannot open " << file << '\n';
      return false;
    }
    source << input.rdbuf() << '\n';
  }

  Env env;
  if (!parse(env, source.str(), mod, files.front().string())) {
    mod.print_diags(stderr);
    return false;
  }
  return true;
}

std::map<std::string, fs::path, std::less<>>
available(const std::vector<fs::path>& roots) {
  std::map<std::string, fs::path, std::less<>> result;
  for (const fs::path& root : roots) {
    std::error_code error;
    fs::directory_iterator items(root, error);
    if (error)
      continue;
    for (const fs::directory_entry& item : items) {
      if (!item.is_directory(error) || error)
        continue;
      const fs::path declaration = item.path() / "module.jog";
      if (fs::is_regular_file(declaration, error) && !error)
        result.try_emplace(item.path().filename().string(), item.path());
      error.clear();
    }
  }
  return result;
}

fs::path locate(std::string_view name, const std::vector<fs::path>& roots) {
  for (const fs::path& root : roots) {
    const fs::path candidate = root / name;
    if (fs::is_regular_file(candidate / "module.jog"))
      return candidate;
  }
  return {};
}

bool safe_tree(const fs::path& source) {
  std::error_code error;
  if (!fs::is_directory(source, error) || error || fs::is_symlink(source))
    return false;
  for (fs::recursive_directory_iterator items(source, error), end;
       !error && items != end; items.increment(error)) {
    const fs::file_status status = items->symlink_status(error);
    if (error || fs::is_symlink(status) ||
        (!fs::is_directory(status) && !fs::is_regular_file(status)))
      return false;
  }
  return !error;
}

fs::path stage(const fs::path& root, std::string_view action,
               std::string_view name) {
  const auto stamp =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return root / (".joggle-" + std::string(action) + "-" +
                 std::string(name) + "-" + std::to_string(stamp));
}

std::string type_key(const Ty& type,
                     const std::map<std::string, std::string>& generics) {
  const auto generic = generics.find(std::string(type.name()));
  if (type.args().empty() && generic != generics.end())
    return generic->second;
  std::string result(type.name());
  if (!type.args().empty()) {
    result += '<';
    for (std::size_t index = 0; index < type.args().size(); ++index) {
      if (index)
        result += ',';
      result += type_key(type.args()[index], generics);
    }
    result += '>';
  }
  return result;
}

std::string signature(Fn fn) {
  std::map<std::string, std::string> generics;
  const std::vector<Val> parameters = fn.generics();
  for (std::size_t index = 0; index < parameters.size(); ++index)
    generics.emplace(parameters[index].name(), "$" + std::to_string(index));

  std::string result(fn.name());
  if (!parameters.empty()) {
    result += '<';
    for (std::size_t index = 0; index < parameters.size(); ++index) {
      if (index)
        result += ',';
      result += "$" + std::to_string(index) + ':' +
                type_key(parameters[index].type(), generics);
    }
    result += '>';
  }
  result += '(';
  const std::vector<Val> inputs = fn.params();
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    if (index)
      result += ',';
    result += type_key(inputs[index].type(), generics);
  }
  result += ")->(";
  const std::vector<Ty> outputs = fn.returns();
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    if (index)
      result += ',';
    result += type_key(outputs[index], generics);
  }
  result += ')';
  return result;
}

std::string declaration(Fn fn) {
  std::ostringstream out;
  const std::string_view name = fn.name();
  const bool symbolic = name.starts_with("operator ");
  const std::string_view spelling = symbolic ? name.substr(9) : name;
  out << "fn " << spelling;

  const std::vector<Val> generics = fn.generics();
  if (!generics.empty()) {
    if (symbolic && spelling.find('<') != std::string_view::npos)
      out << ' ';
    out << '<';
    for (std::size_t index = 0; index < generics.size(); ++index) {
      if (index)
        out << ", ";
      out << generics[index].name();
      if (generics[index].type().text() != "_")
        out << ": " << generics[index].type().text();
    }
    out << '>';
  }

  out << '(';
  const std::vector<Val> inputs = fn.params();
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    if (index)
      out << ", ";
    out << inputs[index].name() << ": " << inputs[index].type().text();
  }
  out << ") -> ";
  const std::vector<Ty> outputs = fn.returns();
  if (outputs.size() != 1)
    out << '(';
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    if (index)
      out << ", ";
    out << outputs[index].text();
  }
  if (outputs.size() != 1)
    out << ')';
  out << ';';
  return out.str();
}

bool compatible(const Mod& installed, const Mod& replacement) {
  std::multiset<std::string> available;
  for (Fn fn : replacement.fns())
    available.insert(signature(fn));
  for (Fn fn : installed.fns()) {
    const std::string required = signature(fn);
    const auto found = available.find(required);
    if (found == available.end()) {
      std::cerr << "joggle: incompatible upgrade removes declaration: "
                << installed.name() << '.' << required << '\n';
      return false;
    }
    available.erase(found);
  }
  return true;
}

bool copy_module(const fs::path& source, const fs::path& staging,
                 std::string_view name) {
  std::error_code error;
  fs::create_directories(staging, error);
  if (!error)
    fs::copy(source, staging / name, fs::copy_options::recursive, error);
  if (!error)
    return true;
  fs::remove_all(staging);
  std::cerr << "joggle: cannot stage module: " << error.message() << '\n';
  return false;
}

bool validate(const fs::path& staging, const fs::path& root,
              const fs::path& source,
              const std::vector<fs::path>& dependencies,
              std::string_view name) {
  Env env;
  env.path(staging.string());
  env.path(root.string());
  env.path(source.parent_path().string());
  add_paths(env, dependencies);
  const bool valid = env.load(name);
  if (!valid)
    env.print_diags(stderr);
  return valid;
}

int list(const std::vector<fs::path>& roots) {
  for (const auto& [name, ignored] : available(roots)) {
    (void)ignored;
    std::cout << name << '\n';
  }
  return 0;
}

int check(std::string_view name, const std::vector<fs::path>& roots) {
  Env env;
  add_paths(env, roots);
  if (!env.load(name)) {
    env.print_diags(stderr);
    return 1;
  }
  return 0;
}

int info(std::string_view name, const std::vector<fs::path>& roots) {
  const fs::path directory = locate(name, roots);
  if (directory.empty()) {
    std::cerr << "joggle: module not found: " << name << '\n';
    return 1;
  }
  if (check(name, roots) != 0)
    return 1;

  Mod mod;
  std::vector<fs::path> files;
  if (!read(directory, mod, files))
    return 1;

  std::cout << "module " << mod.name() << '\n';
  std::cout << "path " << fs::absolute(directory).lexically_normal().string()
            << '\n';
  for (const std::string& dependency : mod.uses())
    std::cout << "use " << dependency << '\n';
  for (const fs::path& file : files)
    std::cout << "source " << fs::relative(file, directory).string() << '\n';

  const fs::path native = directory / "native";
  if (fs::is_directory(native)) {
    std::vector<fs::path> libraries;
    for (const fs::directory_entry& item : fs::directory_iterator(native)) {
      if (item.is_regular_file())
        libraries.push_back(item.path().filename());
    }
    std::sort(libraries.begin(), libraries.end());
    for (const fs::path& library : libraries)
      std::cout << "native " << library.string() << '\n';
  }
  for (Fn fn : mod.fns())
    std::cout << declaration(fn) << '\n';
  return 0;
}

int install(const fs::path& source, const fs::path& root,
            const std::vector<fs::path>& dependencies) {
  if (!safe_tree(source)) {
    std::cerr << "joggle: module must contain only regular files and "
                 "directories: "
              << source << '\n';
    return 1;
  }

  Mod declaration;
  std::vector<fs::path> files;
  if (!read(source, declaration, files))
    return 1;
  const std::string name(declaration.name());
  if (name.empty()) {
    std::cerr << "joggle: module has no name\n";
    return 1;
  }

  std::error_code error;
  fs::create_directories(root, error);
  if (error) {
    std::cerr << "joggle: cannot create module directory " << root << ": "
              << error.message() << '\n';
    return 1;
  }
  const fs::path target = root / name;
  if (fs::exists(target)) {
    std::cerr << "joggle: module already installed: " << name << '\n';
    return 1;
  }

  const fs::path staging = stage(root, "install", name);
  const fs::path staged = staging / name;
  if (!copy_module(source, staging, name))
    return 1;

  if (!validate(staging, root, source, dependencies, name)) {
    fs::remove_all(staging);
    return 1;
  }

  fs::rename(staged, target, error);
  if (error) {
    fs::remove_all(staging);
    std::cerr << "joggle: cannot install module: " << error.message() << '\n';
    return 1;
  }
  fs::remove(staging, error);
  std::cout << "installed " << name << " in " << root.string() << '\n';
  return 0;
}

int upgrade(const fs::path& source, const fs::path& root,
            const std::vector<fs::path>& dependencies) {
  if (!safe_tree(source)) {
    std::cerr << "joggle: module must contain only regular files and "
                 "directories: "
              << source << '\n';
    return 1;
  }

  Mod replacement;
  std::vector<fs::path> replacement_files;
  if (!read(source, replacement, replacement_files))
    return 1;
  const std::string name(replacement.name());
  if (name.empty()) {
    std::cerr << "joggle: module has no name\n";
    return 1;
  }

  const fs::path target = root / name;
  if (!safe_tree(target)) {
    std::cerr << "joggle: module is not safely installed: " << name << '\n';
    return 1;
  }
  Mod installed;
  std::vector<fs::path> installed_files;
  if (!read(target, installed, installed_files))
    return 1;
  if (installed.name() != name) {
    std::cerr << "joggle: installed directory declares '" << installed.name()
              << "', refusing to upgrade it as '" << name << "'\n";
    return 1;
  }
  if (!compatible(installed, replacement))
    return 1;

  const fs::path staging = stage(root, "upgrade", name);
  const fs::path staged = staging / name;
  if (!copy_module(source, staging, name))
    return 1;
  if (!validate(staging, root, source, dependencies, name)) {
    std::error_code ignored;
    fs::remove_all(staging, ignored);
    return 1;
  }

  const fs::path backup = stage(root, "backup", name);
  std::error_code error;
  fs::rename(target, backup, error);
  if (error) {
    fs::remove_all(staging);
    std::cerr << "joggle: cannot detach installed module: "
              << error.message() << '\n';
    return 1;
  }
  fs::rename(staged, target, error);
  if (error) {
    std::error_code rollback;
    fs::rename(backup, target, rollback);
    if (rollback) {
      std::cerr << "joggle: upgrade failed and the prior module remains at "
                << backup << ": " << rollback.message() << '\n';
      return 1;
    }
    fs::remove_all(staging);
    std::cerr << "joggle: cannot commit upgrade: " << error.message() << '\n';
    return 1;
  }

  fs::remove_all(backup, error);
  if (!error)
    fs::remove(staging, error);
  if (error) {
    std::cerr << "joggle: module was upgraded but cleanup failed: "
              << error.message() << '\n';
    return 1;
  }
  std::cout << "upgraded " << name << " in " << root.string() << '\n';
  return 0;
}

bool find_dependents(std::string_view name, const fs::path& root,
                     std::vector<std::string>& out) {
  const fs::path target = (root / name).lexically_normal();
  for (const auto& [directory_name, directory] : available({root})) {
    (void)directory_name;
    if (directory.lexically_normal() == target)
      continue;

    Mod declaration;
    std::vector<fs::path> files;
    if (!read(directory, declaration, files))
      return false;
    const std::vector<std::string> dependencies = declaration.uses();
    if (std::find(dependencies.begin(), dependencies.end(), name) !=
        dependencies.end())
      out.emplace_back(declaration.name());
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return true;
}

int uninstall(std::string_view name, const fs::path& root) {
  const fs::path target = root / name;
  Mod declaration;
  std::vector<fs::path> files;
  if (!read(target, declaration, files))
    return 1;
  if (declaration.name() != name) {
    std::cerr << "joggle: module declares '" << declaration.name()
              << "', refusing to uninstall it as '" << name << "'\n";
    return 1;
  }

  std::vector<std::string> dependents;
  if (!find_dependents(name, root, dependents))
    return 1;
  if (!dependents.empty()) {
    std::cerr << "joggle: cannot uninstall " << name << "; required by";
    for (const std::string& dependent : dependents)
      std::cerr << ' ' << dependent;
    std::cerr << '\n';
    return 1;
  }

  const fs::path staging = stage(root, "uninstall", name);
  std::error_code error;
  fs::rename(target, staging, error);
  if (error) {
    std::cerr << "joggle: cannot detach module: " << error.message() << '\n';
    return 1;
  }
  fs::remove_all(staging, error);
  if (error) {
    std::cerr << "joggle: module was detached but cleanup failed at "
              << staging << ": " << error.message() << '\n';
    return 1;
  }
  std::cout << "uninstalled " << name << " from " << root.string() << '\n';
  return 0;
}

}  // namespace

int module(int argc, char** argv) {
  if (argc < 3)
    return 2;
  const std::string_view action = argv[2];
  std::vector<fs::path> roots;
  if (action == "list") {
    if (!paths(argc, argv, 3, roots))
      return 2;
    return list(roots);
  }
  if (action == "info" || action == "check") {
    if (argc < 4 || !paths(argc, argv, 4, roots))
      return 2;
    return action == "info" ? info(argv[3], roots) : check(argv[3], roots);
  }
  if (action == "install" || action == "upgrade") {
    if (argc < 5 || !paths(argc, argv, 5, roots))
      return 2;
    return action == "install" ? install(argv[3], argv[4], roots)
                               : upgrade(argv[3], argv[4], roots);
  }
  if (action == "uninstall") {
    if (argc != 5)
      return 2;
    return uninstall(argv[3], argv[4]);
  }
  return 2;
}

}  // namespace joggle::tool
