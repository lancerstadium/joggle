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

std::optional<joggle::Mod> mark(joggle::Compiler& compiler, joggle::Mod input,
                                std::string name,
                                joggle::Diagnostics& diagnostics) {
  auto marker = compiler.create_fn();
  if (!marker ||
      !input.insert(std::move(name), std::move(*marker), diagnostics)) {
    return std::nullopt;
  }
  return input;
}

void bind(joggle::Compiler& compiler, const joggle::Mod& mod,
          joggle::Diagnostics& diagnostics) {
  const auto positive = mod.type("positive");
  if (!positive) {
    diagnostics.report("test native does not match its linked schema");
    return;
  }
  compiler.verify(*positive,
                  [](const joggle::Type& type, joggle::Diagnostics&) {
                    const auto value = type.get<std::int64_t>("value");
                    return value && *value > 0;
                  });
#if defined(JOGGLE_TEST_NATIVE_FAIL)
  compiler.bind(
      mod, "cached", [](std::int64_t value) { return value + 100; },
      joggle::HostEvaluation::Hermetic);
  const auto integer = compiler.make("int");
  const auto one = integer ? compiler.known(*integer, std::int64_t{1})
                           : std::optional<joggle::Val>{};
  const auto probe =
      one ? compiler.materialize("native_plugin.cache_probe", {*one})
          : std::optional<joggle::Fn>{};
  const auto cached =
      probe && !probe->arguments().empty()
          ? probe->arguments().front().type().get<std::int64_t>("value")
          : std::optional<std::int64_t>{};
  if (cached != std::optional<std::int64_t>{101}) {
    diagnostics.report("failed native could not prime its evaluation cache");
    return;
  }
#else
  compiler.bind(
      mod, "cached", [](std::int64_t value) { return value + 1; },
      joggle::HostEvaluation::Hermetic);
#endif
  compiler.bind(mod, "noop",
                [](joggle::Compiler&, joggle::Fn fn, joggle::Diagnostics&) {
                  return fn;
                });
  compiler.bind(mod, "reverse", [](joggle::Bytes input) {
    std::reverse(input.begin(), input.end());
    return input;
  });
  compiler.bind(
      mod, "read_model",
      [](joggle::Compiler& current, const joggle::Bytes& input,
         joggle::Diagnostics& model_diagnostics) -> std::optional<joggle::Mod> {
        joggle::Mod model("loaded_model", {1, 0, 0});
        static_cast<void>(model.store(input));
        auto main = current.create_fn();
        if (!main ||
            !model.insert("main", std::move(*main), model_diagnostics)) {
          return std::nullopt;
        }
        return model;
      });
  compiler.bind(mod, "normalize_model",
                [](joggle::Compiler& current, joggle::Mod input,
                   joggle::Diagnostics& transform_diagnostics) {
                  return mark(current, std::move(input), "normalized",
                              transform_diagnostics);
                });
  compiler.bind(mod, "specialize_model",
                [](joggle::Compiler& current, joggle::Mod input,
                   joggle::Diagnostics& transform_diagnostics) {
                  return mark(current, std::move(input), "specialized",
                              transform_diagnostics);
                });
  compiler.bind(mod, "reject_model",
                [](joggle::Mod, joggle::Diagnostics& transform_diagnostics)
                    -> std::optional<joggle::Mod> {
                  transform_diagnostics.report(
                      "test transform requested rejection");
                  return std::nullopt;
                });
  compiler.bind(mod, "emit_model", [](const joggle::Mod& model) {
    return bytes(joggle::format(model));
  });
#if defined(JOGGLE_TEST_NATIVE_FAIL)
  diagnostics.report("test native requested failure");
#endif
}

}  // namespace

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diagnostics& diagnostics) {
  bind(compiler, mod, diagnostics);
}
