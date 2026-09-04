#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

namespace {

bool is(const joggle::Op& op, std::string_view module,
        std::string_view name) {
  return op.callee().symbol().module_name() == module &&
         op.callee().symbol().local_name() == name;
}

bool axis(const joggle::Op& op, std::int64_t expected) {
  return op.property<std::int64_t>("axis") == expected;
}

bool element(const joggle::Value& value, std::string_view expected) {
  const auto type = value.type().get<joggle::Type>("element");
  return type && type->schema().name() == expected;
}

bool same_shape(const joggle::Value& left, const joggle::Value& right) {
  const auto left_shape =
      left.type().get<std::vector<std::int64_t>>("shape");
  const auto right_shape =
      right.type().get<std::vector<std::int64_t>>("shape");
  return left_shape && right_shape && *left_shape == *right_shape;
}

bool control_uses(const joggle::Function& function,
                  const joggle::Value& value) {
  for (const joggle::Block& block : function.blocks()) {
    const joggle::Terminator terminator = block.terminator();
    const auto returned = terminator.returned();
    if (terminator.condition() == std::optional<joggle::Value>{value} ||
        std::find(returned.begin(), returned.end(), value) != returned.end()) {
      return true;
    }
    for (std::size_t successor = 0;
         successor < terminator.successor_count(); ++successor) {
      const auto arguments = terminator.arguments(successor);
      if (std::find(arguments.begin(), arguments.end(), value) !=
          arguments.end()) {
        return true;
      }
    }
  }
  return false;
}

std::optional<std::vector<joggle::Value>>
arguments(const joggle::Function& function, const joggle::Op& output) {
  if (!is(output, "quant", "quantize") || !axis(output, 1) ||
      output.arguments().size() != 4U || output.operands().size() != 3U) {
    return std::nullopt;
  }
  const auto convolution = output.arguments()[0].defining_op();
  if (!convolution || !is(*convolution, "tensor", "conv") ||
      convolution->arguments().size() != 7U ||
      convolution->results().size() != 1U ||
      function.users(convolution->result(0)).size() != 1U ||
      control_uses(function, convolution->result(0)) ||
      !element(convolution->result(0), "f32") ||
      !element(output.arguments()[1], "f32") ||
      !element(output.arguments()[2], "u8") ||
      !element(output.result(0), "u8") ||
      !same_shape(output.arguments()[1], output.arguments()[2]) ||
      !same_shape(convolution->result(0), output.result(0))) {
    return std::nullopt;
  }

  std::vector<joggle::Value> result;
  result.reserve(15U);
  constexpr std::int64_t axes[] = {1, 0, 0};
  constexpr std::string_view storage[] = {"u8", "i8", "i32"};
  for (std::size_t index = 0; index < 3U; ++index) {
    const auto producer = convolution->arguments()[index].defining_op();
    if (!producer || !is(*producer, "quant", "dequantize") ||
        !axis(*producer, axes[index]) ||
        producer->arguments().size() != 4U ||
        producer->operands().size() != 3U ||
        producer->results().size() != 1U ||
        !element(producer->arguments()[0], storage[index]) ||
        !element(producer->arguments()[1], "f32") ||
        !element(producer->arguments()[2], storage[index]) ||
        !element(producer->result(0), "f32") ||
        !same_shape(producer->arguments()[1], producer->arguments()[2]) ||
        !same_shape(producer->arguments()[0], producer->result(0))) {
      return std::nullopt;
    }
    const auto values = producer->arguments();
    result.insert(result.end(), values.begin(), values.begin() + 3);
  }
  const auto output_arguments = output.arguments();
  result.insert(result.end(), output_arguments.begin() + 1,
                output_arguments.begin() + 3);
  const auto convolution_arguments = convolution->arguments();
  result.insert(result.end(), convolution_arguments.begin() + 3,
                convolution_arguments.end());
  return result;
}

std::optional<joggle::Module::FunctionDecl>
reference(const joggle::Module& module) {
  const auto overloads = module.overloads("nchw_conv");
  const auto found = std::find_if(
      overloads.begin(), overloads.end(), [](const auto& current) {
        return current.inputs().size() == 15U;
      });
  return found == overloads.end()
             ? std::optional<joggle::Module::FunctionDecl>{}
             : std::optional<joggle::Module::FunctionDecl>{*found};
}

template <typename Subject>
std::optional<Subject> run(joggle::Compiler& compiler, Subject input,
                           const joggle::Module& module,
                           joggle::Diagnostics& diagnostics) {
  const auto function = reference(module);
  if (!function) {
    diagnostics.report("qdq.run cannot find its NCHW Conv reference function");
    return std::nullopt;
  }
  const auto changed = joggle::outline(
      compiler, input, *function,
      [](const joggle::Function& body, const joggle::Op& root) {
        return arguments(body, root);
      },
      diagnostics);
  return changed ? std::optional<Subject>{std::move(input)} : std::nullopt;
}

}  // namespace

void joggle_module(joggle::Compiler& compiler, const joggle::Module& module,
                   joggle::Diagnostics&) {
  compiler.bind(module, "run",
                [module](joggle::Compiler& active, joggle::Function input,
                         joggle::Diagnostics& diagnostics) {
                  return run(active, std::move(input), module, diagnostics);
                });
  compiler.bind(module, "run",
                [module](joggle::Compiler& active, joggle::Module input,
                         joggle::Diagnostics& diagnostics) {
                  return run(active, std::move(input), module, diagnostics);
                });
}
