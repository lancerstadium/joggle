#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <joggle/joggle.h>

namespace {

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto nn = compiler.module("nn");
  const auto accelerator = compiler.module("example_accel");
  const auto nn_relu = nn ? nn->function("relu") : std::nullopt;
  const auto nn_batch_norm =
      nn ? nn->function("batch_norm_nchw") : std::nullopt;
  const auto fused = accelerator
                         ? accelerator->function("batch_norm_relu_nchw")
                         : std::nullopt;
  if (!nn_relu || !nn_batch_norm || !fused) {
    diagnostics.report("nn_pipeline behavior does not match its Module set");
    return;
  }

  compiler.bind(module, "fuse_norm_relu",
                [nn_relu, nn_batch_norm, fused](
                    joggle::Module input, joggle::Diagnostics& reported)
                    -> std::optional<joggle::Module> {
                  const auto changed = joggle::rewrite(
                      input,
                      [&](const joggle::Op& op,
                          joggle::Function::Edit& edit, joggle::Diagnostics&) {
                        if (op.callee() != *nn_relu) {
                          return false;
                        }
                        const auto operands = op.operands();
                        const auto producer =
                            operands.size() == 1U
                                ? operands.front().defining_op()
                                : std::optional<joggle::Op>{};
                        if (!producer || producer->callee() != *nn_batch_norm ||
                            producer->results().size() != 1U ||
                            producer->parent() != op.parent() ||
                            producer->value().users() !=
                                std::vector<joggle::Op>{op}) {
                          return false;
                        }
                        std::vector<joggle::Value> arguments =
                            producer->operands();
                        const auto epsilon = producer->property("epsilon");
                        if (!epsilon) {
                          return false;
                        }
                        arguments.push_back(*epsilon);
                        const auto replacement = edit.insert(
                            op, *fused, std::move(arguments),
                            {op.value().type()});
                        edit.replace(op, replacement.results());
                        edit.erase(*producer);
                        return true;
                      },
                      reported);
                  if (!changed) {
                    return std::nullopt;
                  }
                  return input;
                });

  compiler.bind(
      module, "count_ops",
      [](const joggle::Module& input,
         joggle::Diagnostics& reported) -> std::optional<std::int64_t> {
        std::size_t count = 0;
        for (const joggle::Module::FunctionDecl& member : input.functions()) {
          const joggle::Function* function = member.body();
          if (function == nullptr) {
            continue;
          }
          const std::size_t size = function->ops().size();
          if (std::numeric_limits<std::size_t>::max() - count < size) {
            reported.report("op count overflowed");
            return std::nullopt;
          }
          count += size;
        }
        if (count > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
          reported.report("op count does not fit in int");
          return std::nullopt;
        }
        return static_cast<std::int64_t>(count);
      });

  compiler.bind(module, "emit_joggle",
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
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
