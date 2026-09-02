#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

#include <joggle/behavior.h>
#include <joggle/joggle.h>

#include "module_repository.h"

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
  joggle::Diagnostics parse_diagnostics;
  const auto module = joggle::parse_module(R"(
    joggle 1;
    module atomic@1.0.0 {
      type word(width: int);
    }
  )",
                                           parse_diagnostics, "atomic.joggle");
  if (!module) {
    parse_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  joggle::Diagnostics second_parse_diagnostics;
  const auto second =
      joggle::parse_module(R"(
    joggle 1;
    module atomic@2.0.0 {
      type word(width: int);
    }
  )",
                           second_parse_diagnostics, "atomic-v2.joggle");
  if (!second) {
    second_parse_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  joggle::Diagnostics failed_install;
  const auto failed = joggle::detail::install_module(
      root.path(), *module, failed_install, root.path() / "missing-behavior");
  ok &= expect(!failed && !failed_install.ok(),
               "a missing behavior makes installation fail");

  joggle::Diagnostics after_failure;
  const auto initially_visible =
      joggle::detail::installed_modules(root.path(), after_failure);
  ok &= expect(after_failure.ok() && initially_visible.empty(),
               "a failed behavior install publishes no module");

  const std::filesystem::path version = root.path() / "atomic" / "1.0.0";
  const std::filesystem::path abandoned =
      version / ".joggle-install-abandoned" / "module.joggle";
  ok &= expect(write(abandoned, joggle::format(*module)),
               "create an abandoned staging fixture");
  joggle::Diagnostics staging_scan;
  const auto visible_with_staging =
      joggle::detail::installed_modules(root.path(), staging_scan);
  ok &= expect(staging_scan.ok() && visible_with_staging.empty(),
               "unfinished staging is not an installed module");

  const std::filesystem::path abandoned_removal =
      root.path() / "atomic" / ".joggle-remove-1.0.0-abandoned" /
      "module.joggle";
  ok &= expect(write(abandoned_removal, joggle::format(*module)),
               "create an abandoned removal fixture");
  joggle::Diagnostics removal_scan;
  const auto visible_with_removal =
      joggle::detail::installed_modules(root.path(), removal_scan);
  ok &= expect(removal_scan.ok() && visible_with_removal.empty(),
               "unfinished removal is not an installed module");

  joggle::Diagnostics install_diagnostics;
  const auto installed =
      joggle::detail::install_module(root.path(), *module, install_diagnostics);
  ok &= expect(installed && install_diagnostics.ok(),
               "a complete module is atomically published");

  joggle::Diagnostics visible_diagnostics;
  const auto visible =
      joggle::detail::installed_modules(root.path(), visible_diagnostics);
  ok &= expect(visible_diagnostics.ok() && visible.size() == 1U &&
                   visible.front().module == *module,
               "only the published identity is visible");

  joggle::Diagnostics second_install_diagnostics;
  const auto second_installed = joggle::detail::install_module(
      root.path(), *second, second_install_diagnostics);
  ok &= expect(second_installed && second_install_diagnostics.ok(),
               "a second version can coexist under the same module name");

  if (installed) {
    const std::filesystem::path abandoned_behavior =
        installed->parent_path() / "behavior" / joggle::behavior_target /
        ".joggle-install-abandoned" / joggle::detail::behavior_file_name();
    ok &= expect(write(abandoned_behavior, "unfinished"),
                 "create an abandoned behavior staging fixture");
    joggle::Diagnostics behavior_scan;
    const auto behaviors =
        joggle::detail::behavior_candidates(*installed, behavior_scan);
    ok &= expect(behavior_scan.ok() && behaviors.empty(),
                 "unfinished behavior staging is not discoverable");

    ok &= expect(write(*installed, "not a module\n"),
                 "corrupt the installed module fixture");
    joggle::Diagnostics reinstall_diagnostics;
    const auto reinstalled = joggle::detail::install_module(
        root.path(), *module, reinstall_diagnostics);
    ok &= expect(!reinstalled && !reinstall_diagnostics.ok(),
                 "idempotent install revalidates existing content");

    joggle::Diagnostics remove_diagnostics;
    ok &= expect(joggle::detail::remove_module(root.path(), module->name(),
                                               module->version(),
                                               remove_diagnostics) &&
                     remove_diagnostics.ok(),
                 "uninstall hides and removes the complete version");
    ok &= expect(!std::filesystem::exists(*installed),
                 "an uninstalled identity is no longer visible");

    joggle::Diagnostics remaining_diagnostics;
    const auto remaining =
        joggle::detail::installed_modules(root.path(), remaining_diagnostics);
    ok &= expect(remaining_diagnostics.ok() && remaining.size() == 1U &&
                     remaining.front().module == *second,
                 "uninstalling one exact version preserves sibling versions");

    joggle::Diagnostics missing_remove_diagnostics;
    ok &= expect(!joggle::detail::remove_module(root.path(), module->name(),
                                                module->version(),
                                                missing_remove_diagnostics) &&
                     !missing_remove_diagnostics.ok() && second_installed &&
                     std::filesystem::exists(*second_installed),
                 "a missing exact version fails without changing siblings");
  }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
