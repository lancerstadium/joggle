#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include <joggle/detail/native.h>
#include <joggle/joggle.h>

#include "repository.h"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

class TemporaryRoot {
public:
  TemporaryRoot()
      : path_(std::filesystem::temp_directory_path() /
              ("joggle-repository-test-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {}
  ~TemporaryRoot() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
  }

  const std::filesystem::path& path() const { return path_; }

private:
  std::filesystem::path path_;
};

bool write(const std::filesystem::path& path, std::string_view text) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  output.close();
  return static_cast<bool>(output);
}

}  // namespace

int main() {
  TemporaryRoot root;
  TemporaryRoot bundles;
  joggle::Diagnostics parse_diagnostics;
  const auto mod = joggle::parse_mod(R"(
    joggle 1;
    mod atomic@1.0.0 {
      type word(width: int);
    }
  )",
                                     parse_diagnostics, "atomic.joggle");
  if (!mod) {
    parse_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  joggle::Diagnostics second_parse_diagnostics;
  auto second = joggle::parse_mod(R"(
    joggle 1;
    mod atomic@2.0.0 {
      type word(width: int);
    }
  )",
                                  second_parse_diagnostics, "atomic-v2.joggle");
  if (!second) {
    second_parse_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  const joggle::Bytes second_payload{std::byte{0x00}, std::byte{0x10},
                                     std::byte{0x20}, std::byte{0xff}};
  const std::string second_data = second->store(second_payload);

  bool ok = true;
  const std::filesystem::path bundle = bundles.path() / "external-bundle";
  joggle::Diagnostics bundle_write_diagnostics;
  const auto bundled = joggle::detail::write_mod_bundle(
      bundle, *second, bundle_write_diagnostics);
  joggle::Diagnostics bundle_read_diagnostics;
  const auto read_bundle =
      bundled ? joggle::detail::read_mod_bundle(bundle, bundle_read_diagnostics)
              : std::optional<joggle::Mod>{};
  ok &= expect(bundled && bundle_write_diagnostics.ok() && read_bundle &&
                   bundle_read_diagnostics.ok() && *read_bundle == *second,
               "a directory bundle round-trips source and Mod-owned data");
  joggle::Diagnostics duplicate_bundle_diagnostics;
  ok &= expect(!joggle::detail::write_mod_bundle(
                   bundle, *second, duplicate_bundle_diagnostics) &&
                   !duplicate_bundle_diagnostics.ok(),
               "bundle publication never overwrites an existing destination");

  joggle::Diagnostics failed_install;
  const auto failed = joggle::detail::install_mod(
      root.path(), *mod, failed_install, root.path() / "missing-native");
  ok &= expect(!failed && !failed_install.ok(),
               "a missing native makes installation fail");

  joggle::Diagnostics after_failure;
  const auto initially_visible =
      joggle::detail::installed_mods(root.path(), after_failure);
  ok &= expect(after_failure.ok() && initially_visible.empty(),
               "a failed native install publishes no mod");

  const std::filesystem::path version = root.path() / "atomic" / "1.0.0";
  const std::filesystem::path abandoned =
      version / ".joggle-install-abandoned" / "mod.joggle";
  ok &= expect(write(abandoned, joggle::format(*mod)),
               "create an abandoned staging fixture");
  joggle::Diagnostics staging_scan;
  const auto visible_with_staging =
      joggle::detail::installed_mods(root.path(), staging_scan);
  ok &= expect(staging_scan.ok() && visible_with_staging.empty(),
               "unfinished staging is not an installed mod");

  const std::filesystem::path abandoned_removal =
      root.path() / "atomic" / ".joggle-remove-1.0.0-abandoned" / "mod.joggle";
  ok &= expect(write(abandoned_removal, joggle::format(*mod)),
               "create an abandoned removal fixture");
  joggle::Diagnostics removal_scan;
  const auto visible_with_removal =
      joggle::detail::installed_mods(root.path(), removal_scan);
  ok &= expect(removal_scan.ok() && visible_with_removal.empty(),
               "unfinished removal is not an installed mod");

  joggle::Diagnostics install_diagnostics;
  const auto installed =
      joggle::detail::install_mod(root.path(), *mod, install_diagnostics);
  ok &= expect(installed && install_diagnostics.ok(),
               "a complete mod is atomically published");

  joggle::Diagnostics visible_diagnostics;
  const auto visible =
      joggle::detail::installed_mods(root.path(), visible_diagnostics);
  ok &= expect(visible_diagnostics.ok() && visible.size() == 1U &&
                   visible.front().mod == *mod,
               "only the published identity is visible");

  joggle::Diagnostics second_install_diagnostics;
  const auto second_installed = joggle::detail::install_mod(
      root.path(), *second, second_install_diagnostics);
  ok &= expect(second_installed && second_install_diagnostics.ok(),
               "a second version can coexist under the same mod name");
  const std::filesystem::path second_data_path =
      second_installed
          ? second_installed->parent_path() / "data" /
                second_data.substr(std::string_view("sha256:").size())
          : std::filesystem::path{};
  ok &= expect(second_installed &&
                   std::filesystem::is_regular_file(second_data_path),
               "Mod-owned data is installed beside canonical source");

  if (installed) {
    const std::filesystem::path abandoned_native =
        installed->parent_path() / "native" / joggle::detail::native_target /
        ".joggle-install-abandoned" / joggle::detail::native_file_name();
    ok &= expect(write(abandoned_native, "unfinished"),
                 "create an abandoned native staging fixture");
    joggle::Diagnostics native_scan;
    const auto natives =
        joggle::detail::native_candidates(*installed, native_scan);
    ok &= expect(native_scan.ok() && natives.empty(),
                 "unfinished native staging is not discoverable");

    ok &= expect(write(*installed, "not a mod\n"),
                 "corrupt the installed mod fixture");
    joggle::Diagnostics reinstall_diagnostics;
    const auto reinstalled =
        joggle::detail::install_mod(root.path(), *mod, reinstall_diagnostics);
    ok &= expect(!reinstalled && !reinstall_diagnostics.ok(),
                 "idempotent install revalidates existing content");

    joggle::Diagnostics remove_diagnostics;
    ok &=
        expect(joggle::detail::remove_mod(root.path(), mod->name(),
                                          mod->version(), remove_diagnostics) &&
                   remove_diagnostics.ok(),
               "uninstall hides and removes the complete version");
    ok &= expect(!std::filesystem::exists(*installed),
                 "an uninstalled identity is no longer visible");

    joggle::Diagnostics remaining_diagnostics;
    const auto remaining =
        joggle::detail::installed_mods(root.path(), remaining_diagnostics);
    ok &= expect(remaining_diagnostics.ok() && remaining.size() == 1U &&
                     remaining.front().mod == *second,
                 "uninstalling one exact version preserves sibling versions");

    joggle::Diagnostics missing_remove_diagnostics;
    ok &= expect(!joggle::detail::remove_mod(root.path(), mod->name(),
                                             mod->version(),
                                             missing_remove_diagnostics) &&
                     !missing_remove_diagnostics.ok() && second_installed &&
                     std::filesystem::exists(*second_installed),
                 "a missing exact version fails without changing siblings");

    ok &= expect(write(second_data_path, "corrupt"),
                 "corrupt installed Mod data fixture");
    joggle::Diagnostics corrupt_data_diagnostics;
    const auto corrupt =
        joggle::detail::installed_mods(root.path(), corrupt_data_diagnostics);
    ok &= expect(corrupt.empty() && !corrupt_data_diagnostics.ok(),
                 "installed Mod data is verified against its filename");
    joggle::Diagnostics corrupt_reinstall_diagnostics;
    const auto corrupt_reinstall = joggle::detail::install_mod(
        root.path(), *second, corrupt_reinstall_diagnostics);
    ok &= expect(!corrupt_reinstall && !corrupt_reinstall_diagnostics.ok(),
                 "idempotent install rejects corrupt Mod-owned data");
  }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
