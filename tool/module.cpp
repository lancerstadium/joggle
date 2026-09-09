#include "module.h"

#include "joggle/joggle.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
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
  fs::create_directories(staging, error);
  if (!error)
    fs::copy(source, staged, fs::copy_options::recursive, error);
  if (error) {
    fs::remove_all(staging);
    std::cerr << "joggle: cannot stage module: " << error.message() << '\n';
    return 1;
  }

  bool valid = false;
  {
    Env env;
    env.path(staging.string());
    env.path(root.string());
    env.path(source.parent_path().string());
    add_paths(env, dependencies);
    valid = env.load(name);
    if (!valid)
      env.print_diags(stderr);
  }
  if (!valid) {
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
  if (action == "install") {
    if (argc < 5 || !paths(argc, argv, 5, roots))
      return 2;
    return install(argv[3], argv[4], roots);
  }
  if (action == "uninstall") {
    if (argc != 5)
      return 2;
    return uninstall(argv[3], argv[4]);
  }
  return 2;
}

}  // namespace joggle::tool
