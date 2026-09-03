#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto map_relu = module.declaration("map_relu");
  const auto emit_joggle = module.declaration("emit_joggle");
  const auto nn = compiler.module("nn");
  const auto accelerator = compiler.module("example_accel");
  const auto nn_relu = nn ? nn->declaration("relu") : std::nullopt;
  const auto accelerator_relu =
      accelerator ? accelerator->declaration("relu") : std::nullopt;
  if (!map_relu || !emit_joggle || !nn_relu || !accelerator_relu) {
    diagnostics.report("nn_pipeline behavior does not match its Module set");
    return false;
  }

  compiler.bind(*map_relu,
                [nn_relu, accelerator_relu](joggle::Module input,
                                            joggle::Diagnostics& reported)
                    -> std::optional<joggle::Module> {
                  if (!joggle::ir::replace_calls(input, *nn_relu,
                                                 *accelerator_relu, reported)) {
                    return std::nullopt;
                  }
                  return input;
                });

  compiler.bind(*emit_joggle,
                [](const joggle::Module& input, joggle::Diagnostics& reported)
                    -> std::optional<joggle::Bytes> {
                  try {
                    const std::string source = joggle::format(input);
                    joggle::Bytes bytes(source.size());
                    std::transform(source.begin(), source.end(), bytes.begin(),
                                   [](char value) {
                                     return static_cast<std::byte>(
                                         static_cast<unsigned char>(value));
                                   });
                    return bytes;
                  } catch (const std::exception& error) {
                    reported.report(error.what());
                    return std::nullopt;
                  }
                });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
