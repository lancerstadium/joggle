#include "repository.h"

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
                         Diag& diagnostics) {
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
                                    Diag& diagnostics) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    diagnostics.report("cannot open Mod data '" + path.string() + "'");
    return std::nullopt;
  }
  std::ostringstream content;
  content << input.rdbuf();
  if (!input.eof() && input.fail()) {
    diagnostics.report("cannot read Mod data '" + path.string() + "'");
    return std::nullopt;
  }
  const std::string bytes = content.str();
  Bytes result;
  result.reserve(bytes.size());
  for (const char value : bytes) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

bool load_mod_data(const std::filesystem::path& identity, Mod& mod,
                   Diag& diagnostics) {
  const std::filesystem::path directory = identity / "data";
  std::error_code error;
  if (!std::filesystem::exists(directory, error)) {
    if (error) {
      diagnostics.report("cannot inspect Mod data directory '" +
                         directory.string() + "': " + error.message());
      return false;
    }
    return true;
  }
  if (!std::filesystem::is_directory(directory, error) || error) {
    diagnostics.report("Mod data path is not a directory: '" +
                       directory.string() + "'");
    return false;
  }
  for (std::filesystem::directory_iterator current(directory, error), end;
       current != end && !error; current.increment(error)) {
    const auto status = current->symlink_status(error);
    if (error || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
      diagnostics.report("invalid entry in Mod data directory '" +
                         current->path().string() + "'");
      return false;
    }
    const std::string expected = current->path().filename().string();
    if (!valid_data_digest(expected)) {
      diagnostics.report("invalid Mod data filename '" +
                         current->path().string() + "'");
      return false;
    }
    auto bytes = read_data_file(current->path(), diagnostics);
    if (!bytes) {
      return false;
    }
    const std::string actual = mod.store(std::move(*bytes));
    if (actual != "sha256:" + expected) {
      diagnostics.report("Mod data content does not match its filename: '" +
                         current->path().string() + "'");
      return false;
    }
  }
  if (error) {
    diagnostics.report("cannot inspect Mod data directory '" +
                       directory.string() + "': " + error.message());
    return false;
  }
  return true;
}

bool write_mod_data(const std::filesystem::path& identity, const Mod& mod,
                    Diag& diagnostics) {
  const auto names = mod.data();
  if (names.empty()) {
    return true;
  }
  const std::filesystem::path directory = identity / "data";
  std::error_code error;
  if (!std::filesystem::create_directory(directory, error) || error) {
    diagnostics.report("cannot create Mod data directory '" +
                       directory.string() + "': " + error.message());
    return false;
  }
  for (const std::string& name : names) {
    constexpr std::string_view prefix = "sha256:";
    if (!std::string_view(name).starts_with(prefix) ||
        !valid_data_digest(std::string_view(name).substr(prefix.size()))) {
      diagnostics.report("Mod contains an invalid data name '" + name + "'");
      return false;
    }
    const auto content = mod.data(name);
    if (!content ||
        content->size() > static_cast<std::size_t>(
                              std::numeric_limits<std::streamsize>::max())) {
      diagnostics.report("Mod data '" + name + "' cannot be written");
      return false;
    }
    const std::filesystem::path destination =
        directory / name.substr(prefix.size());
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
      diagnostics.report("cannot write Mod data '" + destination.string() +
                         "'");
      return false;
    }
    output.write(reinterpret_cast<const char*>(content->data()),
                 static_cast<std::streamsize>(content->size()));
    output.close();
    if (!output) {
      diagnostics.report("cannot finish Mod data '" + destination.string() +
                         "'");
      return false;
    }
  }
  return true;
}

std::optional<InstalledMod> read_installed(const std::filesystem::path& root,
                                           const std::filesystem::path& source,
                                           Diag& diagnostics) {
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    diagnostics.report("cannot open installed mod '" + source.string() + "'");
    return std::nullopt;
  }
  std::ostringstream text;
  text << input.rdbuf();
  if (!input.eof() && input.fail()) {
    diagnostics.report("cannot read installed mod '" + source.string() + "'");
    return std::nullopt;
  }
  auto mod = parse_mod(text.str(), diagnostics, source.string());
  if (!mod) {
    return std::nullopt;
  }

  const std::filesystem::path relative = source.lexically_relative(root);
  auto component = relative.begin();
  if (component == relative.end() || component->string() != mod->name()) {
    diagnostics.report("installed mod path does not match its name: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  ++component;
  if (component == relative.end() ||
      component->string() != to_string(mod->version())) {
    diagnostics.report("installed mod path does not match its version: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  ++component;
  if (component == relative.end()) {
    diagnostics.report("installed mod path has no digest: '" + source.string() +
                       "'");
    return std::nullopt;
  }
  const std::string expected_digest = component->string();
  ++component;
  if (component == relative.end() || component->string() != "mod.joggle") {
    diagnostics.report("invalid installed mod filename: '" + source.string() +
                       "'");
    return std::nullopt;
  }
  ++component;
  if (component != relative.end()) {
    diagnostics.report("invalid installed mod path: '" + source.string() + "'");
    return std::nullopt;
  }
  if (!load_mod_data(source.parent_path(), *mod, diagnostics)) {
    return std::nullopt;
  }
  if (expected_digest != mod->digest()) {
    diagnostics.report("installed mod path does not match its digest: '" +
                       source.string() + "'");
    return std::nullopt;
  }
  return InstalledMod{std::move(*mod), source};
}

bool empty_directory(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error) && !error &&
         std::filesystem::directory_iterator(path, error) ==
             std::filesystem::directory_iterator() &&
         !error;
}

std::optional<std::string> file_digest(const std::filesystem::path& path,
                                       Diag& diagnostics) {
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
                    std::string_view expected, Diag& diagnostics) {
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
                    const std::filesystem::path& source, Diag& diagnostics) {
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

std::filesystem::path default_mod_root() {
  if (const char* configured = std::getenv("JOGGLE_MOD_ROOT")) {
    if (*configured != '\0') {
      return configured;
    }
  }
  if (const char* home = std::getenv("HOME")) {
    if (*home != '\0') {
      return std::filesystem::path(home) / ".joggle" / "mods";
    }
  }
  return std::filesystem::current_path() / ".joggle" / "mods";
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
native_candidates(const std::filesystem::path& mod_source, Diag& diagnostics) {
  std::vector<std::filesystem::path> result;
  const std::filesystem::path identity = mod_source.parent_path();
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
                                         Diag& diagnostics) {
  return file_digest(library, diagnostics);
}

std::vector<InstalledMod> installed_mods(const std::filesystem::path& root,
                                         Diag& diagnostics) {
  std::vector<InstalledMod> result;
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    if (error) {
      diagnostics.report("cannot inspect mod root '" + root.string() +
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
        current->path().filename() != "mod.joggle") {
      continue;
    }
    if (auto mod = read_installed(root, current->path(), diagnostics)) {
      result.push_back(std::move(*mod));
    }
  }
  if (error) {
    diagnostics.report("cannot traverse mod root '" + root.string() +
                       "': " + error.message());
  }
  std::sort(result.begin(), result.end(),
            [](const auto& left, const auto& right) {
              if (left.mod.name() != right.mod.name()) {
                return left.mod.name() < right.mod.name();
              }
              if (left.mod.version() != right.mod.version()) {
                return left.mod.version() < right.mod.version();
              }
              return left.mod.digest() < right.mod.digest();
            });
  return result;
}

std::optional<InstalledMod>
resolve_mod(std::span<const std::filesystem::path> roots, std::string_view name,
            VersionRange range, Diag& diagnostics) {
  std::vector<InstalledMod> candidates;
  for (const std::filesystem::path& root : roots) {
    auto installed = installed_mods(root, diagnostics);
    for (InstalledMod& candidate : installed) {
      if (candidate.mod.name() == name &&
          range.contains(candidate.mod.version())) {
        candidates.push_back(std::move(candidate));
      }
    }
  }
  if (!diagnostics.ok() || candidates.empty()) {
    return std::nullopt;
  }
  std::sort(candidates.begin(), candidates.end(),
            [](const auto& left, const auto& right) {
              if (left.mod.version() != right.mod.version()) {
                return left.mod.version() > right.mod.version();
              }
              return left.mod.digest() < right.mod.digest();
            });
  const Version selected = candidates.front().mod.version();
  const std::string digest(candidates.front().mod.digest());
  const auto conflict = std::find_if(
      candidates.begin() + 1, candidates.end(), [&](const auto& candidate) {
        return candidate.mod.version() == selected &&
               candidate.mod.digest() != digest;
      });
  if (conflict != candidates.end()) {
    diagnostics.report("installed mod '" + std::string(name) + "@" +
                       to_string(selected) + "' has conflicting digests");
    return std::nullopt;
  }
  return candidates.front();
}

std::optional<InstalledMod>
resolve_mod(std::span<const std::filesystem::path> roots, std::string_view name,
            Version version, std::string_view digest, Diag& diagnostics) {
  std::optional<InstalledMod> result;
  for (const std::filesystem::path& root : roots) {
    auto installed = installed_mods(root, diagnostics);
    for (InstalledMod& candidate : installed) {
      if (candidate.mod.name() != name || candidate.mod.version() != version ||
          candidate.mod.digest() != digest) {
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
install_mod(const std::filesystem::path& root, const Mod& mod,
            Diag& diagnostics, std::optional<std::filesystem::path> native) {
  const std::filesystem::path version =
      root / std::string(mod.name()) / to_string(mod.version());
  std::error_code error;
  if (std::filesystem::exists(version, error)) {
    for (std::filesystem::directory_iterator current(version, error), end;
         current != end && !error; current.increment(error)) {
      if (is_staging(current->path())) {
        continue;
      }
      if (!current->is_directory(error) || error) {
        diagnostics.report("invalid entry in mod installation '" +
                           current->path().string() + "'");
        return std::nullopt;
      }
      if (current->path().filename() != mod.digest()) {
        diagnostics.report("mod '" + std::string(mod.name()) + "@" +
                           to_string(mod.version()) +
                           "' is already installed with another digest");
        return std::nullopt;
      }
    }
    if (error) {
      diagnostics.report("cannot inspect mod installation '" +
                         version.string() + "': " + error.message());
      return std::nullopt;
    }
  }
  const std::filesystem::path identity = version / std::string(mod.digest());
  const std::filesystem::path destination = identity / "mod.joggle";
  const bool destination_exists = std::filesystem::exists(destination, error);
  if (error) {
    diagnostics.report("cannot inspect mod installation '" +
                       destination.string() + "': " + error.message());
    return std::nullopt;
  }
  if (destination_exists) {
    const auto installed = read_installed(root, destination, diagnostics);
    if (!installed || installed->mod != mod) {
      diagnostics.report("installed mod content does not match '" +
                         destination.string() + "'");
      return std::nullopt;
    }
    if (native && !install_native(identity, *native, diagnostics)) {
      return std::nullopt;
    }
    return destination;
  }
  if (std::filesystem::exists(identity, error)) {
    diagnostics.report("incomplete mod installation at '" + identity.string() +
                       "'");
    return std::nullopt;
  }
  if (error) {
    diagnostics.report("cannot inspect mod installation '" + identity.string() +
                       "': " + error.message());
    return std::nullopt;
  }
  auto staging_path = create_staging_directory(version, diagnostics);
  if (!staging_path) {
    return std::nullopt;
  }
  StagingDirectory staging(std::move(*staging_path));
  const std::filesystem::path staged_mod = staging.path() / "mod.joggle";
  std::ofstream output(staged_mod, std::ios::binary | std::ios::trunc);
  if (!output) {
    diagnostics.report("cannot write installed mod '" + staged_mod.string() +
                       "'");
    return std::nullopt;
  }
  output << format(mod);
  output.close();
  if (!output) {
    diagnostics.report("cannot finish installed mod '" + staged_mod.string() +
                       "'");
    return std::nullopt;
  }
  if (!write_mod_data(staging.path(), mod, diagnostics)) {
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
      if (!installed || installed->mod != mod) {
        diagnostics.report("installed mod content does not match '" +
                           destination.string() + "'");
        return std::nullopt;
      }
      if (native && !install_native(identity, *native, diagnostics)) {
        return std::nullopt;
      }
      return destination;
    }
    diagnostics.report("cannot publish mod installation '" + identity.string() +
                       "': " + publish_error.message());
    return std::nullopt;
  }
  staging.published();
  return destination;
}

std::optional<Mod> read_mod_bundle(const std::filesystem::path& directory,
                                   Diag& diagnostics) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(directory, error);
  if (error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    diagnostics.report("Mod bundle is not a directory: '" + directory.string() +
                       "'");
    return std::nullopt;
  }
  const std::filesystem::path source = directory / "mod.joggle";
  std::ifstream input(source, std::ios::binary);
  if (!input) {
    diagnostics.report("cannot open Mod bundle source '" + source.string() +
                       "'");
    return std::nullopt;
  }
  std::ostringstream text;
  text << input.rdbuf();
  if (!input.eof() && input.fail()) {
    diagnostics.report("cannot read Mod bundle source '" + source.string() +
                       "'");
    return std::nullopt;
  }
  auto mod = parse_mod(text.str(), diagnostics, source.string());
  if (!mod || !load_mod_data(directory, *mod, diagnostics)) {
    return std::nullopt;
  }
  return mod;
}

std::optional<std::filesystem::path>
write_mod_bundle(const std::filesystem::path& directory, const Mod& mod,
                 Diag& diagnostics) {
  std::error_code error;
  if (std::filesystem::exists(directory, error)) {
    diagnostics.report("Mod bundle destination already exists: '" +
                       directory.string() + "'");
    return std::nullopt;
  }
  if (error) {
    diagnostics.report("cannot inspect Mod bundle destination '" +
                       directory.string() + "': " + error.message());
    return std::nullopt;
  }
  const std::filesystem::path parent =
      directory.has_parent_path() ? directory.parent_path() : ".";
  auto staging_path = create_staging_directory(parent, diagnostics);
  if (!staging_path) {
    return std::nullopt;
  }
  StagingDirectory staging(std::move(*staging_path));
  const std::filesystem::path source = staging.path() / "mod.joggle";
  std::ofstream output(source, std::ios::binary | std::ios::trunc);
  if (!output) {
    diagnostics.report("cannot write Mod bundle source '" + source.string() +
                       "'");
    return std::nullopt;
  }
  output << format(mod);
  output.close();
  if (!output) {
    diagnostics.report("cannot finish Mod bundle source '" + source.string() +
                       "'");
    return std::nullopt;
  }
  if (!write_mod_data(staging.path(), mod, diagnostics)) {
    return std::nullopt;
  }
  std::filesystem::rename(staging.path(), directory, error);
  if (error) {
    diagnostics.report("cannot publish Mod bundle '" + directory.string() +
                       "': " + error.message());
    return std::nullopt;
  }
  staging.published();
  return directory / "mod.joggle";
}

bool remove_mod(const std::filesystem::path& root, std::string_view name,
                Version version, Diag& diagnostics) {
  const std::filesystem::path mod_directory = root / std::string(name);
  const std::string version_text = to_string(version);
  const std::filesystem::path target = mod_directory / version_text;
  std::error_code error;
  if (!std::filesystem::is_directory(target, error) || error) {
    diagnostics.report("mod '" + std::string(name) + "@" + version_text +
                       "' is not installed");
    return false;
  }

  std::optional<std::filesystem::path> hidden;
  for (std::size_t attempt = 0; attempt < 4096U; ++attempt) {
    const std::filesystem::path candidate =
        mod_directory / (std::string(removal_prefix) + version_text + "-" +
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
    diagnostics.report("cannot hide mod '" + std::string(name) + "@" +
                       version_text + "' before removal: " + error.message());
    return false;
  }
  if (!hidden) {
    diagnostics.report("too many unfinished removals for mod '" +
                       std::string(name) + "@" + version_text + "'");
    return false;
  }

  std::filesystem::remove_all(*hidden, error);
  if (error) {
    diagnostics.report("mod '" + std::string(name) + "@" + version_text +
                       "' is uninstalled, but cannot reclaim '" +
                       hidden->string() + "': " + error.message());
    return false;
  }
  if (empty_directory(mod_directory)) {
    std::filesystem::remove(mod_directory, error);
  }
  return true;
}

}  // namespace joggle::detail
