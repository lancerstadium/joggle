#include "joggle/ir.h"

#include "ir_internal.h"
#include "domain.h"
#include "module_internal.h"
#include "prelude.h"
#include "type_contract.h"
#include "type_internal.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace joggle::detail {

struct ValueData {
  enum class Origin {
    FunctionArgument,
    BlockArgument,
    InstructionResult,
    FunctionReference
  };

  Type type;
  Origin origin = Origin::FunctionArgument;
  std::uint64_t owner = 0;
  std::size_t index = 0;
  std::optional<Module::FunctionDecl> reference;
};

struct KnownValueStorage {
  Type type;
  ParameterValue value;
};

struct StoredValue {
  std::uint64_t id = 0;
  std::shared_ptr<const KnownValueStorage> known;
};

struct StoredArgument {
  std::size_t parameter = 0;
  StoredValue value;
};

struct InstructionData {
  Module::FunctionDecl schema;
  std::uint64_t parent = 0;
  std::vector<StoredArgument> arguments;
  std::vector<std::uint64_t> results;
  std::optional<SourceRange> location;
};

struct EdgeData {
  std::uint64_t target = 0;
  std::vector<std::uint64_t> arguments;
};

struct TerminatorData {
  Terminator::Kind kind = Terminator::Kind::Return;
  std::optional<std::uint64_t> condition;
  std::vector<std::uint64_t> returned;
  std::vector<EdgeData> successors;
};

struct BlockData {
  std::vector<std::uint64_t> arguments;
  std::vector<std::uint64_t> instructions;
  std::optional<TerminatorData> terminator;
};

struct FunctionState {
  struct Signature {
    Module::FunctionDecl declaration;
    std::vector<Type> arguments;
    std::vector<Type> results;
  };

  std::map<std::string, Module, std::less<>> modules;
  std::unordered_map<std::uint64_t, ValueData> values;
  std::unordered_map<std::uint64_t, InstructionData> instructions;
  std::vector<std::uint64_t> arguments;
  std::unordered_map<std::uint64_t, BlockData> blocks;
  std::vector<std::uint64_t> block_order;
  std::uint64_t entry = 0;
  std::optional<Signature> signature;
};

struct FunctionIdentity {
  std::shared_ptr<FunctionState> state;
  std::uint64_t next_id = 1;
  bool editing = false;
};

struct FunctionEditState {
  std::shared_ptr<FunctionIdentity> function;
  std::shared_ptr<FunctionState> backup;
  bool active = true;
};

const std::shared_ptr<FunctionIdentity>&
FunctionAccess::owner(const Value& value) {
  return value.function_;
}

const std::shared_ptr<FunctionIdentity>&
FunctionAccess::owner(const Instruction& instruction) {
  return instruction.function_;
}

const std::shared_ptr<FunctionIdentity>&
FunctionAccess::owner(const Block& block) {
  return block.function_;
}

const std::shared_ptr<const KnownValueStorage>&
FunctionAccess::known(const Value& value) {
  return value.known_;
}

std::uint64_t FunctionAccess::id(const Value& value) { return value.id_; }
std::uint64_t FunctionAccess::id(const Instruction& instruction) {
  return instruction.id_;
}
std::uint64_t FunctionAccess::id(const Block& block) { return block.id_; }

Value FunctionAccess::restore(std::shared_ptr<FunctionIdentity> function,
                              std::uint64_t id,
                              std::shared_ptr<const KnownValueStorage> known) {
  if (known) {
    Value value(std::move(function), 0);
    value.function_.reset();
    value.known_ = std::move(known);
    return value;
  }
  return Value(std::move(function), id);
}
void FunctionAccess::locate(Function::Edit& edit,
                            const Instruction& instruction,
                            SourceRange source) {
  if (instruction.function_ != edit.state_->function || !instruction.valid()) {
    throw std::invalid_argument(
        "instruction does not belong to this function edit");
  }
  edit.state_->function->state->instructions.at(id(instruction)).location =
      std::move(source);
}

std::optional<SourceRange>
FunctionAccess::location(const Instruction& instruction) {
  if (!instruction.valid()) {
    return std::nullopt;
  }
  return instruction.function_->state->instructions.at(instruction.id_)
      .location;
}

std::optional<ParameterValue> FunctionAccess::known_value(const Value& value) {
  return value.known_value();
}

std::size_t FunctionAccess::argument_parameter(const Instruction& instruction,
                                               std::size_t argument) {
  if (!instruction.valid() ||
      argument >= instruction.function_->state->instructions.at(instruction.id_)
                      .arguments.size()) {
    throw std::out_of_range("instruction argument index is out of range");
  }
  return instruction.function_->state->instructions.at(instruction.id_)
      .arguments[argument]
      .parameter;
}

}  // namespace joggle::detail

namespace joggle::ir {

namespace {

using detail::FunctionIdentity;
using detail::FunctionState;
using detail::InstructionData;
using detail::ParameterValue;
using detail::ValueData;

template <typename Map> bool contains(const Map& map, std::uint64_t id) {
  return map.find(id) != map.end();
}

bool owns(const FunctionState& function, const Module::Symbol& symbol) {
  const auto module = function.modules.find(symbol.module_name());
  return module != function.modules.end() &&
         module->second.version() == symbol.module_version() &&
         module->second.digest() == symbol.module_digest();
}

bool owns(const FunctionState& function, const ParameterValue& value);

bool owns(const FunctionState& function, const Type& type) {
  if (!owns(function, type.schema().symbol())) {
    return false;
  }
  const auto parameters = detail::TypeAccess::parameters(type);
  return std::all_of(
      parameters.begin(), parameters.end(),
      [&](const ParameterValue& value) { return owns(function, value); });
}

bool owns(const FunctionState& function, const Attribute& attribute) {
  const auto parameters = detail::TypeAccess::parameters(attribute);
  return owns(function, attribute.schema().symbol()) &&
         std::all_of(parameters.begin(), parameters.end(),
                     [&](const ParameterValue& value) {
                       return owns(function, value);
                     });
}

bool owns(const FunctionState& function, const ParameterValue& value) {
  if (const Type* type = value.as_type()) {
    return owns(function, *type);
  }
  if (const Attribute* attribute = value.as_attribute()) {
    return owns(function, *attribute);
  }
  if (value.kind() == ParameterValue::Kind::List) {
    return std::all_of(
        value.elements().begin(), value.elements().end(),
        [&](const ParameterValue& element) { return owns(function, element); });
  }
  return true;
}

bool matches(const Module::ParameterDecl& schema, const ParameterValue& value);

std::optional<Type>
reflected_parameter_type(const FunctionState& function,
                         const Module::Expression& expression) {
  const auto domain = detail::kernel_domain(expression);
  if (!domain) {
    return std::nullopt;
  }
  const auto prelude = function.modules.find(detail::prelude_module_name);
  if (prelude == function.modules.end()) {
    return std::nullopt;
  }
  const std::string_view name = domain->list
                                    ? std::string_view{"list"}
                                    : detail::domain_name(domain->element);
  const auto declaration = prelude->second.type(name);
  if (!declaration) {
    return std::nullopt;
  }
  std::vector<ParameterValue> parameters;
  if (domain->list) {
    auto element =
        reflected_parameter_type(function, expression.arguments.front());
    if (!element) {
      return std::nullopt;
    }
    parameters.emplace_back(*element);
  }
  return detail::TypeAccess::make(*declaration, std::move(parameters));
}

std::shared_ptr<const detail::KnownValueStorage>
make_known(const FunctionState& function,
           const Module::ParameterDecl& parameter, ParameterValue value) {
  auto type = reflected_parameter_type(function, parameter.domain);
  if (!type || !matches(parameter, value) || !owns(function, *type) ||
      !owns(function, value)) {
    return {};
  }
  return std::make_shared<const detail::KnownValueStorage>(
      detail::KnownValueStorage{std::move(*type), std::move(value)});
}

bool matches(const Module::ParameterDecl& schema, const ParameterValue& value) {
  return detail::matches_parameter(schema, value);
}

bool accepts_count(std::span<const Module::ParameterDecl> parameters,
                   std::size_t count) {
  std::size_t minimum = 0;
  bool variadic = false;
  for (const auto& parameter : parameters) {
    if (parameter.variadic) {
      variadic = true;
    } else {
      ++minimum;
    }
  }
  return variadic ? count >= minimum : count == minimum;
}

std::optional<std::size_t> instruction_position(const FunctionState& function,
                                                std::uint64_t instruction) {
  const auto item = function.instructions.find(instruction);
  if (item == function.instructions.end()) {
    return std::nullopt;
  }
  const auto owner = function.blocks.find(item->second.parent);
  if (owner == function.blocks.end()) {
    return std::nullopt;
  }
  const auto found = std::find(owner->second.instructions.begin(),
                               owner->second.instructions.end(), instruction);
  if (found == owner->second.instructions.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(
      std::distance(owner->second.instructions.begin(), found));
}

using BlockSet = std::unordered_set<std::uint64_t>;

std::unordered_map<std::uint64_t, BlockSet>
dominators(const FunctionState& function) {
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> predecessors;
  for (const std::uint64_t block_id : function.block_order) {
    predecessors.try_emplace(block_id);
    const auto& block = function.blocks.at(block_id);
    if (!block.terminator) {
      continue;
    }
    for (const auto& edge : block.terminator->successors) {
      predecessors[edge.target].push_back(block_id);
    }
  }

  const BlockSet all(function.block_order.begin(), function.block_order.end());
  std::unordered_map<std::uint64_t, BlockSet> result;
  for (const std::uint64_t block : function.block_order) {
    result.emplace(block, block == function.entry ? BlockSet{block} : all);
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (const std::uint64_t block : function.block_order) {
      if (block == function.entry || predecessors[block].empty()) {
        continue;
      }
      BlockSet next = result.at(predecessors[block].front());
      for (std::size_t index = 1; index < predecessors[block].size(); ++index) {
        const BlockSet& other = result.at(predecessors[block][index]);
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
    const FunctionState& function, const ValueData& definition,
    std::uint64_t user_block, std::optional<std::uint64_t> user_instruction,
    const std::unordered_map<std::uint64_t, BlockSet>& dom) {
  if (definition.origin == ValueData::Origin::FunctionArgument) {
    return definition.owner == 0 &&
           definition.index < function.arguments.size();
  }
  if (definition.origin == ValueData::Origin::FunctionReference) {
    return definition.reference.has_value();
  }
  if (definition.origin == ValueData::Origin::BlockArgument) {
    const auto owner = function.blocks.find(definition.owner);
    return owner != function.blocks.end() &&
           definition.index < owner->second.arguments.size() &&
           dom.at(user_block).contains(definition.owner);
  }
  const auto producer = function.instructions.find(definition.owner);
  if (producer == function.instructions.end()) {
    return false;
  }
  if (producer->second.parent != user_block) {
    return dom.at(user_block).contains(producer->second.parent);
  }
  if (!user_instruction) {
    return instruction_position(function, definition.owner).has_value();
  }
  const auto producer_position =
      instruction_position(function, definition.owner);
  const auto user_position = instruction_position(function, *user_instruction);
  return producer_position && user_position &&
         *producer_position < *user_position;
}

bool matches_function_reference(const FunctionState& function,
                                const ValueData& value) {
  if (value.origin != ValueData::Origin::FunctionReference ||
      !value.reference || !owns(function, value.reference->symbol()) ||
      !owns(function, value.type)) {
    return false;
  }
  const Module::Symbol type = value.type.schema().symbol();
  const auto inputs = value.type.get<std::vector<Type>>("inputs");
  const auto results = value.type.get<std::vector<Type>>("results");
  if (type.module_name() != detail::prelude_module_name ||
      type.local_name() != "callable" || !inputs || !results ||
      !detail::parameter_inputs(*value.reference).empty() ||
      !detail::parameter_results(*value.reference).empty()) {
    return false;
  }

  std::vector<Module> modules;
  modules.reserve(function.modules.size());
  for (const auto& [name, module] : function.modules) {
    static_cast<void>(name);
    modules.push_back(module);
  }
  std::vector<std::optional<Type>> expected;
  expected.reserve(results->size());
  for (const Type& result : *results) {
    expected.emplace_back(result);
  }
  Diagnostics diagnostics;
  const auto resolved = detail::resolve_call_types(
      modules, *value.reference, *inputs, {}, expected, diagnostics);
  return resolved && resolved->results == *results;
}

bool verify_instruction(const FunctionState& function, std::uint64_t id,
                        const InstructionData& instruction,
                        const std::unordered_map<std::uint64_t, BlockSet>& dom,
                        Diagnostics& diagnostics) {
  bool valid = true;
  const std::string name(instruction.schema.symbol().qualified_name());
  if (!owns(function, instruction.schema.symbol())) {
    diagnostics.report("instruction '" + name +
                       "' is outside the function's module closure");
    valid = false;
  }
  if (!contains(function.blocks, instruction.parent)) {
    diagnostics.report("instruction '" + name + "' has no parent block");
    valid = false;
  }
  if (!accepts_count(detail::ir_results(instruction.schema),
                     instruction.results.size())) {
    diagnostics.report("instruction '" + name +
                       "' has the wrong number of results");
    valid = false;
  }

  const auto parameters = instruction.schema.inputs();
  const auto& contract = detail::FunctionTypeAccess::get(instruction.schema);
  std::vector<std::size_t> counts(parameters.size());
  std::size_t previous_parameter = 0;
  for (std::size_t index = 0; index < instruction.arguments.size(); ++index) {
    const detail::StoredArgument& stored = instruction.arguments[index];
    if (stored.parameter >= parameters.size() ||
        (index != 0U && stored.parameter < previous_parameter)) {
      diagnostics.report("instruction '" + name +
                         "' has a malformed argument order");
      valid = false;
      continue;
    }
    previous_parameter = stored.parameter;
    ++counts[stored.parameter];
    const Module::ParameterDecl& parameter = parameters[stored.parameter];
    if (!parameter.variadic && counts[stored.parameter] > 1U) {
      diagnostics.report("instruction '" + name + "' repeats argument '" +
                         parameter.name + "'");
      valid = false;
    }
    const detail::StoredValue& argument = stored.value;
    if (argument.known) {
      if (!owns(function, argument.known->type) ||
          !owns(function, argument.known->value) ||
          (!contract.ir_inputs[stored.parameter] &&
           !matches(parameter, argument.known->value))) {
        diagnostics.report("argument " + std::to_string(index) +
                           " of instruction '" + name +
                           "' has an invalid Known value");
        valid = false;
      }
      continue;
    }
    if (!contract.ir_inputs[stored.parameter]) {
      diagnostics.report("argument '" + parameter.name +
                         "' must be Known in the current IR");
      valid = false;
      continue;
    }
    const auto value = function.values.find(argument.id);
    if (value == function.values.end()) {
      diagnostics.report("instruction '" + name + "' has an invalid argument");
      valid = false;
    } else if (contains(function.blocks, instruction.parent) &&
               !definition_dominates(function, value->second,
                                     instruction.parent, id, dom)) {
      diagnostics.report("argument " + std::to_string(index) +
                         " of instruction '" + name +
                         "' is not dominated by its definition");
      valid = false;
    }
  }
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    if (!parameters[index].variadic && counts[index] != 1U) {
      diagnostics.report("instruction '" + name + "' is missing argument '" +
                         parameters[index].name + "'");
      valid = false;
    }
  }
  for (std::uint64_t result : instruction.results) {
    const auto value = function.values.find(result);
    if (value == function.values.end() ||
        value->second.origin != ValueData::Origin::InstructionResult ||
        value->second.owner != id || !owns(function, value->second.type)) {
      diagnostics.report("instruction '" + name + "' has an invalid result");
      valid = false;
    }
  }
  return valid;
}

bool verify_function(const FunctionState& function, Diagnostics& diagnostics) {
  bool valid = true;
  if (function.block_order.empty() ||
      function.block_order.front() != function.entry ||
      !contains(function.blocks, function.entry)) {
    diagnostics.report("function has no valid entry block");
    return false;
  }
  std::unordered_set<std::uint64_t> listed_instructions;
  std::unordered_set<std::uint64_t> listed_blocks;
  for (const auto& [id, value] : function.values) {
    static_cast<void>(id);
    if (!owns(function, value.type)) {
      diagnostics.report("function contains a value with an invalid type");
      valid = false;
    }
    if (value.origin == ValueData::Origin::FunctionReference) {
      if (!matches_function_reference(function, value)) {
        diagnostics.report("function contains an invalid function reference");
        valid = false;
      }
    } else if (value.reference) {
      diagnostics.report("non-function value contains a function reference");
      valid = false;
    }
  }
  for (std::size_t index = 0; index < function.arguments.size(); ++index) {
    const auto value = function.values.find(function.arguments[index]);
    if (value == function.values.end() ||
        value->second.origin != ValueData::Origin::FunctionArgument ||
        value->second.owner != 0 || value->second.index != index ||
        !owns(function, value->second.type)) {
      diagnostics.report("function has an invalid argument");
      valid = false;
    }
  }
  for (const std::uint64_t block_id : function.block_order) {
    const auto block = function.blocks.find(block_id);
    if (block == function.blocks.end() ||
        !listed_blocks.insert(block_id).second) {
      diagnostics.report("function has an invalid block order");
      valid = false;
      continue;
    }
    for (std::size_t index = 0; index < block->second.arguments.size();
         ++index) {
      const auto value = function.values.find(block->second.arguments[index]);
      if (value == function.values.end() ||
          value->second.origin != ValueData::Origin::BlockArgument ||
          value->second.owner != block_id || value->second.index != index ||
          !owns(function, value->second.type)) {
        diagnostics.report("block has an invalid argument");
        valid = false;
      }
    }
    if (!block->second.terminator) {
      diagnostics.report("block has no terminator");
      valid = false;
    }
    for (const std::uint64_t id : block->second.instructions) {
      const auto instruction = function.instructions.find(id);
      if (instruction == function.instructions.end() ||
          instruction->second.parent != block_id ||
          !listed_instructions.insert(id).second) {
        diagnostics.report("block has an invalid instruction order");
        valid = false;
      }
    }
  }
  for (const auto& [id, block] : function.blocks) {
    static_cast<void>(block);
    if (!listed_blocks.contains(id)) {
      diagnostics.report("function contains an unordered block");
      valid = false;
    }
  }
  const auto dom = dominators(function);
  for (const auto& [id, instruction] : function.instructions) {
    if (!listed_instructions.contains(id)) {
      diagnostics.report("function contains an unordered instruction");
      valid = false;
    }
    valid = verify_instruction(function, id, instruction, dom, diagnostics) &&
            valid;
  }

  if (function.signature) {
    if (!owns(function, function.signature->declaration.symbol())) {
      diagnostics.report("function declaration is outside its module closure");
      valid = false;
    }
    if (function.arguments.size() != function.signature->arguments.size()) {
      diagnostics.report(
          "function argument count does not match its signature");
      valid = false;
    }
    const std::size_t count = std::min(function.arguments.size(),
                                       function.signature->arguments.size());
    for (std::size_t index = 0; index < count; ++index) {
      const auto value = function.values.find(function.arguments[index]);
      if (value != function.values.end() &&
          value->second.type != function.signature->arguments[index]) {
        diagnostics.report(
            "function argument type does not match its signature");
        valid = false;
      }
    }
  }

  std::optional<std::vector<Type>> inferred_results;
  std::unordered_set<std::uint64_t> reachable{function.entry};
  std::vector<std::uint64_t> pending{function.entry};
  while (!pending.empty()) {
    const std::uint64_t block_id = pending.back();
    pending.pop_back();
    const auto block = function.blocks.find(block_id);
    if (block == function.blocks.end() || !block->second.terminator) {
      continue;
    }
    const detail::TerminatorData& terminator = *block->second.terminator;
    const std::size_t expected_successors =
        terminator.kind == Terminator::Kind::Return
            ? 0U
            : (terminator.kind == Terminator::Kind::Jump ? 1U : 2U);
    if (terminator.successors.size() != expected_successors ||
        (terminator.kind == Terminator::Kind::Branch) !=
            terminator.condition.has_value()) {
      diagnostics.report("block has a malformed terminator");
      valid = false;
    }
    const auto verify_use = [&](std::uint64_t id) {
      const auto value = function.values.find(id);
      if (value == function.values.end() ||
          !definition_dominates(function, value->second, block_id, std::nullopt,
                                dom)) {
        diagnostics.report("terminator uses a value that does not dominate it");
        valid = false;
      }
    };
    if (terminator.condition) {
      verify_use(*terminator.condition);
      const auto condition = function.values.find(*terminator.condition);
      if (condition != function.values.end()) {
        const Module::Symbol symbol = condition->second.type.schema().symbol();
        if (symbol.module_name() != detail::prelude_module_name ||
            symbol.local_name() != "i1") {
          diagnostics.report("branch condition must have type i1");
          valid = false;
        }
      }
    }
    for (const std::uint64_t value : terminator.returned) {
      verify_use(value);
    }
    if (terminator.kind == Terminator::Kind::Return) {
      std::vector<Type> returned_types;
      returned_types.reserve(terminator.returned.size());
      for (const std::uint64_t value : terminator.returned) {
        const auto found = function.values.find(value);
        if (found != function.values.end()) {
          returned_types.push_back(found->second.type);
        }
      }
      const std::vector<Type>* expected = nullptr;
      if (function.signature) {
        expected = &function.signature->results;
      } else if (!inferred_results) {
        inferred_results = returned_types;
        expected = &*inferred_results;
      } else {
        expected = &*inferred_results;
      }
      if (returned_types != *expected) {
        diagnostics.report("function return types do not match its signature");
        valid = false;
      }
    }
    for (const detail::EdgeData& edge : terminator.successors) {
      const auto target = function.blocks.find(edge.target);
      if (target == function.blocks.end()) {
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
        const auto argument = function.values.find(edge.arguments[index]);
        const auto parameter =
            function.values.find(target->second.arguments[index]);
        if (argument != function.values.end() &&
            parameter != function.values.end() &&
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
  for (const std::uint64_t block : function.block_order) {
    if (!reachable.contains(block)) {
      diagnostics.report("function contains an unreachable block");
      valid = false;
    }
  }
  return valid;
}

template <typename Resolve>
bool verify_instruction_contracts(const FunctionState& function,
                                  Diagnostics& diagnostics, Resolve&& resolve) {
  bool valid = true;
  for (const std::uint64_t block_id : function.block_order) {
    for (const std::uint64_t instruction_id :
         function.blocks.at(block_id).instructions) {
      const InstructionData& instruction =
          function.instructions.at(instruction_id);
      const Module::FunctionDecl schema = instruction.schema;

      const auto& contract = detail::FunctionTypeAccess::get(schema);
      std::vector<Type> arguments;
      std::vector<std::optional<ParameterValue>> known_arguments;
      known_arguments.reserve(detail::parameter_inputs(schema).size());
      for (std::size_t index = 0; index < schema.inputs().size(); ++index) {
        if (!contract.ir_inputs[index]) {
          known_arguments.emplace_back();
        }
      }
      std::size_t known_index = 0;
      std::vector<std::size_t> known_indices(schema.inputs().size());
      for (std::size_t index = 0; index < schema.inputs().size(); ++index) {
        if (!contract.ir_inputs[index]) {
          known_indices[index] = known_index++;
        }
      }
      for (const detail::StoredArgument& stored : instruction.arguments) {
        if (stored.parameter >= schema.inputs().size()) {
          continue;
        }
        const detail::StoredValue& argument = stored.value;
        if (contract.ir_inputs[stored.parameter]) {
          if (argument.known) {
            arguments.push_back(argument.known->type);
          } else {
            const auto value = function.values.find(argument.id);
            if (value != function.values.end()) {
              arguments.push_back(value->second.type);
            }
          }
        } else if (argument.known) {
          known_arguments[known_indices[stored.parameter]] =
              argument.known->value;
        }
      }

      std::vector<std::optional<Type>> results;
      results.reserve(instruction.results.size());
      for (const std::uint64_t result : instruction.results) {
        results.push_back(function.values.at(result).type);
      }

      auto resolved = resolve(schema, arguments, known_arguments, results,
                              diagnostics, instruction.location);
      if (!resolved) {
        valid = false;
      }
    }
  }
  return valid;
}

bool verify_instruction_contracts(const FunctionState& function,
                                  Diagnostics& diagnostics) {
  std::vector<Module> modules;
  modules.reserve(function.modules.size());
  for (const auto& [name, module] : function.modules) {
    static_cast<void>(name);
    modules.push_back(module);
  }
  return verify_instruction_contracts(
      function, diagnostics,
      [&](const Module::FunctionDecl& schema, std::span<const Type> arguments,
          std::span<const std::optional<ParameterValue>> known_arguments,
          std::span<const std::optional<Type>> results, Diagnostics& reported,
          std::optional<SourceRange> location) {
        return resolve_call_types(modules, schema, arguments, known_arguments,
                                  results, reported, std::move(location));
      });
}

bool verify_instruction_contracts(const FunctionState& function,
                                  Compiler& compiler,
                                  Diagnostics& diagnostics) {
  return verify_instruction_contracts(
      function, diagnostics,
      [&](const Module::FunctionDecl& schema, std::span<const Type> arguments,
          std::span<const std::optional<ParameterValue>> known_arguments,
          std::span<const std::optional<Type>> results, Diagnostics& reported,
          std::optional<SourceRange> location) {
        return resolve_call_types(compiler, schema, arguments, known_arguments,
                                  results, reported, std::move(location));
      });
}

template <typename Handle>
void check_same_function(const std::shared_ptr<FunctionIdentity>& function,
                         const Handle& handle, std::string_view kind) {
  if (detail::FunctionAccess::owner(handle) != function || !handle.valid()) {
    throw std::invalid_argument(std::string(kind) +
                                " does not belong to this function edit");
  }
}

void check_same_function(const std::shared_ptr<FunctionIdentity>& function,
                         const Value& value, std::string_view kind) {
  const auto& known = detail::FunctionAccess::known(value);
  if (known) {
    if (!owns(*function->state, known->type) ||
        !owns(*function->state, known->value)) {
      throw std::invalid_argument(std::string(kind) +
                                  " is outside this function's module closure");
    }
    return;
  }
  if (detail::FunctionAccess::owner(value) != function || !value.valid()) {
    throw std::invalid_argument(std::string(kind) +
                                " does not belong to this function edit");
  }
}

}  // namespace

}  // namespace joggle::ir

namespace joggle::detail {

bool FunctionAccess::verify_structure(const ir::Function& function,
                                      Diagnostics& diagnostics) {
  return ir::verify_function(*function.function_->state, diagnostics);
}

bool FunctionAccess::verify_contracts(const ir::Function& function,
                                      Diagnostics& diagnostics) {
  return ir::verify_instruction_contracts(*function.function_->state,
                                          diagnostics);
}

bool FunctionAccess::verify_contracts(const ir::Function& function,
                                      Compiler& compiler,
                                      Diagnostics& diagnostics) {
  return ir::verify_instruction_contracts(*function.function_->state, compiler,
                                          diagnostics);
}

void FunctionAccess::declare(ir::Function& function,
                             Module::FunctionDecl declaration,
                             std::vector<Type> argument_types,
                             std::vector<Type> result_types) {
  auto& identity = function.function_;
  auto& state = *identity->state;
  if (identity->editing || state.signature) {
    throw std::logic_error("function signature is already fixed");
  }
  if (!state.arguments.empty() || state.blocks.size() != 1U ||
      !state.instructions.empty()) {
    throw std::logic_error("function signature must be fixed before its body");
  }
  if (!ir::owns(state, declaration.symbol()) ||
      ir_inputs(declaration).size() != argument_types.size() ||
      ir_results(declaration).size() != result_types.size()) {
    throw std::invalid_argument(
        "function signature does not match its declaration");
  }
  const bool owned_arguments =
      std::all_of(argument_types.begin(), argument_types.end(),
                  [&](const Type& type) { return ir::owns(state, type); });
  const bool owned_results =
      std::all_of(result_types.begin(), result_types.end(),
                  [&](const Type& type) { return ir::owns(state, type); });
  if (!owned_arguments || !owned_results) {
    throw std::invalid_argument(
        "function signature references a type outside its module closure");
  }
  state.signature = FunctionState::Signature{std::move(declaration),
                                             std::move(argument_types),
                                             std::move(result_types)};
}

bool FunctionAccess::commit(ir::Function::Edit& edit, Compiler& compiler,
                            Diagnostics& diagnostics) {
  if (!edit.state_ || !edit.state_->active) {
    throw std::logic_error("function edit is no longer active");
  }
  if (!ir::verify_function(*edit.state_->function->state, diagnostics) ||
      !ir::verify_instruction_contracts(*edit.state_->function->state, compiler,
                                        diagnostics)) {
    edit.state_->function->state = std::move(edit.state_->backup);
    edit.state_->function->editing = false;
    edit.state_->active = false;
    return false;
  }
  edit.state_->active = false;
  edit.state_->backup.reset();
  edit.state_->function->editing = false;
  return true;
}

}  // namespace joggle::detail

namespace joggle::ir {

Function::Revision::Revision(std::shared_ptr<const FunctionState> state)
    : state_(std::move(state)) {}

Value::Value(std::shared_ptr<FunctionIdentity> function, std::uint64_t id)
    : function_(std::move(function)), id_(id) {}

Value::Value(Type type, ParameterValue value)
    : known_(std::make_shared<const detail::KnownValueStorage>(
          detail::KnownValueStorage{std::move(type), std::move(value)})) {}

bool Value::valid() const {
  return known_ || (function_ && contains(function_->state->values, id_));
}

bool Value::known() const { return static_cast<bool>(known_); }

Type Value::type() const {
  if (known_) {
    return known_->type;
  }
  const auto found = function_->state->values.find(id_);
  if (found == function_->state->values.end()) {
    throw std::logic_error("value is no longer valid");
  }
  return found->second.type;
}

std::optional<ParameterValue> Value::known_value() const {
  return known_ ? std::optional<ParameterValue>{known_->value} : std::nullopt;
}

bool Value::operator==(const Value& other) const {
  if (known_ || other.known_) {
    return known_ && other.known_ && known_->type == other.known_->type &&
           known_->value == other.known_->value;
  }
  return function_ == other.function_ && id_ == other.id_;
}

bool Value::is_function_argument() const {
  if (!function_) {
    return false;
  }
  const auto found = function_->state->values.find(id_);
  return found != function_->state->values.end() &&
         found->second.origin == ValueData::Origin::FunctionArgument;
}

bool Value::is_block_argument() const {
  if (!function_) {
    return false;
  }
  const auto found = function_->state->values.find(id_);
  return found != function_->state->values.end() &&
         found->second.origin == ValueData::Origin::BlockArgument;
}

std::optional<Instruction> Value::defining_instruction() const {
  if (!function_) {
    return std::nullopt;
  }
  const auto found = function_->state->values.find(id_);
  if (found == function_->state->values.end() ||
      found->second.origin != ValueData::Origin::InstructionResult) {
    return std::nullopt;
  }
  return Instruction(function_, found->second.owner);
}

std::optional<Module::FunctionDecl> Value::referenced_function() const {
  if (!function_) {
    return std::nullopt;
  }
  const auto found = function_->state->values.find(id_);
  return found != function_->state->values.end() &&
                 found->second.origin == ValueData::Origin::FunctionReference
             ? found->second.reference
             : std::nullopt;
}

Instruction::Instruction(std::shared_ptr<FunctionIdentity> function,
                         std::uint64_t id)
    : function_(std::move(function)), id_(id) {}

bool Instruction::valid() const {
  return function_ && contains(function_->state->instructions, id_);
}

Module::FunctionDecl Instruction::callee() const {
  const auto found = function_->state->instructions.find(id_);
  if (found == function_->state->instructions.end()) {
    throw std::logic_error("instruction is no longer valid");
  }
  return found->second.schema;
}

Block Instruction::parent() const {
  const auto found = function_->state->instructions.find(id_);
  if (found == function_->state->instructions.end() ||
      !contains(function_->state->blocks, found->second.parent)) {
    throw std::logic_error("instruction has no valid parent block");
  }
  return Block(function_, found->second.parent);
}

std::vector<Value> Instruction::arguments() const {
  const auto found = function_->state->instructions.find(id_);
  if (found == function_->state->instructions.end()) {
    throw std::logic_error("instruction is no longer valid");
  }
  std::vector<Value> values;
  values.reserve(found->second.arguments.size());
  for (const detail::StoredArgument& argument : found->second.arguments) {
    const detail::StoredValue& value = argument.value;
    values.push_back(
        detail::FunctionAccess::restore(function_, value.id, value.known));
  }
  return values;
}

std::vector<Value> Instruction::results() const {
  const auto found = function_->state->instructions.find(id_);
  if (found == function_->state->instructions.end()) {
    throw std::logic_error("instruction is no longer valid");
  }
  std::vector<Value> values;
  values.reserve(found->second.results.size());
  for (std::uint64_t value : found->second.results) {
    values.push_back(Value(function_, value));
  }
  return values;
}

Value Instruction::value() const {
  const auto found = function_->state->instructions.find(id_);
  if (found == function_->state->instructions.end()) {
    throw std::logic_error("instruction is no longer valid");
  }
  if (found->second.results.size() != 1U) {
    throw std::logic_error("instruction does not have exactly one value");
  }
  return Value(function_, found->second.results.front());
}

Value Instruction::result(std::size_t index) const {
  const auto found = function_->state->instructions.find(id_);
  if (found == function_->state->instructions.end() ||
      index >= found->second.results.size()) {
    throw std::out_of_range("instruction result index is out of range");
  }
  return Value(function_, found->second.results[index]);
}

std::optional<Value> Instruction::argument(std::string_view name) const {
  const auto found = function_->state->instructions.find(id_);
  if (found == function_->state->instructions.end()) {
    return std::nullopt;
  }
  const auto parameters = found->second.schema.inputs();
  const auto parameter =
      std::find_if(parameters.begin(), parameters.end(),
                   [&](const Module::ParameterDecl& candidate) {
                     return candidate.name == name;
                   });
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
  return detail::FunctionAccess::restore(function_, argument->value.id,
                                         argument->value.known);
}

Terminator::Terminator(std::shared_ptr<FunctionIdentity> function,
                       std::uint64_t block)
    : function_(std::move(function)), block_(block) {}

bool Terminator::valid() const {
  if (!function_) {
    return false;
  }
  const auto block = function_->state->blocks.find(block_);
  return block != function_->state->blocks.end() &&
         block->second.terminator.has_value();
}

Terminator::Kind Terminator::kind() const {
  const auto block = function_->state->blocks.find(block_);
  if (block == function_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  return block->second.terminator->kind;
}

std::optional<Value> Terminator::condition() const {
  const auto block = function_->state->blocks.find(block_);
  if (block == function_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  const auto condition = block->second.terminator->condition;
  return condition ? std::optional<Value>{Value(function_, *condition)}
                   : std::nullopt;
}

std::vector<Value> Terminator::returned() const {
  const auto block = function_->state->blocks.find(block_);
  if (block == function_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  std::vector<Value> values;
  values.reserve(block->second.terminator->returned.size());
  for (const std::uint64_t value : block->second.terminator->returned) {
    values.push_back(Value(function_, value));
  }
  return values;
}

std::size_t Terminator::successor_count() const {
  const auto block = function_->state->blocks.find(block_);
  if (block == function_->state->blocks.end() || !block->second.terminator) {
    throw std::logic_error("terminator is no longer valid");
  }
  return block->second.terminator->successors.size();
}

Block Terminator::successor(std::size_t index) const {
  const auto block = function_->state->blocks.find(block_);
  if (block == function_->state->blocks.end() || !block->second.terminator ||
      index >= block->second.terminator->successors.size()) {
    throw std::out_of_range("terminator successor index is out of range");
  }
  return Block(function_, block->second.terminator->successors[index].target);
}

std::vector<Value> Terminator::arguments(std::size_t successor) const {
  const auto block = function_->state->blocks.find(block_);
  if (block == function_->state->blocks.end() || !block->second.terminator ||
      successor >= block->second.terminator->successors.size()) {
    throw std::out_of_range("terminator successor index is out of range");
  }
  std::vector<Value> values;
  const auto& arguments =
      block->second.terminator->successors[successor].arguments;
  values.reserve(arguments.size());
  for (const std::uint64_t value : arguments) {
    values.push_back(Value(function_, value));
  }
  return values;
}

Block::Block(std::shared_ptr<FunctionIdentity> function, std::uint64_t id)
    : function_(std::move(function)), id_(id) {}

bool Block::valid() const {
  return function_ && contains(function_->state->blocks, id_);
}

bool Block::is_entry() const {
  return valid() && function_->state->entry == id_;
}

std::vector<Value> Block::arguments() const {
  const auto found = function_->state->blocks.find(id_);
  if (found == function_->state->blocks.end()) {
    throw std::logic_error("block is no longer valid");
  }
  std::vector<Value> values;
  values.reserve(found->second.arguments.size());
  for (const std::uint64_t value : found->second.arguments) {
    values.push_back(Value(function_, value));
  }
  return values;
}

std::vector<Instruction> Block::instructions() const {
  const auto found = function_->state->blocks.find(id_);
  if (found == function_->state->blocks.end()) {
    throw std::logic_error("block is no longer valid");
  }
  std::vector<Instruction> instructions;
  instructions.reserve(found->second.instructions.size());
  for (const std::uint64_t instruction : found->second.instructions) {
    instructions.push_back(Instruction(function_, instruction));
  }
  return instructions;
}

Terminator Block::terminator() const {
  if (!valid()) {
    throw std::logic_error("block is no longer valid");
  }
  return Terminator(function_, id_);
}

Function::Edit::Edit(std::shared_ptr<FunctionIdentity> function)
    : state_(std::make_unique<detail::FunctionEditState>()) {
  if (function->editing) {
    throw std::logic_error("a function already has an active edit");
  }
  function->editing = true;
  state_->function = std::move(function);
  state_->backup = state_->function->state;
  state_->function->state = std::make_shared<FunctionState>(*state_->backup);
}

Function::Edit::~Edit() {
  if (state_ && state_->active) {
    state_->function->state = std::move(state_->backup);
    state_->function->editing = false;
  }
}

Function::Edit::Edit(Edit&&) noexcept = default;

Function::Edit& Function::Edit::operator=(Edit&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (state_ && state_->active) {
    state_->function->state = std::move(state_->backup);
    state_->function->editing = false;
  }
  state_ = std::move(other.state_);
  return *this;
}

Value Function::Edit::argument(Type type) {
  const std::uint64_t id = state_->function->next_id++;
  const std::size_t index = state_->function->state->arguments.size();
  state_->function->state->values.emplace(
      id, ValueData{std::move(type), ValueData::Origin::FunctionArgument, 0,
                    index, std::nullopt});
  state_->function->state->arguments.push_back(id);
  return Function::make_value(state_->function, id);
}

Value Function::Edit::reference(Module::FunctionDecl function, Type type) {
  if (!state_ || !state_->active) {
    throw std::logic_error("cannot edit an inactive function");
  }
  if (!owns(*state_->function->state, function.symbol()) ||
      !owns(*state_->function->state, type)) {
    throw std::invalid_argument(
        "referenced function is outside the module closure");
  }
  detail::ValueData data{std::move(type),
                         detail::ValueData::Origin::FunctionReference, 0, 0,
                         std::move(function)};
  if (!matches_function_reference(*state_->function->state, data)) {
    throw std::invalid_argument(
        "function reference type does not match its declaration");
  }
  const std::uint64_t id = state_->function->next_id++;
  state_->function->state->values.emplace(id, std::move(data));
  return Function::make_value(state_->function, id);
}

Block Function::Edit::block(std::vector<Type> argument_types) {
  const std::uint64_t block_id = state_->function->next_id++;
  detail::BlockData data;
  data.arguments.reserve(argument_types.size());
  for (std::size_t index = 0; index < argument_types.size(); ++index) {
    const std::uint64_t value_id = state_->function->next_id++;
    state_->function->state->values.emplace(
        value_id, ValueData{std::move(argument_types[index]),
                            ValueData::Origin::BlockArgument, block_id, index,
                            std::nullopt});
    data.arguments.push_back(value_id);
  }
  state_->function->state->blocks.emplace(block_id, std::move(data));
  state_->function->state->block_order.push_back(block_id);
  return Function::make_block(state_->function, block_id);
}

Instruction Function::Edit::append(Module::FunctionDecl schema,
                                   std::vector<Value> arguments,
                                   std::vector<Type> result_types) {
  return add(
      Function::make_block(state_->function, state_->function->state->entry),
      std::nullopt, std::move(schema), std::move(arguments),
      std::move(result_types));
}

Instruction Function::Edit::append(Block block, Module::FunctionDecl schema,
                                   std::vector<Value> arguments,
                                   std::vector<Type> result_types) {
  return add(std::move(block), std::nullopt, std::move(schema),
             std::move(arguments), std::move(result_types));
}

Instruction Function::Edit::insert(Instruction before,
                                   Module::FunctionDecl schema,
                                   std::vector<Value> arguments,
                                   std::vector<Type> result_types) {
  check_same_function(state_->function, before, "insertion point");
  return add(before.parent(), before, std::move(schema), std::move(arguments),
             std::move(result_types));
}

Instruction Function::Edit::add(Block block, std::optional<Instruction> before,
                                Module::FunctionDecl schema,
                                std::vector<Value> arguments,
                                std::vector<Type> result_types) {
  check_same_function(state_->function, block, "block");
  const std::uint64_t block_id = detail::FunctionAccess::id(block);
  const auto location = before ? state_->function->state->instructions
                                     .at(detail::FunctionAccess::id(*before))
                                     .location
                               : std::optional<SourceRange>{};
  const auto parameters = schema.inputs();
  const auto& contract = detail::FunctionTypeAccess::get(schema);
  std::vector<detail::StoredArgument> argument_ids;
  argument_ids.reserve(std::max(arguments.size(), parameters.size()));
  std::size_t supplied = 0;
  for (std::size_t parameter_index = 0; parameter_index < parameters.size();
       ++parameter_index) {
    const Module::ParameterDecl& parameter = parameters[parameter_index];
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
      auto known = value ? make_known(*state_->function->state, parameter,
                                      std::move(*value))
                         : nullptr;
      if (!known) {
        throw std::invalid_argument("cannot construct default argument '" +
                                    parameter.name + "' for instruction '" +
                                    schema.symbol().qualified_name() + "'");
      }
      argument_ids.push_back(
          {parameter_index, detail::StoredValue{0, std::move(known)}});
      continue;
    }
    if (supplied + count > arguments.size()) {
      throw std::invalid_argument(
          "instruction '" + schema.symbol().qualified_name() +
          "' is missing argument '" + parameter.name + "'");
    }
    for (std::size_t item = 0; item < count; ++item) {
      const Value& argument = arguments[supplied++];
      check_same_function(state_->function, argument, "argument");
      const auto& known = detail::FunctionAccess::known(argument);
      if (!contract.ir_inputs[parameter_index] &&
          (!known || !matches(parameter, known->value))) {
        throw std::invalid_argument("argument '" + parameter.name +
                                    "' of instruction '" +
                                    schema.symbol().qualified_name() +
                                    "' must be a compatible Known value");
      }
      argument_ids.push_back(
          {parameter_index,
           detail::StoredValue{detail::FunctionAccess::id(argument), known}});
    }
  }
  if (supplied != arguments.size()) {
    throw std::invalid_argument("instruction '" +
                                schema.symbol().qualified_name() +
                                "' has too many arguments");
  }

  if (result_types.empty() && !detail::ir_results(schema).empty()) {
    std::vector<Type> argument_types;
    std::vector<std::optional<Type>> expected(
        detail::ir_results(schema).size());
    std::vector<std::optional<ParameterValue>> inference_known;
    inference_known.reserve(detail::parameter_inputs(schema).size());
    std::size_t current_parameter = 0;
    for (std::size_t parameter_index = 0; parameter_index < parameters.size();
         ++parameter_index) {
      if (!contract.ir_inputs[parameter_index]) {
        const auto item =
            std::find_if(argument_ids.begin(), argument_ids.end(),
                         [&](const detail::StoredArgument& argument) {
                           return argument.parameter == parameter_index;
                         });
        inference_known.push_back(
            item == argument_ids.end() || !item->value.known
                ? std::optional<ParameterValue>{}
                : std::optional<ParameterValue>{item->value.known->value});
        continue;
      }
      while (current_parameter < argument_ids.size() &&
             argument_ids[current_parameter].parameter < parameter_index) {
        ++current_parameter;
      }
      while (current_parameter < argument_ids.size() &&
             argument_ids[current_parameter].parameter == parameter_index) {
        const detail::StoredValue& argument =
            argument_ids[current_parameter++].value;
        argument_types.push_back(
            argument.known
                ? argument.known->type
                : state_->function->state->values.at(argument.id).type);
      }
    }
    std::vector<Module> modules;
    modules.reserve(state_->function->state->modules.size());
    for (const auto& [name, module] : state_->function->state->modules) {
      static_cast<void>(name);
      modules.push_back(module);
    }
    Diagnostics diagnostics;
    auto inferred =
        detail::infer_call_types(modules, schema, argument_types,
                                 inference_known, expected, diagnostics);
    if (!inferred) {
      std::string message = "cannot infer results for instruction '" +
                            schema.symbol().qualified_name() + "'";
      if (!diagnostics.entries().empty()) {
        message += ": " + diagnostics.entries().front().message;
      }
      throw std::invalid_argument(std::move(message));
    }
    result_types = std::move(*inferred);
  }
  const std::uint64_t id = state_->function->next_id++;
  std::vector<std::uint64_t> results;
  results.reserve(result_types.size());
  for (std::size_t index = 0; index < result_types.size(); ++index) {
    const std::uint64_t result = state_->function->next_id++;
    state_->function->state->values.emplace(
        result, ValueData{std::move(result_types[index]),
                          ValueData::Origin::InstructionResult, id, index,
                          std::nullopt});
    results.push_back(result);
  }
  state_->function->state->instructions.emplace(
      id, InstructionData{std::move(schema), block_id, std::move(argument_ids),
                          std::move(results), location});
  auto& instruction_order =
      state_->function->state->blocks.at(block_id).instructions;
  if (before) {
    const std::uint64_t before_id = detail::FunctionAccess::id(*before);
    const auto position = std::find(instruction_order.begin(),
                                    instruction_order.end(), before_id);
    if (position == instruction_order.end()) {
      throw std::invalid_argument("insertion point is not in this function");
    }
    instruction_order.insert(position, id);
  } else {
    instruction_order.push_back(id);
  }
  return Function::make_instruction(state_->function, id);
}

void Function::Edit::ret(Block block, std::vector<Value> values) {
  check_same_function(state_->function, block, "return block");
  std::vector<std::uint64_t> ids;
  ids.reserve(values.size());
  for (const Value& value : values) {
    check_same_function(state_->function, value, "return value");
    if (value.known()) {
      throw std::invalid_argument(
          "a Known value must be materialized before it is returned");
    }
    ids.push_back(detail::FunctionAccess::id(value));
  }
  auto& terminator =
      state_->function->state->blocks.at(detail::FunctionAccess::id(block))
          .terminator;
  terminator = detail::TerminatorData{
      Terminator::Kind::Return, std::nullopt, std::move(ids), {}};
}

void Function::Edit::jump(Block block, Block target,
                          std::vector<Value> arguments) {
  check_same_function(state_->function, block, "jump block");
  check_same_function(state_->function, target, "jump target");
  std::vector<std::uint64_t> ids;
  ids.reserve(arguments.size());
  for (const Value& value : arguments) {
    check_same_function(state_->function, value, "jump argument");
    if (value.known()) {
      throw std::invalid_argument(
          "a Known value must be materialized before it crosses an edge");
    }
    ids.push_back(detail::FunctionAccess::id(value));
  }
  auto& terminator =
      state_->function->state->blocks.at(detail::FunctionAccess::id(block))
          .terminator;
  terminator = detail::TerminatorData{
      Terminator::Kind::Jump,
      std::nullopt,
      {},
      {{detail::FunctionAccess::id(target), std::move(ids)}}};
}

void Function::Edit::branch(Block block, Value condition, Block true_target,
                            std::vector<Value> true_arguments,
                            Block false_target,
                            std::vector<Value> false_arguments) {
  check_same_function(state_->function, block, "branch block");
  check_same_function(state_->function, condition, "branch condition");
  if (condition.known()) {
    throw std::invalid_argument(
        "a Known branch condition must be specialized before IR construction");
  }
  check_same_function(state_->function, true_target, "true target");
  check_same_function(state_->function, false_target, "false target");
  const auto ids = [&](std::span<const Value> values) {
    std::vector<std::uint64_t> result;
    result.reserve(values.size());
    for (const Value& value : values) {
      check_same_function(state_->function, value, "branch argument");
      if (value.known()) {
        throw std::invalid_argument(
            "a Known value must be materialized before it crosses an edge");
      }
      result.push_back(detail::FunctionAccess::id(value));
    }
    return result;
  };
  auto& terminator =
      state_->function->state->blocks.at(detail::FunctionAccess::id(block))
          .terminator;
  terminator = detail::TerminatorData{
      Terminator::Kind::Branch,
      detail::FunctionAccess::id(condition),
      {},
      {{detail::FunctionAccess::id(true_target), ids(true_arguments)},
       {detail::FunctionAccess::id(false_target), ids(false_arguments)}}};
}

void Function::Edit::replace(Value from, Value to) {
  check_same_function(state_->function, from, "source value");
  check_same_function(state_->function, to, "replacement value");
  if (from.known() || to.known()) {
    throw std::invalid_argument(
        "Known values are immutable and cannot cross a Function boundary");
  }
  if (from.type() != to.type()) {
    throw std::invalid_argument("replacement value has a different type");
  }
  const std::uint64_t from_id = detail::FunctionAccess::id(from);
  const std::uint64_t to_id = detail::FunctionAccess::id(to);
  for (auto& [id, instruction] : state_->function->state->instructions) {
    static_cast<void>(id);
    for (detail::StoredArgument& argument : instruction.arguments) {
      if (!argument.value.known && argument.value.id == from_id) {
        argument.value.id = to_id;
      }
    }
  }
  for (auto& [id, block] : state_->function->state->blocks) {
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

Instruction Function::Edit::replace(Instruction instruction,
                                    Module::FunctionDecl schema) {
  check_same_function(state_->function, instruction, "instruction");
  std::vector<Type> result_types;
  result_types.reserve(instruction.results().size());
  for (const Value& result : instruction.results()) {
    result_types.push_back(result.type());
  }
  const Instruction replacement = insert(instruction, std::move(schema),
                                         instruction.arguments(), result_types);
  for (std::size_t index = 0; index < result_types.size(); ++index) {
    replace(instruction.result(index), replacement.result(index));
  }
  erase(instruction);
  return replacement;
}

void Function::Edit::replace(Instruction instruction,
                             std::vector<Value> results) {
  check_same_function(state_->function, instruction, "instruction");
  const auto previous = instruction.results();
  if (previous.size() != results.size()) {
    throw std::invalid_argument(
        "replacement result count does not match the instruction");
  }
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (results[index].known()) {
      throw std::invalid_argument(
          "a Known value cannot replace an Instruction result");
    }
    check_same_function(state_->function, results[index], "replacement value");
    if (previous[index].type() != results[index].type()) {
      throw std::invalid_argument(
          "replacement result has a different type at position " +
          std::to_string(index));
    }
  }
  for (std::size_t index = 0; index < results.size(); ++index) {
    replace(previous[index], results[index]);
  }
  erase(instruction);
}

void Function::Edit::erase(Instruction instruction) {
  check_same_function(state_->function, instruction, "instruction");
  auto& state = *state_->function->state;
  const std::uint64_t instruction_id = detail::FunctionAccess::id(instruction);
  const auto found = state.instructions.find(instruction_id);
  std::unordered_set<std::uint64_t> values;
  values.insert(found->second.results.begin(), found->second.results.end());
  for (const auto& [id, user] : state.instructions) {
    if (id == instruction_id) {
      continue;
    }
    if (std::any_of(user.arguments.begin(), user.arguments.end(),
                    [&](const detail::StoredArgument& argument) {
                      return !argument.value.known &&
                             values.contains(argument.value.id);
                    })) {
      throw std::invalid_argument("instruction still has live result uses");
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
      throw std::invalid_argument(
          "instruction still has a live terminator use");
    }
  }
  auto& instructions = state.blocks.at(found->second.parent).instructions;
  instructions.erase(
      std::remove(instructions.begin(), instructions.end(), instruction_id),
      instructions.end());
  for (const std::uint64_t value : values) {
    state.values.erase(value);
  }
  state.instructions.erase(instruction_id);
}

bool Function::Edit::commit(Diagnostics& diagnostics) {
  if (!state_ || !state_->active) {
    throw std::logic_error("function edit is no longer active");
  }
  if (!verify_function(*state_->function->state, diagnostics) ||
      !verify_instruction_contracts(*state_->function->state, diagnostics)) {
    state_->function->state = std::move(state_->backup);
    state_->function->editing = false;
    state_->active = false;
    return false;
  }
  state_->active = false;
  state_->backup.reset();
  state_->function->editing = false;
  return true;
}

Function::Function(std::vector<Module> modules)
    : function_(std::make_shared<FunctionIdentity>()) {
  function_->state = std::make_shared<FunctionState>();
  for (Module& module : modules) {
    function_->state->modules.emplace(std::string(module.name()),
                                      std::move(module));
  }
  function_->state->entry = function_->next_id++;
  detail::BlockData entry;
  entry.terminator = detail::TerminatorData{};
  function_->state->blocks.emplace(function_->state->entry, std::move(entry));
  function_->state->block_order.push_back(function_->state->entry);
}

Function::~Function() = default;

Function::Function(const Function& other)
    : function_(std::make_shared<FunctionIdentity>()) {
  if (!other.function_ || other.function_->editing) {
    throw std::logic_error(
        "cannot copy a moved-from function or one with an active edit");
  }
  function_->state = other.function_->state;
  function_->next_id = other.function_->next_id;
}

Function& Function::operator=(const Function& other) {
  if (this == &other) {
    return *this;
  }
  if (!other.function_ || (function_ && function_->editing) ||
      other.function_->editing) {
    throw std::logic_error(
        "cannot copy a moved-from function or one with an active edit");
  }
  auto identity = std::make_shared<FunctionIdentity>();
  identity->state = other.function_->state;
  identity->next_id = other.function_->next_id;
  function_ = std::move(identity);
  return *this;
}

Function::Function(Function&&) noexcept = default;
Function& Function::operator=(Function&&) noexcept = default;

std::vector<Value> Function::arguments() const {
  std::vector<Value> result;
  result.reserve(function_->state->arguments.size());
  for (const std::uint64_t value : function_->state->arguments) {
    result.push_back(make_value(function_, value));
  }
  return result;
}

std::optional<Module::FunctionDecl> Function::declaration() const {
  return function_->state->signature
             ? std::optional<Module::FunctionDecl>{function_->state->signature
                                                       ->declaration}
             : std::nullopt;
}

std::vector<Type> Function::result_types() const {
  if (function_->state->signature) {
    return function_->state->signature->results;
  }
  for (const std::uint64_t block_id : function_->state->block_order) {
    const auto& block = function_->state->blocks.at(block_id);
    if (!block.terminator ||
        block.terminator->kind != Terminator::Kind::Return) {
      continue;
    }
    std::vector<Type> result;
    result.reserve(block.terminator->returned.size());
    for (const std::uint64_t value : block.terminator->returned) {
      result.push_back(function_->state->values.at(value).type);
    }
    return result;
  }
  return {};
}

Block Function::entry() const {
  return make_block(function_, function_->state->entry);
}

std::vector<Block> Function::blocks() const {
  std::vector<Block> result;
  result.reserve(function_->state->block_order.size());
  for (const std::uint64_t block : function_->state->block_order) {
    result.push_back(make_block(function_, block));
  }
  return result;
}

std::vector<Instruction> Function::instructions() const {
  std::vector<Instruction> result;
  result.reserve(function_->state->instructions.size());
  for (const std::uint64_t block : function_->state->block_order) {
    for (const std::uint64_t instruction :
         function_->state->blocks.at(block).instructions) {
      result.push_back(make_instruction(function_, instruction));
    }
  }
  return result;
}

std::vector<Block> Function::predecessors(Block block) const {
  if (block.function_ != function_ || !block.valid()) {
    throw std::invalid_argument("predecessor query block is outside function");
  }
  std::vector<Block> result;
  for (const std::uint64_t candidate : function_->state->block_order) {
    const auto& data = function_->state->blocks.at(candidate);
    if (!data.terminator) {
      continue;
    }
    const bool reaches = std::any_of(
        data.terminator->successors.begin(), data.terminator->successors.end(),
        [&](const detail::EdgeData& edge) { return edge.target == block.id_; });
    if (reaches) {
      result.push_back(make_block(function_, candidate));
    }
  }
  return result;
}

std::vector<Instruction> Function::users(Value value) const {
  if (!value.valid() || (!value.known() && value.function_ != function_) ||
      (value.known() &&
       (!owns(*function_->state, ParameterValue(value.type())) ||
        !owns(*function_->state, *value.known_value())))) {
    throw std::invalid_argument("use query value is outside function");
  }
  std::vector<Instruction> result;
  for (const std::uint64_t block : function_->state->block_order) {
    for (const std::uint64_t instruction_id :
         function_->state->blocks.at(block).instructions) {
      const auto& instruction =
          function_->state->instructions.at(instruction_id);
      const bool consumes = std::any_of(
          instruction.arguments.begin(), instruction.arguments.end(),
          [&](const detail::StoredArgument& argument) {
            return detail::FunctionAccess::restore(function_, argument.value.id,
                                                   argument.value.known) ==
                   value;
          });
      if (consumes) {
        result.push_back(make_instruction(function_, instruction_id));
      }
    }
  }
  return result;
}

bool Function::has_uses(Value value) const {
  if (!users(value).empty()) {
    return true;
  }
  if (value.known()) {
    return false;
  }
  for (const std::uint64_t block : function_->state->block_order) {
    const auto& terminator = function_->state->blocks.at(block).terminator;
    if (!terminator) {
      continue;
    }
    if ((terminator->condition && *terminator->condition == value.id_) ||
        std::find(terminator->returned.begin(), terminator->returned.end(),
                  value.id_) != terminator->returned.end()) {
      return true;
    }
    for (const auto& edge : terminator->successors) {
      if (std::find(edge.arguments.begin(), edge.arguments.end(), value.id_) !=
          edge.arguments.end()) {
        return true;
      }
    }
  }
  return false;
}

bool Function::dominates(Block dominator, Block block) const {
  if (dominator.function_ != function_ || block.function_ != function_ ||
      !dominator.valid() || !block.valid()) {
    throw std::invalid_argument("dominance query block is outside function");
  }
  const auto relation = dominators(*function_->state);
  return relation.at(block.id_).contains(dominator.id_);
}

bool Function::dominates(Value definition, Instruction instruction) const {
  if (!definition.valid() || !instruction.valid() ||
      instruction.function_ != function_ ||
      (!definition.known() && definition.function_ != function_)) {
    throw std::invalid_argument("dominance query value is outside function");
  }
  if (definition.known()) {
    return owns(*function_->state, ParameterValue(definition.type())) &&
           owns(*function_->state, *definition.known_value());
  }
  const auto found = function_->state->values.find(definition.id_);
  const auto user = function_->state->instructions.find(instruction.id_);
  if (found == function_->state->values.end() ||
      user == function_->state->instructions.end()) {
    return false;
  }
  const auto relation = dominators(*function_->state);
  return definition_dominates(*function_->state, found->second,
                              user->second.parent, instruction.id_, relation);
}

Function::Revision Function::revision() const {
  return Revision(function_->state);
}

Function::Edit Function::edit() { return Edit(function_); }

bool Function::accepts(const Module::Symbol& symbol) const {
  return owns(*function_->state, symbol);
}

Value Function::make_value(std::shared_ptr<FunctionIdentity> function,
                           std::uint64_t id) {
  return Value(std::move(function), id);
}

Instruction
Function::make_instruction(std::shared_ptr<FunctionIdentity> function,
                           std::uint64_t id) {
  return Instruction(std::move(function), id);
}

Block Function::make_block(std::shared_ptr<FunctionIdentity> function,
                           std::uint64_t id) {
  return Block(std::move(function), id);
}

}  // namespace joggle::ir
