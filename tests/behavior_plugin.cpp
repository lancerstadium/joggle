#include <cstdint>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto positive = module.type("positive");
  const auto noop = module.pass("noop");
  if (!positive || !noop) {
    diagnostics.report("test behavior does not match its linked schema");
    return false;
  }
  compiler.bind(*positive,
                [](const joggle::Type& type, joggle::Diagnostics&) {
    const auto value = type.get<std::int64_t>("value");
    return value && *value > 0;
  });
  compiler.bind(*noop, [](joggle::Compiler&, joggle::Graph&,
                          joggle::Diagnostics&) { return true; });
#if defined(JOGGLE_TEST_BEHAVIOR_FAIL)
  return false;
#else
  return true;
#endif
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
