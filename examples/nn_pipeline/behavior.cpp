#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace {

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto map_relu = module.function("map_relu");
  const auto emit_joggle = module.function("emit_joggle");
  const auto nn = compiler.module("nn");
  const auto accelerator = compiler.module("example_accel");
  const auto nn_relu = nn ? nn->function("relu") : std::nullopt;
  const auto accelerator_relu =
      accelerator ? accelerator->function("relu") : std::nullopt;
  if (!map_relu || !emit_joggle || !nn_relu || !accelerator_relu) {
    diagnostics.report("nn_pipeline behavior does not match its Module set");
    return false;
  }

  compiler.bind(
      *map_relu,
      [nn_relu, accelerator_relu](joggle::ir::Module input,
                                 joggle::Diagnostics& reported)
          -> std::optional<joggle::ir::Module> {
        for (const std::string& name : input.function_names()) {
          joggle::ir::Function* function = input.function(name);
          if (function == nullptr) {
            reported.report("IR Module lost function '" + name + "'");
            return std::nullopt;
          }
          auto edit = function->edit();
          for (const joggle::ir::Instruction& instruction :
               function->instructions()) {
            if (instruction.callee() == *nn_relu) {
              edit.replace(instruction, *accelerator_relu);
            }
          }
          if (!edit.commit(reported)) {
            return std::nullopt;
          }
        }
        return input;
      });

  compiler.bind(
      *emit_joggle,
      [](const joggle::ir::Module& input,
         joggle::Diagnostics& reported) -> std::optional<joggle::Bytes> {
        try {
          const std::string source =
              joggle::format(input, "compiled_model", {1, 0, 0});
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
