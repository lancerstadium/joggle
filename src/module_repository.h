#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "joggle/diagnostic.h"
#include "joggle/module.h"

namespace joggle::detail {

struct InstalledModule {
  Module module;
  std::filesystem::path source;
};

std::filesystem::path default_module_root();
std::string_view native_file_name();
std::vector<std::filesystem::path>
native_candidates(const std::filesystem::path& module_source,
                  Diagnostics& diagnostics);
std::optional<std::string> native_digest(const std::filesystem::path& library,
                                         Diagnostics& diagnostics);

std::vector<InstalledModule>
installed_modules(const std::filesystem::path& root, Diagnostics& diagnostics);

std::optional<InstalledModule>
resolve_module(std::span<const std::filesystem::path> roots,
               std::string_view name, VersionRange range,
               Diagnostics& diagnostics);

std::optional<InstalledModule>
resolve_module(std::span<const std::filesystem::path> roots,
               std::string_view name, Version version, std::string_view digest,
               Diagnostics& diagnostics);

std::optional<std::filesystem::path>
install_module(const std::filesystem::path& root, const Module& module,
               Diagnostics& diagnostics,
               std::optional<std::filesystem::path> native = std::nullopt);

// Lossless external representation for a data-bearing Module. The directory
// contains canonical module.joggle and the same verified data layout used by
// installed identities.
std::optional<Module>
read_module_bundle(const std::filesystem::path& directory,
                   Diagnostics& diagnostics);
std::optional<std::filesystem::path>
write_module_bundle(const std::filesystem::path& directory,
                    const Module& module, Diagnostics& diagnostics);

bool remove_module(const std::filesystem::path& root, std::string_view name,
                   Version version, Diagnostics& diagnostics);

}  // namespace joggle::detail
