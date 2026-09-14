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
          std::vector<fs::path>& files, DiagFormat format) {
  files = source_files(directory);
  if (files.empty()) {
    print_error(stderr, "no module.jog in " + directory.string(), format);
    return false;
  }

  std::vector<Source> sources;
  sources.reserve(files.size());
  for (const fs::path& file : files) {
    std::ifstream input(file);
    if (!input) {
      print_error(stderr, "cannot open " + file.string(), format);
      return false;
    }
    std::ostringstream source;
    source << input.rdbuf();
    sources.push_back({source.str(), file.string()});
  }

  Env env;
  if (!parse(env, sources, mod)) {
    print_diags(stderr, mod.diags(), format);
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

bool compatible(const Mod& installed, const Mod& replacement,
                DiagFormat format) {
  std::multiset<std::string> available;
  for (Fn fn : replacement.fns())
    if (!fn.local())
      available.insert(signature(fn));
  for (Fn fn : installed.fns()) {
    if (fn.local())
      continue;
    const std::string required = signature(fn);
    const auto found = available.find(required);
    if (found == available.end()) {
      print_error(stderr,
                  "incompatible upgrade removes declaration: " +
                      std::string(installed.name()) + "." + required,
                  format, fn.loc());
      return false;
    }
    available.erase(found);
  }
  return true;
}

bool copy_module(const fs::path& source, const fs::path& staging,
                 std::string_view name, DiagFormat format) {
  std::error_code error;
  fs::create_directories(staging, error);
  if (!error)
    fs::copy(source, staging / name, fs::copy_options::recursive, error);
  if (!error)
    return true;
  fs::remove_all(staging);
  print_error(stderr, "cannot stage module: " + error.message(), format);
  return false;
}

bool validate(const fs::path& staging, const fs::path& root,
              const std::vector<fs::path>& dependencies,
              std::string_view name, DiagFormat format) {
  Env env;
  env.path(staging.string());
  env.path(root.string());
  add_paths(env, dependencies);
  const bool valid = env.load(name);
  if (!valid)
    print_diags(stderr, env.diags(), format);
  return valid;
}

bool affected_modules(std::string_view name, const fs::path& root,
                      std::vector<std::string>& out, DiagFormat format) {
  std::map<std::string, std::vector<std::string>, std::less<>> uses;
  for (const auto& [directory_name, directory] : available({root})) {
    (void)directory_name;
    Mod declaration;
    std::vector<fs::path> files;
    if (!read(directory, declaration, files, format))
      return false;
    uses.emplace(std::string(declaration.name()), declaration.uses());
  }

  std::set<std::string, std::less<>> affected{std::string(name)};
  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& [module, dependencies] : uses) {
      if (affected.contains(module))
        continue;
      if (std::any_of(dependencies.begin(), dependencies.end(),
                      [&](const std::string& dependency) {
                        return affected.contains(dependency);
                      })) {
        affected.insert(module);
        changed = true;
      }
    }
  }

  affected.erase(std::string(name));
  out.assign(affected.begin(), affected.end());
  return true;
}

std::string symbol(Fn fn) {
  return std::string(fn.module()) + '.' + signature(fn);
}

Fn resolve_call(const Env& env, Fn owner, Op call) {
  if (call.callee().empty())
    return {};
  bool ambiguous = false;
  const std::vector<Fn> candidates =
      env.resolve_fns(owner, call.callee());
  return env.match(call, candidates, &ambiguous);
}

bool preserves_resolutions(const Env& installed, const Env& replacement,
                           const Mod& dependent, DiagFormat format) {
  for (Fn fn : dependent.fns()) {
    for (Op call : fn.ops()) {
      const Fn old_target = resolve_call(installed, fn, call);
      if (!old_target)
        continue;
      const Fn new_target = resolve_call(replacement, fn, call);
      if (new_target && symbol(new_target) == symbol(old_target))
        continue;
      print_error(stderr,
                  "upgrade would break installed module '" +
                      std::string(dependent.name()) + "': call to '" +
                      std::string(call.callee()) + "' no longer resolves to " +
                      symbol(old_target),
                  format, call.loc());
      return false;
    }
  }
  return true;
}

bool validate_upgrade(const fs::path& staging, const fs::path& root,
                      const std::vector<fs::path>& dependencies,
                      std::string_view name, DiagFormat format) {
  std::vector<std::string> affected;
  if (!affected_modules(name, root, affected, format))
    return false;

  Env installed;
  installed.path(root.string());
  add_paths(installed, dependencies);

  Env replacement;
  replacement.path(staging.string());
  replacement.path(root.string());
  add_paths(replacement, dependencies);
  if (!replacement.load(name)) {
    print_diags(stderr, replacement.diags(), format);
    return false;
  }
  for (const std::string& module : affected) {
    if (!installed.load(module)) {
      print_error(stderr, "cannot validate installed module '" + module +
                              "' before upgrade", format);
      print_diags(stderr, installed.diags(), format);
      return false;
    }
    if (!replacement.load(module)) {
      print_error(stderr, "upgrade would break installed module '" + module +
                              "'", format);
      print_diags(stderr, replacement.diags(), format);
      return false;
    }
    Mod dependent;
    std::vector<fs::path> files;
    if (!read(root / module, dependent, files, format) ||
        !preserves_resolutions(installed, replacement, dependent, format))
      return false;
  }
  return true;
}

int list(const std::vector<fs::path>& roots) {
  for (const auto& [name, ignored] : available(roots)) {
    (void)ignored;
    std::cout << name << '\n';
  }
  return 0;
}

int check(std::string_view name, const std::vector<fs::path>& roots,
          DiagFormat format) {
  Env env;
  add_paths(env, roots);
  if (!env.load(name)) {
    print_diags(stderr, env.diags(), format);
    return 1;
  }
  return 0;
}

int info(std::string_view name, const std::vector<fs::path>& roots,
         DiagFormat format) {
  if (check(name, roots, format) != 0)
    return 1;
  const fs::path directory = locate(name, roots);
  if (directory.empty()) {
    print_error(stderr, "module not found: " + std::string(name), format);
    return 1;
  }

  Mod mod;
  std::vector<fs::path> files;
  if (!read(directory, mod, files, format))
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
    if (!fn.local())
      std::cout << declaration(fn) << '\n';
  return 0;
}

int install(const fs::path& source, const fs::path& root,
            const std::vector<fs::path>& dependencies, DiagFormat format) {
  if (!safe_tree(source)) {
    print_error(stderr,
                "module must contain only regular files and directories: " +
                    source.string(),
                format);
    return 1;
  }

  Mod declaration;
  std::vector<fs::path> files;
  if (!read(source, declaration, files, format))
    return 1;
  const std::string name(declaration.name());
  if (name.empty()) {
    print_error(stderr, "module has no name", format);
    return 1;
  }

  std::error_code error;
  fs::create_directories(root, error);
  if (error) {
    print_error(stderr, "cannot create module directory " + root.string() +
                            ": " + error.message(), format);
    return 1;
  }
  const fs::path target = root / name;
  if (fs::exists(target)) {
    print_error(stderr, "module already installed: " + name, format);
    return 1;
  }

  const fs::path staging = stage(root, "install", name);
  const fs::path staged = staging / name;
  if (!copy_module(source, staging, name, format))
    return 1;

  if (!validate(staging, root, dependencies, name, format)) {
    fs::remove_all(staging);
    return 1;
  }

  fs::rename(staged, target, error);
  if (error) {
    fs::remove_all(staging);
    print_error(stderr, "cannot install module: " + error.message(), format);
    return 1;
  }
  fs::remove(staging, error);
  std::cout << "installed " << name << " in " << root.string() << '\n';
  return 0;
}

int upgrade(const fs::path& source, const fs::path& root,
            const std::vector<fs::path>& dependencies, DiagFormat format) {
  if (!safe_tree(source)) {
    print_error(stderr,
                "module must contain only regular files and directories: " +
                    source.string(),
                format);
    return 1;
  }

  Mod replacement;
  std::vector<fs::path> replacement_files;
  if (!read(source, replacement, replacement_files, format))
    return 1;
  const std::string name(replacement.name());
  if (name.empty()) {
    print_error(stderr, "module has no name", format);
    return 1;
  }

  const fs::path target = root / name;
  if (!safe_tree(target)) {
    print_error(stderr, "module is not safely installed: " + name, format);
    return 1;
  }
  Mod installed;
  std::vector<fs::path> installed_files;
  if (!read(target, installed, installed_files, format))
    return 1;
  if (installed.name() != name) {
    print_error(stderr,
                "installed directory declares '" +
                    std::string(installed.name()) +
                    "', refusing to upgrade it as '" + name + "'",
                format);
    return 1;
  }
  if (!compatible(installed, replacement, format))
    return 1;

  const fs::path staging = stage(root, "upgrade", name);
  const fs::path staged = staging / name;
  if (!copy_module(source, staging, name, format))
    return 1;
  if (!validate_upgrade(staging, root, dependencies, name, format)) {
    std::error_code ignored;
    fs::remove_all(staging, ignored);
    return 1;
  }

  const fs::path backup = stage(root, "backup", name);
  std::error_code error;
  fs::rename(target, backup, error);
  if (error) {
    fs::remove_all(staging);
    print_error(stderr,
                "cannot detach installed module: " + error.message(), format);
    return 1;
  }
  fs::rename(staged, target, error);
  if (error) {
    std::error_code rollback;
    fs::rename(backup, target, rollback);
    if (rollback) {
      print_error(stderr,
                  "upgrade failed and the prior module remains at " +
                      backup.string() + ": " + rollback.message(),
                  format);
      return 1;
    }
    fs::remove_all(staging);
    print_error(stderr, "cannot commit upgrade: " + error.message(), format);
    return 1;
  }

  fs::remove_all(backup, error);
  if (!error)
    fs::remove(staging, error);
  if (error) {
    print_error(stderr,
                "module was upgraded but cleanup failed: " + error.message(),
                format);
    return 1;
  }
  std::cout << "upgraded " << name << " in " << root.string() << '\n';
  return 0;
}

bool find_dependents(std::string_view name, const fs::path& root,
                     std::vector<std::string>& out, DiagFormat format) {
  const fs::path target = (root / name).lexically_normal();
  for (const auto& [directory_name, directory] : available({root})) {
    (void)directory_name;
    if (directory.lexically_normal() == target)
      continue;

    Mod declaration;
    std::vector<fs::path> files;
    if (!read(directory, declaration, files, format))
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

int uninstall(std::string_view name, const fs::path& root,
              DiagFormat format) {
  const fs::path target = root / name;
  Mod declaration;
  std::vector<fs::path> files;
  if (!read(target, declaration, files, format))
    return 1;
  if (declaration.name() != name) {
    print_error(stderr,
                "module declares '" + std::string(declaration.name()) +
                    "', refusing to uninstall it as '" + std::string(name) +
                    "'",
                format);
    return 1;
  }

  std::vector<std::string> dependents;
  if (!find_dependents(name, root, dependents, format))
    return 1;
  if (!dependents.empty()) {
    std::string message = "cannot uninstall " + std::string(name) +
                          "; required by";
    for (const std::string& dependent : dependents)
      message += " " + dependent;
    print_error(stderr, std::move(message), format);
    return 1;
  }

  const fs::path staging = stage(root, "uninstall", name);
  std::error_code error;
  fs::rename(target, staging, error);
  if (error) {
    print_error(stderr, "cannot detach module: " + error.message(), format);
    return 1;
  }
  fs::remove_all(staging, error);
  if (error) {
    print_error(stderr,
                "module was detached but cleanup failed at " +
                    staging.string() + ": " + error.message(),
                format);
    return 1;
  }
  std::cout << "uninstalled " << name << " from " << root.string() << '\n';
  return 0;
}

}  // namespace

int module(int argc, char** argv, DiagFormat format) {
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
    return action == "info" ? info(argv[3], roots, format)
                            : check(argv[3], roots, format);
  }
  if (action == "install" || action == "upgrade") {
    if (argc < 5 || !paths(argc, argv, 5, roots))
      return 2;
    return action == "install" ? install(argv[3], argv[4], roots, format)
                               : upgrade(argv[3], argv[4], roots, format);
  }
  if (action == "uninstall") {
    if (argc != 5)
      return 2;
    return uninstall(argv[3], argv[4], format);
  }
  return 2;
}

}  // namespace joggle::tool
