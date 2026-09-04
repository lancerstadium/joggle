#include "module_repository.h"

#include "joggle/detail/native.h"
#include "joggle/digest.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace joggle::detail {
namespace {

constexpr std::string_view staging_prefix = ".joggle-install-";
constexpr std::string_view removal_prefix = ".joggle-remove-";

bool is_staging(const std::filesystem::path& path) {
  return path.filename().string().starts_with(staging_prefix);
}

bool is_removal(const std::filesystem::path& path) {
  return path.filename().string().starts_with(removal_prefix);
}

std::optional<std::filesystem::path>
create_staging_directory(const std::filesystem::path& parent,
                         Diagnostics& diagnostics) {
  std::error_code error;
  std::filesystem::create_directories(parent, error);
  if (error) {
    diagnostics.report("cannot create installation directory '" +
                       parent.string() + "': " + error.message());
    return std::nullopt;
  }
  for (std::size_t attempt = 0; attempt < 4096U; ++attempt) {
    const std::filesystem::path candidate =
        parent / (std::string(staging_prefix) + std::to_string(attempt));
    error.clear();
    if (std::filesystem::create_directory(candidate, error)) {
      return candidate;
    }
    if (error && error != std::errc::file_exists) {
      diagnostics.report("cannot create installation staging directory '" +
                         candidate.string() + "': " + error.message());
      return std::nullopt;
    }
  }
  diagnostics.report("too many unfinished installations in '" +
                     parent.string() + "'");
  return std::nullopt;
}

class StagingDirectory {
public:
  explicit StagingDirectory(std::filesystem::path path)
      : path_(std::move(path)) {}
  ~StagingDirectory() {
    if (!published_) {
      std::error_code ignored;
      std::filesystem::remove_all(path_, ignored);
    }
  }
  StagingDirectory(const StagingDirectory&) = delete;
  StagingDirectory& operator=(const StagingDirectory&) = delete;

  const std::filesystem::path& path() const { return path_; }
  void published() { published_ = true; }

private:
  std::filesystem::path path_;
  bool published_ = false;
};

bool valid_data_digest(std::string_view digest) {
  return digest.size() == 64U &&
         std::all_of(digest.begin(), digest.end(), [](char value) {
           const auto character = static_cast<unsigned char>(value);
           return std::isdigit(character) != 0 ||
                  (value >= 'a' && value <= 'f');
         });
}

std::optional<Bytes> read_data_file(const std::filesystem::path& path,
                                    Diagnostics& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.report("cannot open Module data '" + path.string() + "'");
    return std::nullopt;
  }
  std::ostringstream content;
  content << input.rdbuf();
  if (!input.eof() && input.fail()) {
    diagnostics.report("cannot read Module data '" + path.string() + "'");
    return std::nullopt;
  }
  const std::string bytes = content.str();
  Bytes result;
  result.reserve(bytes.size());
  for (const char value : bytes) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

bool load_module_data(const std::filesystem::path& identity, Module& module,
                      Diagnostics& diagnostics) {
  const std::filesystem::path directory = identity / "data";
  std::error_code error;
  if (!std::filesystem::exists(directory, error)) {
    if (error) {
      diagnostics.report("cannot inspect Module data directory '" +
                         directory.string() + "': " + error.message());
      return false;
    }
    return true;
  }
  if (!std::filesystem::is_directory(directory, error) || error) {
    diagnostics.report("Module data path is not a directory: '" +
                       directory.string() + "'");
    return false;
  }
  for (std::filesystem::directory_iterator current(directory, error), end;
       current != end && !error; current.increment(error)) {
    const auto status = current->symlink_status(error);
    if (error || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
      diagnostics.report("invalid entry in Module data directory '" +
                         current->path().string() + "'");
      return false;
    }
    const std::string expected = current->path().filename().string();
    if (!valid_data_digest(expected)) {
      diagnostics.report("invalid Module data filename '" +
                         current->path().string() + "'");
      return false;
    }
    auto bytes = read_data_file(current->path(), diagnostics);
    if (!bytes) {
      return false;
    }
    const std::string actual = module.store(std::move(*bytes));
    if (actual != "sha256:" + expected) {
      diagnostics.report("Module data content does not match its filename: '" +
                         current->path().string() + "'");
      return false;
    }
  }
  if (error) {
    diagnostics.report("cannot inspect Module data directory '" +
                       directory.string() + "': " + error.message());
    return false;
  }
  return true;
}

bool write_module_data(const std::filesystem::path& identity,
                       const Module& module, Diagnostics& diagnostics) {
  const auto names = module.data();
  if (names.empty()) {
    return true;
  }
  const std::filesystem::path directory = identity / "data";
  std::error_code error;
  if (!std::filesystem::create_directory(directory, error) || error) {
    diagnostics.report("cannot create Module data directory '" +
                       directory.string() + "': " + error.message());
    return false;
  }
  for (const std::string& name : names) {
    constexpr std::string_view prefix = "sha256:";
    if (!std::string_view(name).starts_with(prefix) ||
        !valid_data_digest(std::string_view(name).substr(prefix.size()))) {
      diagnostics.report("Module contains an invalid data name '" + name +
                         "'");
      return false;
    }
    const auto content = module.data(name);
    if (!content ||
        content->size() >
            static_cast<std::size_t>(
                std::numeric_limits<std::streamsize>::max())) {
      diagnostics.report("Module data '" + name + "' cannot be written");
      return false;
    }
    const std::filesystem::path destination =
        directory / name.substr(prefix.size());
    std::ofstream output(destination,
                         std::ios::binary | std::ios::trunc);
    if (!output) {
      diagnostics.report("cannot write Module data '" +
                         destination.string() + "'");
      return false;
    }
    output.write(reinterpret_cast<const char*>(content->data()),
                 static_cast<std::streamsize>(content->size()));
    output.close();
    if (!output) {
      diagnostics.report("cannot finish Module data '" +
                         destination.string() + "'");
      return false;
    }
  }
  return true;
}

std::optional<InstalledModule>
read_installed(const std::filesystem::path& root,
               const std::filesystem::path& source, Diagnostics& diagnostics) {
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    diagnostics.report("cannot open installed module '" + source.string() +
                       "'");
    return std::nullopt;
  }
  std::ostringstream text;
  text << input.rdbuf();
  if (!input.eof() && input.fail()) {
    diagnostics.report("cannot read installed module '" + source.string() +
                       "'");
    return std::nullopt;
  }
  auto module = parse_module(text.str(), diagnostics, source.string());
  if (!module) {
    return std::nullopt;
  }

  const std::filesystem::path relative = source.lexically_relative(root);
  auto component = relative.begin();
  if (component == relative.end() || component->string() != module->name()) {
    diagnostics.report("installed module path does not match its name: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  ++component;
  if (component == relative.end() ||
      component->string() != to_string(module->version())) {
    diagnostics.report("installed module path does not match its version: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  ++component;
  if (component == relative.end()) {
    diagnostics.report("installed module path has no digest: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  const std::string expected_digest = component->string();
  ++component;
  if (component == relative.end() || component->string() != "module.joggle") {
    diagnostics.report("invalid installed module filename: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  ++component;
  if (component != relative.end()) {
    diagnostics.report("invalid installed module path: '" + source.string() +
                       "'");
    return std::nullopt;
  }
  if (!load_module_data(source.parent_path(), *module, diagnostics)) {
    return std::nullopt;
  }
  if (expected_digest != module->digest()) {
    diagnostics.report("installed module path does not match its digest: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  return InstalledModule{std::move(*module), source};
}

bool empty_directory(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error) && !error &&
         std::filesystem::directory_iterator(path, error) ==
             std::filesystem::directory_iterator() &&
         !error;
}

std::optional<std::string> file_digest(const std::filesystem::path& path,
                                       Diagnostics& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.report("cannot open native library '" + path.string() + "'");
    return std::nullopt;
  }
  std::ostringstream bytes;
  bytes << input.rdbuf();
  if (!input.eof() && input.fail()) {
    diagnostics.report("cannot read native library '" + path.string() + "'");
    return std::nullopt;
  }
  return sha256(bytes.str());
}

bool native_matches(const std::filesystem::path& destination,
                    std::string_view expected, Diagnostics& diagnostics) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(destination, error) || error) {
    diagnostics.report("installed native is missing its library: '" +
                       destination.string() + "'");
    return false;
  }
  const auto actual = file_digest(destination, diagnostics);
  if (!actual || *actual != expected) {
    diagnostics.report("installed native content does not match its path: '" +
                       destination.string() + "'");
    return false;
  }
  return true;
}

bool install_native(const std::filesystem::path& identity,
                    const std::filesystem::path& source,
                    Diagnostics& diagnostics) {
  const auto digest = file_digest(source, diagnostics);
  if (!digest) {
    return false;
  }
  const std::filesystem::path target_root = identity / "native" / native_target;
  std::error_code error;
  if (std::filesystem::exists(target_root, error)) {
    for (std::filesystem::directory_iterator current(target_root, error), end;
         current != end && !error; current.increment(error)) {
      if (is_staging(current->path())) {
        continue;
      }
      if (!current->is_directory(error) || error) {
        diagnostics.report("invalid entry in native installation '" +
                           current->path().string() + "'");
        return false;
      }
      if (current->path().filename() != *digest) {
        diagnostics.report("native for target '" + std::string(native_target) +
                           "' is already installed with another digest");
        return false;
      }
    }
    if (error) {
      diagnostics.report("cannot inspect native installation '" +
                         target_root.string() + "': " + error.message());
      return false;
    }
  }
  const std::filesystem::path destination =
      target_root / *digest / std::string(native_file_name());
  const bool destination_exists = std::filesystem::exists(destination, error);
  if (error) {
    diagnostics.report("cannot inspect native installation '" +
                       destination.string() + "': " + error.message());
    return false;
  }
  if (destination_exists) {
    return native_matches(destination, *digest, diagnostics);
  }
  auto staging_path = create_staging_directory(target_root, diagnostics);
  if (!staging_path) {
    return false;
  }
  StagingDirectory staging(std::move(*staging_path));
  const std::filesystem::path staged_library =
      staging.path() / std::string(native_file_name());
  if (!std::filesystem::copy_file(source, staged_library,
                                  std::filesystem::copy_options::none, error) ||
      error) {
    diagnostics.report("cannot install native library at '" +
                       staged_library.string() + "': " + error.message());
    return false;
  }
  std::filesystem::rename(staging.path(), destination.parent_path(), error);
  if (error) {
    const std::error_code publish_error = error;
    error.clear();
    if (std::filesystem::exists(destination, error) && !error) {
      return native_matches(destination, *digest, diagnostics);
    }
    diagnostics.report("cannot publish native installation '" +
                       destination.parent_path().string() +
                       "': " + publish_error.message());
    return false;
  }
  staging.published();
  return true;
}

}  // namespace

std::filesystem::path default_module_root() {
  if (const char* configured = std::getenv("JOGGLE_MODULE_ROOT")) {
    if (*configured != '\0') {
      return configured;
    }
  }
  if (const char* home = std::getenv("HOME")) {
    if (*home != '\0') {
      return std::filesystem::path(home) / ".joggle" / "modules";
    }
  }
  return std::filesystem::current_path() / ".joggle" / "modules";
}

std::string_view native_file_name() {
#if defined(_WIN32)
  return "native.dll";
#elif defined(__APPLE__)
  return "native.dylib";
#else
  return "native.so";
#endif
}

std::vector<std::filesystem::path>
native_candidates(const std::filesystem::path& module_source,
                  Diagnostics& diagnostics) {
  std::vector<std::filesystem::path> result;
  const std::filesystem::path identity = module_source.parent_path();
  const std::filesystem::path adjacent =
      identity / std::string(native_file_name());
  std::error_code error;
  if (std::filesystem::exists(adjacent, error) && !error) {
    if (!std::filesystem::is_regular_file(adjacent, error) || error) {
      diagnostics.report("native path is not a regular file: '" +
                         adjacent.string() + "'");
      return result;
    }
    result.push_back(adjacent);
  } else if (error) {
    diagnostics.report("cannot inspect native library '" + adjacent.string() +
                       "': " + error.message());
    return result;
  }

  const std::filesystem::path target_root = identity / "native" / native_target;
  if (!std::filesystem::exists(target_root, error)) {
    if (error) {
      diagnostics.report("cannot inspect native directory '" +
                         target_root.string() + "': " + error.message());
    }
    return result;
  }
  for (std::filesystem::directory_iterator current(target_root, error), end;
       current != end && !error; current.increment(error)) {
    if (is_staging(current->path())) {
      continue;
    }
    if (!current->is_directory(error) || error) {
      diagnostics.report("invalid entry in native installation '" +
                         current->path().string() + "'");
      return result;
    }
    const std::string expected = current->path().filename().string();
    const std::filesystem::path library =
        current->path() / std::string(native_file_name());
    if (!std::filesystem::is_regular_file(library, error) || error) {
      diagnostics.report("installed native is missing its library: '" +
                         library.string() + "'");
      return result;
    }
    const auto actual = file_digest(library, diagnostics);
    if (!actual || *actual != expected) {
      diagnostics.report("installed native content does not match its path: '" +
                         library.string() + "'");
      return result;
    }
    result.push_back(library);
  }
  if (error) {
    diagnostics.report("cannot inspect native directory '" +
                       target_root.string() + "': " + error.message());
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::optional<std::string> native_digest(const std::filesystem::path& library,
                                         Diagnostics& diagnostics) {
  return file_digest(library, diagnostics);
}

std::vector<InstalledModule>
installed_modules(const std::filesystem::path& root, Diagnostics& diagnostics) {
  std::vector<InstalledModule> result;
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    if (error) {
      diagnostics.report("cannot inspect module root '" + root.string() +
                         "': " + error.message());
    }
    return result;
  }
  for (std::filesystem::recursive_directory_iterator current(root, error), end;
       current != end && !error; current.increment(error)) {
    if (current->is_directory(error)) {
      if (is_staging(current->path()) || is_removal(current->path())) {
        current.disable_recursion_pending();
      }
      continue;
    }
    if (error) {
      break;
    }
    if (!current->is_regular_file(error) || error ||
        current->path().filename() != "module.joggle") {
      continue;
    }
    if (auto module = read_installed(root, current->path(), diagnostics)) {
      result.push_back(std::move(*module));
    }
  }
  if (error) {
    diagnostics.report("cannot traverse module root '" + root.string() +
                       "': " + error.message());
  }
  std::sort(result.begin(), result.end(),
            [](const auto& left, const auto& right) {
              if (left.module.name() != right.module.name()) {
                return left.module.name() < right.module.name();
              }
              if (left.module.version() != right.module.version()) {
                return left.module.version() < right.module.version();
              }
              return left.module.digest() < right.module.digest();
            });
  return result;
}

std::optional<InstalledModule>
resolve_module(std::span<const std::filesystem::path> roots,
               std::string_view name, VersionRange range,
               Diagnostics& diagnostics) {
  std::vector<InstalledModule> candidates;
  for (const std::filesystem::path& root : roots) {
    auto installed = installed_modules(root, diagnostics);
    for (InstalledModule& candidate : installed) {
      if (candidate.module.name() == name &&
          range.contains(candidate.module.version())) {
        candidates.push_back(std::move(candidate));
      }
    }
  }
  if (!diagnostics.ok() || candidates.empty()) {
    return std::nullopt;
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& left, const auto& right) {
              if (left.module.version() != right.module.version()) {
                return left.module.version() > right.module.version();
              }
              return left.module.digest() < right.module.digest();
            });
  const Version selected = candidates.front().module.version();
  const std::string digest(candidates.front().module.digest());
  const auto conflict = std::find_if(
      candidates.begin() + 1, candidates.end(), [&](const auto& candidate) {
        return candidate.module.version() == selected &&
               candidate.module.digest() != digest;
      });
  if (conflict != candidates.end()) {
    diagnostics.report("installed module '" + std::string(name) + "@" +
                       to_string(selected) + "' has conflicting digests");
    return std::nullopt;
  }
  return candidates.front();
}

std::optional<InstalledModule>
resolve_module(std::span<const std::filesystem::path> roots,
               std::string_view name, Version version, std::string_view digest,
               Diagnostics& diagnostics) {
  std::optional<InstalledModule> result;
  for (const std::filesystem::path& root : roots) {
    auto installed = installed_modules(root, diagnostics);
    for (InstalledModule& candidate : installed) {
      if (candidate.module.name() != name ||
          candidate.module.version() != version ||
          candidate.module.digest() != digest) {
        continue;
      }
      if (!result) {
        result = std::move(candidate);
      }
    }
  }
  return result;
}

std::optional<std::filesystem::path>
install_module(const std::filesystem::path& root, const Module& module,
               Diagnostics& diagnostics,
               std::optional<std::filesystem::path> native) {
  const std::filesystem::path version =
      root / std::string(module.name()) / to_string(module.version());
  std::error_code error;
  if (std::filesystem::exists(version, error)) {
    for (std::filesystem::directory_iterator current(version, error), end;
         current != end && !error; current.increment(error)) {
      if (is_staging(current->path())) {
        continue;
      }
      if (!current->is_directory(error) || error) {
        diagnostics.report("invalid entry in module installation '" +
                           current->path().string() + "'");
        return std::nullopt;
      }
      if (current->path().filename() != module.digest()) {
        diagnostics.report("module '" + std::string(module.name()) + "@" +
                           to_string(module.version()) +
                           "' is already installed with another digest");
        return std::nullopt;
      }
    }
    if (error) {
      diagnostics.report("cannot inspect module installation '" +
                         version.string() + "': " + error.message());
      return std::nullopt;
    }
  }
  const std::filesystem::path identity = version / std::string(module.digest());
  const std::filesystem::path destination = identity / "module.joggle";
  const bool destination_exists = std::filesystem::exists(destination, error);
  if (error) {
    diagnostics.report("cannot inspect module installation '" +
                       destination.string() + "': " + error.message());
    return std::nullopt;
  }
  if (destination_exists) {
    const auto installed = read_installed(root, destination, diagnostics);
    if (!installed || installed->module != module) {
      diagnostics.report("installed module content does not match '" +
                         destination.string() + "'");
      return std::nullopt;
    }
    if (native && !install_native(identity, *native, diagnostics)) {
      return std::nullopt;
    }
    return destination;
  }
  if (std::filesystem::exists(identity, error)) {
    diagnostics.report("incomplete module installation at '" +
                       identity.string() + "'");
    return std::nullopt;
  }
  if (error) {
    diagnostics.report("cannot inspect module installation '" +
                       identity.string() + "': " + error.message());
    return std::nullopt;
  }
  auto staging_path = create_staging_directory(version, diagnostics);
  if (!staging_path) {
    return std::nullopt;
  }
  StagingDirectory staging(std::move(*staging_path));
  const std::filesystem::path staged_module = staging.path() / "module.joggle";
  std::ofstream output(staged_module, std::ios::binary | std::ios::trunc);
  if (!output) {
    diagnostics.report("cannot write installed module '" +
                       staged_module.string() + "'");
    return std::nullopt;
  }
  output << format(module);
  output.close();
  if (!output) {
    diagnostics.report("cannot finish installed module '" +
                       staged_module.string() + "'");
    return std::nullopt;
  }
  if (!write_module_data(staging.path(), module, diagnostics)) {
    return std::nullopt;
  }
  if (native && !install_native(staging.path(), *native, diagnostics)) {
    return std::nullopt;
  }
  std::filesystem::rename(staging.path(), identity, error);
  if (error) {
    const std::error_code publish_error = error;
    error.clear();
    if (std::filesystem::exists(destination, error) && !error) {
      const auto installed = read_installed(root, destination, diagnostics);
      if (!installed || installed->module != module) {
        diagnostics.report("installed module content does not match '" +
                           destination.string() + "'");
        return std::nullopt;
      }
      if (native && !install_native(identity, *native, diagnostics)) {
        return std::nullopt;
      }
      return destination;
    }
    diagnostics.report("cannot publish module installation '" +
                       identity.string() + "': " + publish_error.message());
    return std::nullopt;
  }
  staging.published();
  return destination;
}

bool remove_module(const std::filesystem::path& root, std::string_view name,
                   Version version, Diagnostics& diagnostics) {
  const std::filesystem::path module_directory = root / std::string(name);
  const std::string version_text = to_string(version);
  const std::filesystem::path target = module_directory / version_text;
  std::error_code error;
  if (!std::filesystem::is_directory(target, error) || error) {
    diagnostics.report("module '" + std::string(name) + "@" + version_text +
                       "' is not installed");
    return false;
  }

  std::optional<std::filesystem::path> hidden;
  for (std::size_t attempt = 0; attempt < 4096U; ++attempt) {
    const std::filesystem::path candidate =
        module_directory / (std::string(removal_prefix) + version_text + "-" +
                            std::to_string(attempt));
    error.clear();
    if (std::filesystem::exists(candidate, error)) {
      continue;
    }
    if (error) {
      diagnostics.report("cannot inspect uninstall location '" +
                         candidate.string() + "': " + error.message());
      return false;
    }
    std::filesystem::rename(target, candidate, error);
    if (!error) {
      hidden = candidate;
      break;
    }
    if (error == std::errc::file_exists ||
        error == std::errc::directory_not_empty) {
      continue;
    }
    diagnostics.report("cannot hide module '" + std::string(name) + "@" +
                       version_text + "' before removal: " + error.message());
    return false;
  }
  if (!hidden) {
    diagnostics.report("too many unfinished removals for module '" +
                       std::string(name) + "@" + version_text + "'");
    return false;
  }

  std::filesystem::remove_all(*hidden, error);
  if (error) {
    diagnostics.report("module '" + std::string(name) + "@" + version_text +
                       "' is uninstalled, but cannot reclaim '" +
                       hidden->string() + "': " + error.message());
    return false;
  }
  if (empty_directory(module_directory)) {
    std::filesystem::remove(module_directory, error);
  }
  return true;
}

}  // namespace joggle::detail
