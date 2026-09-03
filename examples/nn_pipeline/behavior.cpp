#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto convert_relu = module.function("convert_relu");
  const auto count_instructions = module.function("count_instructions");
  const auto emit_joggle = module.function("emit_joggle");
  const auto nn = compiler.module("nn");
  const auto accelerator = compiler.module("example_accel");
  const auto nn_relu = nn ? nn->function("relu") : std::nullopt;
  const auto accelerator_relu =
      accelerator ? accelerator->function("relu") : std::nullopt;
  if (!convert_relu || !count_instructions || !emit_joggle || !nn_relu ||
      !accelerator_relu) {
    diagnostics.report("nn_pipeline behavior does not match its Module set");
    return false;
  }

  compiler.bind(
      *convert_relu,
      [nn_relu, accelerator_relu](
          joggle::Module input,
          joggle::Diagnostics& reported) -> std::optional<joggle::Module> {
        const auto converted = joggle::ir::convert(
            input,
            [&](const joggle::ir::Instruction& instruction,
                joggle::ir::Function::Edit& edit, joggle::Diagnostics&) {
              if (instruction.callee() != *nn_relu) {
                return false;
              }
              edit.replace(instruction, *accelerator_relu);
              return true;
            },
            [&](const joggle::ir::Instruction& instruction) {
              return instruction.callee() != *nn_relu;
            },
            reported);
        if (!converted) {
          return std::nullopt;
        }
        return input;
      });

  compiler.bind(
      *count_instructions,
      [](const joggle::Module& input,
         joggle::Diagnostics& reported) -> std::optional<std::int64_t> {
        std::size_t count = 0;
        for (const joggle::Module::FunctionDecl& member : input.functions()) {
          const joggle::ir::Function* function = member.body();
          if (function == nullptr) {
            continue;
          }
          const std::size_t size = function->instructions().size();
          if (std::numeric_limits<std::size_t>::max() - count < size) {
            reported.report("instruction count overflowed");
            return std::nullopt;
          }
          count += size;
        }
        if (count > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
          reported.report("instruction count does not fit in int");
          return std::nullopt;
        }
        return static_cast<std::int64_t>(count);
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
