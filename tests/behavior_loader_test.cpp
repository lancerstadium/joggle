#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 4 && argc != 6) {
    return EXIT_FAILURE;
  }
  joggle::Compiler compiler;
  if (argc == 6) {
    compiler.search(argv[4]);
    compiler.lock(argv[5]);
  }
  compiler.load(argv[1]);
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto module = compiler.module("behavior_plugin");
  if (!module || !compiler.load_behavior("behavior_plugin", argv[2])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto positive = module->type("positive");
  const auto value = positive ? compiler.make(*positive, std::int64_t{7})
                              : std::optional<joggle::Type>{};
  auto function = compiler.function();
  bool ok = true;
  ok &= expect(value.has_value(),
               "the loaded behavior refines type construction");
  ok &= expect(function && compiler.run(*function, "behavior_plugin.noop"),
               "the loaded behavior implements a bodyless compiler function");
  ok &= expect(compiler.load_behavior("behavior_plugin", argv[2]),
               "loading the same exact behavior is idempotent");

  joggle::Compiler adjacent;
  adjacent.load(argv[1]);
  const bool adjacent_linked = adjacent.link();
  const bool adjacent_loaded = adjacent_linked &&
                               adjacent.load_behavior("behavior_plugin");
  if (!adjacent_loaded) {
    adjacent.diagnostics().print(std::cerr);
  }
  ok &= expect(adjacent_loaded,
               "a package-adjacent platform library is discovered directly");
  ok &= expect(!adjacent.load_behavior("missing") && !adjacent.ok(),
               "behavior loading resolves one linked Module name");

  joggle::Compiler rollback;
  rollback.load(argv[1]);
  const bool linked = rollback.link();
  const auto rollback_module = rollback.module("behavior_plugin");
  const bool rejected =
      rollback_module &&
      !rollback.load_behavior("behavior_plugin", argv[3]);
  const auto rollback_type = rollback_module
                                 ? rollback_module->type("positive")
                                 : std::optional<joggle::Module::TypeDecl>{};
  const auto negative = rollback_type
                            ? rollback.make(*rollback_type, std::int64_t{-1})
                            : std::optional<joggle::Type>{};
  ok &= expect(linked && rejected && negative.has_value(),
               "a failed behavior load rolls back every partial binding");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
