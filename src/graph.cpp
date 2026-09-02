#include "joggle/graph.h"

#include "graph_internal.h"
#include "type_contract.h"
#include "type_internal.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace joggle::detail {

struct ValueData {
  enum class Origin { FunctionArgument, OperationResult };

  Type type;
  Origin origin = Origin::FunctionArgument;
  std::uint64_t owner = 0;
  std::size_t index = 0;
};

struct OperationData {
  Module::FunctionDecl schema;
  std::vector<std::uint64_t> operands;
  std::vector<std::uint64_t> results;
  std::map<std::string, ParameterValue, std::less<>> properties;
  std::optional<SourceRange> location;
};

struct GraphState {
  std::map<std::string, Module, std::less<>> modules;
  std::unordered_map<std::uint64_t, ValueData> values;
  std::unordered_map<std::uint64_t, OperationData> operations;
  std::vector<std::uint64_t> inputs;
  std::vector<std::uint64_t> operation_order;
  std::vector<std::uint64_t> outputs;
};

struct GraphIdentity {
  std::shared_ptr<GraphState> state;
  std::uint64_t next_id = 1;
  bool editing = false;
};

struct GraphEditState {
  std::shared_ptr<GraphIdentity> graph;
  std::shared_ptr<GraphState> backup;
  bool active = true;
};

const std::shared_ptr<GraphIdentity>& GraphAccess::graph(const Value& value) {
  return value.graph_;
}

const std::shared_ptr<GraphIdentity>&
GraphAccess::graph(const Operation& operation) {
  return operation.graph_;
}

std::uint64_t GraphAccess::id(const Value& value) { return value.id_; }
std::uint64_t GraphAccess::id(const Operation& operation) {
  return operation.id_;
}
std::string PropertyAccess::take_name(Property& property) {
  return std::move(property.name_);
}

ParameterValue PropertyAccess::take_value(Property& property) {
  return std::move(property.value_);
}

void GraphAccess::locate(Graph::Edit& edit, const Operation& operation,
                         SourceRange source) {
  if (operation.graph_ != edit.state_->graph || !operation.valid()) {
    throw std::invalid_argument("operation does not belong to this graph edit");
  }
  edit.state_->graph->state->operations.at(id(operation)).location =
      std::move(source);
}

std::optional<SourceRange> GraphAccess::location(const Operation& operation) {
  if (!operation.valid()) {
    return std::nullopt;
  }
  return operation.graph_->state->operations.at(operation.id_).location;
}

}  // namespace joggle::detail

namespace joggle {

struct Graph::Snapshot {
  std::shared_ptr<detail::GraphState> state;
};

namespace {

using detail::GraphIdentity;
using detail::GraphState;
using detail::OperationData;
using detail::ParameterValue;
using detail::ValueData;

template <typename Map> bool contains(const Map& map, std::uint64_t id) {
  return map.find(id) != map.end();
}

bool owns(const GraphState& graph, const Module::Symbol& symbol) {
  const auto module = graph.modules.find(symbol.module_name());
  return module != graph.modules.end() &&
         module->second.version() == symbol.module_version() &&
         module->second.digest() == symbol.module_digest();
}

bool owns(const GraphState& graph, const ParameterValue& value);

bool owns(const GraphState& graph, const Type& type) {
  if (!owns(graph, type.schema().symbol())) {
    return false;
  }
  const auto parameters = detail::TypeAccess::parameters(type);
  return std::all_of(
      parameters.begin(), parameters.end(),
      [&](const ParameterValue& value) { return owns(graph, value); });
}

bool owns(const GraphState& graph, const Attribute& attribute) {
  const auto parameters = detail::TypeAccess::parameters(attribute);
  return owns(graph, attribute.schema().symbol()) &&
         std::all_of(
             parameters.begin(), parameters.end(),
             [&](const ParameterValue& value) { return owns(graph, value); });
}

bool owns(const GraphState& graph, const ParameterValue& value) {
  if (const Type* type = value.as_type()) {
    return owns(graph, *type);
  }
  if (const Attribute* attribute = value.as_attribute()) {
    return owns(graph, *attribute);
  }
  if (value.kind() == ParameterValue::Kind::List) {
    return std::all_of(
        value.elements().begin(), value.elements().end(),
        [&](const ParameterValue& element) { return owns(graph, element); });
  }
  return true;
}

bool matches(const Module::ParameterDecl& schema, const ParameterValue& value) {
  return detail::matches_parameter(schema, value);
}

bool accepts_count(
    std::span<const Module::ParameterDecl> parameters,
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

std::optional<std::size_t> operation_position(const GraphState& graph,
                                              std::uint64_t operation) {
  const auto found = std::find(graph.operation_order.begin(),
                               graph.operation_order.end(), operation);
  if (found == graph.operation_order.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(
      std::distance(graph.operation_order.begin(), found));
}

bool dominates(const GraphState& graph, const ValueData& definition,
               const OperationData&, std::uint64_t user_id) {
  if (definition.origin == ValueData::Origin::FunctionArgument) {
    return definition.owner == 0 && definition.index < graph.inputs.size();
  }
  if (!contains(graph.operations, definition.owner)) {
    return false;
  }
  const auto producer_position = operation_position(graph, definition.owner);
  const auto user_position = operation_position(graph, user_id);
  return producer_position && user_position &&
         *producer_position < *user_position;
}

bool verify_operation(const GraphState& graph, std::uint64_t id,
                      const OperationData& operation,
                      Diagnostics& diagnostics) {
  bool valid = true;
  const std::string name(operation.schema.symbol().qualified_name());
  if (!owns(graph, operation.schema.symbol())) {
    diagnostics.report("operation '" + name +
                       "' is outside the graph's module closure");
    valid = false;
  }
  if (!accepts_count(operation.schema.value_inputs(), operation.operands.size())) {
    diagnostics.report("operation '" + name +
                       "' has the wrong number of operands");
    valid = false;
  }
  if (!accepts_count(operation.schema.value_results(), operation.results.size())) {
    diagnostics.report("operation '" + name +
                       "' has the wrong number of results");
    valid = false;
  }

  for (const Module::ParameterDecl& parameter :
       operation.schema.static_inputs()) {
    const auto property = operation.properties.find(parameter.name);
    if (property == operation.properties.end()) {
      if (!parameter.default_value) {
        diagnostics.report("operation '" + name + "' is missing property '" +
                           parameter.name + "'");
        valid = false;
      }
    } else if (!matches(parameter, property->second)) {
      diagnostics.report("property '" + parameter.name + "' of operation '" +
                         name + "' has the wrong kind");
      valid = false;
    }
  }
  const auto static_inputs = operation.schema.static_inputs();
  for (const auto& [property_name, value] : operation.properties) {
    const auto parameter = std::find_if(
        static_inputs.begin(), static_inputs.end(),
        [&](const Module::ParameterDecl& item) {
          return item.name == property_name;
        });
    if (parameter == static_inputs.end()) {
      diagnostics.report("operation '" + name + "' has unknown property '" +
                         property_name + "'");
      valid = false;
    }
    if (!owns(graph, value)) {
      diagnostics.report("property '" + property_name + "' of operation '" +
                         name +
                         "' references a value outside the module "
                         "closure");
      valid = false;
    }
  }
  for (std::size_t index = 0; index < operation.operands.size(); ++index) {
    const auto value = graph.values.find(operation.operands[index]);
    if (value == graph.values.end()) {
      diagnostics.report("operation '" + name + "' has an invalid operand");
      valid = false;
    } else if (!dominates(graph, value->second, operation, id)) {
      diagnostics.report("operand " + std::to_string(index) +
                         " of operation '" + name +
                         "' is not dominated by its definition");
      valid = false;
    }
  }
  for (std::uint64_t result : operation.results) {
    const auto value = graph.values.find(result);
    if (value == graph.values.end() ||
        value->second.origin != ValueData::Origin::OperationResult ||
        value->second.owner != id || !owns(graph, value->second.type)) {
      diagnostics.report("operation '" + name + "' has an invalid result");
      valid = false;
    }
  }
  return valid;
}

bool verify_graph(const GraphState& graph, Diagnostics& diagnostics) {
  bool valid = true;
  std::unordered_set<std::uint64_t> listed_operations;
  for (std::size_t index = 0; index < graph.inputs.size(); ++index) {
    const auto value = graph.values.find(graph.inputs[index]);
    if (value == graph.values.end() ||
        value->second.origin != ValueData::Origin::FunctionArgument ||
        value->second.owner != 0 || value->second.index != index ||
        !owns(graph, value->second.type)) {
      diagnostics.report("function has an invalid argument");
      valid = false;
    }
  }
  for (const std::uint64_t id : graph.operation_order) {
    if (!contains(graph.operations, id) || !listed_operations.insert(id).second) {
      diagnostics.report("function has an invalid operation order");
      valid = false;
    }
  }
  for (const auto& [id, operation] : graph.operations) {
    if (!listed_operations.contains(id)) {
      diagnostics.report("function contains an unordered operation");
      valid = false;
    }
    valid = verify_operation(graph, id, operation, diagnostics) && valid;
  }
  for (std::uint64_t output : graph.outputs) {
    const auto value = graph.values.find(output);
    if (value == graph.values.end()) {
      diagnostics.report("graph has an invalid output");
      valid = false;
      continue;
    }
    const bool available =
        (value->second.origin == ValueData::Origin::FunctionArgument &&
         value->second.owner == 0) ||
        (value->second.origin == ValueData::Origin::OperationResult &&
         contains(graph.operations, value->second.owner));
    if (!available) {
      diagnostics.report("graph output is not defined in the graph body");
      valid = false;
    }
  }
  return valid;
}

template <typename Resolve>
bool verify_operation_contracts(const GraphState& graph,
                                Diagnostics& diagnostics,
                                Resolve&& resolve) {
  bool valid = true;
  for (const std::uint64_t operation_id : graph.operation_order) {
      const OperationData& operation = graph.operations.at(operation_id);
      const Module::FunctionDecl schema = operation.schema;

      std::vector<Type> operands;
      operands.reserve(operation.operands.size());
      for (const std::uint64_t operand : operation.operands) {
        operands.push_back(graph.values.at(operand).type);
      }

      std::vector<std::optional<Type>> results;
      results.reserve(operation.results.size());
      for (const std::uint64_t result : operation.results) {
        results.push_back(graph.values.at(result).type);
      }

      std::vector<std::optional<ParameterValue>> properties;
      properties.reserve(schema.static_inputs().size());
      for (const Module::ParameterDecl& input : schema.static_inputs()) {
        const auto value = operation.properties.find(input.name);
        properties.push_back(value == operation.properties.end()
                                 ? std::optional<ParameterValue>{}
                                 : value->second);
      }

      auto resolved = resolve(schema, operands, properties, results,
                              diagnostics, operation.location);
      if (!resolved) {
        valid = false;
      }
  }
  return valid;
}

bool verify_operation_contracts(const GraphState& graph,
                                Diagnostics& diagnostics) {
  std::vector<Module> modules;
  modules.reserve(graph.modules.size());
  for (const auto& [name, module] : graph.modules) {
    static_cast<void>(name);
    modules.push_back(module);
  }
  return verify_operation_contracts(
      graph, diagnostics,
      [&](const Module::FunctionDecl& schema, std::span<const Type> operands,
          std::span<const std::optional<ParameterValue>> properties,
          std::span<const std::optional<Type>> results,
          Diagnostics& reported, std::optional<SourceRange> location) {
        return resolve_operation_types(modules, schema, operands, properties,
                                       results, reported, std::move(location));
      });
}

bool verify_operation_contracts(const GraphState& graph, Compiler& compiler,
                                Diagnostics& diagnostics) {
  return verify_operation_contracts(
      graph, diagnostics,
      [&](const Module::FunctionDecl& schema, std::span<const Type> operands,
          std::span<const std::optional<ParameterValue>> properties,
          std::span<const std::optional<Type>> results,
          Diagnostics& reported, std::optional<SourceRange> location) {
        return resolve_operation_types(compiler, schema, operands, properties,
                                       results, reported, std::move(location));
      });
}

template <typename Handle>
void check_same_graph(const std::shared_ptr<GraphIdentity>& graph,
                      const Handle& handle, std::string_view kind) {
  if (detail::GraphAccess::graph(handle) != graph || !handle.valid()) {
    throw std::invalid_argument(std::string(kind) +
                                " does not belong to this graph edit");
  }
}

}  // namespace

bool detail::GraphAccess::verify_structure(const Graph& graph,
                                           Diagnostics& diagnostics) {
  return verify_graph(*graph.graph_->state, diagnostics);
}

bool detail::GraphAccess::verify_contracts(const Graph& graph,
                                           Diagnostics& diagnostics) {
  return verify_operation_contracts(*graph.graph_->state, diagnostics);
}

bool detail::GraphAccess::verify_contracts(const Graph& graph,
                                           Compiler& compiler,
                                           Diagnostics& diagnostics) {
  return verify_operation_contracts(*graph.graph_->state, compiler,
                                    diagnostics);
}

bool detail::GraphAccess::commit(Graph::Edit& edit, Compiler& compiler,
                                 Diagnostics& diagnostics) {
  if (!edit.state_ || !edit.state_->active) {
    throw std::logic_error("graph edit is no longer active");
  }
  if (!verify_graph(*edit.state_->graph->state, diagnostics) ||
      !verify_operation_contracts(*edit.state_->graph->state, compiler,
                                  diagnostics)) {
    edit.state_->graph->state = std::move(edit.state_->backup);
    edit.state_->graph->editing = false;
    edit.state_->active = false;
    return false;
  }
  edit.state_->active = false;
  edit.state_->backup.reset();
  edit.state_->graph->editing = false;
  return true;
}

Value::Value(std::shared_ptr<GraphIdentity> graph, std::uint64_t id)
    : graph_(std::move(graph)), id_(id) {}

bool Value::valid() const {
  return graph_ && contains(graph_->state->values, id_);
}

Type Value::type() const {
  const auto found = graph_->state->values.find(id_);
  if (found == graph_->state->values.end()) {
    throw std::logic_error("value is no longer valid");
  }
  return found->second.type;
}

bool Value::is_argument() const {
  const auto found = graph_->state->values.find(id_);
  return found != graph_->state->values.end() &&
         found->second.origin == ValueData::Origin::FunctionArgument;
}

std::optional<Operation> Value::defining_operation() const {
  const auto found = graph_->state->values.find(id_);
  if (found == graph_->state->values.end() ||
      found->second.origin != ValueData::Origin::OperationResult) {
    return std::nullopt;
  }
  return Operation(graph_, found->second.owner);
}

Operation::Operation(std::shared_ptr<GraphIdentity> graph, std::uint64_t id)
    : graph_(std::move(graph)), id_(id) {}

bool Operation::valid() const {
  return graph_ && contains(graph_->state->operations, id_);
}

Module::FunctionDecl Operation::schema() const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end()) {
    throw std::logic_error("operation is no longer valid");
  }
  return found->second.schema;
}

std::vector<Value> Operation::operands() const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end()) {
    throw std::logic_error("operation is no longer valid");
  }
  std::vector<Value> values;
  values.reserve(found->second.operands.size());
  for (std::uint64_t value : found->second.operands) {
    values.push_back(Value(graph_, value));
  }
  return values;
}

std::vector<Value> Operation::results() const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end()) {
    throw std::logic_error("operation is no longer valid");
  }
  std::vector<Value> values;
  values.reserve(found->second.results.size());
  for (std::uint64_t value : found->second.results) {
    values.push_back(Value(graph_, value));
  }
  return values;
}

Value Operation::value() const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end()) {
    throw std::logic_error("operation is no longer valid");
  }
  if (found->second.results.size() != 1U) {
    throw std::logic_error("operation does not have exactly one value");
  }
  return Value(graph_, found->second.results.front());
}

Value Operation::result(std::size_t index) const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end() ||
      index >= found->second.results.size()) {
    throw std::out_of_range("operation result index is out of range");
  }
  return Value(graph_, found->second.results[index]);
}

std::optional<ParameterValue> Operation::property(std::string_view name) const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end()) {
    return std::nullopt;
  }
  const auto property = found->second.properties.find(name);
  return property == found->second.properties.end()
             ? std::nullopt
             : std::optional<ParameterValue>{property->second};
}

std::optional<ParameterValue>
detail::GraphAccess::property(const Operation& operation,
                              std::string_view name) {
  return operation.property(name);
}

Graph::Edit::Edit(std::shared_ptr<GraphIdentity> graph)
    : state_(std::make_unique<detail::GraphEditState>()) {
  if (graph->editing) {
    throw std::logic_error("a graph already has an active edit");
  }
  graph->editing = true;
  state_->graph = std::move(graph);
  state_->backup = state_->graph->state;
  state_->graph->state = std::make_shared<GraphState>(*state_->backup);
}

Graph::Edit::~Edit() {
  if (state_ && state_->active) {
    state_->graph->state = std::move(state_->backup);
    state_->graph->editing = false;
  }
}

Graph::Edit::Edit(Edit&&) noexcept = default;

Graph::Edit& Graph::Edit::operator=(Edit&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (state_ && state_->active) {
    state_->graph->state = std::move(state_->backup);
    state_->graph->editing = false;
  }
  state_ = std::move(other.state_);
  return *this;
}

Value Graph::Edit::argument(Type type) {
  const std::uint64_t id = state_->graph->next_id++;
  const std::size_t index = state_->graph->state->inputs.size();
  state_->graph->state->values.emplace(
      id, ValueData{std::move(type), ValueData::Origin::FunctionArgument,
                    0, index});
  state_->graph->state->inputs.push_back(id);
  return Graph::make_value(state_->graph, id);
}

Operation Graph::Edit::append(Module::FunctionDecl schema,
                              std::vector<Value> operands,
                              std::vector<Type> result_types) {
  return append_with_properties(std::move(schema), std::move(operands),
                                std::move(result_types), {});
}

Operation Graph::Edit::append_with_properties(
    Module::FunctionDecl schema, std::vector<Value> operands,
    std::vector<Type> result_types, std::vector<Property> properties) {
  return add(std::nullopt, std::move(schema), std::move(operands),
             std::move(result_types),
             std::move(properties));
}

Operation Graph::Edit::insert(Operation before, Module::FunctionDecl schema,
                              std::vector<Value> operands,
                              std::vector<Type> result_types) {
  return insert_with_properties(std::move(before), std::move(schema),
                                std::move(operands), std::move(result_types), {});
}

Operation Graph::Edit::insert_with_properties(
    Operation before, Module::FunctionDecl schema, std::vector<Value> operands,
    std::vector<Type> result_types, std::vector<Property> properties) {
  check_same_graph(state_->graph, before, "insertion point");
  return add(before, std::move(schema), std::move(operands),
             std::move(result_types), std::move(properties));
}

Operation Graph::Edit::add(std::optional<Operation> before,
                           Module::FunctionDecl schema,
                           std::vector<Value> operands,
                           std::vector<Type> result_types,
                           std::vector<Property> arguments) {
  const auto location = before ? state_->graph->state->operations
                                     .at(detail::GraphAccess::id(*before))
                                     .location
                               : std::optional<SourceRange>{};
  std::vector<std::uint64_t> operand_ids;
  operand_ids.reserve(operands.size());
  for (const Value& operand : operands) {
    check_same_graph(state_->graph, operand, "operand");
    operand_ids.push_back(detail::GraphAccess::id(operand));
  }

  std::map<std::string, ParameterValue, std::less<>> properties;
  for (const Module::ParameterDecl& parameter : schema.static_inputs()) {
    if (parameter.default_value) {
      if (const auto value = detail::parameter_default(parameter)) {
        properties.emplace(parameter.name, *value);
      }
    }
  }
  std::unordered_set<std::string> explicit_properties;
  const auto static_inputs = schema.static_inputs();
  for (Property& argument : arguments) {
    std::string name = detail::PropertyAccess::take_name(argument);
    ParameterValue value = detail::PropertyAccess::take_value(argument);
    const auto parameter = std::find_if(
        static_inputs.begin(), static_inputs.end(),
        [&](const Module::ParameterDecl& input) {
          return input.name == name;
        });
    if (parameter == static_inputs.end()) {
      throw std::invalid_argument("operation '" +
                                  schema.symbol().qualified_name() +
                                  "' has no property named '" + name +
                                  "'");
    }
    if (!explicit_properties.insert(name).second) {
      throw std::invalid_argument("operation property '" + name +
                                  "' was provided more than once");
    }
    if (!matches(*parameter, value)) {
      throw std::invalid_argument("operation property '" + name +
                                  "' has the wrong kind");
    }
    if (!owns(*state_->graph->state, value)) {
      throw std::invalid_argument("operation property '" + name +
                                  "' references a value outside this graph");
    }
    properties.insert_or_assign(std::move(name), std::move(value));
  }

  if (result_types.empty() && !schema.value_results().empty()) {
    std::vector<Type> operand_types;
    operand_types.reserve(operands.size());
    for (const Value& operand : operands) {
      operand_types.push_back(operand.type());
    }
    std::vector<std::optional<Type>> expected(schema.value_results().size());
    std::vector<std::optional<ParameterValue>> inference_properties;
    inference_properties.reserve(schema.static_inputs().size());
    for (const Module::ParameterDecl& parameter : schema.static_inputs()) {
      const auto property_value = properties.find(parameter.name);
      inference_properties.push_back(property_value == properties.end()
                                         ? std::optional<ParameterValue>{}
                                         : property_value->second);
    }
    std::vector<Module> modules;
    modules.reserve(state_->graph->state->modules.size());
    for (const auto& [name, module] : state_->graph->state->modules) {
      static_cast<void>(name);
      modules.push_back(module);
    }
    Diagnostics diagnostics;
    auto inferred = detail::infer_operation_types(
        modules, schema, operand_types, inference_properties, expected,
        diagnostics);
    if (!inferred) {
      std::string message = "cannot infer results for operation '" +
                            schema.symbol().qualified_name() + "'";
      if (!diagnostics.entries().empty()) {
        message += ": " + diagnostics.entries().front().message;
      }
      throw std::invalid_argument(std::move(message));
    }
    result_types = std::move(*inferred);
  }
  const std::uint64_t id = state_->graph->next_id++;
  std::vector<std::uint64_t> results;
  results.reserve(result_types.size());
  for (std::size_t index = 0; index < result_types.size(); ++index) {
    const std::uint64_t result = state_->graph->next_id++;
    state_->graph->state->values.emplace(
        result, ValueData{std::move(result_types[index]),
                          ValueData::Origin::OperationResult, id, index});
    results.push_back(result);
  }
  state_->graph->state->operations.emplace(id,
                                           OperationData{std::move(schema),
                                                         std::move(operand_ids),
                                                         std::move(results),
                                                         std::move(properties),
                                                         location});
  auto& operation_order = state_->graph->state->operation_order;
  if (before) {
    const std::uint64_t before_id = detail::GraphAccess::id(*before);
    const auto position = std::find(operation_order.begin(),
                                    operation_order.end(), before_id);
    if (position == operation_order.end()) {
      throw std::invalid_argument("insertion point is not in this function");
    }
    operation_order.insert(position, id);
  } else {
    operation_order.push_back(id);
  }
  return Graph::make_operation(state_->graph, id);
}

void Graph::Edit::set_value(Operation operation, std::string name,
                            ParameterValue value) {
  check_same_graph(state_->graph, operation, "operation");
  state_->graph->state->operations.at(detail::GraphAccess::id(operation))
      .properties.insert_or_assign(std::move(name), std::move(value));
}

void Graph::Edit::output(Value value) {
  check_same_graph(state_->graph, value, "graph output");
  state_->graph->state->outputs.push_back(detail::GraphAccess::id(value));
}

void Graph::Edit::replace(Value from, Value to) {
  check_same_graph(state_->graph, from, "source value");
  check_same_graph(state_->graph, to, "replacement value");
  if (from.type() != to.type()) {
    throw std::invalid_argument("replacement value has a different type");
  }
  const std::uint64_t from_id = detail::GraphAccess::id(from);
  const std::uint64_t to_id = detail::GraphAccess::id(to);
  for (auto& [id, operation] : state_->graph->state->operations) {
    static_cast<void>(id);
    std::replace(operation.operands.begin(), operation.operands.end(), from_id,
                 to_id);
  }
  std::replace(state_->graph->state->outputs.begin(),
               state_->graph->state->outputs.end(), from_id, to_id);
}

Operation Graph::Edit::replace(Operation operation,
                               Module::FunctionDecl schema) {
  check_same_graph(state_->graph, operation, "operation");
  std::vector<Type> result_types;
  result_types.reserve(operation.results().size());
  for (const Value& result : operation.results()) {
    result_types.push_back(result.type());
  }
  const Operation replacement =
      insert(operation, std::move(schema), operation.operands(), result_types);
  for (std::size_t index = 0; index < result_types.size(); ++index) {
    replace(operation.result(index), replacement.result(index));
  }
  erase(operation);
  return replacement;
}

void Graph::Edit::erase(Operation operation) {
  check_same_graph(state_->graph, operation, "operation");
  auto& state = *state_->graph->state;
  const std::uint64_t operation_id = detail::GraphAccess::id(operation);
  const auto found = state.operations.find(operation_id);
  std::unordered_set<std::uint64_t> values;
  values.insert(found->second.results.begin(), found->second.results.end());
  if (std::any_of(
          state.outputs.begin(), state.outputs.end(),
          [&](std::uint64_t output) { return values.contains(output); })) {
    throw std::invalid_argument(
        "cannot erase an operation that defines a function output");
  }
  for (const auto& [id, user] : state.operations) {
    if (id == operation_id) {
      continue;
    }
    if (std::any_of(
            user.operands.begin(), user.operands.end(),
            [&](std::uint64_t operand) { return values.contains(operand); })) {
      throw std::invalid_argument(
          "operation still has live result uses");
    }
  }
  state.operation_order.erase(
      std::remove(state.operation_order.begin(), state.operation_order.end(),
                  operation_id),
      state.operation_order.end());
  for (const std::uint64_t value : values) {
    state.values.erase(value);
  }
  state.operations.erase(operation_id);
}

bool Graph::Edit::commit(Diagnostics& diagnostics) {
  if (!state_ || !state_->active) {
    throw std::logic_error("graph edit is no longer active");
  }
  if (!verify_graph(*state_->graph->state, diagnostics) ||
      !verify_operation_contracts(*state_->graph->state, diagnostics)) {
    state_->graph->state = std::move(state_->backup);
    state_->graph->editing = false;
    state_->active = false;
    return false;
  }
  state_->active = false;
  state_->backup.reset();
  state_->graph->editing = false;
  return true;
}

Graph::Graph(std::vector<Module> modules)
    : graph_(std::make_shared<GraphIdentity>()) {
  graph_->state = std::make_shared<GraphState>();
  for (Module& module : modules) {
    graph_->state->modules.emplace(std::string(module.name()),
                                   std::move(module));
  }
}

Graph::~Graph() = default;
Graph::Graph(Graph&&) noexcept = default;
Graph& Graph::operator=(Graph&&) noexcept = default;

std::vector<Value> Graph::inputs() const {
  std::vector<Value> result;
  result.reserve(graph_->state->inputs.size());
  for (const std::uint64_t value : graph_->state->inputs) {
    result.push_back(make_value(graph_, value));
  }
  return result;
}

std::vector<Operation> Graph::operations() const {
  std::vector<Operation> result;
  result.reserve(graph_->state->operation_order.size());
  for (const std::uint64_t operation : graph_->state->operation_order) {
    result.push_back(make_operation(graph_, operation));
  }
  return result;
}

std::vector<Operation> Graph::all_operations() const {
  return operations();
}

std::vector<Value> Graph::outputs() const {
  std::vector<Value> result;
  result.reserve(graph_->state->outputs.size());
  for (std::uint64_t value : graph_->state->outputs) {
    result.push_back(make_value(graph_, value));
  }
  return result;
}

Graph::Edit Graph::edit() { return Edit(graph_); }

bool Graph::accepts(const Module::Symbol& symbol) const {
  return owns(*graph_->state, symbol);
}

std::shared_ptr<const Graph::Snapshot> Graph::snapshot() const {
  return std::make_shared<const Snapshot>(
      Snapshot{std::make_shared<GraphState>(*graph_->state)});
}

void Graph::restore(std::shared_ptr<const Snapshot> snapshot) {
  if (graph_->editing) {
    throw std::logic_error("cannot restore a graph with an active edit");
  }
  graph_->state = std::make_shared<GraphState>(*snapshot->state);
}

Value Graph::make_value(std::shared_ptr<GraphIdentity> graph,
                        std::uint64_t id) {
  return Value(std::move(graph), id);
}

Operation Graph::make_operation(std::shared_ptr<GraphIdentity> graph,
                                std::uint64_t id) {
  return Operation(std::move(graph), id);
}

}  // namespace joggle
