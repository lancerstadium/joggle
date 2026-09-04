#include <algorithm>
#include <cstddef>
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

std::optional<joggle::Module::FunctionDecl>
fused_declaration(const joggle::Module& module, std::size_t arity) {
  const auto overloads = module.overloads("conv_relu");
  const auto found = std::find_if(
      overloads.begin(), overloads.end(), [&](const auto& declaration) {
        return declaration.inputs().size() == arity;
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
templates(joggle::Compiler& compiler, const joggle::Op& conv,
          const joggle::Op& relu,
          const joggle::Module::FunctionDecl& fused,
          joggle::Diagnostics& diagnostics) {
  auto build = [&](bool use_fused) -> std::optional<joggle::Function> {
    auto function = compiler.create_function();
    if (!function) {
      diagnostics.report("fusion.run cannot create an expression template");
      return std::nullopt;
    }
    auto edit = function->edit();
    const auto operands = conv.operands();
    std::vector<joggle::Value> parameters;
    parameters.reserve(operands.size());
    for (const joggle::Value& operand : operands) {
      parameters.push_back(edit.argument(operand.type()));
    }

    std::vector<joggle::Value> arguments;
    arguments.reserve(conv.arguments().size());
    for (const joggle::Value& argument : conv.arguments()) {
      if (argument.known()) {
        arguments.push_back(argument);
        continue;
      }
      const auto found = std::find(operands.begin(), operands.end(), argument);
      if (found == operands.end()) {
        diagnostics.report("fusion.run lost a Conv operand");
        return std::nullopt;
      }
      arguments.push_back(parameters[static_cast<std::size_t>(
          std::distance(operands.begin(), found))]);
    }

    std::optional<joggle::Op> result;
    if (use_fused) {
      result = edit.append(fused, std::move(arguments),
                           {relu.result(0).type()});
      if (const auto location = relu.location()) {
        edit.locate(*result, *location);
      }
    } else {
      const joggle::Op convolved = edit.append(
          conv.callee(), std::move(arguments), {conv.result(0).type()});
      result = edit.append(relu.callee(), {convolved.result(0)},
                           {relu.result(0).type()});
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
  const std::size_t limit = input.ops().size();
  if (limit == 0U) {
    return input;
  }
  for (std::size_t iteration = 0; iteration < limit; ++iteration) {
    std::optional<joggle::Op> selected_conv;
    std::optional<joggle::Op> selected_relu;
    for (const joggle::Op& relu : input.ops()) {
      if (!is(relu, "tensor", "relu") || relu.operands().size() != 1U) {
        continue;
      }
      const auto conv = relu.operands().front().defining_op();
      if (!conv || !is(*conv, "tensor", "conv") ||
          conv->results().size() != 1U ||
          input.users(conv->result(0)).size() != 1U) {
        continue;
      }
      selected_conv = *conv;
      selected_relu = relu;
      break;
    }
    if (!selected_conv || !selected_relu) {
      return input;
    }

    const auto fused =
        fused_declaration(module, selected_conv->arguments().size());
    if (!fused) {
      diagnostics.report("fusion.run has no Conv/ReLU semantic overload for "
                         "this call shape");
      return std::nullopt;
    }
    auto pair = templates(compiler, *selected_conv, *selected_relu, *fused,
                          diagnostics);
    if (!pair) {
      return std::nullopt;
    }
    const auto changed = joggle::replace(
        compiler, input, pair->before, pair->after, diagnostics);
    if (!changed || *changed == 0U) {
      if (changed) {
        diagnostics.report("fusion.run selected a pair that did not match");
      }
      return std::nullopt;
    }
  }
  diagnostics.report("fusion.run exceeded its structural progress bound");
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
        overloads.begin(), overloads.end(), [&](const auto& declaration) {
          return declaration.signature() == update.signature;
        });
    joggle::Function* body = current == overloads.end()
                                 ? nullptr
                                 : input.body(*current);
    if (body == nullptr) {
      diagnostics.report("fusion.run lost function '" + update.signature +
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
