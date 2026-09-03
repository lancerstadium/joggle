#include <algorithm>
#include <cstdint>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto positive = module.type("positive");
  const auto noop = module.function("noop");
  const auto reverse = module.function("reverse");
  if (!positive || !noop || !reverse) {
    diagnostics.report("test behavior does not match its linked schema");
    return false;
  }
  compiler.verify(
      *positive, [](const joggle::Type& type, joggle::Diagnostics&) {
        const auto value = type.get<std::int64_t>("value");
        return value && *value > 0;
      });
  compiler.bind(*noop,
                [](joggle::Compiler&, joggle::ir::Function function,
                   joggle::Diagnostics&) { return function; });
  compiler.bind(*reverse, [](joggle::Bytes input) {
    std::reverse(input.begin(), input.end());
    return input;
  });
#if defined(JOGGLE_TEST_BEHAVIOR_FAIL)
  return false;
#else
  return true;
#endif
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
