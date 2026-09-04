#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "joggle/diag.h"
#include "joggle/mod.h"

namespace joggle::detail {

struct InstalledMod {
  Mod mod;
  std::filesystem::path source;
};

std::filesystem::path default_mod_root();
std::string_view native_file_name();
std::vector<std::filesystem::path>
native_candidates(const std::filesystem::path& mod_source, Diag& diagnostics);
std::optional<std::string> native_digest(const std::filesystem::path& library,
                                         Diag& diagnostics);

std::vector<InstalledMod> installed_mods(const std::filesystem::path& root,
                                         Diag& diagnostics);

std::optional<InstalledMod>
resolve_mod(std::span<const std::filesystem::path> roots, std::string_view name,
            VersionRange range, Diag& diagnostics);

std::optional<InstalledMod>
resolve_mod(std::span<const std::filesystem::path> roots, std::string_view name,
            Version version, std::string_view digest, Diag& diagnostics);

std::optional<std::filesystem::path>
install_mod(const std::filesystem::path& root, const Mod& mod,
            Diag& diagnostics,
            std::optional<std::filesystem::path> native = std::nullopt);

// Lossless external representation for a data-bearing Mod. The directory
// contains canonical mod.joggle and the same verified data layout used by
// installed identities.
std::optional<Mod> read_mod_bundle(const std::filesystem::path& directory,
                                   Diag& diagnostics);
std::optional<std::filesystem::path>
write_mod_bundle(const std::filesystem::path& directory, const Mod& mod,
                 Diag& diagnostics);

bool remove_mod(const std::filesystem::path& root, std::string_view name,
                Version version, Diag& diagnostics);

}  // namespace joggle::detail
