#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <joggle/joggle.h>

namespace {

joggle::Bytes bytes(std::string_view text) {
  joggle::Bytes result;
  result.reserve(text.size());
  for (const char value : text) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

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
                [](joggle::Compiler&, joggle::Function function,
                   joggle::Diagnostics&) { return function; });
  compiler.bind(module, "reverse", [](joggle::Bytes input) {
    std::reverse(input.begin(), input.end());
    return input;
  });
  compiler.bind(module, "read_model",
                [](joggle::Compiler& current, const joggle::Bytes&,
                   joggle::Diagnostics& model_diagnostics)
                    -> std::optional<joggle::Module> {
                  joggle::Module model("loaded_model", {1, 0, 0});
                  auto main = current.create_function();
                  if (!main || !model.insert("main", std::move(*main),
                                             model_diagnostics)) {
                    return std::nullopt;
                  }
                  return model;
                });
  compiler.bind(module, "emit_model", [](const joggle::Module& model) {
    return bytes(joggle::format(model));
  });
#if defined(JOGGLE_TEST_BEHAVIOR_FAIL)
  diagnostics.report("test behavior requested failure");
#endif
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
