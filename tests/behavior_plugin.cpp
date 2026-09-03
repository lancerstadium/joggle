#include <algorithm>
#include <cstdint>

#include <joggle/joggle.h>

namespace {

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto positive = module.type("positive");
  if (!positive) {
    diagnostics.report("test behavior does not match its linked schema");
    return;
  }
  compiler.verify(*positive,
                  [](const joggle::Type& type, joggle::Diagnostics&) {
                    const auto value = type.get<std::int64_t>("value");
                    return value && *value > 0;
                  });
  compiler.bind(module, "noop",
                [](joggle::Compiler&, joggle::ir::Function function,
                   joggle::Diagnostics&) { return function; });
  compiler.bind(module, "reverse", [](joggle::Bytes input) {
    std::reverse(input.begin(), input.end());
    return input;
  });
#if defined(JOGGLE_TEST_BEHAVIOR_FAIL)
  diagnostics.report("test behavior requested failure");
#endif
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
