#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
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
  const auto module = compiler.module("native_plugin");
  if (!module || !compiler.load_native("native_plugin", argv[2])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto positive = module->type("positive");
  const auto value = positive ? compiler.make(*positive, std::int64_t{7})
                              : std::optional<joggle::Type>{};
  auto function = compiler.create_function();
  const auto transformed =
      function ? compiler.run<joggle::Function>("native_plugin.noop", *function)
               : std::nullopt;
  bool ok = true;
  ok &=
      expect(value.has_value(), "the loaded native refines type construction");
  ok &= expect(transformed.has_value(),
               "the loaded native implements a bodyless compiler function");
  ok &= expect(compiler.load_native("native_plugin", argv[2]),
               "loading the same exact native is idempotent");

  joggle::Compiler adjacent;
  adjacent.load(argv[1]);
  const bool adjacent_linked = adjacent.link();
  const bool adjacent_loaded =
      adjacent_linked && adjacent.load_native("native_plugin");
  if (!adjacent_loaded) {
    adjacent.diagnostics().print(std::cerr);
  }
  ok &= expect(adjacent_loaded,
               "a Module-adjacent platform library is discovered directly");
  ok &= expect(!adjacent.load_native("missing") && !adjacent.ok(),
               "native loading resolves one linked Module name");

  joggle::Compiler rollback;
  rollback.load(argv[1]);
  const bool linked = rollback.link();
  const auto rollback_module = rollback.module("native_plugin");
  const bool rejected =
      rollback_module && !rollback.load_native("native_plugin", argv[3]);
  const auto rollback_type = rollback_module
                                 ? rollback_module->type("positive")
                                 : std::optional<joggle::Module::TypeDecl>{};
  const auto negative = rollback_type
                            ? rollback.make(*rollback_type, std::int64_t{-1})
                            : std::optional<joggle::Type>{};
  const bool recovered =
      rejected && rollback.load_native("native_plugin", argv[2]);
  const auto integer =
      recovered ? rollback.make("int") : std::optional<joggle::Type>{};
  const auto one = integer ? rollback.known(*integer, std::int64_t{1})
                           : std::optional<joggle::Value>{};
  const auto probe =
      one ? rollback.materialize("native_plugin.cache_probe", {*one})
          : std::optional<joggle::Function>{};
  const auto cached =
      probe && !probe->arguments().empty()
          ? probe->arguments().front().type().get<std::int64_t>("value")
          : std::optional<std::int64_t>{};
  ok &= expect(linked && rejected && negative.has_value() && recovered &&
                   cached == std::optional<std::int64_t>{2},
               "a failed native load rolls back bindings, verifiers, and "
               "Hermetic evaluation cache entries");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
