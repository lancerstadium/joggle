#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

#include "pass.h"

namespace {

using Map = std::vector<std::pair<joggle::Val, joggle::Val>>;

std::optional<joggle::Val> mapped(const Map& values,
                                  const joggle::Val& source) {
  if (source.known()) {
    return source;
  }
  for (const auto& [before, after] : values) {
    if (before == source) {
      return after;
    }
  }
  return std::nullopt;
}

class Composer {
public:
  Composer(joggle::Compiler& compiler, const joggle::Mod& tensor,
           joggle::Diag& diagnostics)
      : compiler_(compiler), tensor_(tensor), diagnostics_(diagnostics) {}

  std::optional<joggle::Fn> run(joggle::Fn input) {
    for (;;) {
      const auto changed = joggle::inline_calls(compiler_, input, diagnostics_);
      if (!changed) {
        return std::nullopt;
      }
      if (*changed == 0U) {
        break;
      }
    }

    if (input.blks().size() != 1U ||
        input.entry().terminator().kind() != joggle::Term::Kind::Return) {
      return fail("tensor.fuse requires one straight-line Fn");
    }
    const auto returned = input.entry().terminator().returned();
    if (returned.size() != 1U || !is_tensor(returned.front().type())) {
      return fail("tensor.fuse requires one tensor result");
    }
    if (!resolve_basis()) {
      return std::nullopt;
    }
    for (const joggle::Val& argument : input.arguments()) {
      if (is_effect(argument.type())) {
        return fail("tensor.fuse does not cross an effect boundary");
      }
    }
    for (const joggle::Op& op : input.ops()) {
      for (const joggle::Val& result : op.results()) {
        if (is_effect(result.type())) {
          return fail("tensor.fuse does not cross an effect boundary");
        }
        if (is_tensor(result.type()) && result != returned.front() &&
            input.users(result).size() > 1U) {
          return fail("tensor.fuse currently refuses shared tensor producers; "
                      "a later planner must choose recomputation or storage");
        }
      }
    }

    const joggle::Type result_type = returned.front().type();
    const auto element = result_type.get<joggle::Type>("element");
    const auto shape = result_type.get<std::vector<std::int64_t>>("shape");
    if (!element || !shape) {
      return fail("tensor.fuse needs a concrete result element and shape");
    }

    auto output = compiler_.create_fn();
    auto body = compiler_.create_fn();
    if (!output || !body) {
      return fail("tensor.fuse could not create its result Fns");
    }

    auto output_edit = output->edit();
    std::vector<joggle::Val> captures;
    for (const joggle::Val& argument : input.arguments()) {
      const joggle::Val replacement = output_edit.argument(argument.type());
      captures.push_back(replacement);
    }

    auto body_edit = body->edit();
    std::vector<joggle::Val> coordinates;
    coordinates.reserve(shape->size());
    for (std::size_t dimension = 0; dimension < shape->size(); ++dimension) {
      static_cast<void>(dimension);
      coordinates.push_back(body_edit.argument(*index_type_));
    }
    Map body_values;
    for (const joggle::Val& argument : input.arguments()) {
      body_values.emplace_back(argument, body_edit.argument(argument.type()));
    }

    const auto scalar = sample(body_edit, returned.front(), coordinates,
                               body_values, body->entry());
    if (!scalar) {
      return std::nullopt;
    }
    body_edit.ret(scalar->first, {scalar->second});
    if (!body_edit.commit(compiler_, diagnostics_)) {
      return std::nullopt;
    }

    std::vector<joggle::Type> coordinate_types(shape->size(), *index_type_);
    const auto callable = compiler_.make(*callable_type_, coordinate_types,
                                         std::vector<joggle::Type>{*element});
    const auto shape_type = compiler_.make(*list_type_, *integer_type_);
    const auto shape_value = shape_type ? compiler_.known(*shape_type, *shape)
                                        : std::optional<joggle::Val>{};
    if (!callable || !shape_value) {
      return fail("tensor.fuse could not construct the output contract");
    }

    std::optional<joggle::Val> closure;
    try {
      closure = output_edit.callable(std::move(*body), *callable,
                                     std::move(captures));
    } catch (const std::exception& error) {
      return fail("tensor.fuse could not close the output body: " +
                  std::string(error.what()));
    }
    const auto constructed =
        emit(output_edit, output->entry(), *tensor_make_, {*closure},
             {result_type}, {{"shape", *shape_value}});
    if (!constructed) {
      return std::nullopt;
    }
    output_edit.ret(constructed->parent(), {constructed->value()});
    if (!output_edit.commit(compiler_, diagnostics_)) {
      return std::nullopt;
    }
    return output;
  }

private:
  using Built = std::pair<joggle::Blk, joggle::Val>;

  std::optional<joggle::Fn> fail(std::string message) {
    diagnostics_.report(std::move(message));
    return std::nullopt;
  }

  bool resolve_basis() {
    const auto prelude = compiler_.mod("prelude");
    tensor_make_ = tensor_.fn("tensor");
    tensor_map_ = tensor_.fn("map");
    tensor_index_ = tensor_.fn("[]");
    callable_type_ = prelude ? prelude->type("callable") : std::nullopt;
    list_type_ = prelude ? prelude->type("list") : std::nullopt;
    integer_type_ = compiler_.make("int");
    index_type_ = compiler_.make("index");
    if (!tensor_make_ || !tensor_map_ || !tensor_index_ || !callable_type_ ||
        !list_type_ || !integer_type_ || !index_type_) {
      diagnostics_.report("tensor.fuse could not resolve the tensor basis");
      return false;
    }
    return true;
  }

  bool is_tensor(const joggle::Type& type) const {
    const auto symbol = type.schema().symbol();
    return symbol.mod_name() == "tensor" && symbol.local_name() == "tensor";
  }

  bool is_effect(const joggle::Type& type) const {
    const auto symbol = type.schema().symbol();
    return symbol.mod_name() == "prelude" && symbol.local_name() == "effect";
  }

  bool is(const joggle::Op& op,
          const std::optional<joggle::Mod::FnDecl>& declaration) const {
    const auto callee = op.callee().referenced_fn();
    return callee && declaration && callee->symbol() == declaration->symbol();
  }

  std::optional<joggle::Op>
  emit(joggle::Fn::Edit& edit, joggle::Blk block,
       const joggle::Mod::FnDecl& declaration,
       std::vector<joggle::Val> arguments, std::vector<joggle::Type> results,
       std::vector<std::pair<std::string, joggle::Val>> bindings = {}) {
    std::vector<joggle::Type> inputs;
    inputs.reserve(arguments.size());
    for (const joggle::Val& argument : arguments) {
      inputs.push_back(argument.type());
    }
    const auto signature = compiler_.make(*callable_type_, inputs, results);
    if (!signature) {
      diagnostics_.report("could not construct a callable type for '" +
                          declaration.symbol().qualified_name() + "'");
      return std::nullopt;
    }
    try {
      const joggle::Val callee =
          edit.reference(declaration, *signature, std::move(bindings));
      return edit.call(block, callee, std::move(arguments), std::move(results));
    } catch (const std::exception& error) {
      diagnostics_.report("could not emit '" +
                          declaration.symbol().qualified_name() +
                          "': " + error.what());
      return std::nullopt;
    }
  }

  std::optional<Built> copy(joggle::Fn::Edit& edit, const joggle::Val& source,
                            Map& values, joggle::Blk block) {
    if (const auto existing = mapped(values, source)) {
      return Built{block, *existing};
    }

    if (const auto declaration = source.referenced_fn()) {
      std::vector<std::pair<std::string, joggle::Val>> bindings;
      for (const auto& [name, value] : source.bindings()) {
        const auto replacement = copy(edit, value, values, block);
        if (!replacement) {
          return std::nullopt;
        }
        block = replacement->first;
        bindings.emplace_back(name, replacement->second);
      }
      try {
        const joggle::Val replacement =
            edit.reference(*declaration, source.type(), std::move(bindings));
        values.emplace_back(source, replacement);
        return Built{block, replacement};
      } catch (const std::exception& error) {
        diagnostics_.report("tensor.fuse could not copy a fn reference: " +
                            std::string(error.what()));
        return std::nullopt;
      }
    }

    if (const auto inline_body = source.inline_fn()) {
      std::vector<joggle::Val> captures;
      for (const joggle::Val& capture : source.captures()) {
        const auto replacement = copy(edit, capture, values, block);
        if (!replacement) {
          return std::nullopt;
        }
        block = replacement->first;
        captures.push_back(replacement->second);
      }
      try {
        const joggle::Val replacement =
            edit.callable(*inline_body, source.type(), std::move(captures));
        values.emplace_back(source, replacement);
        return Built{block, replacement};
      } catch (const std::exception& error) {
        diagnostics_.report("tensor.fuse could not copy an inline Fn: " +
                            std::string(error.what()));
        return std::nullopt;
      }
    }

    const auto definition = source.defining_op();
    if (!definition) {
      diagnostics_.report("tensor.fuse found an unbound value");
      return std::nullopt;
    }

    if (is(*definition, tensor_index_)) {
      const auto arguments = definition->arguments();
      if (arguments.size() < 2U) {
        diagnostics_.report("tensor indexing has no coordinates");
        return std::nullopt;
      }
      std::vector<joggle::Val> coordinates;
      for (std::size_t index = 1U; index < arguments.size(); ++index) {
        const auto coordinate = copy(edit, arguments[index], values, block);
        if (!coordinate) {
          return std::nullopt;
        }
        block = coordinate->first;
        coordinates.push_back(coordinate->second);
      }
      const auto sampled =
          sample(edit, arguments.front(), coordinates, values, block);
      if (sampled) {
        values.emplace_back(source, sampled->second);
      }
      return sampled;
    }

    const auto callee = copy(edit, definition->callee(), values, block);
    if (!callee) {
      return std::nullopt;
    }
    block = callee->first;
    std::vector<joggle::Val> arguments;
    for (const joggle::Val& argument : definition->arguments()) {
      const auto replacement = copy(edit, argument, values, block);
      if (!replacement) {
        return std::nullopt;
      }
      block = replacement->first;
      arguments.push_back(replacement->second);
    }
    std::vector<joggle::Type> result_types;
    for (const joggle::Val& result : definition->results()) {
      result_types.push_back(result.type());
    }
    std::optional<joggle::Op> cloned;
    try {
      cloned = edit.call(block, callee->second, std::move(arguments),
                         std::move(result_types));
    } catch (const std::exception& error) {
      diagnostics_.report("tensor.fuse could not copy a Call: " +
                          std::string(error.what()));
      return std::nullopt;
    }
    const auto before = definition->results();
    const auto after = cloned->results();
    for (std::size_t index = 0; index < before.size(); ++index) {
      values.emplace_back(before[index], after[index]);
    }
    const auto replacement = mapped(values, source);
    return replacement ? std::optional<Built>{{cloned->parent(), *replacement}}
                       : std::nullopt;
  }

  std::optional<Built> invoke(joggle::Fn::Edit& edit,
                              const joggle::Val& callable,
                              const std::vector<joggle::Val>& arguments,
                              const Map& outer, joggle::Blk block) {
    const auto body = callable.inline_fn();
    if (!body || body->blks().size() != 1U ||
        body->entry().terminator().kind() != joggle::Term::Kind::Return ||
        body->entry().terminator().returned().size() != 1U) {
      diagnostics_.report("tensor bodies must be straight-line Fns");
      return std::nullopt;
    }
    const auto parameters = body->arguments();
    const auto captures = callable.captures();
    if (parameters.size() != arguments.size() + captures.size()) {
      diagnostics_.report("a tensor body has an inconsistent signature");
      return std::nullopt;
    }
    Map local = outer;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      local.emplace_back(parameters[index], arguments[index]);
    }
    for (std::size_t index = 0; index < captures.size(); ++index) {
      auto replacement = mapped(outer, captures[index]);
      if (!replacement) {
        Map mutable_outer = outer;
        const auto copied = copy(edit, captures[index], mutable_outer, block);
        if (!copied) {
          diagnostics_.report("a tensor body capture is not available");
          return std::nullopt;
        }
        block = copied->first;
        replacement = copied->second;
      }
      local.emplace_back(parameters[arguments.size() + index], *replacement);
    }
    return copy(edit, body->entry().terminator().returned().front(), local,
                block);
  }

  std::optional<Built> sample(joggle::Fn::Edit& edit, const joggle::Val& source,
                              const std::vector<joggle::Val>& coordinates,
                              Map& values, joggle::Blk block) {
    if (!is_tensor(source.type())) {
      diagnostics_.report("tensor.fuse can only sample a tensor value");
      return std::nullopt;
    }
    const auto element = source.type().get<joggle::Type>("element");
    if (!element) {
      diagnostics_.report("a sampled tensor has no element type");
      return std::nullopt;
    }
    if (const auto input = mapped(values, source)) {
      std::vector<joggle::Val> arguments{*input};
      arguments.insert(arguments.end(), coordinates.begin(), coordinates.end());
      const auto loaded =
          emit(edit, block, *tensor_index_, std::move(arguments), {*element});
      return loaded ? std::optional<Built>{{loaded->parent(), loaded->value()}}
                    : std::nullopt;
    }
    const auto definition = source.defining_op();
    if (!definition || !definition->callee().referenced_fn()) {
      diagnostics_.report("a tensor value has no composable definition");
      return std::nullopt;
    }
    if (is(*definition, tensor_make_)) {
      const auto body = definition->operand("body");
      if (!body) {
        diagnostics_.report("tensor construction has no body");
        return std::nullopt;
      }
      return invoke(edit, *body, coordinates, values, block);
    }
    if (is(*definition, tensor_map_)) {
      const auto input = definition->operand("input");
      const auto body = definition->operand("body");
      if (!input || !body) {
        diagnostics_.report("tensor map is incomplete");
        return std::nullopt;
      }
      const auto value = sample(edit, *input, coordinates, values, block);
      return value ? invoke(edit, *body, {value->second}, values, value->first)
                   : std::nullopt;
    }
    diagnostics_.report(
        "tensor.fuse reached an opaque tensor producer; give it a body or "
        "leave it outside the fusion region");
    return std::nullopt;
  }

  joggle::Compiler& compiler_;
  const joggle::Mod& tensor_;
  joggle::Diag& diagnostics_;
  std::optional<joggle::Mod::FnDecl> tensor_make_;
  std::optional<joggle::Mod::FnDecl> tensor_map_;
  std::optional<joggle::Mod::FnDecl> tensor_index_;
  std::optional<joggle::Mod::TypeDecl> callable_type_;
  std::optional<joggle::Mod::TypeDecl> list_type_;
  std::optional<joggle::Type> integer_type_;
  std::optional<joggle::Type> index_type_;
};

}  // namespace

namespace joggle_tensor {

std::optional<joggle::Fn> fuse(joggle::Compiler& compiler,
                               const joggle::Mod& tensor, joggle::Fn input,
                               joggle::Diag& diagnostics) {
  return Composer(compiler, tensor, diagnostics).run(std::move(input));
}

}  // namespace joggle_tensor
