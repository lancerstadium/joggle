#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

#include "module_internal.h"
#include "module_repository.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

void usage(std::ostream& output) {
  output << "usage:\n"
         << "  joggle check <file.joggle> [--with <module.joggle>] "
            "[--behavior <library>] [--root <directory>]\n"
         << "  joggle fmt <file.joggle> [--write | -o <file>]\n"
         << "  joggle run <file.joggle> <function> [transform ...] "
            "[--with <module.joggle>] [--load-behavior <module[=library]>] "
            "[--behavior <library>] [--root <directory>] [-o <file>]\n"
         << "  joggle install <module.joggle> [--behavior <library>] "
            "[--root <directory>]\n"
         << "  joggle uninstall <name@version> [--root <directory>]\n"
         << "  joggle list [--root <directory>]\n"
         << "  joggle lock <root.joggle> [--root <directory>] [-o <file>]\n";
}

std::optional<std::string> read(const std::filesystem::path& path,
                                joggle::Diagnostics& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.report("cannot open '" + path.string() + "'");
    return std::nullopt;
  }
  std::ostringstream text;
  text << input.rdbuf();
  if (!input.eof() && input.fail()) {
    diagnostics.report("cannot read '" + path.string() + "'");
    return std::nullopt;
  }
  return text.str();
}

struct ExactModule {
  std::string name;
  joggle::Version version;
};

std::optional<ExactModule> exact_module(std::string_view request,
                                        joggle::Diagnostics& diagnostics) {
  const std::string source = "joggle 1; module " + std::string(request) + " {}";
  auto module = joggle::parse_module(source, diagnostics, "<module request>");
  if (!module) {
    return std::nullopt;
  }
  return ExactModule{std::string(module->name()), module->version()};
}

struct Options {
  std::filesystem::path root = joggle::detail::default_module_root();
  std::optional<std::filesystem::path> output;
  std::optional<std::filesystem::path> behavior;
  std::vector<std::filesystem::path> with;
  std::vector<std::string> loaded_behaviors;
  bool root_explicit = false;
  bool in_place = false;
  std::vector<std::string> positional;
};

std::optional<Options> options(int argc, char** argv,
                               joggle::Diagnostics& diagnostics) {
  Options result;
  for (int index = 2; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value =
        [&](std::string_view message) -> std::optional<std::string_view> {
      if (index + 1 >= argc ||
          std::string_view(argv[index + 1]).starts_with('-')) {
        diagnostics.report(std::string(message));
        return std::nullopt;
      }
      return std::string_view(argv[++index]);
    };
    if (argument == "--") {
      while (++index < argc) {
        result.positional.emplace_back(argv[index]);
      }
      break;
    }
    if (argument == "--root") {
      if (result.root_explicit) {
        diagnostics.report("duplicate option '--root'");
        return std::nullopt;
      }
      const auto directory = value("--root needs a directory");
      if (!directory) {
        return std::nullopt;
      }
      result.root_explicit = true;
      result.root = *directory;
    } else if (argument == "-o" || argument == "--output") {
      if (result.output) {
        diagnostics.report("duplicate option '--output'");
        return std::nullopt;
      }
      const auto file = value("--output needs a file");
      if (!file) {
        return std::nullopt;
      }
      result.output = std::filesystem::path(*file);
    } else if (argument == "--behavior") {
      if (result.behavior) {
        diagnostics.report("duplicate option '--behavior'");
        return std::nullopt;
      }
      const auto library = value("--behavior needs a shared library");
      if (!library) {
        return std::nullopt;
      }
      result.behavior = std::filesystem::path(*library);
    } else if (argument == "--with") {
      const auto file = value("--with needs a Module source file");
      if (!file) {
        return std::nullopt;
      }
      result.with.emplace_back(*file);
    } else if (argument == "--load-behavior") {
      const auto request =
          value("--load-behavior needs module or module=library");
      if (!request) {
        return std::nullopt;
      }
      result.loaded_behaviors.emplace_back(*request);
    } else if (argument == "-w" || argument == "--write") {
      if (result.in_place) {
        diagnostics.report("duplicate option '--write'");
        return std::nullopt;
      }
      result.in_place = true;
    } else if (argument.starts_with('-')) {
      diagnostics.report("unknown option '" + std::string(argument) + "'");
      return std::nullopt;
    } else {
      result.positional.emplace_back(argument);
    }
  }
  return result;
}

class PendingWrite {
public:
  explicit PendingWrite(std::filesystem::path directory)
      : directory_(std::move(directory)) {}
  ~PendingWrite() {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }
  PendingWrite(const PendingWrite&) = delete;
  PendingWrite& operator=(const PendingWrite&) = delete;

  const std::filesystem::path& directory() const { return directory_; }

private:
  std::filesystem::path directory_;
};

std::optional<std::filesystem::path>
pending_directory(const std::filesystem::path& target,
                  joggle::Diagnostics& diagnostics) {
  const std::filesystem::path parent =
      target.has_parent_path() ? target.parent_path() : ".";
  const std::string prefix = "." + target.filename().string() + ".joggle-";
  std::error_code error;
  for (std::size_t attempt = 0; attempt < 4096U; ++attempt) {
    const std::filesystem::path candidate =
        parent / (prefix + std::to_string(attempt));
    if (std::filesystem::create_directory(candidate, error)) {
      return candidate;
    }
    if (error && error != std::errc::file_exists) {
      diagnostics.report("cannot stage '" + target.string() +
                         "': " + error.message());
      return std::nullopt;
    }
    error.clear();
  }
  diagnostics.report("too many unfinished writes beside '" + target.string() +
                     "'");
  return std::nullopt;
}

bool publish(const std::filesystem::path& source,
             const std::filesystem::path& target,
             joggle::Diagnostics& diagnostics) {
#if defined(_WIN32)
  if (MoveFileExW(source.c_str(), target.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
    return true;
  }
  diagnostics.report("cannot replace '" + target.string() + "'");
  return false;
#else
  std::error_code error;
  std::filesystem::rename(source, target, error);
  if (!error) {
    return true;
  }
  diagnostics.report("cannot replace '" + target.string() +
                     "': " + error.message());
  return false;
#endif
}

bool write(const std::filesystem::path& requested, std::string_view text,
           joggle::Diagnostics& diagnostics) {
  std::filesystem::path target = requested;
  std::error_code error;
  const auto requested_status = std::filesystem::symlink_status(target, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    diagnostics.report("cannot inspect '" + target.string() +
                       "': " + error.message());
    return false;
  }
  error.clear();
  if (std::filesystem::is_symlink(requested_status)) {
    target = std::filesystem::canonical(target, error);
    if (error) {
      diagnostics.report("cannot resolve '" + requested.string() +
                         "': " + error.message());
      return false;
    }
  }

  std::optional<std::filesystem::perms> permissions;
  if (std::filesystem::exists(target, error)) {
    if (!std::filesystem::is_regular_file(target, error) || error) {
      diagnostics.report("output path is not a regular file: '" +
                         target.string() + "'");
      return false;
    }
    permissions = std::filesystem::status(target, error).permissions();
    if (error) {
      diagnostics.report("cannot inspect '" + target.string() +
                         "': " + error.message());
      return false;
    }
  } else if (error) {
    diagnostics.report("cannot inspect '" + target.string() +
                       "': " + error.message());
    return false;
  }

  auto directory = pending_directory(target, diagnostics);
  if (!directory) {
    return false;
  }
  PendingWrite pending(std::move(*directory));
  const std::filesystem::path staged = pending.directory() / "content";
  std::ofstream output(staged, std::ios::binary | std::ios::trunc);
  if (!output) {
    diagnostics.report("cannot write '" + target.string() + "'");
    return false;
  }
  output << text;
  output.close();
  if (!output) {
    diagnostics.report("cannot finish writing '" + target.string() + "'");
    return false;
  }
  if (permissions) {
    std::filesystem::permissions(staged, *permissions,
                                 std::filesystem::perm_options::replace, error);
    if (error) {
      diagnostics.report("cannot preserve permissions for '" + target.string() +
                         "': " + error.message());
      return false;
    }
  }
  return publish(staged, target, diagnostics);
}

int fail(const joggle::Diagnostics& diagnostics) {
  diagnostics.print(std::cerr);
  return EXIT_FAILURE;
}

int usage_error(joggle::Diagnostics& diagnostics, std::string message) {
  diagnostics.report(std::move(message));
  diagnostics.print(std::cerr);
  usage(std::cerr);
  return EXIT_FAILURE;
}

bool validate_module(joggle::Compiler& compiler, const joggle::Module& module,
                     std::string_view source,
                     const std::filesystem::path& source_path,
                     const std::filesystem::path& root,
                     const std::optional<std::filesystem::path>& behavior,
                     std::span<const std::filesystem::path> with = {}) {
  compiler.search(root);
  compiler.add(source, source_path.string());
  for (const std::filesystem::path& path : with) {
    compiler.load(path);
  }
  if (!compiler.link()) {
    return false;
  }
  const auto linked = compiler.module(module.name());
  if (!linked) {
    return false;
  }
  if (behavior && !compiler.load_behavior(linked->name(), *behavior)) {
    return false;
  }
  for (const joggle::Module& loaded : compiler.modules()) {
    for (const joggle::Module::FunctionDecl& function : loaded.functions()) {
      if (function.form() == joggle::Module::FunctionDecl::Form::Body &&
          joggle::detail::ModuleAccess::expression(function) == nullptr &&
          (!joggle::detail::ir_inputs(function).empty() ||
           !joggle::detail::ir_results(function).empty()) &&
          joggle::detail::has_default_specialization(function) &&
          !compiler.function(function.symbol())) {
        return false;
      }
    }
  }
  return true;
}

enum class TransformKind { Function, Module };

struct Transform {
  joggle::Module::FunctionDecl declaration;
  TransformKind kind;
};

std::optional<Transform> transform(joggle::Compiler& compiler,
                                   std::string_view qualified,
                                   joggle::Diagnostics& diagnostics) {
  const std::size_t separator = qualified.find('.');
  if (separator == std::string_view::npos) {
    diagnostics.report("a transform name must use module.member");
    return std::nullopt;
  }
  const std::string_view module_name = qualified.substr(0U, separator);
  const std::string_view function_name = qualified.substr(separator + 1U);
  const auto module = compiler.module(module_name);
  if (!module) {
    diagnostics.report("transform references unknown module '" +
                       std::string(module_name) + "'");
    return std::nullopt;
  }
  std::vector<Transform> matches;
  for (const joggle::Module::FunctionDecl& function : module->functions()) {
    if (function.name() != function_name || function.inputs().size() != 1U ||
        function.results().size() != 1U) {
      continue;
    }
    if (compiler.invocable<joggle::Module, joggle::Module>(function)) {
      matches.push_back({function, TransformKind::Module});
    } else if (compiler.invocable<joggle::ir::Function, joggle::ir::Function&>(
                   function)) {
      matches.push_back({function, TransformKind::Function});
    }
  }
  if (matches.size() != 1U) {
    diagnostics.report(matches.empty()
                           ? "no unary Function or module transform named '" +
                                 std::string(qualified) + "'"
                           : "transform name '" + std::string(qualified) +
                                 "' is ambiguous");
    return std::nullopt;
  }
  return matches.front();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage(std::cerr);
    return EXIT_FAILURE;
  }
  const std::string_view command(argv[1]);
  if (command == "help" || command == "--help" || command == "-h") {
    usage(std::cout);
    return EXIT_SUCCESS;
  }

  joggle::Diagnostics diagnostics;
  constexpr std::array<std::string_view, 7> commands{
      "check", "fmt", "run", "install", "uninstall", "list", "lock"};
  if (std::find(commands.begin(), commands.end(), command) == commands.end()) {
    return usage_error(diagnostics,
                       "unknown command '" + std::string(command) + "'");
  }
  auto parsed_options = options(argc, argv, diagnostics);
  if (!parsed_options) {
    return fail(diagnostics);
  }
  Options& parsed = *parsed_options;

  if (command == "check" || command == "fmt") {
    if (parsed.positional.size() != 1U) {
      return usage_error(diagnostics, std::string(command) +
                                          " expects one Module source file");
    }
    if (parsed.output && parsed.in_place) {
      return usage_error(diagnostics,
                         "--output and --write are mutually exclusive");
    }
    if (command == "fmt" && parsed.behavior) {
      return usage_error(diagnostics, "fmt does not accept --behavior");
    }
    if (command == "fmt" && parsed.root_explicit) {
      return usage_error(diagnostics, "fmt does not accept --root");
    }
    if (command == "fmt" && !parsed.with.empty()) {
      return usage_error(diagnostics, "fmt does not accept --with");
    }
    if (command == "check" && parsed.output) {
      return usage_error(diagnostics, "check does not accept --output");
    }
    if (command == "check" && parsed.in_place) {
      return usage_error(diagnostics, "check does not accept --write");
    }
    if (!parsed.loaded_behaviors.empty()) {
      return usage_error(diagnostics, std::string(command) +
                                          " does not accept --load-behavior");
    }
    auto source = read(parsed.positional[0], diagnostics);
    if (!source) {
      return fail(diagnostics);
    }
    auto module =
        joggle::parse_module(*source, diagnostics, parsed.positional[0]);
    if (!module) {
      return fail(diagnostics);
    }
    if (command == "fmt") {
      const std::string formatted = joggle::format(*module);
      if (parsed.in_place) {
        if (!write(parsed.positional[0], formatted, diagnostics)) {
          return fail(diagnostics);
        }
      } else if (parsed.output) {
        if (!write(*parsed.output, formatted, diagnostics)) {
          return fail(diagnostics);
        }
      } else {
        std::cout << formatted;
      }
    } else {
      joggle::Compiler compiler;
      if (!validate_module(compiler, *module, *source, parsed.positional[0],
                           parsed.root, parsed.behavior, parsed.with)) {
        return fail(compiler.diagnostics());
      }
      const auto linked = compiler.module(module->name());
      if (!linked) {
        return fail(compiler.diagnostics());
      }
      std::cout << linked->name() << '@' << joggle::to_string(linked->version())
                << '#' << linked->digest() << '\n';
    }
    return EXIT_SUCCESS;
  }

  if (command == "run") {
    if (parsed.positional.size() < 2U) {
      return usage_error(diagnostics,
                         "run expects a Module source file and function name");
    }
    if (parsed.in_place) {
      return usage_error(diagnostics, "run does not accept --write");
    }
    const std::filesystem::path root_path = parsed.positional.front();
    auto root_source = read(root_path, diagnostics);
    if (!root_source) {
      return fail(diagnostics);
    }
    auto root =
        joggle::parse_module(*root_source, diagnostics, root_path.string());
    if (!root) {
      return fail(diagnostics);
    }

    joggle::Compiler compiler;
    compiler.search(parsed.root);
    compiler.add(*root_source, root_path.string());
    for (const std::filesystem::path& path : parsed.with) {
      auto source = read(path, diagnostics);
      if (!source) {
        return fail(diagnostics);
      }
      compiler.add(*source, path.string());
    }
    if (!compiler.link()) {
      return fail(compiler.diagnostics());
    }
    if (parsed.behavior &&
        !compiler.load_behavior(root->name(), *parsed.behavior)) {
      return fail(compiler.diagnostics());
    }
    for (const std::string& request : parsed.loaded_behaviors) {
      const std::size_t separator = request.find('=');
      const std::string_view module =
          separator == std::string::npos
              ? std::string_view(request)
              : std::string_view(request).substr(0U, separator);
      const bool loaded =
          separator == std::string::npos
              ? compiler.load_behavior(module)
              : compiler.load_behavior(module, request.substr(separator + 1U));
      if (!loaded) {
        return fail(compiler.diagnostics());
      }
    }

    const auto qualified = [&](std::string_view member) {
      return member.find('.') == std::string_view::npos
                 ? std::string(root->name()) + "." + std::string(member)
                 : std::string(member);
    };
    const std::string function_name = qualified(parsed.positional[1]);
    auto function = compiler.function(function_name);
    if (!function) {
      return fail(compiler.diagnostics());
    }
    const std::size_t separator = function_name.find('.');
    const std::string function_body = function_name.substr(separator + 1U);
    std::string artifact_name(root->name());
    const auto root_members = root->members();
    const bool already_derived =
        artifact_name.ends_with("_compiled") && root_members.size() == 1U &&
        root_members.front().kind() == joggle::Module::SymbolKind::Function &&
        root_members.front().local_name() == function_body;
    if (!already_derived) {
      artifact_name += "_" + function_body + "_compiled";
    }
    joggle::Module module(artifact_name, root->version());
    if (!module.insert(function_body, std::move(*function), diagnostics)) {
      return fail(diagnostics);
    }
    for (std::size_t index = 2U; index < parsed.positional.size(); ++index) {
      const std::string name = qualified(parsed.positional[index]);
      auto selected = transform(compiler, name, diagnostics);
      if (!selected) {
        return fail(diagnostics);
      }
      if (selected->kind == TransformKind::Module) {
        auto transformed = compiler.run<joggle::Module>(selected->declaration,
                                                        std::move(module));
        if (!transformed) {
          return fail(compiler.diagnostics());
        }
        module = std::move(*transformed);
        continue;
      }
      joggle::ir::Function* entry = module.body(function_body);
      if (entry == nullptr) {
        diagnostics.report("Function transform '" + name +
                           "' needs missing entry '" + function_body + "'");
        return fail(diagnostics);
      }
      if (!compiler.run(*entry, selected->declaration)) {
        return fail(compiler.diagnostics());
      }
    }
    const auto dependencies = module.dependencies();
    const auto references = [&](std::string_view name) {
      return std::any_of(dependencies.begin(), dependencies.end(),
                         [&](const joggle::Module::Dependency& dependency) {
                           return dependency.name == name;
                         });
    };
    if (references(artifact_name)) {
      diagnostics.report("compiled Module name '" + artifact_name +
                         "' conflicts with a referenced Module");
      return fail(diagnostics);
    }
    std::string artifact_source;
    try {
      artifact_source = joggle::format(module);
    } catch (const std::exception& exception) {
      diagnostics.report(exception.what());
      return fail(diagnostics);
    }
    const auto artifact =
        joggle::parse_module(artifact_source, diagnostics, "<compiled Module>");
    if (!artifact) {
      return fail(diagnostics);
    }
    const std::string output = joggle::format(*artifact);
    if (parsed.output) {
      if (!write(*parsed.output, output, diagnostics)) {
        return fail(diagnostics);
      }
    } else {
      std::cout << output;
    }
    return EXIT_SUCCESS;
  }

  if (command == "install") {
    if (parsed.positional.size() != 1U) {
      return usage_error(diagnostics, "install expects one Module source file");
    }
    if (parsed.output) {
      return usage_error(diagnostics, "install does not accept --output");
    }
    if (parsed.in_place) {
      return usage_error(diagnostics, "install does not accept --write");
    }
    if (!parsed.with.empty()) {
      return usage_error(diagnostics, "install does not accept --with");
    }
    if (!parsed.loaded_behaviors.empty()) {
      return usage_error(diagnostics,
                         "install does not accept --load-behavior");
    }
    auto source = read(parsed.positional[0], diagnostics);
    if (!source) {
      return fail(diagnostics);
    }
    auto module =
        joggle::parse_module(*source, diagnostics, parsed.positional[0]);
    if (!module) {
      return fail(diagnostics);
    }
    joggle::Compiler compiler;
    if (!validate_module(compiler, *module, *source, parsed.positional[0],
                         parsed.root, parsed.behavior)) {
      return fail(compiler.diagnostics());
    }
    auto installed = joggle::detail::install_module(
        parsed.root, *module, diagnostics, parsed.behavior);
    if (!installed) {
      return fail(diagnostics);
    }
    std::cout << installed->string() << '\n';
    return EXIT_SUCCESS;
  }

  if (command == "uninstall") {
    if (parsed.positional.size() != 1U) {
      return usage_error(diagnostics,
                         "uninstall expects one exact name@version");
    }
    if (parsed.output) {
      return usage_error(diagnostics, "uninstall does not accept --output");
    }
    if (parsed.behavior) {
      return usage_error(diagnostics, "uninstall does not accept --behavior");
    }
    if (parsed.in_place) {
      return usage_error(diagnostics, "uninstall does not accept --write");
    }
    if (!parsed.with.empty()) {
      return usage_error(diagnostics, "uninstall does not accept --with");
    }
    if (!parsed.loaded_behaviors.empty()) {
      return usage_error(diagnostics,
                         "uninstall does not accept --load-behavior");
    }
    auto module = exact_module(parsed.positional[0], diagnostics);
    if (!module ||
        !joggle::detail::remove_module(parsed.root, module->name,
                                       module->version, diagnostics)) {
      return fail(diagnostics);
    }
    std::cout << "uninstalled " << module->name << '@'
              << joggle::to_string(module->version) << '\n';
    return EXIT_SUCCESS;
  }

  if (command == "list") {
    if (!parsed.positional.empty()) {
      return usage_error(diagnostics, "list does not accept positional input");
    }
    if (parsed.output) {
      return usage_error(diagnostics, "list does not accept --output");
    }
    if (parsed.behavior) {
      return usage_error(diagnostics, "list does not accept --behavior");
    }
    if (parsed.in_place) {
      return usage_error(diagnostics, "list does not accept --write");
    }
    if (!parsed.with.empty()) {
      return usage_error(diagnostics, "list does not accept --with");
    }
    if (!parsed.loaded_behaviors.empty()) {
      return usage_error(diagnostics, "list does not accept --load-behavior");
    }
    const auto modules =
        joggle::detail::installed_modules(parsed.root, diagnostics);
    if (!diagnostics.ok()) {
      return fail(diagnostics);
    }
    for (const auto& installed : modules) {
      std::cout << installed.module.name() << '@'
                << joggle::to_string(installed.module.version()) << '#'
                << installed.module.digest() << '\n';
    }
    return EXIT_SUCCESS;
  }

  if (command == "lock") {
    if (parsed.positional.size() != 1U) {
      return usage_error(diagnostics, "lock expects one root Module source");
    }
    if (parsed.behavior) {
      return usage_error(diagnostics, "lock does not accept --behavior");
    }
    if (parsed.in_place) {
      return usage_error(diagnostics, "lock does not accept --write");
    }
    if (!parsed.with.empty()) {
      return usage_error(diagnostics, "lock does not accept --with");
    }
    if (!parsed.loaded_behaviors.empty()) {
      return usage_error(diagnostics, "lock does not accept --load-behavior");
    }
    auto source = read(parsed.positional[0], diagnostics);
    if (!source) {
      return fail(diagnostics);
    }
    auto root =
        joggle::parse_module(*source, diagnostics, parsed.positional[0]);
    if (!root) {
      return fail(diagnostics);
    }
    joggle::Compiler compiler;
    if (!validate_module(compiler, *root, *source, parsed.positional[0],
                         parsed.root, std::nullopt)) {
      return fail(compiler.diagnostics());
    }
    std::ostringstream lock;
    lock << "joggle-lock 1;\n"
         << "root " << root->name() << '@' << joggle::to_string(root->version())
         << '#' << root->digest() << ";\n";
    for (const joggle::Module& module : compiler.modules()) {
      if (module.name() == root->name() &&
          module.version() == root->version() &&
          module.digest() == root->digest()) {
        continue;
      }
      lock << "module " << module.name() << '@'
           << joggle::to_string(module.version()) << '#' << module.digest()
           << ";\n";
    }
    for (const joggle::Module& module : compiler.modules()) {
      const std::array roots{parsed.root};
      auto installed = joggle::detail::resolve_module(
          roots, module.name(), module.version(), module.digest(), diagnostics);
      if (!installed) {
        continue;
      }
      auto behaviors =
          joggle::detail::behavior_candidates(installed->source, diagnostics);
      if (behaviors.size() > 1U) {
        diagnostics.report("cannot lock ambiguous behavior for module '" +
                           std::string(module.name()) + "@" +
                           joggle::to_string(module.version()) +
                           "' and target '" + joggle::behavior_target + "'");
        continue;
      }
      for (const auto& behavior : behaviors) {
        const auto digest =
            joggle::detail::behavior_digest(behavior, diagnostics);
        if (digest) {
          lock << "behavior " << module.name() << '@'
               << joggle::to_string(module.version()) << '#' << module.digest()
               << ' ' << joggle::behavior_target << '#' << *digest << ";\n";
        }
      }
    }
    if (!diagnostics.ok()) {
      return fail(diagnostics);
    }
    if (parsed.output) {
      if (!write(*parsed.output, lock.str(), diagnostics)) {
        return fail(diagnostics);
      }
    } else {
      std::cout << lock.str();
    }
    return EXIT_SUCCESS;
  }

  return EXIT_FAILURE;
}
