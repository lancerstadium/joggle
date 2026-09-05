#include "joggle/ir.h"

#include "ir/fn.h"
#include "sema/domain.h"
#include "ir/mod.h"
#include "lang/prelude.h"
#include "sema/infer.h"
#include "ir/type.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace joggle::detail {

struct KnownValStorage {
  Type type;
  ParamVal value;
};

struct StoredVal {
  std::uint64_t id = 0;
  std::shared_ptr<const KnownValStorage> known;
};

struct StoredArgument {
  std::size_t parameter = 0;
  StoredVal value;
};

struct ValData {
  enum class Origin {
    FnArg,
    BlkArg,
    OpResult,
    FnRef,
    InlineFn,
  };

  Type type;
  Origin origin = Origin::FnArg;
  std::uint64_t owner = 0;
  std::size_t index = 0;
  std::optional<Mod::FnDecl> reference;
  std::shared_ptr<Fn> inline_fn;
  std::vector<StoredVal> captures;
  std::vector<StoredArgument> bindings;
};

struct OpData {
  StoredVal callee;
  std::uint64_t parent = 0;
  std::vector<StoredArgument> arguments;
  std::vector<std::uint64_t> results;
  std::optional<Loc> location;
};

struct EdgeData {
  std::uint64_t target = 0;
  std::vector<std::uint64_t> arguments;
};

struct TermData {
  Term::Kind kind = Term::Kind::Return;
  std::optional<std::uint64_t> condition;
  std::vector<std::uint64_t> returned;
  std::vector<EdgeData> successors;
};

struct BlkData {
  std::vector<std::uint64_t> arguments;
  std::vector<std::uint64_t> ops;
  std::optional<TermData> terminator;
};

struct FnState {
  struct Signature {
    std::optional<Mod::FnDecl> declaration;
    std::vector<Type> arguments;
    std::vector<Type> results;
  };

  std::map<std::string, Mod, std::less<>> mods;
  std::unordered_map<std::uint64_t, ValData> values;
  std::unordered_map<std::uint64_t, OpData> ops;
  std::vector<std::uint64_t> arguments;
  std::unordered_map<std::uint64_t, BlkData> blocks;
  std::vector<std::uint64_t> block_order;
  std::uint64_t entry = 0;
  std::optional<Signature> signature;
};

struct FnIdentity {
  std::shared_ptr<FnState> state;
  std::uint64_t next_id = 1;
  bool editing = false;
};

struct FnEditState {
  std::shared_ptr<FnIdentity> fn;
  std::shared_ptr<FnState> backup;
  bool active = true;
};

const std::shared_ptr<FnIdentity>& FnAccess::owner(const Val& value) {
  return value.fn_;
}

const std::shared_ptr<FnIdentity>& FnAccess::owner(const Op& op) {
  return op.fn_;
}

const std::shared_ptr<FnIdentity>& FnAccess::owner(const Blk& block) {
  return block.fn_;
}

const std::shared_ptr<const KnownValStorage>&
FnAccess::known(const Val& value) {
  return value.known_;
}

std::uint64_t FnAccess::id(const Val& value) { return value.id_; }
std::uint64_t FnAccess::id(const Op& op) { return op.id_; }
std::uint64_t FnAccess::id(const Blk& block) { return block.id_; }

Val FnAccess::restore(std::shared_ptr<FnIdentity> fn, std::uint64_t id,
                      std::shared_ptr<const KnownValStorage> known) {
  if (known) {
    Val value(std::move(fn), 0);
    value.fn_.reset();
    value.known_ = std::move(known);
    return value;
  }
  return Val(std::move(fn), id);
}
void FnAccess::locate(Fn::Edit& edit, const Op& op, Loc source) {
  edit.locate(op, std::move(source));
}

std::optional<Loc> FnAccess::location(const Op& op) { return op.location(); }

std::optional<ParamVal> FnAccess::known_value(const Val& value) {
  return value.known_value();
}

std::size_t FnAccess::argument_parameter(const Op& op, std::size_t argument) {
  if (!op.valid() ||
      argument >= op.fn_->state->ops.at(op.id_).arguments.size()) {
    throw std::out_of_range("op argument index is out of range");
  }
  return op.fn_->state->ops.at(op.id_).arguments[argument].parameter;
}

}  // namespace joggle::detail

namespace joggle {

namespace {

using detail::FnIdentity;
using detail::FnState;
using detail::OpData;
using detail::ParamVal;
using detail::ValData;

template <typename Map> bool contains(const Map& map, std::uint64_t id) {
  return map.find(id) != map.end();
}

bool owns(const FnState& fn, const Mod::Symbol& symbol) {
  const auto mod = fn.mods.find(symbol.mod_name());
  return mod != fn.mods.end() && mod->second.version() == symbol.mod_version();
}

bool owns(const FnState& fn, const ParamVal& value);

bool owns(const FnState& fn, const Type& type) {
  if (!owns(fn, type.schema().symbol())) {
    return false;
  }
  const auto parameters = detail::TypeAccess::parameters(type);
  return std::all_of(parameters.begin(), parameters.end(),
                     [&](const ParamVal& value) { return owns(fn, value); });
}

bool owns(const FnState& fn, const ParamVal& value) {
  if (const Type* type = value.as_type()) {
    return owns(fn, *type);
  }
  if (value.kind() == ParamVal::Kind::List) {
    return std::all_of(
        value.elements().begin(), value.elements().end(),
        [&](const ParamVal& element) { return owns(fn, element); });
  }
  return true;
}

std::optional<std::pair<std::vector<Type>, std::vector<Type>>>
callable_signature(const Type& type) {
  const Mod::Symbol schema = type.schema().symbol();
  const auto inputs = type.get<std::vector<Type>>("inputs");
  const auto results = type.get<std::vector<Type>>("results");
  if (schema.mod_name() != detail::prelude_mod_name ||
      schema.local_name() != "callable" || !inputs || !results) {
    return std::nullopt;
  }
  return std::pair{*inputs, *results};
}

std::optional<Type> make_callable(const FnState& fn, std::vector<Type> inputs,
                                  std::vector<Type> results) {
  const auto prelude = fn.mods.find(detail::prelude_mod_name);
  const auto declaration = prelude == fn.mods.end()
                               ? std::optional<Mod::TypeDecl>{}
                               : prelude->second.type("callable");
  if (!declaration) {
    return std::nullopt;
  }
  std::vector<ParamVal> input_values;
  input_values.reserve(inputs.size());
  for (Type& input : inputs) {
    input_values.emplace_back(std::move(input));
  }
  std::vector<ParamVal> result_values;
  result_values.reserve(results.size());
  for (Type& result : results) {
    result_values.emplace_back(std::move(result));
  }
  return detail::TypeAccess::make(*declaration,
                                  {ParamVal::list(std::move(input_values)),
                                   ParamVal::list(std::move(result_values))});
}

bool matches(const Mod::ParamDecl& schema, const ParamVal& value);

std::optional<Type> reflected_parameter_type(const FnState& fn,
                                             const Mod::Expr& expression) {
  const auto domain = detail::kernel_domain(expression);
  if (!domain) {
    return std::nullopt;
  }
  const auto prelude = fn.mods.find(detail::prelude_mod_name);
  if (prelude == fn.mods.end()) {
    return std::nullopt;
  }
  const std::string_view name = domain->list
                                    ? std::string_view{"list"}
                                    : detail::domain_name(domain->element);
  const auto declaration = prelude->second.type(name);
  if (!declaration) {
    return std::nullopt;
  }
  std::vector<ParamVal> parameters;
  if (domain->list) {
    auto element = reflected_parameter_type(fn, expression.arguments.front());
    if (!element) {
      return std::nullopt;
    }
    parameters.emplace_back(*element);
  }
  return detail::TypeAccess::make(*declaration, std::move(parameters));
}

std::shared_ptr<const detail::KnownValStorage>
make_known(const FnState& fn, const Mod::ParamDecl& parameter, ParamVal value) {
  auto type = reflected_parameter_type(fn, parameter.domain);
  if (!type || !matches(parameter, value) || !owns(fn, *type) ||
      !owns(fn, value)) {
    return {};
  }
  return std::make_shared<const detail::KnownValStorage>(
      detail::KnownValStorage{std::move(*type), std::move(value)});
}

bool matches(const Mod::ParamDecl& schema, const ParamVal& value) {
  return detail::matches_parameter(schema, value);
}

std::optional<std::size_t> op_position(const FnState& fn, std::uint64_t op) {
  const auto item = fn.ops.find(op);
  if (item == fn.ops.end()) {
    return std::nullopt;
  }
  const auto owner = fn.blocks.find(item->second.parent);
  if (owner == fn.blocks.end()) {
    return std::nullopt;
  }
  const auto found =
      std::find(owner->second.ops.begin(), owner->second.ops.end(), op);
  if (found == owner->second.ops.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(
      std::distance(owner->second.ops.begin(), found));
}

using BlkSet = std::unordered_set<std::uint64_t>;

std::unordered_map<std::uint64_t, BlkSet> dominators(const FnState& fn) {
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> predecessors;
  for (const std::uint64_t block_id : fn.block_order) {
    predecessors.try_emplace(block_id);
    const auto& block = fn.blocks.at(block_id);
    if (!block.terminator) {
      continue;
    }
    for (const auto& edge : block.terminator->successors) {
      predecessors[edge.target].push_back(block_id);
    }
  }

  const BlkSet all(fn.block_order.begin(), fn.block_order.end());
  std::unordered_map<std::uint64_t, BlkSet> result;
  for (const std::uint64_t block : fn.block_order) {
    result.emplace(block, block == fn.entry ? BlkSet{block} : all);
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (const std::uint64_t block : fn.block_order) {
      if (block == fn.entry || predecessors[block].empty()) {
        continue;
      }
      BlkSet next = result.at(predecessors[block].front());
      for (std::size_t index = 1; index < predecessors[block].size(); ++index) {
        const BlkSet& other = result.at(predecessors[block][index]);
        for (auto item = next.begin(); item != next.end();) {
          item = other.contains(*item) ? std::next(item) : next.erase(item);
        }
      }
      next.insert(block);
      if (next != result.at(block)) {
        result.at(block) = std::move(next);
        changed = true;
      }
    }
  }
  return result;
}

bool definition_dominates(
    const FnState& fn, const ValData& definition, std::uint64_t user_block,
    std::optional<std::uint64_t> user_op,
    const std::unordered_map<std::uint64_t, BlkSet>& dom) {
  if (definition.origin == ValData::Origin::FnArg) {
    return definition.owner == 0 && definition.index < fn.arguments.size();
  }
  if (definition.origin == ValData::Origin::FnRef) {
    return definition.reference.has_value();
  }
  if (definition.origin == ValData::Origin::InlineFn) {
    if (!definition.inline_fn) {
      return false;
    }
    return std::all_of(definition.captures.begin(), definition.captures.end(),
                       [&](const detail::StoredVal& capture) {
                         if (capture.known) {
                           return owns(fn, capture.known->type) &&
                                  owns(fn, capture.known->value);
                         }
                         const auto value = fn.values.find(capture.id);
                         return value != fn.values.end() &&
                                definition_dominates(fn, value->second,
                                                     user_block, user_op, dom);
                       });
  }
  if (definition.origin == ValData::Origin::BlkArg) {
    const auto owner = fn.blocks.find(definition.owner);
    return owner != fn.blocks.end() &&
           definition.index < owner->second.arguments.size() &&
           dom.at(user_block).contains(definition.owner);
  }
  const auto producer = fn.ops.find(definition.owner);
  if (producer == fn.ops.end()) {
    return false;
  }
  if (producer->second.parent != user_block) {
    return dom.at(user_block).contains(producer->second.parent);
  }
  if (!user_op) {
    return op_position(fn, definition.owner).has_value();
  }
  const auto producer_position = op_position(fn, definition.owner);
  const auto user_position = op_position(fn, *user_op);
  return producer_position && user_position &&
         *producer_position < *user_position;
}

bool stored_uses(const FnState& fn, const detail::StoredVal& candidate,
                 std::uint64_t target,
                 std::unordered_set<std::uint64_t>& visiting) {
  if (candidate.known || candidate.id == 0) {
    return false;
  }
  if (candidate.id == target) {
    return true;
  }
  const auto value = fn.values.find(candidate.id);
  if (value == fn.values.end() ||
      value->second.origin != ValData::Origin::InlineFn ||
      !visiting.insert(candidate.id).second) {
    return false;
  }
  const bool found =
      std::any_of(value->second.captures.begin(), value->second.captures.end(),
                  [&](const detail::StoredVal& capture) {
                    return stored_uses(fn, capture, target, visiting);
                  });
  visiting.erase(candidate.id);
  return found;
}

bool stored_uses(const FnState& fn, const detail::StoredVal& candidate,
                 std::uint64_t target) {
  std::unordered_set<std::uint64_t> visiting;
  return stored_uses(fn, candidate, target, visiting);
}

bool value_uses(const FnState& fn, std::uint64_t candidate,
                std::uint64_t target) {
  return stored_uses(fn, detail::StoredVal{candidate, {}}, target);
}

bool matches_fn_reference(const FnState& fn, const ValData& value) {
  if (value.origin != ValData::Origin::FnRef || !value.reference ||
      !owns(fn, value.reference->symbol()) || !owns(fn, value.type)) {
    return false;
  }
  const Mod::Symbol type = value.type.schema().symbol();
  const auto inputs = value.type.get<std::vector<Type>>("inputs");
  const auto results = value.type.get<std::vector<Type>>("results");
  if (type.mod_name() != detail::prelude_mod_name ||
      type.local_name() != "callable" || !inputs || !results ||
      !detail::compiler_results(*value.reference).empty()) {
    return false;
  }

  const auto parameters = value.reference->inputs();
  std::vector<std::optional<ParamVal>> known_arguments;
  known_arguments.reserve(detail::compiler_inputs(*value.reference).size());
  std::vector<std::size_t> known_indices(parameters.size());
  std::size_t known_count = 0;
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!detail::is_value_port(parameters[index])) {
      known_indices[index] = known_count++;
      known_arguments.emplace_back();
    }
  }
  std::vector<bool> supplied(parameters.size(), false);
  for (const detail::StoredArgument& binding : value.bindings) {
    if (binding.parameter >= parameters.size() ||
        detail::is_value_port(parameters[binding.parameter]) ||
        supplied[binding.parameter] || !binding.value.known ||
        !owns(fn, binding.value.known->type) ||
        !owns(fn, binding.value.known->value) ||
        !matches(parameters[binding.parameter], binding.value.known->value)) {
      return false;
    }
    supplied[binding.parameter] = true;
    known_arguments[known_indices[binding.parameter]] =
        binding.value.known->value;
  }
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!detail::is_value_port(parameters[index]) && !supplied[index]) {
      return false;
    }
  }

  std::vector<Mod> mods;
  mods.reserve(fn.mods.size());
  for (const auto& [name, mod] : fn.mods) {
    static_cast<void>(name);
    mods.push_back(mod);
  }
  std::vector<std::optional<Type>> expected;
  expected.reserve(results->size());
  for (const Type& result : *results) {
    expected.emplace_back(result);
  }
  Diag diagnostics;
  const auto resolved = detail::resolve_call_types(
      mods, *value.reference, *inputs, known_arguments, expected, diagnostics);
  // Some result types call ordinary compiler fns and therefore need the
  // linked Compiler evaluator. The Fn-local verifier still proves ownership,
  // binding shape, and the concrete callable contract; Compiler::verify
  // performs the stronger declaration check when evaluation is available.
  if (resolved) {
    return resolved->results == *results;
  }
  if (!value.reference->generics().empty()) {
    return true;
  }
  const auto needs_compiler = [&](const auto& self,
                                  const Mod::Expr& expression) -> bool {
    using Kind = Mod::Expr::Kind;
    if (expression.kind == Kind::Call || expression.kind == Kind::Evaluate ||
        expression.kind == Kind::If || expression.kind == Kind::Prefix ||
        expression.kind == Kind::Infix || expression.kind == Kind::Postfix) {
      return true;
    }
    return std::any_of(expression.arguments.begin(), expression.arguments.end(),
                       [&](const Mod::Expr& item) { return self(self, item); });
  };
  return std::any_of(value.reference->inputs().begin(),
                     value.reference->inputs().end(),
                     [&](const auto& input) {
                       return needs_compiler(needs_compiler, input.domain);
                     }) ||
         std::any_of(value.reference->results().begin(),
                     value.reference->results().end(), [&](const auto& result) {
                       return needs_compiler(needs_compiler, result.domain);
                     });
}

bool matches_inline_fn(const FnState& owner, const ValData& value) {
  if (value.origin != ValData::Origin::InlineFn || !value.inline_fn ||
      !owns(owner, value.type)) {
    return false;
  }
  const Mod::Symbol schema = value.type.schema().symbol();
  const auto inputs = value.type.get<std::vector<Type>>("inputs");
  const auto results = value.type.get<std::vector<Type>>("results");
  if (schema.mod_name() != detail::prelude_mod_name ||
      schema.local_name() != "callable" || !inputs || !results) {
    return false;
  }
  const auto arguments = value.inline_fn->arguments();
  if (arguments.size() != inputs->size() + value.captures.size() ||
      value.inline_fn->result_types() != *results) {
    return false;
  }
  for (std::size_t index = 0; index < inputs->size(); ++index) {
    if (arguments[index].type() != (*inputs)[index] ||
        !owns(owner, arguments[index].type())) {
      return false;
    }
  }
  for (std::size_t index = 0; index < value.captures.size(); ++index) {
    const detail::StoredVal& capture = value.captures[index];
    const Type* capture_type = nullptr;
    if (capture.known) {
      if (!owns(owner, capture.known->type) ||
          !owns(owner, capture.known->value)) {
        return false;
      }
      capture_type = &capture.known->type;
    } else {
      const auto found = owner.values.find(capture.id);
      if (found == owner.values.end()) {
        return false;
      }
      capture_type = &found->second.type;
    }
    if (detail::is_effect_type(*capture_type) ||
        arguments[inputs->size() + index].type() != *capture_type) {
      return false;
    }
  }
  for (const Type& result : *results) {
    if (!owns(owner, result)) {
      return false;
    }
  }
  for (const Op& op : value.inline_fn->ops()) {
    const Val callee = op.callee();
    if (!owns(owner, callee.type())) {
      return false;
    }
    for (const Val& argument : op.arguments()) {
      if (!owns(owner, argument.type())) {
        return false;
      }
    }
    for (const Val& result : op.results()) {
      if (!owns(owner, result.type())) {
        return false;
      }
    }
  }
  Diag diagnostics;
  return detail::FnAccess::verify_structure(*value.inline_fn, diagnostics);
}

bool verify_op(const FnState& fn, std::uint64_t id, const OpData& op,
               const std::unordered_map<std::uint64_t, BlkSet>& dom,
               Diag& diagnostics) {
  bool valid = true;
  const auto callee =
      op.callee.known ? fn.values.end() : fn.values.find(op.callee.id);
  const std::string name =
      callee != fn.values.end() && callee->second.reference
          ? std::string(callee->second.reference->symbol().qualified_name())
          : std::string("call");
  if (op.callee.known || callee == fn.values.end()) {
    diagnostics.report("op '" + name + "' has an invalid callee");
    valid = false;
  } else if (contains(fn.blocks, op.parent) &&
             !definition_dominates(fn, callee->second, op.parent, id, dom)) {
    diagnostics.report("callee of op '" + name +
                       "' is not dominated by its definition");
    valid = false;
  }
  if (!contains(fn.blocks, op.parent)) {
    diagnostics.report("op '" + name + "' has no parent block");
    valid = false;
  }
  const auto input_types =
      callee == fn.values.end()
          ? std::optional<std::vector<Type>>{}
          : callee->second.type.get<std::vector<Type>>("inputs");
  const auto result_types =
      callee == fn.values.end()
          ? std::optional<std::vector<Type>>{}
          : callee->second.type.get<std::vector<Type>>("results");
  if (!input_types || !result_types ||
      op.arguments.size() != input_types->size() ||
      op.results.size() != result_types->size()) {
    diagnostics.report("op '" + name + "' does not match its callable type");
    valid = false;
  }
  for (std::size_t index = 0; index < op.arguments.size(); ++index) {
    const detail::StoredVal& argument = op.arguments[index].value;
    const auto value =
        argument.known ? fn.values.end() : fn.values.find(argument.id);
    if (argument.known) {
      if (!owns(fn, argument.known->type) || !owns(fn, argument.known->value) ||
          detail::is_effect_type(argument.known->type) ||
          (input_types && index < input_types->size() &&
           argument.known->type != (*input_types)[index])) {
        diagnostics.report("argument " + std::to_string(index) + " of op '" +
                           name + "' has an invalid Known value");
        valid = false;
      }
    } else if (value == fn.values.end()) {
      diagnostics.report("argument " + std::to_string(index) + " of op '" +
                         name + "' is invalid");
      valid = false;
    } else {
      if (input_types && index < input_types->size() &&
          value->second.type != (*input_types)[index]) {
        diagnostics.report("argument " + std::to_string(index) + " of op '" +
                           name + "' has the wrong type");
        valid = false;
      }
      if (contains(fn.blocks, op.parent) &&
          !definition_dominates(fn, value->second, op.parent, id, dom)) {
        diagnostics.report("argument " + std::to_string(index) + " of op '" +
                           name + "' is not dominated by its definition");
        valid = false;
      }
    }
  }
  for (std::uint64_t result : op.results) {
    const auto value = fn.values.find(result);
    if (value == fn.values.end() ||
        value->second.origin != ValData::Origin::OpResult ||
        value->second.owner != id || !owns(fn, value->second.type)) {
      diagnostics.report("op '" + name + "' has an invalid result");
      valid = false;
    }
  }
  if (result_types) {
    for (std::size_t index = 0;
         index < op.results.size() && index < result_types->size(); ++index) {
      const auto value = fn.values.find(op.results[index]);
      if (value != fn.values.end() &&
          value->second.type != (*result_types)[index]) {
        diagnostics.report("result " + std::to_string(index) + " of op '" +
                           name + "' has the wrong type");
        valid = false;
      }
    }
  }
  return valid;
}

bool verify_effect_uses(const FnState& fn, Diag& diagnostics) {
  bool valid = true;
  std::unordered_map<std::uint64_t, std::size_t> uses;
  const auto effect = [&](std::uint64_t id) {
    const auto value = fn.values.find(id);
    return value != fn.values.end() &&
           detail::is_effect_type(value->second.type);
  };
  const auto consume = [&](std::uint64_t id) {
    if (effect(id)) {
      ++uses[id];
    }
  };

  for (const auto& [id, op] : fn.ops) {
    static_cast<void>(id);
    for (const detail::StoredArgument& argument : op.arguments) {
      if (!argument.value.known) {
        consume(argument.value.id);
      }
    }
  }
  for (const auto& [id, block] : fn.blocks) {
    static_cast<void>(id);
    if (!block.terminator) {
      continue;
    }
    const detail::TermData& terminator = *block.terminator;
    for (const std::uint64_t returned : terminator.returned) {
      consume(returned);
    }
    if (terminator.kind != Term::Kind::Branch) {
      for (const detail::EdgeData& edge : terminator.successors) {
        for (const std::uint64_t argument : edge.arguments) {
          consume(argument);
        }
      }
      continue;
    }

    std::unordered_set<std::uint64_t> branch_uses;
    for (const detail::EdgeData& edge : terminator.successors) {
      std::unordered_set<std::uint64_t> edge_uses;
      for (const std::uint64_t argument : edge.arguments) {
        if (!effect(argument)) {
          continue;
        }
        if (!edge_uses.insert(argument).second) {
          diagnostics.report("one branch path repeats the same effect token");
          valid = false;
        }
        branch_uses.insert(argument);
      }
    }
    for (const std::uint64_t argument : branch_uses) {
      ++uses[argument];
    }
  }
  for (const auto& [id, count] : uses) {
    static_cast<void>(id);
    if (count > 1U) {
      diagnostics.report("effect token has more than one consuming use");
      valid = false;
    }
  }
  return valid;
}

bool verify_fn(const FnState& fn, Diag& diagnostics) {
  bool valid = true;
  if (fn.block_order.empty() || fn.block_order.front() != fn.entry ||
      !contains(fn.blocks, fn.entry)) {
    diagnostics.report("fn has no valid entry block");
    return false;
  }
  std::unordered_set<std::uint64_t> listed_ops;
  std::unordered_set<std::uint64_t> listed_blocks;
  for (const auto& [id, value] : fn.values) {
    static_cast<void>(id);
    if (!owns(fn, value.type)) {
      diagnostics.report("fn contains a value with an invalid type");
      valid = false;
    }
    if (value.origin == ValData::Origin::FnRef) {
      if (!matches_fn_reference(fn, value)) {
        diagnostics.report("fn contains an invalid fn reference");
        valid = false;
      }
    } else if (value.origin == ValData::Origin::InlineFn) {
      if (!matches_inline_fn(fn, value)) {
        diagnostics.report("fn contains an invalid inline fn");
        valid = false;
      }
    } else if (value.reference || value.inline_fn || !value.captures.empty() ||
               !value.bindings.empty()) {
      diagnostics.report("non-callable value contains a callable payload");
      valid = false;
    }
  }
  for (std::size_t index = 0; index < fn.arguments.size(); ++index) {
    const auto value = fn.values.find(fn.arguments[index]);
    if (value == fn.values.end() ||
        value->second.origin != ValData::Origin::FnArg ||
        value->second.owner != 0 || value->second.index != index ||
        !owns(fn, value->second.type)) {
      diagnostics.report("fn has an invalid argument");
      valid = false;
    }
  }
  for (const std::uint64_t block_id : fn.block_order) {
    const auto block = fn.blocks.find(block_id);
    if (block == fn.blocks.end() || !listed_blocks.insert(block_id).second) {
      diagnostics.report("fn has an invalid block order");
      valid = false;
      continue;
    }
    for (std::size_t index = 0; index < block->second.arguments.size();
         ++index) {
      const auto value = fn.values.find(block->second.arguments[index]);
      if (value == fn.values.end() ||
          value->second.origin != ValData::Origin::BlkArg ||
          value->second.owner != block_id || value->second.index != index ||
          !owns(fn, value->second.type)) {
        diagnostics.report("block has an invalid argument");
        valid = false;
      }
    }
    if (!block->second.terminator) {
      diagnostics.report("block has no terminator");
      valid = false;
    }
    for (const std::uint64_t id : block->second.ops) {
      const auto op = fn.ops.find(id);
      if (op == fn.ops.end() || op->second.parent != block_id ||
          !listed_ops.insert(id).second) {
        diagnostics.report("block has an invalid op order");
        valid = false;
      }
    }
  }
  for (const auto& [id, block] : fn.blocks) {
    static_cast<void>(block);
    if (!listed_blocks.contains(id)) {
      diagnostics.report("fn contains an unordered block");
      valid = false;
    }
  }
  const auto dom = dominators(fn);
  for (const auto& [id, op] : fn.ops) {
    if (!listed_ops.contains(id)) {
      diagnostics.report("fn contains an unordered op");
      valid = false;
    }
    valid = verify_op(fn, id, op, dom, diagnostics) && valid;
  }

  if (fn.signature) {
    if (fn.signature->declaration &&
        !owns(fn, fn.signature->declaration->symbol())) {
      diagnostics.report("fn declaration is outside its mod closure");
      valid = false;
    }
    if (fn.arguments.size() != fn.signature->arguments.size()) {
      diagnostics.report("fn argument count does not match its signature");
      valid = false;
    }
    const std::size_t count =
        std::min(fn.arguments.size(), fn.signature->arguments.size());
    for (std::size_t index = 0; index < count; ++index) {
      const auto value = fn.values.find(fn.arguments[index]);
      if (value != fn.values.end() &&
          value->second.type != fn.signature->arguments[index]) {
        diagnostics.report("fn argument type does not match its signature");
        valid = false;
      }
    }
  }

  std::optional<std::vector<Type>> inferred_results;
  std::unordered_set<std::uint64_t> reachable{fn.entry};
  std::vector<std::uint64_t> pending{fn.entry};
  while (!pending.empty()) {
    const std::uint64_t block_id = pending.back();
    pending.pop_back();
    const auto block = fn.blocks.find(block_id);
    if (block == fn.blocks.end() || !block->second.terminator) {
      continue;
    }
    const detail::TermData& terminator = *block->second.terminator;
    const std::size_t expected_successors =
        terminator.kind == Term::Kind::Return
            ? 0U
            : (terminator.kind == Term::Kind::Jump ? 1U : 2U);
    if (terminator.successors.size() != expected_successors ||
        (terminator.kind == Term::Kind::Branch) !=
            terminator.condition.has_value()) {
      diagnostics.report("block has a malformed terminator");
      valid = false;
    }
    const auto verify_use = [&](std::uint64_t id) {
      const auto value = fn.values.find(id);
      if (value == fn.values.end() ||
          !definition_dominates(fn, value->second, block_id, std::nullopt,
                                dom)) {
        diagnostics.report("terminator uses a value that does not dominate it");
        valid = false;
      }
    };
    if (terminator.condition) {
      verify_use(*terminator.condition);
      const auto condition = fn.values.find(*terminator.condition);
      if (condition != fn.values.end()) {
        const Mod::Symbol symbol = condition->second.type.schema().symbol();
        if (symbol.mod_name() != detail::prelude_mod_name ||
            symbol.local_name() != "i1") {
          diagnostics.report("branch condition must have type i1");
          valid = false;
        }
      }
    }
    for (const std::uint64_t value : terminator.returned) {
      verify_use(value);
    }
    if (terminator.kind == Term::Kind::Return) {
      std::vector<Type> returned_types;
      returned_types.reserve(terminator.returned.size());
      for (const std::uint64_t value : terminator.returned) {
        const auto found = fn.values.find(value);
        if (found != fn.values.end()) {
          returned_types.push_back(found->second.type);
        }
      }
      const std::vector<Type>* expected = nullptr;
      if (fn.signature) {
        expected = &fn.signature->results;
      } else if (!inferred_results) {
        inferred_results = returned_types;
        expected = &*inferred_results;
      } else {
        expected = &*inferred_results;
      }
      if (returned_types != *expected) {
        diagnostics.report("fn return types do not match its signature");
        valid = false;
      }
    }
    for (const detail::EdgeData& edge : terminator.successors) {
      const auto target = fn.blocks.find(edge.target);
      if (target == fn.blocks.end()) {
        diagnostics.report("terminator has an invalid successor");
        valid = false;
        continue;
      }
      if (edge.arguments.size() != target->second.arguments.size()) {
        diagnostics.report("successor edge has the wrong number of arguments");
        valid = false;
      }
      const std::size_t count =
          std::min(edge.arguments.size(), target->second.arguments.size());
      for (std::size_t index = 0; index < count; ++index) {
        verify_use(edge.arguments[index]);
        const auto argument = fn.values.find(edge.arguments[index]);
        const auto parameter = fn.values.find(target->second.arguments[index]);
        if (argument != fn.values.end() && parameter != fn.values.end() &&
            argument->second.type != parameter->second.type) {
          diagnostics.report("successor edge argument has the wrong type");
          valid = false;
        }
      }
      if (reachable.insert(edge.target).second) {
        pending.push_back(edge.target);
      }
    }
  }
  for (const std::uint64_t block : fn.block_order) {
    if (!reachable.contains(block)) {
      diagnostics.report("fn contains an unreachable block");
      valid = false;
    }
  }
  valid = verify_effect_uses(fn, diagnostics) && valid;
  return valid;
}

template <typename Resolve>
bool verify_op_contracts(const FnState& fn, Diag& diagnostics,
                         Resolve&& resolve) {
  bool valid = true;
  for (const std::uint64_t block_id : fn.block_order) {
    for (const std::uint64_t op_id : fn.blocks.at(block_id).ops) {
      const OpData& op = fn.ops.at(op_id);
      if (op.callee.known) {
        continue;
      }
      const auto callee = fn.values.find(op.callee.id);
      if (callee == fn.values.end() || !callee->second.reference) {
        continue;
      }
      const Mod::FnDecl schema = *callee->second.reference;

      std::vector<Type> arguments;
      arguments.reserve(op.arguments.size());
      for (const detail::StoredArgument& stored : op.arguments) {
        if (stored.value.known) {
          arguments.push_back(stored.value.known->type);
        } else {
          const auto value = fn.values.find(stored.value.id);
          if (value != fn.values.end()) {
            arguments.push_back(value->second.type);
          }
        }
      }
      std::vector<std::optional<ParamVal>> known_arguments;
      known_arguments.reserve(detail::compiler_inputs(schema).size());
      for (std::size_t index = 0; index < schema.inputs().size(); ++index) {
        if (!detail::is_value_port(schema.inputs()[index])) {
          known_arguments.emplace_back();
        }
      }
      std::size_t known_index = 0;
      std::vector<std::size_t> known_indices(schema.inputs().size());
      for (std::size_t index = 0; index < schema.inputs().size(); ++index) {
        if (!detail::is_value_port(schema.inputs()[index])) {
          known_indices[index] = known_index++;
        }
      }
      for (const detail::StoredArgument& stored : callee->second.bindings) {
        if (stored.parameter >= schema.inputs().size() || !stored.value.known) {
          continue;
        }
        if (!detail::is_value_port(schema.inputs()[stored.parameter])) {
          known_arguments[known_indices[stored.parameter]] =
              stored.value.known->value;
        }
      }

      std::vector<std::optional<Type>> results;
      results.reserve(op.results.size());
      for (const std::uint64_t result : op.results) {
        results.push_back(fn.values.at(result).type);
      }

      auto resolved = resolve(schema, arguments, known_arguments, results,
                              diagnostics, op.location);
      if (!resolved) {
        diagnostics.report("while verifying call '" +
                               schema.symbol().qualified_name() + "'",
                           op.location);
        valid = false;
      }
    }
  }
  return valid;
}

bool verify_op_contracts(const FnState& fn, Diag& diagnostics) {
  std::vector<Mod> mods;
  mods.reserve(fn.mods.size());
  for (const auto& [name, mod] : fn.mods) {
    static_cast<void>(name);
    mods.push_back(mod);
  }
  return verify_op_contracts(
      fn, diagnostics,
      [&](const Mod::FnDecl& schema, std::span<const Type> arguments,
          std::span<const std::optional<ParamVal>> known_arguments,
          std::span<const std::optional<Type>> results, Diag& reported,
          std::optional<Loc> location) {
        return resolve_call_types(mods, schema, arguments, known_arguments,
                                  results, reported, std::move(location));
      });
}

bool verify_op_contracts(const FnState& fn, Compiler& compiler,
                         Diag& diagnostics) {
  return verify_op_contracts(
      fn, diagnostics,
      [&](const Mod::FnDecl& schema, std::span<const Type> arguments,
          std::span<const std::optional<ParamVal>> known_arguments,
          std::span<const std::optional<Type>> results, Diag& reported,
          std::optional<Loc> location) {
        return resolve_call_types(compiler, schema, arguments, known_arguments,
                                  results, reported, std::move(location));
      });
}

template <typename Handle>
void check_same_fn(const std::shared_ptr<FnIdentity>& fn, const Handle& handle,
                   std::string_view kind) {
  if (detail::FnAccess::owner(handle) != fn || !handle.valid()) {
    throw std::invalid_argument(std::string(kind) +
                                " does not belong to this fn edit");
  }
}

void check_same_fn(const std::shared_ptr<FnIdentity>& fn, const Val& value,
                   std::string_view kind) {
  const auto& known = detail::FnAccess::known(value);
  if (known) {
    if (!owns(*fn->state, known->type) || !owns(*fn->state, known->value)) {
      throw std::invalid_argument(std::string(kind) +
                                  " is outside this fn's mod closure");
    }
    return;
  }
  if (detail::FnAccess::owner(value) != fn || !value.valid()) {
    throw std::invalid_argument(std::string(kind) +
                                " does not belong to this fn edit");
  }
}

}  // namespace

}  // namespace joggle

namespace joggle::detail {

bool FnAccess::verify_structure(const Fn& fn, Diag& diagnostics) {
  return verify_fn(*fn.fn_->state, diagnostics);
}

bool FnAccess::verify_contracts(const Fn& fn, Diag& diagnostics) {
  return verify_op_contracts(*fn.fn_->state, diagnostics);
}

bool FnAccess::verify_contracts(const Fn& fn, Compiler& compiler,
                                Diag& diagnostics) {
  return verify_op_contracts(*fn.fn_->state, compiler, diagnostics);
}

void FnAccess::declare(Fn& fn, Mod::FnDecl declaration,
                       std::vector<Type> argument_types,
                       std::vector<Type> result_types) {
  auto& identity = fn.fn_;
  auto& state = *identity->state;
  if (identity->editing || state.signature) {
    throw std::logic_error("fn signature is already fixed");
  }
  if (!state.arguments.empty() || state.blocks.size() != 1U ||
      !state.ops.empty()) {
    throw std::logic_error("fn signature must be fixed before its body");
  }
  if (!owns(state, declaration.symbol()) ||
      value_inputs(declaration).size() != argument_types.size() ||
      value_results(declaration).size() != result_types.size()) {
    throw std::invalid_argument("fn signature does not match its declaration");
  }
  const bool owned_arguments =
      std::all_of(argument_types.begin(), argument_types.end(),
                  [&](const Type& type) { return owns(state, type); });
  const bool owned_results =
      std::all_of(result_types.begin(), result_types.end(),
                  [&](const Type& type) { return owns(state, type); });
  if (!owned_arguments || !owned_results) {
    throw std::invalid_argument(
        "fn signature references a type outside its mod closure");
  }
  state.signature =
      FnState::Signature{std::move(declaration), std::move(argument_types),
                         std::move(result_types)};
}

void FnAccess::define(Fn& fn, std::vector<Type> argument_types,
                      std::vector<Type> result_types) {
  auto& identity = fn.fn_;
  auto& state = *identity->state;
  if (identity->editing || state.signature) {
    throw std::logic_error("fn signature is already fixed");
  }
  if (!state.arguments.empty() || state.blocks.size() != 1U ||
      !state.ops.empty()) {
    throw std::logic_error("fn signature must be fixed before its body");
  }
  const bool owned_arguments =
      std::all_of(argument_types.begin(), argument_types.end(),
                  [&](const Type& type) { return owns(state, type); });
  const bool owned_results =
      std::all_of(result_types.begin(), result_types.end(),
                  [&](const Type& type) { return owns(state, type); });
  if (!owned_arguments || !owned_results) {
    throw std::invalid_argument(
        "fn signature references a type outside its mod closure");
  }
  state.signature = FnState::Signature{std::nullopt, std::move(argument_types),
                                       std::move(result_types)};
}

bool FnAccess::attach(Fn& fn, Mod::FnDecl declaration, Mod owner,
                      Diag& diagnostics) {
  if (!fn.fn_ || fn.fn_->editing) {
    throw std::logic_error(
        "cannot attach a moved-from fn or one with an active edit");
  }
  Fn candidate = fn;
  auto state = std::make_shared<FnState>(*candidate.fn_->state);
  state->mods.insert_or_assign(std::string(owner.name()), std::move(owner));

  std::vector<Type> arguments;
  arguments.reserve(state->arguments.size());
  for (const std::uint64_t argument : state->arguments) {
    const auto found = state->values.find(argument);
    if (found == state->values.end()) {
      diagnostics.report("cannot attach a malformed Fn to a Mod");
      return false;
    }
    arguments.push_back(found->second.type);
  }
  std::vector<Type> results = fn.result_types();
  state->signature = FnState::Signature{
      std::move(declaration), std::move(arguments), std::move(results)};
  candidate.fn_->state = std::move(state);
  if (!verify_fn(*candidate.fn_->state, diagnostics)) {
    return false;
  }
  fn = std::move(candidate);
  return true;
}

bool FnAccess::commit(Fn::Edit& edit, Compiler& compiler, Diag& diagnostics) {
  if (!edit.state_ || !edit.state_->active) {
    throw std::logic_error("fn edit is no longer active");
  }
  if (!verify_fn(*edit.state_->fn->state, diagnostics) ||
      !verify_op_contracts(*edit.state_->fn->state, compiler, diagnostics)) {
    edit.state_->fn->state = std::move(edit.state_->backup);
    edit.state_->fn->editing = false;
    edit.state_->active = false;
    return false;
  }
  edit.state_->active = false;
  edit.state_->backup.reset();
  edit.state_->fn->editing = false;
  return true;
}

}  // namespace joggle::detail

namespace joggle {

Fn::Revision::Revision(std::shared_ptr<const FnState> state)
    : state_(std::move(state)) {}

Val::Val(std::shared_ptr<FnIdentity> fn, std::uint64_t id)
    : fn_(std::move(fn)), id_(id) {}

Val::Val(Type type, ParamVal value)
    : known_(std::make_shared<const detail::KnownValStorage>(
          detail::KnownValStorage{std::move(type), std::move(value)})) {}

bool Val::valid() const {
  return known_ || (fn_ && contains(fn_->state->values, id_));
}

bool Val::known() const { return static_cast<bool>(known_); }

Type Val::type() const {
  if (known_) {
    return known_->type;
  }
  const auto found = fn_->state->values.find(id_);
  if (found == fn_->state->values.end()) {
    throw std::logic_error("value is no longer valid");
  }
  return found->second.type;
}

std::optional<ParamVal> Val::known_value() const {
  return known_ ? std::optional<ParamVal>{known_->value} : std::nullopt;
}

bool Val::operator==(const Val& other) const {
  if (known_ || other.known_) {
    return known_ && other.known_ && known_->type == other.known_->type &&
           known_->value == other.known_->value;
  }
  return fn_ == other.fn_ && id_ == other.id_;
}

bool Val::is_fn_arg() const {
  if (!fn_) {
    return false;
  }
  const auto found = fn_->state->values.find(id_);
  return found != fn_->state->values.end() &&
         found->second.origin == ValData::Origin::FnArg;
}

bool Val::is_blk_arg() const {
  if (!fn_) {
    return false;
  }
  const auto found = fn_->state->values.find(id_);
  return found != fn_->state->values.end() &&
         found->second.origin == ValData::Origin::BlkArg;
}

std::optional<Op> Val::defining_op() const {
  if (!fn_) {
    return std::nullopt;
  }
  const auto found = fn_->state->values.find(id_);
  if (found == fn_->state->values.end() ||
      found->second.origin != ValData::Origin::OpResult) {
    return std::nullopt;
  }
  return Op(fn_, found->second.owner);
}

std::vector<Op> Val::users() const {
  if (!valid() || known()) {
    return {};
  }
  std::vector<Op> result;
  for (const std::uint64_t block : fn_->state->block_order) {
    for (const std::uint64_t op_id : fn_->state->blocks.at(block).ops) {
      const auto& op = fn_->state->ops.at(op_id);
      const bool consumes =
          stored_uses(*fn_->state, op.callee, id_) ||
          std::any_of(op.arguments.begin(), op.arguments.end(),
                      [&](const detail::StoredArgument& argument) {
                        return stored_uses(*fn_->state, argument.value, id_);
                      });
      if (consumes) {
        result.push_back(Op(fn_, op_id));
      }
    }
  }
  return result;
}

std::optional<Mod::FnDecl> Val::referenced_fn() const {
  if (!fn_) {
    return std::nullopt;
  }
  const auto found = fn_->state->values.find(id_);
  return found != fn_->state->values.end() &&
                 found->second.origin == ValData::Origin::FnRef
             ? found->second.reference
             : std::nullopt;
}

std::optional<Fn> Val::inline_fn() const {
  if (!fn_) {
    return std::nullopt;
  }
  const auto found = fn_->state->values.find(id_);
  return found != fn_->state->values.end() &&
                 found->second.origin == ValData::Origin::InlineFn &&
                 found->second.inline_fn
             ? std::optional<Fn>{*found->second.inline_fn}
             : std::nullopt;
}

std::vector<Val> Val::captures() const {
  if (!fn_) {
    return {};
  }
  const auto found = fn_->state->values.find(id_);
  if (found == fn_->state->values.end() ||
      found->second.origin != ValData::Origin::InlineFn) {
    return {};
  }
  std::vector<Val> result;
  result.reserve(found->second.captures.size());
  for (const detail::StoredVal& capture : found->second.captures) {
    result.push_back(detail::FnAccess::restore(fn_, capture.id, capture.known));
  }
  return result;
}

std::vector<std::pair<std::string, Val>> Val::bindings() const {
  if (!fn_) {
    return {};
  }
  const auto found = fn_->state->values.find(id_);
  if (found == fn_->state->values.end() || !found->second.reference) {
    return {};
  }
  const auto parameters = found->second.reference->inputs();
  std::vector<std::pair<std::string, Val>> result;
  result.reserve(found->second.bindings.size());
  for (const detail::StoredArgument& binding : found->second.bindings) {
    if (binding.parameter < parameters.size()) {
      result.emplace_back(parameters[binding.parameter].name,
                          detail::FnAccess::restore(fn_, binding.value.id,
                                                    binding.value.known));
    }
  }
  return result;
}

std::optional<Val> Val::binding(std::string_view name) const {
  const auto values = bindings();
  const auto found =
      std::find_if(values.begin(), values.end(),
                   [&](const auto& item) { return item.first == name; });
  return found == values.end() ? std::optional<Val>{}
                               : std::optional<Val>{found->second};
}

Op::Op(std::shared_ptr<FnIdentity> fn, std::uint64_t id)
    : fn_(std::move(fn)), id_(id) {}

bool Op::valid() const { return fn_ && contains(fn_->state->ops, id_); }

Val Op::callee() const {
  const auto found = fn_->state->ops.find(id_);
  if (found == fn_->state->ops.end()) {
    throw std::logic_error("op is no longer valid");
  }
  return detail::FnAccess::restore(fn_, found->second.callee.id,
                                   found->second.callee.known);
}

Blk Op::parent() const {
  const auto found = fn_->state->ops.find(id_);
  if (found == fn_->state->ops.end() ||
      !contains(fn_->state->blocks, found->second.parent)) {
    throw std::logic_error("op has no valid parent block");
  }
  return Blk(fn_, found->second.parent);
}

std::vector<Val> Op::arguments() const {
  const auto found = fn_->state->ops.find(id_);
  if (found == fn_->state->ops.end()) {
    throw std::logic_error("op is no longer valid");
  }
  std::vector<Val> values;
  values.reserve(found->second.arguments.size());
  for (const detail::StoredArgument& argument : found->second.arguments) {
    const detail::StoredVal& value = argument.value;
    values.push_back(detail::FnAccess::restore(fn_, value.id, value.known));
  }
  return values;
}

std::optional<Val> Op::operand(std::string_view name) const {
  const auto declaration = callee().referenced_fn();
  if (!declaration) {
    return std::nullopt;
  }
  const auto parameters = declaration->inputs();
  const auto parameter = std::find_if(
      parameters.begin(), parameters.end(),
      [&](const Mod::ParamDecl& candidate) { return candidate.name == name; });
  return parameter != parameters.end() && detail::is_value_port(*parameter)
             ? argument(name)
             : std::nullopt;
}

std::vector<Val> Op::results() const {
  const auto found = fn_->state->ops.find(id_);
  if (found == fn_->state->ops.end()) {
    throw std::logic_error("op is no longer valid");
  }
  std::vector<Val> values;
  values.reserve(found->second.results.size());
  for (std::uint64_t value : found->second.results) {
    values.push_back(Val(fn_, value));
  }
  return values;
}

Val Op::value() const {
  const auto found = fn_->state->ops.find(id_);
  if (found == fn_->state->ops.end()) {
    throw std::logic_error("op is no longer valid");
  }
  if (found->second.results.size() != 1U) {
    throw std::logic_error("op does not have exactly one value");
  }
  return Val(fn_, found->second.results.front());
}

Val Op::result(std::size_t index) const {
  const auto found = fn_->state->ops.find(id_);
  if (found == fn_->state->ops.end() || index >= found->second.results.size()) {
    throw std::out_of_range("op result index is out of range");
  }
  return Val(fn_, found->second.results[index]);
}

std::optional<Loc> Op::location() const {
  if (!valid()) {
    return std::nullopt;
  }
  return fn_->state->ops.at(id_).location;
}

std::optional<Val> Op::argument(std::string_view name) const {
  const auto found = fn_->state->ops.find(id_);
  if (found == fn_->state->ops.end()) {
    return std::nullopt;
  }
  const auto declaration = callee().referenced_fn();
  if (!declaration) {
    return std::nullopt;
  }
  const auto parameters = declaration->inputs();
  const auto parameter = std::find_if(
      parameters.begin(), parameters.end(),
      [&](const Mod::ParamDecl& candidate) { return candidate.name == name; });
  if (parameter == parameters.end()) {
    return std::nullopt;
  }
  const std::size_t index =
      static_cast<std::size_t>(std::distance(parameters.begin(), parameter));
  const auto argument = std::find_if(
      found->second.arguments.begin(), found->second.arguments.end(),
      [&](const detail::StoredArgument& candidate) {
        return candidate.parameter == index;
      });
  if (argument == found->second.arguments.end()) {
    return std::nullopt;
  }
  return detail::FnAccess::restore(fn_, argument->value.id,
                                   argument->value.known);
}

Term::Term(std::shared_ptr<FnIdentity> fn, std::uint64_t block)
    : fn_(std::move(fn)), block_(block) {}

bool Term::valid() const {
  if (!fn_) {
    return false;
  }
  const auto block = fn_->state->blocks.find(block_);
  return block != fn_->state->blocks.end() &&
         block->second.terminator.has_value();
}

Term::Kind Term::kind() const {
  const auto block = fn_->state->blocks.find(block_);
  if (block == fn_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  return block->second.terminator->kind;
}

std::optional<Val> Term::condition() const {
  const auto block = fn_->state->blocks.find(block_);
  if (block == fn_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  const auto condition = block->second.terminator->condition;
  return condition ? std::optional<Val>{Val(fn_, *condition)} : std::nullopt;
}

std::vector<Val> Term::returned() const {
  const auto block = fn_->state->blocks.find(block_);
  if (block == fn_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  std::vector<Val> values;
  values.reserve(block->second.terminator->returned.size());
  for (const std::uint64_t value : block->second.terminator->returned) {
    values.push_back(Val(fn_, value));
  }
  return values;
}

std::size_t Term::successor_count() const {
  const auto block = fn_->state->blocks.find(block_);
  if (block == fn_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  return block->second.terminator->successors.size();
}

Blk Term::successor(std::size_t index) const {
  const auto block = fn_->state->blocks.find(block_);
  if (block == fn_->state->blocks.end() || !block->second.terminator ||
      index >= block->second.terminator->successors.size()) {
    throw std::out_of_range("terminator successor index is out of range");
  }
  return Blk(fn_, block->second.terminator->successors[index].target);
}

std::vector<Val> Term::arguments(std::size_t successor) const {
  const auto block = fn_->state->blocks.find(block_);
  if (block == fn_->state->blocks.end() || !block->second.terminator ||
      successor >= block->second.terminator->successors.size()) {
    throw std::out_of_range("terminator successor index is out of range");
  }
  std::vector<Val> values;
  const auto& arguments =
      block->second.terminator->successors[successor].arguments;
  values.reserve(arguments.size());
  for (const std::uint64_t value : arguments) {
    values.push_back(Val(fn_, value));
  }
  return values;
}

Blk::Blk(std::shared_ptr<FnIdentity> fn, std::uint64_t id)
    : fn_(std::move(fn)), id_(id) {}

bool Blk::valid() const { return fn_ && contains(fn_->state->blocks, id_); }

bool Blk::is_entry() const { return valid() && fn_->state->entry == id_; }

std::vector<Val> Blk::arguments() const {
  const auto found = fn_->state->blocks.find(id_);
  if (found == fn_->state->blocks.end()) {
    throw std::logic_error("block is no longer valid");
  }
  std::vector<Val> values;
  values.reserve(found->second.arguments.size());
  for (const std::uint64_t value : found->second.arguments) {
    values.push_back(Val(fn_, value));
  }
  return values;
}

std::vector<Op> Blk::ops() const {
  const auto found = fn_->state->blocks.find(id_);
  if (found == fn_->state->blocks.end()) {
    throw std::logic_error("block is no longer valid");
  }
  std::vector<Op> ops;
  ops.reserve(found->second.ops.size());
  for (const std::uint64_t op : found->second.ops) {
    ops.push_back(Op(fn_, op));
  }
  return ops;
}

Term Blk::terminator() const {
  if (!valid()) {
    throw std::logic_error("block is no longer valid");
  }
  return Term(fn_, id_);
}

Fn::Edit::Edit(std::shared_ptr<FnIdentity> fn)
    : state_(std::make_unique<detail::FnEditState>()) {
  if (fn->editing) {
    throw std::logic_error("a fn already has an active edit");
  }
  fn->editing = true;
  state_->fn = std::move(fn);
  state_->backup = state_->fn->state;
  state_->fn->state = std::make_shared<FnState>(*state_->backup);
}

Fn::Edit::~Edit() {
  if (state_ && state_->active) {
    state_->fn->state = std::move(state_->backup);
    state_->fn->editing = false;
  }
}

Fn::Edit::Edit(Edit&&) noexcept = default;

Fn::Edit& Fn::Edit::operator=(Edit&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (state_ && state_->active) {
    state_->fn->state = std::move(state_->backup);
    state_->fn->editing = false;
  }
  state_ = std::move(other.state_);
  return *this;
}

Val Fn::Edit::argument(Type type) {
  const std::uint64_t id = state_->fn->next_id++;
  const std::size_t index = state_->fn->state->arguments.size();
  state_->fn->state->values.emplace(id, ValData{std::move(type),
                                                ValData::Origin::FnArg,
                                                0,
                                                index,
                                                std::nullopt,
                                                nullptr,
                                                {},
                                                {}});
  state_->fn->state->arguments.push_back(id);
  return Fn::make_value(state_->fn, id);
}

Val Fn::Edit::reference(Mod::FnDecl fn, Type type,
                        std::vector<std::pair<std::string, Val>> bindings) {
  if (!state_ || !state_->active) {
    throw std::logic_error("cannot edit an inactive fn");
  }
  if (!owns(*state_->fn->state, fn.symbol()) ||
      !owns(*state_->fn->state, type)) {
    throw std::invalid_argument("referenced fn is outside the mod closure");
  }
  const auto parameters = fn.inputs();
  std::vector<detail::StoredArgument> stored;
  stored.reserve(bindings.size());
  for (const auto& [name, value] : bindings) {
    const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                                        [&](const Mod::ParamDecl& candidate) {
                                          return candidate.name == name;
                                        });
    if (parameter == parameters.end() || detail::is_value_port(*parameter)) {
      throw std::invalid_argument("unknown compile-time binding '" + name +
                                  "'");
    }
    check_same_fn(state_->fn, value, "fn binding");
    const auto& known = detail::FnAccess::known(value);
    if (!known || !matches(*parameter, known->value)) {
      throw std::invalid_argument("fn binding '" + name +
                                  "' must be a compatible Known value");
    }
    const std::size_t index =
        static_cast<std::size_t>(std::distance(parameters.begin(), parameter));
    if (std::any_of(stored.begin(), stored.end(), [&](const auto& item) {
          return item.parameter == index;
        })) {
      throw std::invalid_argument("duplicate fn binding '" + name + "'");
    }
    stored.push_back({index, {detail::FnAccess::id(value), known}});
  }
  std::sort(stored.begin(), stored.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.parameter < rhs.parameter;
  });
  detail::ValData data{std::move(type),
                       detail::ValData::Origin::FnRef,
                       0,
                       0,
                       std::move(fn),
                       nullptr,
                       {},
                       std::move(stored)};
  if (!matches_fn_reference(*state_->fn->state, data)) {
    throw std::invalid_argument(
        "fn reference '" + data.reference->signature() +
        "' does not match its callable type or compile-time bindings");
  }
  const std::uint64_t id = state_->fn->next_id++;
  state_->fn->state->values.emplace(id, std::move(data));
  return Fn::make_value(state_->fn, id);
}

Val Fn::Edit::callable(Fn fn, Type type, std::vector<Val> captures) {
  if (!state_ || !state_->active) {
    throw std::logic_error("cannot edit an inactive fn");
  }
  std::vector<detail::StoredVal> stored;
  stored.reserve(captures.size());
  for (const Val& capture : captures) {
    check_same_fn(state_->fn, capture, "inline fn capture");
    if (detail::is_effect_type(capture.type())) {
      throw std::invalid_argument("an inline fn cannot capture an effect");
    }
    stored.push_back(
        {detail::FnAccess::id(capture), detail::FnAccess::known(capture)});
  }
  detail::ValData data{std::move(type),
                       detail::ValData::Origin::InlineFn,
                       0,
                       0,
                       std::nullopt,
                       std::make_shared<Fn>(std::move(fn)),
                       std::move(stored),
                       {}};
  if (!matches_inline_fn(*state_->fn->state, data)) {
    throw std::invalid_argument("inline fn type does not match its body");
  }
  const std::uint64_t id = state_->fn->next_id++;
  state_->fn->state->values.emplace(id, std::move(data));
  return Fn::make_value(state_->fn, id);
}

Blk Fn::Edit::blk(std::vector<Type> argument_types) {
  const std::uint64_t block_id = state_->fn->next_id++;
  detail::BlkData data;
  data.arguments.reserve(argument_types.size());
  for (std::size_t index = 0; index < argument_types.size(); ++index) {
    const std::uint64_t value_id = state_->fn->next_id++;
    state_->fn->state->values.emplace(value_id,
                                      ValData{std::move(argument_types[index]),
                                              ValData::Origin::BlkArg,
                                              block_id,
                                              index,
                                              std::nullopt,
                                              nullptr,
                                              {},
                                              {}});
    data.arguments.push_back(value_id);
  }
  state_->fn->state->blocks.emplace(block_id, std::move(data));
  state_->fn->state->block_order.push_back(block_id);
  return Fn::make_blk(state_->fn, block_id);
}

Op Fn::Edit::call(Val callee, std::vector<Val> arguments,
                  std::vector<Type> result_types) {
  return add(Fn::make_blk(state_->fn, state_->fn->state->entry), std::nullopt,
             std::move(callee), std::move(arguments), std::move(result_types));
}

Op Fn::Edit::call(Blk block, Val callee, std::vector<Val> arguments,
                  std::vector<Type> result_types) {
  return add(std::move(block), std::nullopt, std::move(callee),
             std::move(arguments), std::move(result_types));
}

Op Fn::Edit::call_before(Op before, Val callee, std::vector<Val> arguments,
                         std::vector<Type> result_types) {
  check_same_fn(state_->fn, before, "insertion point");
  return add(before.parent(), before, std::move(callee), std::move(arguments),
             std::move(result_types));
}

Op Fn::Edit::call(Mod::FnDecl fn, std::vector<Val> arguments,
                  std::vector<Type> result_types) {
  return direct(Fn::make_blk(state_->fn, state_->fn->state->entry),
                std::nullopt, std::move(fn), std::move(arguments),
                std::move(result_types));
}

Op Fn::Edit::call(Blk block, Mod::FnDecl fn, std::vector<Val> arguments,
                  std::vector<Type> result_types) {
  return direct(std::move(block), std::nullopt, std::move(fn),
                std::move(arguments), std::move(result_types));
}

Op Fn::Edit::call_before(Op before, Mod::FnDecl fn, std::vector<Val> arguments,
                         std::vector<Type> result_types) {
  check_same_fn(state_->fn, before, "insertion point");
  return direct(before.parent(), before, std::move(fn), std::move(arguments),
                std::move(result_types));
}

Op Fn::Edit::direct(Blk block, std::optional<Op> before, Mod::FnDecl fn,
                    std::vector<Val> arguments,
                    std::vector<Type> result_types) {
  check_same_fn(state_->fn, block, "block");
  const auto parameters = fn.inputs();
  std::vector<detail::StoredArgument> argument_ids;
  argument_ids.reserve(std::max(arguments.size(), parameters.size()));
  std::size_t supplied = 0;
  for (std::size_t parameter_index = 0; parameter_index < parameters.size();
       ++parameter_index) {
    const Mod::ParamDecl& parameter = parameters[parameter_index];
    std::size_t count = parameter.variadic ? arguments.size() - supplied : 1U;
    bool use_default = false;
    if (!parameter.variadic && parameter.default_value) {
      std::size_t required_after = 0;
      for (std::size_t index = parameter_index + 1U; index < parameters.size();
           ++index) {
        if (!parameters[index].variadic && !parameters[index].default_value) {
          ++required_after;
        }
      }
      use_default = arguments.size() - supplied == required_after;
    }
    if (use_default) {
      auto value = detail::parameter_default(parameter);
      auto known =
          value ? make_known(*state_->fn->state, parameter, std::move(*value))
                : nullptr;
      if (!known) {
        throw std::invalid_argument("cannot construct default argument '" +
                                    parameter.name + "' for op '" +
                                    fn.symbol().qualified_name() + "'");
      }
      argument_ids.push_back(
          {parameter_index, detail::StoredVal{0, std::move(known)}});
      continue;
    }
    if (supplied + count > arguments.size()) {
      throw std::invalid_argument("op '" + fn.symbol().qualified_name() +
                                  "' is missing argument '" + parameter.name +
                                  "'");
    }
    for (std::size_t item = 0; item < count; ++item) {
      const Val& argument = arguments[supplied++];
      check_same_fn(state_->fn, argument, "argument");
      const auto& known = detail::FnAccess::known(argument);
      if (!detail::is_value_port(parameter) &&
          (!known || !matches(parameter, known->value))) {
        throw std::invalid_argument("argument '" + parameter.name +
                                    "' of op '" + fn.symbol().qualified_name() +
                                    "' must be a compatible Known value");
      }
      argument_ids.push_back(
          {parameter_index,
           detail::StoredVal{detail::FnAccess::id(argument), known}});
    }
  }
  if (supplied != arguments.size()) {
    throw std::invalid_argument("op '" + fn.symbol().qualified_name() +
                                "' has too many arguments");
  }

  if (result_types.empty() && !detail::value_results(fn).empty()) {
    std::vector<Type> argument_types;
    std::vector<std::optional<Type>> expected(detail::value_results(fn).size());
    std::vector<std::optional<ParamVal>> inference_known;
    inference_known.reserve(detail::compiler_inputs(fn).size());
    std::size_t current_parameter = 0;
    for (std::size_t parameter_index = 0; parameter_index < parameters.size();
         ++parameter_index) {
      if (!detail::is_value_port(parameters[parameter_index])) {
        const auto item =
            std::find_if(argument_ids.begin(), argument_ids.end(),
                         [&](const detail::StoredArgument& argument) {
                           return argument.parameter == parameter_index;
                         });
        inference_known.push_back(
            item == argument_ids.end() || !item->value.known
                ? std::optional<ParamVal>{}
                : std::optional<ParamVal>{item->value.known->value});
        continue;
      }
      while (current_parameter < argument_ids.size() &&
             argument_ids[current_parameter].parameter < parameter_index) {
        ++current_parameter;
      }
      while (current_parameter < argument_ids.size() &&
             argument_ids[current_parameter].parameter == parameter_index) {
        const detail::StoredVal& argument =
            argument_ids[current_parameter++].value;
        argument_types.push_back(
            argument.known ? argument.known->type
                           : state_->fn->state->values.at(argument.id).type);
      }
    }
    std::vector<Mod> mods;
    mods.reserve(state_->fn->state->mods.size());
    for (const auto& [name, mod] : state_->fn->state->mods) {
      static_cast<void>(name);
      mods.push_back(mod);
    }
    Diag diagnostics;
    auto inferred = detail::infer_call_types(
        mods, fn, argument_types, inference_known, expected, diagnostics);
    if (!inferred) {
      std::string message =
          "cannot infer results for op '" + fn.symbol().qualified_name() + "'";
      if (!diagnostics.issues().empty()) {
        message += ": " + diagnostics.issues().front().message;
      }
      throw std::invalid_argument(std::move(message));
    }
    result_types = std::move(*inferred);
  }

  std::vector<Val> runtime_arguments;
  std::vector<Type> runtime_types;
  std::vector<std::pair<std::string, Val>> bindings;
  for (const detail::StoredArgument& argument : argument_ids) {
    Val value = detail::FnAccess::restore(state_->fn, argument.value.id,
                                          argument.value.known);
    if (detail::is_value_port(parameters[argument.parameter])) {
      runtime_types.push_back(value.type());
      runtime_arguments.push_back(std::move(value));
    } else {
      bindings.emplace_back(parameters[argument.parameter].name,
                            std::move(value));
    }
  }
  auto type =
      make_callable(*state_->fn->state, std::move(runtime_types), result_types);
  if (!type) {
    throw std::invalid_argument("cannot construct callable type for '" +
                                fn.symbol().qualified_name() + "'");
  }
  Val callee = reference(std::move(fn), std::move(*type), std::move(bindings));
  return add(std::move(block), before, std::move(callee),
             std::move(runtime_arguments), std::move(result_types));
}

Op Fn::Edit::add(Blk block, std::optional<Op> before, Val callee,
                 std::vector<Val> arguments, std::vector<Type> result_types) {
  check_same_fn(state_->fn, block, "block");
  check_same_fn(state_->fn, callee, "callee");
  if (callee.known()) {
    throw std::invalid_argument("a Call callee must be Residual");
  }
  const auto signature = callable_signature(callee.type());
  if (!signature) {
    throw std::invalid_argument("a Call callee must have a fn type");
  }
  if (arguments.size() != signature->first.size()) {
    throw std::invalid_argument("Call argument count does not match callee");
  }
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    check_same_fn(state_->fn, arguments[index], "argument");
    if (arguments[index].type() != signature->first[index]) {
      throw std::invalid_argument("Call argument " + std::to_string(index) +
                                  " has the wrong type");
    }
    if (detail::is_effect_type(arguments[index].type()) &&
        arguments[index].known()) {
      throw std::invalid_argument("an effect argument cannot be Known");
    }
  }
  if (result_types.empty()) {
    result_types = signature->second;
  } else if (result_types != signature->second) {
    throw std::invalid_argument("Call results do not match callee");
  }

  std::vector<std::size_t> parameter_indices;
  parameter_indices.reserve(arguments.size());
  if (const auto declaration = callee.referenced_fn()) {
    for (std::size_t index = 0; index < declaration->inputs().size(); ++index) {
      if (!detail::is_value_port(declaration->inputs()[index])) {
        continue;
      }
      const std::size_t count =
          declaration->inputs()[index].variadic
              ? arguments.size() - parameter_indices.size()
              : 1U;
      for (std::size_t item = 0; item < count; ++item) {
        parameter_indices.push_back(index);
      }
    }
  } else {
    for (std::size_t index = 0; index < arguments.size(); ++index) {
      parameter_indices.push_back(index);
    }
  }
  if (parameter_indices.size() != arguments.size()) {
    throw std::invalid_argument("Call arguments do not fit the declared fn");
  }

  const std::uint64_t block_id = detail::FnAccess::id(block);
  const auto location =
      before ? state_->fn->state->ops.at(detail::FnAccess::id(*before)).location
             : std::optional<Loc>{};
  std::vector<detail::StoredArgument> stored_arguments;
  stored_arguments.reserve(arguments.size());
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    stored_arguments.push_back({parameter_indices[index],
                                {detail::FnAccess::id(arguments[index]),
                                 detail::FnAccess::known(arguments[index])}});
  }
  const std::uint64_t id = state_->fn->next_id++;
  std::vector<std::uint64_t> results;
  results.reserve(result_types.size());
  for (std::size_t index = 0; index < result_types.size(); ++index) {
    const std::uint64_t result = state_->fn->next_id++;
    state_->fn->state->values.emplace(result,
                                      ValData{std::move(result_types[index]),
                                              ValData::Origin::OpResult,
                                              id,
                                              index,
                                              std::nullopt,
                                              nullptr,
                                              {},
                                              {}});
    results.push_back(result);
  }
  state_->fn->state->ops.emplace(id, OpData{{detail::FnAccess::id(callee),
                                             detail::FnAccess::known(callee)},
                                            block_id,
                                            std::move(stored_arguments),
                                            std::move(results),
                                            location});
  auto& op_order = state_->fn->state->blocks.at(block_id).ops;
  if (before) {
    const std::uint64_t before_id = detail::FnAccess::id(*before);
    const auto position =
        std::find(op_order.begin(), op_order.end(), before_id);
    if (position == op_order.end()) {
      throw std::invalid_argument("insertion point is not in this fn");
    }
    op_order.insert(position, id);
  } else {
    op_order.push_back(id);
  }
  return Fn::make_op(state_->fn, id);
}

void Fn::Edit::ret(Blk block, std::vector<Val> values) {
  check_same_fn(state_->fn, block, "return block");
  std::vector<std::uint64_t> ids;
  ids.reserve(values.size());
  for (const Val& value : values) {
    check_same_fn(state_->fn, value, "return value");
    if (value.known()) {
      throw std::invalid_argument(
          "a Known value must be materialized before it is returned");
    }
    ids.push_back(detail::FnAccess::id(value));
  }
  auto& terminator =
      state_->fn->state->blocks.at(detail::FnAccess::id(block)).terminator;
  terminator =
      detail::TermData{Term::Kind::Return, std::nullopt, std::move(ids), {}};
}

void Fn::Edit::jump(Blk block, Blk target, std::vector<Val> arguments) {
  check_same_fn(state_->fn, block, "jump block");
  check_same_fn(state_->fn, target, "jump target");
  std::vector<std::uint64_t> ids;
  ids.reserve(arguments.size());
  for (const Val& value : arguments) {
    check_same_fn(state_->fn, value, "jump argument");
    if (value.known()) {
      throw std::invalid_argument(
          "a Known value must be materialized before it crosses an edge");
    }
    ids.push_back(detail::FnAccess::id(value));
  }
  auto& terminator =
      state_->fn->state->blocks.at(detail::FnAccess::id(block)).terminator;
  terminator =
      detail::TermData{Term::Kind::Jump,
                       std::nullopt,
                       {},
                       {{detail::FnAccess::id(target), std::move(ids)}}};
}

void Fn::Edit::branch(Blk block, Val condition, Blk true_target,
                      std::vector<Val> true_arguments, Blk false_target,
                      std::vector<Val> false_arguments) {
  check_same_fn(state_->fn, block, "branch block");
  check_same_fn(state_->fn, condition, "branch condition");
  if (condition.known()) {
    throw std::invalid_argument(
        "a Known branch condition must be specialized before IR construction");
  }
  check_same_fn(state_->fn, true_target, "true target");
  check_same_fn(state_->fn, false_target, "false target");
  const auto ids = [&](std::span<const Val> values) {
    std::vector<std::uint64_t> result;
    result.reserve(values.size());
    for (const Val& value : values) {
      check_same_fn(state_->fn, value, "branch argument");
      if (value.known()) {
        throw std::invalid_argument(
            "a Known value must be materialized before it crosses an edge");
      }
      result.push_back(detail::FnAccess::id(value));
    }
    return result;
  };
  auto& terminator =
      state_->fn->state->blocks.at(detail::FnAccess::id(block)).terminator;
  terminator = detail::TermData{
      Term::Kind::Branch,
      detail::FnAccess::id(condition),
      {},
      {{detail::FnAccess::id(true_target), ids(true_arguments)},
       {detail::FnAccess::id(false_target), ids(false_arguments)}}};
}

void Fn::Edit::locate(Op op, Loc source) {
  if (!state_ || !state_->active || op.fn_ != state_->fn || !op.valid()) {
    throw std::invalid_argument("op does not belong to this fn edit");
  }
  state_->fn->state->ops.at(op.id_).location = std::move(source);
}

void Fn::Edit::replace(Val from, Val to) {
  check_same_fn(state_->fn, from, "source value");
  check_same_fn(state_->fn, to, "replacement value");
  if (from.known() || to.known()) {
    throw std::invalid_argument(
        "Known values are immutable and cannot cross a Fn boundary");
  }
  if (from.type() != to.type()) {
    throw std::invalid_argument("replacement value has a different type");
  }
  const std::uint64_t from_id = detail::FnAccess::id(from);
  const std::uint64_t to_id = detail::FnAccess::id(to);
  for (auto& [id, op] : state_->fn->state->ops) {
    static_cast<void>(id);
    if (!op.callee.known && op.callee.id == from_id) {
      op.callee.id = to_id;
    }
    for (detail::StoredArgument& argument : op.arguments) {
      if (!argument.value.known && argument.value.id == from_id) {
        argument.value.id = to_id;
      }
    }
  }
  for (auto& [id, value] : state_->fn->state->values) {
    static_cast<void>(id);
    for (detail::StoredVal& capture : value.captures) {
      if (!capture.known && capture.id == from_id) {
        capture.id = to_id;
      }
    }
  }
  for (auto& [id, block] : state_->fn->state->blocks) {
    static_cast<void>(id);
    if (!block.terminator) {
      continue;
    }
    if (block.terminator->condition == from_id) {
      block.terminator->condition = to_id;
    }
    std::replace(block.terminator->returned.begin(),
                 block.terminator->returned.end(), from_id, to_id);
    for (auto& edge : block.terminator->successors) {
      std::replace(edge.arguments.begin(), edge.arguments.end(), from_id,
                   to_id);
    }
  }
}

Op Fn::Edit::replace(Op op, Val callee) {
  check_same_fn(state_->fn, op, "op");
  std::vector<Type> result_types;
  result_types.reserve(op.results().size());
  for (const Val& result : op.results()) {
    result_types.push_back(result.type());
  }
  const Op replacement =
      call_before(op, std::move(callee), op.arguments(), result_types);
  for (std::size_t index = 0; index < result_types.size(); ++index) {
    replace(op.result(index), replacement.result(index));
  }
  erase(op);
  return replacement;
}

Op Fn::Edit::replace(Op op, Mod::FnDecl fn) {
  check_same_fn(state_->fn, op, "op");
  std::vector<Val> arguments;
  const auto old_callee = op.callee();
  const auto old_declaration = old_callee.referenced_fn();
  if (old_declaration &&
      old_declaration->inputs().size() == fn.inputs().size()) {
    const auto runtime = op.arguments();
    std::size_t runtime_index = 0;
    for (const Mod::ParamDecl& parameter : old_declaration->inputs()) {
      if (detail::is_value_port(parameter)) {
        const std::size_t count =
            parameter.variadic ? runtime.size() - runtime_index : 1U;
        for (std::size_t item = 0; item < count; ++item) {
          arguments.push_back(runtime.at(runtime_index++));
        }
      } else {
        const auto binding = old_callee.binding(parameter.name);
        if (!binding) {
          throw std::invalid_argument("old Call has an incomplete callee");
        }
        arguments.push_back(*binding);
      }
    }
  } else {
    arguments = op.arguments();
  }
  std::vector<Type> result_types;
  for (const Val& result : op.results()) {
    result_types.push_back(result.type());
  }
  const Op replacement =
      call_before(op, std::move(fn), std::move(arguments), result_types);
  for (std::size_t index = 0; index < result_types.size(); ++index) {
    replace(op.result(index), replacement.result(index));
  }
  erase(op);
  return replacement;
}

void Fn::Edit::replace(Op op, std::vector<Val> results) {
  check_same_fn(state_->fn, op, "op");
  const auto previous = op.results();
  if (previous.size() != results.size()) {
    throw std::invalid_argument(
        "replacement result count does not match the op");
  }
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (results[index].known()) {
      throw std::invalid_argument("a Known value cannot replace an Op result");
    }
    check_same_fn(state_->fn, results[index], "replacement value");
    if (previous[index].type() != results[index].type()) {
      throw std::invalid_argument(
          "replacement result has a different type at position " +
          std::to_string(index));
    }
  }
  for (std::size_t index = 0; index < results.size(); ++index) {
    replace(previous[index], results[index]);
  }
  erase(op);
}

void Fn::Edit::erase(Op op) {
  check_same_fn(state_->fn, op, "op");
  auto& state = *state_->fn->state;
  const std::uint64_t op_id = detail::FnAccess::id(op);
  const auto found = state.ops.find(op_id);
  std::unordered_set<std::uint64_t> values;
  values.insert(found->second.results.begin(), found->second.results.end());
  for (const auto& [id, user] : state.ops) {
    if (id == op_id) {
      continue;
    }
    if ((!user.callee.known && values.contains(user.callee.id)) ||
        std::any_of(user.arguments.begin(), user.arguments.end(),
                    [&](const detail::StoredArgument& argument) {
                      return !argument.value.known &&
                             values.contains(argument.value.id);
                    })) {
      throw std::invalid_argument("op still has live result uses");
    }
  }
  for (const auto& [id, block] : state.blocks) {
    static_cast<void>(id);
    if (!block.terminator) {
      continue;
    }
    const auto live = [&](std::span<const std::uint64_t> uses) {
      return std::any_of(uses.begin(), uses.end(), [&](std::uint64_t use) {
        return values.contains(use);
      });
    };
    if ((block.terminator->condition &&
         values.contains(*block.terminator->condition)) ||
        live(block.terminator->returned) ||
        std::any_of(block.terminator->successors.begin(),
                    block.terminator->successors.end(),
                    [&](const detail::EdgeData& edge) {
                      return live(edge.arguments);
                    })) {
      throw std::invalid_argument("op still has a live terminator use");
    }
  }
  auto& ops = state.blocks.at(found->second.parent).ops;
  ops.erase(std::remove(ops.begin(), ops.end(), op_id), ops.end());
  for (const std::uint64_t value : values) {
    state.values.erase(value);
  }
  state.ops.erase(op_id);
}

bool Fn::Edit::commit(Diag& diagnostics) {
  if (!state_ || !state_->active) {
    throw std::logic_error("fn edit is no longer active");
  }
  if (!verify_fn(*state_->fn->state, diagnostics) ||
      !verify_op_contracts(*state_->fn->state, diagnostics)) {
    state_->fn->state = std::move(state_->backup);
    state_->fn->editing = false;
    state_->active = false;
    return false;
  }
  state_->active = false;
  state_->backup.reset();
  state_->fn->editing = false;
  return true;
}

bool Fn::Edit::commit(Compiler& compiler, Diag& diagnostics) {
  return detail::FnAccess::commit(*this, compiler, diagnostics);
}

Fn::Fn(std::vector<Mod> mods) : fn_(std::make_shared<FnIdentity>()) {
  fn_->state = std::make_shared<FnState>();
  for (Mod& mod : mods) {
    fn_->state->mods.emplace(std::string(mod.name()), std::move(mod));
  }
  fn_->state->entry = fn_->next_id++;
  detail::BlkData entry;
  entry.terminator = detail::TermData{};
  fn_->state->blocks.emplace(fn_->state->entry, std::move(entry));
  fn_->state->block_order.push_back(fn_->state->entry);
}

Fn::~Fn() = default;

Fn::Fn(const Fn& other) : fn_(std::make_shared<FnIdentity>()) {
  if (!other.fn_ || other.fn_->editing) {
    throw std::logic_error(
        "cannot copy a moved-from fn or one with an active edit");
  }
  fn_->state = other.fn_->state;
  fn_->next_id = other.fn_->next_id;
}

Fn& Fn::operator=(const Fn& other) {
  if (this == &other) {
    return *this;
  }
  if (!other.fn_ || (fn_ && fn_->editing) || other.fn_->editing) {
    throw std::logic_error(
        "cannot copy a moved-from fn or one with an active edit");
  }
  auto identity = std::make_shared<FnIdentity>();
  identity->state = other.fn_->state;
  identity->next_id = other.fn_->next_id;
  fn_ = std::move(identity);
  return *this;
}

Fn::Fn(Fn&&) noexcept = default;
Fn& Fn::operator=(Fn&&) noexcept = default;

std::vector<Val> Fn::arguments() const {
  std::vector<Val> result;
  result.reserve(fn_->state->arguments.size());
  for (const std::uint64_t value : fn_->state->arguments) {
    result.push_back(make_value(fn_, value));
  }
  return result;
}

std::optional<Mod::FnDecl> Fn::declaration() const {
  return fn_->state->signature ? fn_->state->signature->declaration
                               : std::nullopt;
}

std::vector<Type> Fn::result_types() const {
  if (fn_->state->signature) {
    return fn_->state->signature->results;
  }
  for (const std::uint64_t block_id : fn_->state->block_order) {
    const auto& block = fn_->state->blocks.at(block_id);
    if (!block.terminator || block.terminator->kind != Term::Kind::Return) {
      continue;
    }
    std::vector<Type> result;
    result.reserve(block.terminator->returned.size());
    for (const std::uint64_t value : block.terminator->returned) {
      result.push_back(fn_->state->values.at(value).type);
    }
    return result;
  }
  return {};
}

Blk Fn::entry() const { return make_blk(fn_, fn_->state->entry); }

std::vector<Blk> Fn::blks() const {
  std::vector<Blk> result;
  result.reserve(fn_->state->block_order.size());
  for (const std::uint64_t block : fn_->state->block_order) {
    result.push_back(make_blk(fn_, block));
  }
  return result;
}

std::vector<Op> Fn::ops() const {
  std::vector<Op> result;
  result.reserve(fn_->state->ops.size());
  for (const std::uint64_t block : fn_->state->block_order) {
    for (const std::uint64_t op : fn_->state->blocks.at(block).ops) {
      result.push_back(make_op(fn_, op));
    }
  }
  return result;
}

std::vector<Blk> Fn::predecessors(Blk block) const {
  if (block.fn_ != fn_ || !block.valid()) {
    throw std::invalid_argument("predecessor query block is outside fn");
  }
  std::vector<Blk> result;
  for (const std::uint64_t candidate : fn_->state->block_order) {
    const auto& data = fn_->state->blocks.at(candidate);
    if (!data.terminator) {
      continue;
    }
    const bool reaches = std::any_of(
        data.terminator->successors.begin(), data.terminator->successors.end(),
        [&](const detail::EdgeData& edge) { return edge.target == block.id_; });
    if (reaches) {
      result.push_back(make_blk(fn_, candidate));
    }
  }
  return result;
}

std::vector<Op> Fn::users(Val value) const {
  if (!value.valid() || (!value.known() && value.fn_ != fn_) ||
      (value.known() && (!owns(*fn_->state, ParamVal(value.type())) ||
                         !owns(*fn_->state, *value.known_value())))) {
    throw std::invalid_argument("use query value is outside fn");
  }
  std::vector<Op> result;
  for (const std::uint64_t block : fn_->state->block_order) {
    for (const std::uint64_t op_id : fn_->state->blocks.at(block).ops) {
      const auto& op = fn_->state->ops.at(op_id);
      const bool consumes =
          !value.known() &&
          (stored_uses(*fn_->state, op.callee, value.id_) ||
           std::any_of(op.arguments.begin(), op.arguments.end(),
                       [&](const detail::StoredArgument& argument) {
                         return stored_uses(*fn_->state, argument.value,
                                            value.id_);
                       }));
      if (consumes) {
        result.push_back(make_op(fn_, op_id));
      }
    }
  }
  return result;
}

bool Fn::has_uses(Val value) const {
  if (!users(value).empty()) {
    return true;
  }
  if (value.known()) {
    return false;
  }
  for (const std::uint64_t block : fn_->state->block_order) {
    const auto& terminator = fn_->state->blocks.at(block).terminator;
    if (!terminator) {
      continue;
    }
    if ((terminator->condition &&
         value_uses(*fn_->state, *terminator->condition, value.id_)) ||
        std::any_of(terminator->returned.begin(), terminator->returned.end(),
                    [&](std::uint64_t returned) {
                      return value_uses(*fn_->state, returned, value.id_);
                    })) {
      return true;
    }
    for (const auto& edge : terminator->successors) {
      if (std::any_of(edge.arguments.begin(), edge.arguments.end(),
                      [&](std::uint64_t argument) {
                        return value_uses(*fn_->state, argument, value.id_);
                      })) {
        return true;
      }
    }
  }
  return false;
}

bool Fn::dominates(Blk dominator, Blk block) const {
  if (dominator.fn_ != fn_ || block.fn_ != fn_ || !dominator.valid() ||
      !block.valid()) {
    throw std::invalid_argument("dominance query block is outside fn");
  }
  const auto relation = dominators(*fn_->state);
  return relation.at(block.id_).contains(dominator.id_);
}

bool Fn::dominates(Val definition, Op op) const {
  if (!definition.valid() || !op.valid() || op.fn_ != fn_ ||
      (!definition.known() && definition.fn_ != fn_)) {
    throw std::invalid_argument("dominance query value is outside fn");
  }
  if (definition.known()) {
    return owns(*fn_->state, ParamVal(definition.type())) &&
           owns(*fn_->state, *definition.known_value());
  }
  const auto found = fn_->state->values.find(definition.id_);
  const auto user = fn_->state->ops.find(op.id_);
  if (found == fn_->state->values.end() || user == fn_->state->ops.end()) {
    return false;
  }
  const auto relation = dominators(*fn_->state);
  return definition_dominates(*fn_->state, found->second, user->second.parent,
                              op.id_, relation);
}

Fn::Revision Fn::revision() const { return Revision(fn_->state); }

Fn::Edit Fn::edit() { return Edit(fn_); }

bool Fn::accepts(const Mod::Symbol& symbol) const {
  return owns(*fn_->state, symbol);
}

Val Fn::make_value(std::shared_ptr<FnIdentity> fn, std::uint64_t id) {
  return Val(std::move(fn), id);
}

Op Fn::make_op(std::shared_ptr<FnIdentity> fn, std::uint64_t id) {
  return Op(std::move(fn), id);
}

Blk Fn::make_blk(std::shared_ptr<FnIdentity> fn, std::uint64_t id) {
  return Blk(std::move(fn), id);
}

}  // namespace joggle
