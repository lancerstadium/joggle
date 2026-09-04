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

std::optional<joggle::Module> mark(joggle::Compiler& compiler,
                                   joggle::Module input, std::string name,
                                   joggle::Diagnostics& diagnostics) {
  auto marker = compiler.create_function();
  if (!marker ||
      !input.insert(std::move(name), std::move(*marker), diagnostics)) {
    return std::nullopt;
  }
  return input;
}

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto positive = module.type("positive");
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
      module, "cached", [](std::int64_t value) { return value + 100; },
      joggle::HostEvaluation::Hermetic);
  const auto integer = compiler.make("int");
  const auto one = integer ? compiler.known(*integer, std::int64_t{1})
                           : std::optional<joggle::Value>{};
  const auto probe =
      one ? compiler.materialize("native_plugin.cache_probe", {*one})
          : std::optional<joggle::Function>{};
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
      module, "cached", [](std::int64_t value) { return value + 1; },
      joggle::HostEvaluation::Hermetic);
#endif
  compiler.bind(module, "noop",
                [](joggle::Compiler&, joggle::Function function,
                   joggle::Diagnostics&) { return function; });
  compiler.bind(module, "reverse", [](joggle::Bytes input) {
    std::reverse(input.begin(), input.end());
    return input;
  });
  compiler.bind(module, "read_model",
                [](joggle::Compiler& current, const joggle::Bytes& input,
                   joggle::Diagnostics& model_diagnostics)
                    -> std::optional<joggle::Module> {
                  joggle::Module model("loaded_model", {1, 0, 0});
                  static_cast<void>(model.store(input));
                  auto main = current.create_function();
                  if (!main || !model.insert("main", std::move(*main),
                                             model_diagnostics)) {
                    return std::nullopt;
                  }
                  return model;
                });
  compiler.bind(module, "normalize_model",
                [](joggle::Compiler& current, joggle::Module input,
                   joggle::Diagnostics& transform_diagnostics) {
                  return mark(current, std::move(input), "normalized",
                              transform_diagnostics);
                });
  compiler.bind(module, "specialize_model",
                [](joggle::Compiler& current, joggle::Module input,
                   joggle::Diagnostics& transform_diagnostics) {
                  return mark(current, std::move(input), "specialized",
                              transform_diagnostics);
                });
  compiler.bind(module, "reject_model",
                [](joggle::Module, joggle::Diagnostics& transform_diagnostics)
                    -> std::optional<joggle::Module> {
                  transform_diagnostics.report(
                      "test transform requested rejection");
                  return std::nullopt;
                });
  compiler.bind(module, "emit_model", [](const joggle::Module& model) {
    return bytes(joggle::format(model));
  });
#if defined(JOGGLE_TEST_NATIVE_FAIL)
  diagnostics.report("test native requested failure");
#endif
}

}  // namespace

void joggle_module(joggle::Compiler& compiler, const joggle::Module& module,
                   joggle::Diagnostics& diagnostics) {
  bind(compiler, module, diagnostics);
}
