#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

namespace {

using Map = std::vector<std::pair<joggle::Val, joggle::Val>>;

struct Lowered {
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

class Realizer {
public:
  Realizer(joggle::Compiler& compiler, const joggle::Mod& mem,
           joggle::Diag& diagnostics)
      : compiler_(compiler), mem_(mem), diagnostics_(diagnostics) {}

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
      return fail("mem.realize currently requires one normalized block");
    }
    const auto returned = input.entry().terminator().returned();
    if (returned.size() != 1U || !is_tensor(returned.front().type())) {
      return fail("mem.realize currently requires one tensor result");
    }
    const auto root = returned.front().defining_op();
    if (!root || !root->callee().referenced_fn()) {
      return fail("the tensor result has no realizable defining call");
    }

    const auto tensor = compiler_.mod("tensor");
    const auto arith = compiler_.mod("arith");
    const auto prelude = compiler_.mod("prelude");
    if (!tensor || !arith || !prelude) {
      return fail("mem.realize needs tensor, arith, and prelude");
    }
    tensor_make_ = tensor->fn("tensor");
    tensor_map_ = tensor->fn("map");
    tensor_reduce_ = tensor->fn("reduce");
    tensor_index_ = tensor->fn("[]");
    mem_view_type_ = mem_.type("view");
    mem_sink_type_ = mem_.type("sink");
    mem_view_ = mem_.fn("view");
    mem_index_ = mem_.fn("[]");
    mem_alloc_ = mem_.fn("alloc");
    mem_store_ = mem_.fn("store");
    mem_seal_ = mem_.fn("seal");
    literal_ = arith->fn("literal");
    add_ = arith->fn("+");
    less_ = arith->fn("<");
    callable_type_ = prelude->type("callable");
    effect_type_ = prelude->type("effect");
    list_type_ = prelude->type("list");
    integer_type_ = compiler_.make("int");
    index_type_ = compiler_.make("index");
    boolean_type_ = compiler_.make("i1");
    if (!tensor_make_ || !tensor_map_ || !tensor_reduce_ || !tensor_index_ ||
        !mem_view_type_ || !mem_sink_type_ || !mem_view_ || !mem_index_ ||
        !mem_alloc_ || !mem_store_ || !mem_seal_ || !literal_ || !add_ ||
        !less_ || !callable_type_ || !effect_type_ || !list_type_ ||
        !integer_type_ || !index_type_ || !boolean_type_) {
      return fail("mem.realize could not resolve its declared basis");
    }

    const joggle::Type result_type = returned.front().type();
    const auto element = result_type.get<joggle::Type>("element");
    const auto shape = result_type.get<std::vector<std::int64_t>>("shape");
    if (!element || !shape) {
      return fail("the result tensor has no concrete element and shape");
    }
    const auto sink = compiler_.make(*mem_sink_type_, *element, *shape);
    const auto effect = sink ? compiler_.make(*effect_type_, *sink)
                             : std::optional<joggle::Type>{};
    const auto integer_list = compiler_.make(*list_type_, *integer_type_);
    const auto shape_value = integer_list
                                 ? compiler_.known(*integer_list, *shape)
                                 : std::optional<joggle::Val>{};
    auto output = compiler_.create_fn();
    if (!sink || !effect || !shape_value || !output) {
      return fail("mem.realize could not construct output storage types");
    }

    auto edit = output->edit();
    Map values;
    const auto old_arguments = input.arguments();
    values.reserve(old_arguments.size() + input.ops().size());
    for (const auto& argument : old_arguments) {
      const auto replacement = edit.argument(argument.type());
      values.emplace_back(argument, replacement);
    }

    const joggle::Blk entry = output->entry();
    for (std::size_t i = 0; i < old_arguments.size(); ++i) {
      if (!is_tensor(old_arguments[i].type())) {
        continue;
      }
      const auto view = make_view(old_arguments[i].type());
      if (!view) {
        return fail("mem.realize could not construct an input view");
      }
      const auto call =
          emit(edit, entry, *mem_view_, {values[i].second}, {*view});
      if (!call) {
        return std::nullopt;
      }
      values[i].second = call->value();
    }

    const auto allocation = emit(edit, entry, *mem_alloc_, {}, {*sink, *effect},
                                 {{"shape", *shape_value}});
    if (!allocation) {
      return std::nullopt;
    }

    const auto body = [&](joggle::Blk block,
                          const std::vector<joggle::Val>& state,
                          const std::vector<joggle::Val>& coordinates)
        -> std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>> {
      auto scalar =
          lower_tensor(edit, returned.front(), values, block, coordinates);
      if (!scalar) {
        return std::nullopt;
      }
      std::vector<joggle::Val> arguments{allocation->result(0), state.front(),
                                         scalar->value};
      arguments.insert(arguments.end(), coordinates.begin(), coordinates.end());
      const auto store = emit(edit, scalar->tail, *mem_store_,
                              std::move(arguments), {*effect});
      if (!store) {
        return std::nullopt;
      }
      return std::pair{store->parent(),
                       std::vector<joggle::Val>{store->value()}};
    };

    auto loops =
        dimensions(edit, entry, *shape, {allocation->result(1)}, {}, body);
    if (!loops) {
      return std::nullopt;
    }
    const auto sealed =
        emit(edit, loops->first, *mem_seal_,
             {allocation->result(0), loops->second.front()}, {result_type});
    if (!sealed) {
      return std::nullopt;
    }
    edit.ret(sealed->parent(), {sealed->value()});
    if (!edit.commit(compiler_, diagnostics_)) {
      return std::nullopt;
    }
    return output;
  }

private:
  using DimensionBody = std::function<
      std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>>(
          joggle::Blk, const std::vector<joggle::Val>&,
          const std::vector<joggle::Val>&)>;

  std::optional<joggle::Fn> fail(std::string message) {
    diagnostics_.report(std::move(message));
    return std::nullopt;
  }

  bool is_tensor(const joggle::Type& type) const {
    const auto symbol = type.schema().symbol();
    return symbol.mod_name() == "tensor" && symbol.local_name() == "tensor";
  }

  std::optional<joggle::Type> make_view(const joggle::Type& tensor) {
    const auto element = tensor.get<joggle::Type>("element");
    const auto shape = tensor.get<std::vector<std::int64_t>>("shape");
    return element && shape ? compiler_.make(*mem_view_type_, *element, *shape)
                            : std::optional<joggle::Type>{};
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
    for (const auto& argument : arguments) {
      inputs.push_back(argument.type());
    }
    const auto signature = compiler_.make(*callable_type_, inputs, results);
    if (!signature) {
      diagnostics_.report("could not construct a callable type for '" +
                          declaration.symbol().qualified_name() + "'");
      return std::nullopt;
    }
    try {
      const auto callee =
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

  std::optional<joggle::Op>
  index_literal(joggle::Fn::Edit& edit, joggle::Blk block, std::int64_t value) {
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
      diagnostics_.report("a realized loop extent cannot be negative");
      return std::nullopt;
    }
    const auto zero = index_literal(edit, begin, 0);
    const auto bound = index_literal(edit, begin, extent);
    const auto one = index_literal(edit, begin, 1);
    if (!zero || !bound || !one) {
      return std::nullopt;
    }
    std::vector<joggle::Type> state_types;
    for (const auto& value : initial) {
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
    auto next =
        body(iteration, header_arguments.front(), iteration.arguments());
    if (!next || next->second.size() != state_types.size()) {
      diagnostics_.report("realized loop body produced the wrong state");
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
             std::vector<joggle::Val> coordinates, const DimensionBody& body,
             std::size_t dimension = 0) {
    if (dimension == shape.size()) {
      return body(begin, state, coordinates);
    }
    auto nested = loop(
        edit, begin, shape[dimension], std::move(state),
        [&](joggle::Blk block, joggle::Val index,
            const std::vector<joggle::Val>& carried)
            -> std::optional<std::pair<joggle::Blk, std::vector<joggle::Val>>> {
          auto next_coordinates = coordinates;
          next_coordinates.push_back(index);
          return dimensions(edit, block, shape, carried,
                            std::move(next_coordinates), body, dimension + 1U);
        });
    return nested ? std::optional{std::pair{nested->exit, nested->values}}
                  : std::nullopt;
  }

  std::optional<Lowered> callback(joggle::Fn::Edit& edit,
                                  const joggle::Val& callable,
                                  const std::vector<joggle::Val>& arguments,
                                  const Map& outer, joggle::Blk block) {
    const auto body = callable.inline_fn();
    if (!body || body->blks().size() != 1U ||
        body->entry().terminator().kind() != joggle::Term::Kind::Return ||
        body->entry().terminator().returned().size() != 1U) {
      diagnostics_.report("tensor callbacks must be single-block fns");
      return std::nullopt;
    }
    const auto parameters = body->arguments();
    const auto captures = callable.captures();
    if (parameters.size() != arguments.size() + captures.size()) {
      diagnostics_.report("tensor callback argument count is inconsistent");
      return std::nullopt;
    }
    Map local = outer;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
      local.emplace_back(parameters[i], arguments[i]);
    }
    for (std::size_t i = 0; i < captures.size(); ++i) {
      const auto replacement = mapped(outer, captures[i]);
      if (!replacement) {
        diagnostics_.report("a tensor callback capture is not available");
        return std::nullopt;
      }
      local.emplace_back(parameters[arguments.size() + i], *replacement);
    }
    return lower(edit, body->entry().terminator().returned().front(), local,
                 block);
  }

  std::optional<Lowered>
  lower_tensor(joggle::Fn::Edit& edit, const joggle::Val& source,
               const Map& values, joggle::Blk block,
               const std::vector<joggle::Val>& coordinates) {
    if (!is_tensor(source.type())) {
      diagnostics_.report("only a tensor value can be sampled");
      return std::nullopt;
    }
    const auto element = source.type().get<joggle::Type>("element");
    if (!element) {
      diagnostics_.report("a sampled tensor has no element type");
      return std::nullopt;
    }
    if (const auto view = mapped(values, source)) {
      std::vector<joggle::Val> arguments{*view};
      arguments.insert(arguments.end(), coordinates.begin(), coordinates.end());
      const auto loaded =
          emit(edit, block, *mem_index_, std::move(arguments), {*element});
      return loaded ? std::optional{Lowered{loaded->parent(), loaded->value()}}
                    : std::nullopt;
    }
    const auto definition = source.defining_op();
    if (!definition || !definition->callee().referenced_fn()) {
      diagnostics_.report("a tensor value is neither a view nor a direct call");
      return std::nullopt;
    }
    if (is(*definition, tensor_make_)) {
      const auto body = definition->operand("body");
      if (!body) {
        diagnostics_.report("tensor construction has no body");
        return std::nullopt;
      }
      return callback(edit, *body, coordinates, values, block);
    }
    if (is(*definition, tensor_map_)) {
      const auto input = definition->operand("input");
      const auto body = definition->operand("body");
      if (!input || !body) {
        diagnostics_.report("tensor map is incomplete");
        return std::nullopt;
      }
      auto sampled = lower_tensor(edit, *input, values, block, coordinates);
      return sampled ? callback(edit, *body, {sampled->value}, values,
                                sampled->tail)
                     : std::nullopt;
    }
    diagnostics_.report("tensor value must normalize to an input, "
                        "tensor.tensor, or tensor.map");
    return std::nullopt;
  }

  std::optional<Lowered> lower(joggle::Fn::Edit& edit,
                               const joggle::Val& source, Map& values,
                               joggle::Blk block) {
    if (const auto existing = mapped(values, source)) {
      return Lowered{block, *existing};
    }
    const auto definition = source.defining_op();
    if (!definition || !definition->callee().referenced_fn()) {
      diagnostics_.report("a scalar expression has no direct definition");
      return std::nullopt;
    }
    if (is(*definition, tensor_reduce_)) {
      const auto initial = definition->operand("initial");
      const auto body = definition->operand("body");
      const auto extent = definition->callee().binding<std::int64_t>("extent");
      if (!initial || !body || !extent) {
        diagnostics_.report("tensor reduction is incomplete");
        return std::nullopt;
      }
      auto lowered_initial = lower(edit, *initial, values, block);
      if (!lowered_initial) {
        return std::nullopt;
      }
      auto reduced = loop(
          edit, lowered_initial->tail, *extent, {lowered_initial->value},
          [&](joggle::Blk iteration, joggle::Val index,
              const std::vector<joggle::Val>& state)
              -> std::optional<
                  std::pair<joggle::Blk, std::vector<joggle::Val>>> {
            auto next = callback(edit, *body, {state.front(), index}, values,
                                 iteration);
            return next
                       ? std::optional{std::pair{
                             next->tail, std::vector<joggle::Val>{next->value}}}
                       : std::nullopt;
          });
      if (!reduced) {
        return std::nullopt;
      }
      values.emplace_back(source, reduced->values.front());
      return Lowered{reduced->exit, reduced->values.front()};
    }

    std::vector<joggle::Val> arguments;
    joggle::Blk tail = block;
    for (const auto& argument : definition->arguments()) {
      auto value = lower(edit, argument, values, tail);
      if (!value) {
        return std::nullopt;
      }
      tail = value->tail;
      arguments.push_back(value->value);
    }
    std::vector<joggle::Type> results;
    for (const auto& result : definition->results()) {
      results.push_back(result.type());
    }
    std::optional<joggle::Op> emitted;
    if (is(*definition, tensor_index_)) {
      emitted = emit(edit, tail, *mem_index_, std::move(arguments), results);
    } else {
      emitted =
          emit(edit, tail, *definition->callee().referenced_fn(),
               std::move(arguments), results, definition->callee().bindings());
    }
    if (!emitted) {
      return std::nullopt;
    }
    const auto old_results = definition->results();
    const auto new_results = emitted->results();
    for (std::size_t i = 0; i < old_results.size(); ++i) {
      values.emplace_back(old_results[i], new_results[i]);
    }
    const auto replacement = mapped(values, source);
    return replacement ? std::optional{Lowered{emitted->parent(), *replacement}}
                       : std::nullopt;
  }

  joggle::Compiler& compiler_;
  const joggle::Mod& mem_;
  joggle::Diag& diagnostics_;
  std::optional<joggle::Mod::FnDecl> tensor_make_;
  std::optional<joggle::Mod::FnDecl> tensor_map_;
  std::optional<joggle::Mod::FnDecl> tensor_reduce_;
  std::optional<joggle::Mod::FnDecl> tensor_index_;
  std::optional<joggle::Mod::TypeDecl> mem_view_type_;
  std::optional<joggle::Mod::TypeDecl> mem_sink_type_;
  std::optional<joggle::Mod::FnDecl> mem_view_;
  std::optional<joggle::Mod::FnDecl> mem_index_;
  std::optional<joggle::Mod::FnDecl> mem_alloc_;
  std::optional<joggle::Mod::FnDecl> mem_store_;
  std::optional<joggle::Mod::FnDecl> mem_seal_;
  std::optional<joggle::Mod::FnDecl> literal_;
  std::optional<joggle::Mod::FnDecl> add_;
  std::optional<joggle::Mod::FnDecl> less_;
  std::optional<joggle::Mod::TypeDecl> callable_type_;
  std::optional<joggle::Mod::TypeDecl> effect_type_;
  std::optional<joggle::Mod::TypeDecl> list_type_;
  std::optional<joggle::Type> integer_type_;
  std::optional<joggle::Type> index_type_;
  std::optional<joggle::Type> boolean_type_;
};

}  // namespace

void joggle_mod(joggle::Compiler& compiler, const joggle::Mod& mod,
                joggle::Diag&) {
  compiler.bind(
      mod, "realize",
      [mod](joggle::Compiler& active, joggle::Fn input,
            joggle::Diag& diagnostics) -> std::optional<joggle::Fn> {
        return Realizer(active, mod, diagnostics).run(std::move(input));
      });
}
