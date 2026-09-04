#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

struct Candidate {
  joggle::Op output;
  joggle::Op convolution;
  std::vector<joggle::Op> inputs;
};

std::optional<Candidate> candidate(const joggle::Function& function,
                                   const joggle::Op& output) {
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

  std::vector<joggle::Op> inputs;
  inputs.reserve(3U);
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
    inputs.push_back(*producer);
  }
  return Candidate{output, *convolution, std::move(inputs)};
}

std::optional<joggle::Module::FunctionDecl>
declaration(const joggle::Module& module) {
  const auto overloads = module.overloads("conv");
  const auto found = std::find_if(
      overloads.begin(), overloads.end(), [](const auto& current) {
        return current.inputs().size() == 15U;
      });
  return found == overloads.end()
             ? std::optional<joggle::Module::FunctionDecl>{}
             : std::optional<joggle::Module::FunctionDecl>{*found};
}

struct TemplatePair {
  joggle::Function before;
  joggle::Function after;
};

std::optional<TemplatePair>
templates(joggle::Compiler& compiler, const Candidate& selected,
          const joggle::Module::FunctionDecl& fused,
          joggle::Diagnostics& diagnostics) {
  auto build = [&](bool use_fused) -> std::optional<joggle::Function> {
    auto function = compiler.create_function();
    if (!function) {
      diagnostics.report("qconv.run cannot create an expression template");
      return std::nullopt;
    }
    auto edit = function->edit();

    std::vector<std::vector<joggle::Value>> input_parameters;
    input_parameters.reserve(selected.inputs.size());
    for (std::size_t input = 0; input < selected.inputs.size(); ++input) {
      const auto arguments = selected.inputs[input].arguments();
      std::vector<joggle::Value> parameters;
      parameters.reserve(3U);
      for (std::size_t parameter = 0; parameter < 3U; ++parameter) {
        parameters.push_back(edit.argument(arguments[parameter].type()));
      }
      input_parameters.push_back(std::move(parameters));
    }
    const auto output_arguments = selected.output.arguments();
    const joggle::Value output_scale =
        edit.argument(output_arguments[1].type());
    const joggle::Value output_zero =
        edit.argument(output_arguments[2].type());
    const auto convolution_arguments = selected.convolution.arguments();

    std::optional<joggle::Op> result;
    if (use_fused) {
      std::vector<joggle::Value> arguments;
      arguments.reserve(15U);
      for (const auto& input : input_parameters) {
        arguments.insert(arguments.end(), input.begin(), input.end());
      }
      arguments.push_back(output_scale);
      arguments.push_back(output_zero);
      arguments.insert(arguments.end(), convolution_arguments.begin() + 3,
                       convolution_arguments.end());
      result = edit.append(fused, std::move(arguments),
                           {selected.output.result(0).type()});
      if (const auto location = selected.output.location()) {
        edit.locate(*result, *location);
      }
    } else {
      std::vector<joggle::Op> expressed;
      expressed.reserve(selected.inputs.size());
      for (std::size_t input = 0; input < selected.inputs.size(); ++input) {
        std::vector<joggle::Value> arguments(input_parameters[input].begin(),
                                             input_parameters[input].end());
        arguments.push_back(selected.inputs[input].arguments()[3]);
        expressed.push_back(edit.append(
            selected.inputs[input].callee(), std::move(arguments),
            {selected.inputs[input].result(0).type()}));
      }
      std::vector<joggle::Value> arguments{
          expressed[0].result(0), expressed[1].result(0),
          expressed[2].result(0)};
      arguments.insert(arguments.end(), convolution_arguments.begin() + 3,
                       convolution_arguments.end());
      const joggle::Op convolved = edit.append(
          selected.convolution.callee(), std::move(arguments),
          {selected.convolution.result(0).type()});
      result = edit.append(
          selected.output.callee(),
          {convolved.result(0), output_scale, output_zero,
           selected.output.arguments()[3]},
          {selected.output.result(0).type()});
    }
    edit.ret(function->entry(), {result->result(0)});
    return edit.commit(diagnostics) ? std::move(function) : std::nullopt;
  };

  auto before = build(false);
  auto after = build(true);
  if (!before || !after) {
    return std::nullopt;
  }
  return TemplatePair{std::move(*before), std::move(*after)};
}

std::optional<joggle::Function>
run(joggle::Compiler& compiler, joggle::Function input,
    const joggle::Module& module, joggle::Diagnostics& diagnostics) {
  const auto fused = declaration(module);
  if (!fused) {
    diagnostics.report("qconv.run cannot find its Conv semantic function");
    return std::nullopt;
  }
  const std::size_t limit = input.ops().size();
  if (limit == 0U) {
    return input;
  }
  for (std::size_t iteration = 0; iteration < limit; ++iteration) {
    std::optional<Candidate> selected;
    for (const joggle::Op& op : input.ops()) {
      selected = candidate(input, op);
      if (selected) {
        break;
      }
    }
    if (!selected) {
      return input;
    }
    auto pair = templates(compiler, *selected, *fused, diagnostics);
    if (!pair) {
      return std::nullopt;
    }
    const auto changed = joggle::replace(
        compiler, input, pair->before, pair->after, diagnostics);
    if (!changed || *changed == 0U) {
      if (changed) {
        diagnostics.report("qconv.run selected an expression that did not "
                           "match");
      }
      return std::nullopt;
    }
  }
  diagnostics.report("qconv.run exceeded its structural progress bound");
  return std::nullopt;
}

std::optional<joggle::Module>
run(joggle::Compiler& compiler, joggle::Module input,
    const joggle::Module& module, joggle::Diagnostics& diagnostics) {
  struct Update {
    std::string name;
    std::string signature;
    joggle::Function body;
  };
  std::vector<Update> updates;
  for (const joggle::Module::FunctionDecl& member : input.functions()) {
    const joggle::Function* body = member.body();
    if (body == nullptr) {
      continue;
    }
    auto transformed = run(compiler, *body, module, diagnostics);
    if (!transformed) {
      return std::nullopt;
    }
    updates.push_back({std::string(member.name()), member.signature(),
                       std::move(*transformed)});
  }
  for (Update& update : updates) {
    const auto overloads = input.overloads(update.name);
    const auto current = std::find_if(
        overloads.begin(), overloads.end(), [&](const auto& member) {
          return member.signature() == update.signature;
        });
    joggle::Function* body = current == overloads.end()
                                 ? nullptr
                                 : input.body(*current);
    if (body == nullptr) {
      diagnostics.report("qconv.run lost function '" + update.signature +
                         "'");
      return std::nullopt;
    }
    *body = std::move(update.body);
  }
  return input;
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
