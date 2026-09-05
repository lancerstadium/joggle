#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

#include "pass.h"

namespace {

using Map = std::vector<std::pair<joggle::Val, joggle::Val>>;

struct Built {
  joggle::Blk tail;
  joggle::Val value;
};

struct Loop {
  joggle::Blk exit;
  std::vector<joggle::Val> values;
};

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

class LoopBuilder {
public:
  LoopBuilder(joggle::Compiler& compiler, const joggle::Mod& tensor,
              joggle::Diag& diagnostics)
      : compiler_(compiler), tensor_(tensor), diagnostics_(diagnostics) {}

  std::optional<joggle::Fn> run(const joggle::Fn& input) {
    if (input.blks().size() != 1U ||
        input.entry().terminator().kind() != joggle::Term::Kind::Return) {
      return fail("tensor.loops requires one fused straight-line Fn");
    }
    const auto returned = input.entry().terminator().returned();
    if (returned.size() != 1U || !is_tensor(returned.front().type())) {
      return fail("tensor.loops requires one tensor result");
    }
    const auto construction = returned.front().defining_op();
    if (!construction) {
      return fail("tensor.loops requires a constructed tensor result");
    }
    if (!resolve_basis() || !is(*construction, tensor_make_)) {
      return fail("tensor.loops expects tensor.fuse output");
    }
    const auto body = construction->operand("body");
    if (!body) {
      return fail("the fused tensor construction has no body");
    }

    const joggle::Type result_type = returned.front().type();
    const auto element = result_type.get<joggle::Type>("element");
    const auto shape = result_type.get<std::vector<std::int64_t>>("shape");
    if (!element || !shape) {
      return fail("tensor.loops needs a concrete result element and shape");
    }
    const auto shape_type = compiler_.make(*list_type_, *integer_type_);
    const auto shape_value = shape_type ? compiler_.known(*shape_type, *shape)
                                        : std::optional<joggle::Val>{};
    auto output = compiler_.create_fn();
    if (!shape_value || !output) {
      return fail("tensor.loops could not construct its output");
    }

    auto edit = output->edit();
    Map values;
    for (const joggle::Val& argument : input.arguments()) {
      values.emplace_back(argument, edit.argument(argument.type()));
    }
    const auto empty = emit(edit, output->entry(), *tensor_empty_, {},
                            {result_type}, {{"shape", *shape_value}});
    if (!empty) {
      return std::nullopt;
    }

    const auto write = [&](joggle::Blk block,
                           const std::vector<joggle::Val>& state,
                           const std::vector<joggle::Val>& coordinates)
        -> std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>> {
      const auto scalar = invoke(edit, *body, coordinates, values, block);
      if (!scalar) {
        return std::nullopt;
      }
      std::vector<joggle::Val> arguments{state.front(), scalar->value};
      arguments.insert(arguments.end(), coordinates.begin(), coordinates.end());
      const auto update = emit(edit, scalar->tail, *tensor_set_,
                               std::move(arguments), {result_type});
      return update ? std::optional{std::pair{
                          update->parent(),
                          std::vector<joggle::Val>{update->value()}}}
                    : std::nullopt;
    };

    const auto expanded =
        dimensions(edit, empty->parent(), *shape, {empty->value()}, {}, write);
    if (!expanded) {
      return std::nullopt;
    }
    edit.ret(expanded->first, {expanded->second.front()});
    if (!edit.commit(compiler_, diagnostics_)) {
      return std::nullopt;
    }
    return output;
  }

private:
  using Body = std::function<
      std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>>(
          joggle::Blk, const std::vector<joggle::Val>&,
          const std::vector<joggle::Val>&)>;

  std::optional<joggle::Fn> fail(std::string message) {
    diagnostics_.report(std::move(message));
    return std::nullopt;
  }

  bool resolve_basis() {
    const auto arith = compiler_.mod("arith");
    const auto prelude = compiler_.mod("prelude");
    tensor_make_ = tensor_.fn("tensor");
    tensor_reduce_ = tensor_.fn("reduce");
    tensor_index_ = tensor_.fn("[]");
    tensor_empty_ = tensor_.fn("empty");
    tensor_set_ = tensor_.fn("set");
    literal_ = arith ? arith->fn("literal") : std::nullopt;
    add_ = arith ? arith->fn("+") : std::nullopt;
    less_ = arith ? arith->fn("<") : std::nullopt;
    callable_type_ = prelude ? prelude->type("callable") : std::nullopt;
    list_type_ = prelude ? prelude->type("list") : std::nullopt;
    integer_type_ = compiler_.make("int");
    index_type_ = compiler_.make("index");
    boolean_type_ = compiler_.make("i1");
    if (!tensor_make_ || !tensor_reduce_ || !tensor_index_ || !tensor_empty_ ||
        !tensor_set_ || !literal_ || !add_ || !less_ || !callable_type_ ||
        !list_type_ || !integer_type_ || !index_type_ || !boolean_type_) {
      diagnostics_.report("tensor.loops could not resolve its basis");
      return false;
    }
    return true;
  }

  bool is_tensor(const joggle::Type& type) const {
    const auto symbol = type.schema().symbol();
    return symbol.mod_name() == "tensor" && symbol.local_name() == "tensor";
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

  std::optional<joggle::Val> integer(std::int64_t value) {
    return compiler_.known(*integer_type_, value);
  }

  std::optional<joggle::Op> literal(joggle::Fn::Edit& edit, joggle::Blk block,
                                    std::int64_t value) {
    const auto known = integer(value);
    return known ? emit(edit, block, *literal_, {}, {*index_type_},
                        {{"value", *known}})
                 : std::nullopt;
  }

  std::optional<Loop>
  loop(joggle::Fn::Edit& edit, joggle::Blk begin, std::int64_t extent,
       std::vector<joggle::Val> initial,
       const std::function<
           std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>>(
               joggle::Blk, joggle::Val, const std::vector<joggle::Val>&)>&
           body) {
    if (extent < 0) {
      diagnostics_.report("a tensor loop extent cannot be negative");
      return std::nullopt;
    }
    const auto zero = literal(edit, begin, 0);
    const auto bound = literal(edit, begin, extent);
    const auto one = literal(edit, begin, 1);
    if (!zero || !bound || !one) {
      return std::nullopt;
    }
    std::vector<joggle::Type> state_types;
    for (const joggle::Val& value : initial) {
      state_types.push_back(value.type());
    }
    std::vector<joggle::Type> header_types{*index_type_};
    header_types.insert(header_types.end(), state_types.begin(),
                        state_types.end());
    const auto header = edit.blk(header_types);
    const auto iteration = edit.blk(state_types);
    const auto exit = edit.blk(state_types);
    std::vector<joggle::Val> first{zero->value()};
    first.insert(first.end(), initial.begin(), initial.end());
    edit.jump(begin, header, std::move(first));
    const auto header_arguments = header.arguments();
    const auto condition =
        emit(edit, header, *less_, {header_arguments.front(), bound->value()},
             {*boolean_type_});
    if (!condition) {
      return std::nullopt;
    }
    std::vector<joggle::Val> state(header_arguments.begin() + 1,
                                   header_arguments.end());
    edit.branch(condition->parent(), condition->value(), iteration, state, exit,
                state);
    const auto next =
        body(iteration, header_arguments.front(), iteration.arguments());
    if (!next || next->second.size() != state_types.size()) {
      diagnostics_.report("a tensor loop body produced the wrong state");
      return std::nullopt;
    }
    const auto increment =
        emit(edit, next->first, *add_, {header_arguments.front(), one->value()},
             {*index_type_});
    if (!increment) {
      return std::nullopt;
    }
    std::vector<joggle::Val> following{increment->value()};
    following.insert(following.end(), next->second.begin(), next->second.end());
    edit.jump(increment->parent(), header, std::move(following));
    return Loop{exit, exit.arguments()};
  }

  std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>>
  dimensions(joggle::Fn::Edit& edit, joggle::Blk begin,
             const std::vector<std::int64_t>& shape,
             std::vector<joggle::Val> state,
             std::vector<joggle::Val> coordinates, const Body& body,
             std::size_t dimension = 0U) {
    if (dimension == shape.size()) {
      return body(begin, state, coordinates);
    }
    const auto nested = loop(
        edit, begin, shape[dimension], std::move(state),
        [&](joggle::Blk block, joggle::Val index,
            const std::vector<joggle::Val>& carried)
            -> std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>> {
          auto next = coordinates;
          next.push_back(index);
          return dimensions(edit, block, shape, carried, std::move(next), body,
                            dimension + 1U);
        });
    return nested ? std::optional{std::pair{nested->exit, nested->values}}
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
      const auto replacement = mapped(outer, captures[index]);
      if (!replacement) {
        diagnostics_.report("a tensor body capture is not available");
        return std::nullopt;
      }
      local.emplace_back(parameters[arguments.size() + index], *replacement);
    }
    return lower(edit, body->entry().terminator().returned().front(), local,
                 block);
  }

  std::optional<Built> sample(joggle::Fn::Edit& edit, const joggle::Val& source,
                              const std::vector<joggle::Val>& coordinates,
                              Map& values, joggle::Blk block) {
    if (!is_tensor(source.type())) {
      diagnostics_.report("tensor.loops can only sample a tensor value");
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
    if (!definition || !is(*definition, tensor_make_)) {
      diagnostics_.report("tensor.loops found an unfused tensor producer");
      return std::nullopt;
    }
    const auto body = definition->operand("body");
    return body ? invoke(edit, *body, coordinates, values, block)
                : std::nullopt;
  }

  std::optional<Built> lower(joggle::Fn::Edit& edit, const joggle::Val& source,
                             Map& values, joggle::Blk block) {
    if (const auto existing = mapped(values, source)) {
      return Built{block, *existing};
    }
    const auto definition = source.defining_op();
    if (!definition || !definition->callee().referenced_fn()) {
      diagnostics_.report("a scalar expression has no direct definition");
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
        const auto coordinate = lower(edit, arguments[index], values, block);
        if (!coordinate) {
          return std::nullopt;
        }
        block = coordinate->tail;
        coordinates.push_back(coordinate->value);
      }
      const auto loaded =
          sample(edit, arguments.front(), coordinates, values, block);
      if (loaded) {
        values.emplace_back(source, loaded->value);
      }
      return loaded;
    }

    if (is(*definition, tensor_reduce_)) {
      const auto initial = definition->operand("initial");
      const auto body = definition->operand("body");
      const auto extent = definition->callee().binding<std::int64_t>("extent");
      if (!initial || !body || !extent) {
        diagnostics_.report("tensor reduction is incomplete");
        return std::nullopt;
      }
      const auto start = lower(edit, *initial, values, block);
      if (!start) {
        return std::nullopt;
      }
      const auto reduced = loop(
          edit, start->tail, *extent, {start->value},
          [&](joggle::Blk iteration, joggle::Val index,
              const std::vector<joggle::Val>& state)
              -> std::optional<
                  std::pair<joggle::Blk, std::vector<joggle::Val>>> {
            const auto next =
                invoke(edit, *body, {state.front(), index}, values, iteration);
            return next
                       ? std::optional{std::pair{
                             next->tail, std::vector<joggle::Val>{next->value}}}
                       : std::nullopt;
          });
      if (!reduced) {
        return std::nullopt;
      }
      values.emplace_back(source, reduced->values.front());
      return Built{reduced->exit, reduced->values.front()};
    }

    std::vector<joggle::Val> arguments;
    joggle::Blk tail = block;
    for (const joggle::Val& argument : definition->arguments()) {
      const auto value = lower(edit, argument, values, tail);
      if (!value) {
        return std::nullopt;
      }
      tail = value->tail;
      arguments.push_back(value->value);
    }
    std::vector<joggle::Type> results;
    for (const joggle::Val& result : definition->results()) {
      results.push_back(result.type());
    }
    const auto emitted =
        emit(edit, tail, *definition->callee().referenced_fn(),
             std::move(arguments), results, definition->callee().bindings());
    if (!emitted) {
      return std::nullopt;
    }
    const auto before = definition->results();
    const auto after = emitted->results();
    for (std::size_t index = 0; index < before.size(); ++index) {
      values.emplace_back(before[index], after[index]);
    }
    const auto replacement = mapped(values, source);
    return replacement ? std::optional<Built>{{emitted->parent(), *replacement}}
                       : std::nullopt;
  }

  joggle::Compiler& compiler_;
  const joggle::Mod& tensor_;
  joggle::Diag& diagnostics_;
  std::optional<joggle::Mod::FnDecl> tensor_make_;
  std::optional<joggle::Mod::FnDecl> tensor_reduce_;
  std::optional<joggle::Mod::FnDecl> tensor_index_;
  std::optional<joggle::Mod::FnDecl> tensor_empty_;
  std::optional<joggle::Mod::FnDecl> tensor_set_;
  std::optional<joggle::Mod::FnDecl> literal_;
  std::optional<joggle::Mod::FnDecl> add_;
  std::optional<joggle::Mod::FnDecl> less_;
  std::optional<joggle::Mod::TypeDecl> callable_type_;
  std::optional<joggle::Mod::TypeDecl> list_type_;
  std::optional<joggle::Type> integer_type_;
  std::optional<joggle::Type> index_type_;
  std::optional<joggle::Type> boolean_type_;
};

}  // namespace

namespace joggle_tensor {

std::optional<joggle::Fn> loops(joggle::Compiler& compiler,
                                const joggle::Mod& tensor, joggle::Fn input,
                                joggle::Diag& diagnostics) {
  return LoopBuilder(compiler, tensor, diagnostics).run(input);
}

}  // namespace joggle_tensor
