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
  enum class Origin { RegionArgument, OperationResult };

  Type type;
  Origin origin = Origin::RegionArgument;
  std::uint64_t owner = 0;
  std::size_t index = 0;
};

struct OperationData {
  Module::OperationDecl schema;
  std::uint64_t parent = 0;
  std::vector<std::uint64_t> operands;
  std::vector<std::uint64_t> results;
  std::map<std::string, ParameterValue, std::less<>> properties;
  std::vector<std::pair<std::string, std::uint64_t>> regions;
  std::optional<SourceRange> location;
};

struct RegionData {
  std::optional<std::uint64_t> parent;
  std::string parameter;
  std::vector<std::uint64_t> arguments;
  std::vector<std::uint64_t> operations;
};

struct GraphState {
  std::map<std::string, Module, std::less<>> modules;
  std::unordered_map<std::uint64_t, ValueData> values;
  std::unordered_map<std::uint64_t, OperationData> operations;
  std::unordered_map<std::uint64_t, RegionData> regions;
  std::uint64_t root_region = 0;
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

const std::shared_ptr<GraphIdentity>& GraphAccess::graph(const Region& region) {
  return region.graph_;
}

std::uint64_t GraphAccess::id(const Value& value) { return value.id_; }
std::uint64_t GraphAccess::id(const Operation& operation) {
  return operation.id_;
}
std::uint64_t GraphAccess::id(const Region& region) { return region.id_; }

Region GraphAccess::root(const Graph& graph) {
  return Graph::make_region(graph.graph_, graph.graph_->state->root_region);
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
using detail::RegionData;
using detail::ValueData;

ParameterValue from_literal(const Module::Literal& literal) {
  return std::visit([](const auto& value) { return ParameterValue(value); },
                    literal);
}

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

ParameterValue::Kind parameter_kind(Module::ParameterKind kind) {
  switch (kind) {
  case Module::ParameterKind::I64:
    return ParameterValue::Kind::I64;
  case Module::ParameterKind::F64:
    return ParameterValue::Kind::F64;
  case Module::ParameterKind::Boolean:
    return ParameterValue::Kind::Boolean;
  case Module::ParameterKind::String:
    return ParameterValue::Kind::String;
  case Module::ParameterKind::Type:
    return ParameterValue::Kind::Type;
  case Module::ParameterKind::Attribute:
    return ParameterValue::Kind::Attribute;
  case Module::ParameterKind::Value:
  case Module::ParameterKind::Region:
    break;
  }
  return ParameterValue::Kind::List;
}

bool matches(const Module::ParameterDecl& schema, const ParameterValue& value) {
  const auto expected = parameter_kind(schema.kind);
  if (!schema.list) {
    return value.kind() == expected;
  }
  return value.kind() == ParameterValue::Kind::List &&
         std::all_of(value.elements().begin(), value.elements().end(),
                     [&](const ParameterValue& element) {
                       return element.kind() == expected;
                     });
}

bool accepts_count(std::span<const Module::ParameterDecl> parameters,
                   Module::ParameterKind kind, std::size_t count) {
  std::size_t minimum = 0;
  bool variadic = false;
  for (const Module::ParameterDecl& parameter : parameters) {
    if (parameter.kind != kind) {
      continue;
    }
    if (parameter.variadic) {
      variadic = true;
    } else {
      ++minimum;
    }
  }
  return variadic ? count >= minimum : count == minimum;
}

std::optional<std::size_t> operation_position(const GraphState& graph,
                                              std::uint64_t region,
                                              std::uint64_t operation) {
  const auto owner = graph.regions.find(region);
  if (owner == graph.regions.end()) {
    return std::nullopt;
  }
  const auto found = std::find(owner->second.operations.begin(),
                               owner->second.operations.end(), operation);
  if (found == owner->second.operations.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(
      std::distance(owner->second.operations.begin(), found));
}

bool dominates(const GraphState& graph, const ValueData& definition,
               const OperationData& user, std::uint64_t user_id) {
  std::uint64_t anchor = user_id;
  std::uint64_t region = user.parent;
  while (true) {
    if (definition.origin == ValueData::Origin::RegionArgument &&
        definition.owner == region) {
      return true;
    }
    if (definition.origin == ValueData::Origin::OperationResult) {
      const auto producer = graph.operations.find(definition.owner);
      if (producer == graph.operations.end()) {
        return false;
      }
      if (producer->second.parent == region) {
        const auto producer_position =
            operation_position(graph, region, definition.owner);
        const auto user_position = operation_position(graph, region, anchor);
        return producer_position && user_position &&
               *producer_position < *user_position;
      }
    }

    const auto current = graph.regions.find(region);
    if (current == graph.regions.end() || !current->second.parent) {
      return false;
    }
    anchor = *current->second.parent;
    const auto parent_operation = graph.operations.find(anchor);
    if (parent_operation == graph.operations.end()) {
      return false;
    }
    region = parent_operation->second.parent;
  }
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
  if (!contains(graph.regions, operation.parent)) {
    diagnostics.report("operation '" + name + "' has no parent region");
    valid = false;
  }
  if (!accepts_count(operation.schema.inputs(), Module::ParameterKind::Value,
                     operation.operands.size())) {
    diagnostics.report("operation '" + name +
                       "' has the wrong number of operands");
    valid = false;
  }
  if (!accepts_count(operation.schema.results(), Module::ParameterKind::Value,
                     operation.results.size())) {
    diagnostics.report("operation '" + name +
                       "' has the wrong number of results");
    valid = false;
  }
  if (!accepts_count(operation.schema.inputs(), Module::ParameterKind::Region,
                     operation.regions.size())) {
    diagnostics.report("operation '" + name +
                       "' has the wrong number of regions");
    valid = false;
  }

  for (const Module::ParameterDecl& parameter : operation.schema.inputs()) {
    if (parameter.kind == Module::ParameterKind::Value ||
        parameter.kind == Module::ParameterKind::Region) {
      continue;
    }
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
  for (const auto& [property_name, value] : operation.properties) {
    const auto parameter = std::find_if(
        operation.schema.inputs().begin(), operation.schema.inputs().end(),
        [&](const Module::ParameterDecl& item) {
          return item.name == property_name &&
                 item.kind != Module::ParameterKind::Value &&
                 item.kind != Module::ParameterKind::Region;
        });
    if (parameter == operation.schema.inputs().end()) {
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
  for (const auto& [parameter, region] : operation.regions) {
    const auto schema = std::find_if(
        operation.schema.inputs().begin(), operation.schema.inputs().end(),
        [&](const Module::ParameterDecl& item) {
          return item.name == parameter &&
                 item.kind == Module::ParameterKind::Region;
        });
    const auto body = graph.regions.find(region);
    if (schema == operation.schema.inputs().end() ||
        body == graph.regions.end() || body->second.parent != id) {
      diagnostics.report("operation '" + name +
                         "' has an invalid region binding");
      valid = false;
    }
  }
  for (const Module::ParameterDecl& parameter : operation.schema.inputs()) {
    if (parameter.kind != Module::ParameterKind::Region) {
      continue;
    }
    const auto count = static_cast<std::size_t>(std::count_if(
        operation.regions.begin(), operation.regions.end(),
        [&](const auto& binding) { return binding.first == parameter.name; }));
    if (!parameter.variadic && count != 1U) {
      diagnostics.report("operation '" + name +
                         "' has an invalid binding for "
                         "region '" +
                         parameter.name + "'");
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
  if (!contains(graph.regions, graph.root_region)) {
    diagnostics.report("graph root is missing");
    return false;
  }
  for (const auto& [id, region] : graph.regions) {
    if (id == graph.root_region) {
      if (region.parent) {
        diagnostics.report("graph root region has a parent operation");
        valid = false;
      }
    } else if (!region.parent) {
      diagnostics.report("nested region has no parent operation");
      valid = false;
    } else {
      const auto operation = graph.operations.find(*region.parent);
      if (operation == graph.operations.end() ||
          std::none_of(
              operation->second.regions.begin(),
              operation->second.regions.end(),
              [&](const auto& binding) { return binding.second == id; })) {
        diagnostics.report("region has an invalid parent operation");
        valid = false;
      }
    }
    for (std::uint64_t argument : region.arguments) {
      const auto value = graph.values.find(argument);
      if (value == graph.values.end() ||
          value->second.origin != ValueData::Origin::RegionArgument ||
          value->second.owner != id || !owns(graph, value->second.type)) {
        diagnostics.report("region has an invalid argument");
        valid = false;
      }
    }
    for (std::uint64_t operation : region.operations) {
      const auto item = graph.operations.find(operation);
      if (item == graph.operations.end() || item->second.parent != id) {
        diagnostics.report("region has an invalid operation");
        valid = false;
      }
    }
  }
  for (const auto& [id, operation] : graph.operations) {
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
        (value->second.origin == ValueData::Origin::RegionArgument &&
         value->second.owner == graph.root_region) ||
        (value->second.origin == ValueData::Origin::OperationResult &&
         contains(graph.operations, value->second.owner) &&
         graph.operations.at(value->second.owner).parent == graph.root_region);
    if (!available) {
      diagnostics.report("graph output is not defined in the graph body");
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

  bool valid = true;
  const auto verify_region = [&](const auto& self,
                                 const std::uint64_t region_id) -> void {
    const RegionData& region = graph.regions.at(region_id);
    for (const std::uint64_t operation_id : region.operations) {
      const OperationData& operation = graph.operations.at(operation_id);
      const Module::OperationDecl schema = operation.schema;

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
      properties.reserve(schema.inputs().size());
      for (const Module::ParameterDecl& input : schema.inputs()) {
        if (input.kind == Module::ParameterKind::Value ||
            input.kind == Module::ParameterKind::Region) {
          properties.emplace_back();
          continue;
        }
        const auto value = operation.properties.find(input.name);
        properties.push_back(value == operation.properties.end()
                                 ? std::optional<ParameterValue>{}
                                 : value->second);
      }

      if (!infer_operation_types(modules, schema, operands, properties, results,
                                 diagnostics, operation.location)) {
        valid = false;
      }
      for (const auto& [parameter, nested] : operation.regions) {
        static_cast<void>(parameter);
        self(self, nested);
      }
    }
  };
  verify_region(verify_region, graph.root_region);
  return valid;
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
         found->second.origin == ValueData::Origin::RegionArgument;
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

Module::OperationDecl Operation::schema() const {
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

std::vector<Region> Operation::regions() const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end()) {
    throw std::logic_error("operation is no longer valid");
  }
  std::vector<Region> regions;
  regions.reserve(found->second.regions.size());
  for (const auto& [parameter, region] : found->second.regions) {
    static_cast<void>(parameter);
    regions.push_back(Region(graph_, region));
  }
  return regions;
}

std::optional<Region> Operation::parent() const {
  const auto found = graph_->state->operations.find(id_);
  if (found == graph_->state->operations.end()) {
    throw std::logic_error("operation is no longer valid");
  }
  const auto region = graph_->state->regions.find(found->second.parent);
  if (region == graph_->state->regions.end()) {
    throw std::logic_error("operation parent is no longer valid");
  }
  return region->second.parent
             ? std::optional<Region>{Region(graph_, found->second.parent)}
             : std::nullopt;
}

Region::Region(std::shared_ptr<GraphIdentity> graph, std::uint64_t id)
    : graph_(std::move(graph)), id_(id) {}

bool Region::valid() const {
  return graph_ && contains(graph_->state->regions, id_);
}

std::vector<Value> Region::arguments() const {
  const auto found = graph_->state->regions.find(id_);
  if (found == graph_->state->regions.end()) {
    throw std::logic_error("region is no longer valid");
  }
  std::vector<Value> values;
  values.reserve(found->second.arguments.size());
  for (std::uint64_t value : found->second.arguments) {
    values.push_back(Value(graph_, value));
  }
  return values;
}

std::vector<Operation> Region::operations() const {
  const auto found = graph_->state->regions.find(id_);
  if (found == graph_->state->regions.end()) {
    throw std::logic_error("region is no longer valid");
  }
  std::vector<Operation> operations;
  operations.reserve(found->second.operations.size());
  for (std::uint64_t operation : found->second.operations) {
    operations.push_back(Operation(graph_, operation));
  }
  return operations;
}

std::optional<Operation> Region::parent() const {
  const auto found = graph_->state->regions.find(id_);
  if (found == graph_->state->regions.end() || !found->second.parent) {
    return std::nullopt;
  }
  return Operation(graph_, *found->second.parent);
}

std::string_view Region::parameter() const {
  const auto found = graph_->state->regions.find(id_);
  if (found == graph_->state->regions.end()) {
    throw std::logic_error("region is no longer valid");
  }
  return found->second.parameter;
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
  return add_argument(
      Graph::make_region(state_->graph, state_->graph->state->root_region),
      std::move(type));
}

Value Graph::Edit::add_argument(Region region, Type type) {
  check_same_graph(state_->graph, region, "region");
  const std::uint64_t id = state_->graph->next_id++;
  const std::uint64_t region_id = detail::GraphAccess::id(region);
  auto& data = state_->graph->state->regions.at(region_id);
  const std::size_t index = data.arguments.size();
  state_->graph->state->values.emplace(
      id, ValueData{std::move(type), ValueData::Origin::RegionArgument,
                    region_id, index});
  data.arguments.push_back(id);
  return Graph::make_value(state_->graph, id);
}

Operation Graph::Edit::append(Module::OperationDecl schema,
                              std::vector<Value> operands,
                              std::vector<Type> result_types) {
  return append_with_properties(std::move(schema), std::move(operands),
                                std::move(result_types), {});
}

Operation Graph::Edit::append_with_properties(
    Module::OperationDecl schema, std::vector<Value> operands,
    std::vector<Type> result_types, std::vector<Property> properties) {
  return append_with_properties(
      Graph::make_region(state_->graph, state_->graph->state->root_region),
      std::move(schema), std::move(operands), std::move(result_types),
      std::move(properties));
}

Operation Graph::Edit::append(Region region, Module::OperationDecl schema,
                              std::vector<Value> operands,
                              std::vector<Type> result_types) {
  return append_with_properties(std::move(region), std::move(schema),
                                std::move(operands), std::move(result_types), {});
}

Operation Graph::Edit::append_with_properties(
    Region region, Module::OperationDecl schema, std::vector<Value> operands,
    std::vector<Type> result_types, std::vector<Property> properties) {
  return add(std::move(region), std::nullopt, std::move(schema),
             std::move(operands), std::move(result_types),
             std::move(properties));
}

Operation Graph::Edit::insert(Operation before, Module::OperationDecl schema,
                              std::vector<Value> operands,
                              std::vector<Type> result_types) {
  return insert_with_properties(std::move(before), std::move(schema),
                                std::move(operands), std::move(result_types), {});
}

Operation Graph::Edit::insert_with_properties(
    Operation before, Module::OperationDecl schema, std::vector<Value> operands,
    std::vector<Type> result_types, std::vector<Property> properties) {
  check_same_graph(state_->graph, before, "insertion point");
  const auto operation =
      state_->graph->state->operations.find(detail::GraphAccess::id(before));
  if (operation == state_->graph->state->operations.end()) {
    throw std::invalid_argument("insertion point is no longer valid");
  }
  return add(Graph::make_region(state_->graph, operation->second.parent),
             before, std::move(schema), std::move(operands),
             std::move(result_types), std::move(properties));
}

Operation Graph::Edit::add(Region region, std::optional<Operation> before,
                           Module::OperationDecl schema,
                           std::vector<Value> operands,
                           std::vector<Type> result_types,
                           std::vector<Property> arguments) {
  check_same_graph(state_->graph, region, "region");
  const std::uint64_t region_id = detail::GraphAccess::id(region);
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
  for (const Module::ParameterDecl& parameter : schema.inputs()) {
    if (parameter.kind != Module::ParameterKind::Value &&
        parameter.kind != Module::ParameterKind::Region &&
        parameter.default_value) {
      properties.emplace(parameter.name, from_literal(*parameter.default_value));
    }
  }
  std::unordered_set<std::string> explicit_properties;
  for (Property& argument : arguments) {
    std::string name = detail::PropertyAccess::take_name(argument);
    ParameterValue value = detail::PropertyAccess::take_value(argument);
    const auto parameter = std::find_if(
        schema.inputs().begin(), schema.inputs().end(),
        [&](const Module::ParameterDecl& input) {
          return input.name == name &&
                 input.kind != Module::ParameterKind::Value &&
                 input.kind != Module::ParameterKind::Region;
        });
    if (parameter == schema.inputs().end()) {
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

  if (result_types.empty() && !schema.results().empty()) {
    std::vector<Type> operand_types;
    operand_types.reserve(operands.size());
    for (const Value& operand : operands) {
      operand_types.push_back(operand.type());
    }
    std::vector<std::optional<Type>> expected(schema.results().size());
    std::vector<std::optional<ParameterValue>> inference_properties;
    inference_properties.reserve(schema.inputs().size());
    for (const Module::ParameterDecl& parameter : schema.inputs()) {
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
                                                         region_id,
                                                         std::move(operand_ids),
                                                         std::move(results),
                                                         std::move(properties),
                                                         {},
                                                         location});
  auto& region_operations =
      state_->graph->state->regions.at(region_id).operations;
  if (before) {
    const std::uint64_t before_id = detail::GraphAccess::id(*before);
    const auto position = std::find(region_operations.begin(),
                                    region_operations.end(), before_id);
    if (position == region_operations.end()) {
      throw std::invalid_argument(
          "insertion point is not in its parent region");
    }
    region_operations.insert(position, id);
  } else {
    region_operations.push_back(id);
  }
  return Graph::make_operation(state_->graph, id);
}

void Graph::Edit::set_value(Operation operation, std::string name,
                            ParameterValue value) {
  check_same_graph(state_->graph, operation, "operation");
  state_->graph->state->operations.at(detail::GraphAccess::id(operation))
      .properties.insert_or_assign(std::move(name), std::move(value));
}

Region Graph::Edit::region(Operation operation, std::string parameter,
                           std::vector<Type> arguments) {
  check_same_graph(state_->graph, operation, "operation");
  const std::uint64_t operation_id = detail::GraphAccess::id(operation);
  const std::uint64_t region_id = state_->graph->next_id++;
  state_->graph->state->regions.emplace(
      region_id, RegionData{operation_id, parameter, {}, {}});
  state_->graph->state->operations.at(operation_id)
      .regions.emplace_back(std::move(parameter), region_id);
  Region region = Graph::make_region(state_->graph, region_id);
  for (Type& type : arguments) {
    add_argument(region, std::move(type));
  }
  return region;
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
                               Module::OperationDecl schema) {
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
  std::unordered_set<std::uint64_t> operation_ids;
  std::unordered_set<std::uint64_t> regions;
  std::unordered_set<std::uint64_t> values;
  const auto collect_region = [&](const auto& self,
                                  std::uint64_t region_id) -> void {
    regions.insert(region_id);
    const auto& region = state.regions.at(region_id);
    values.insert(region.arguments.begin(), region.arguments.end());
    for (const std::uint64_t nested_id : region.operations) {
      operation_ids.insert(nested_id);
      const auto& nested = state.operations.at(nested_id);
      values.insert(nested.results.begin(), nested.results.end());
      for (const auto& [parameter, child_region] : nested.regions) {
        static_cast<void>(parameter);
        self(self, child_region);
      }
    }
  };
  operation_ids.insert(operation_id);
  values.insert(found->second.results.begin(), found->second.results.end());
  for (const auto& [parameter, region] : found->second.regions) {
    static_cast<void>(parameter);
    collect_region(collect_region, region);
  }
  if (std::any_of(
          state.outputs.begin(), state.outputs.end(),
          [&](std::uint64_t output) { return values.contains(output); })) {
    throw std::invalid_argument(
        "cannot erase an operation subtree with a graph output");
  }
  for (const auto& [id, user] : state.operations) {
    if (operation_ids.contains(id)) {
      continue;
    }
    if (std::any_of(
            user.operands.begin(), user.operands.end(),
            [&](std::uint64_t operand) { return values.contains(operand); })) {
      throw std::invalid_argument(
          "operation subtree still has live result uses");
    }
  }
  auto& parent_operations = state.regions.at(found->second.parent).operations;
  parent_operations.erase(std::remove(parent_operations.begin(),
                                      parent_operations.end(), operation_id),
                          parent_operations.end());
  for (const std::uint64_t value : values) {
    state.values.erase(value);
  }
  for (const std::uint64_t nested : operation_ids) {
    state.operations.erase(nested);
  }
  for (const std::uint64_t region : regions) {
    state.regions.erase(region);
  }
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
  graph_->state->root_region = graph_->next_id++;
  graph_->state->regions.emplace(
      graph_->state->root_region,
      RegionData{std::nullopt, std::string{}, {}, {}});
}

Graph::~Graph() = default;
Graph::Graph(Graph&&) noexcept = default;
Graph& Graph::operator=(Graph&&) noexcept = default;

std::vector<Value> Graph::inputs() const {
  return detail::GraphAccess::root(*this).arguments();
}

std::vector<Operation> Graph::operations() const {
  return detail::GraphAccess::root(*this).operations();
}

std::vector<Operation> Graph::all_operations() const {
  std::vector<Operation> result;
  const auto visit = [&](const auto& self, std::uint64_t region_id) -> void {
    const auto& region = graph_->state->regions.at(region_id);
    for (std::uint64_t operation_id : region.operations) {
      result.push_back(make_operation(graph_, operation_id));
      const auto& operation = graph_->state->operations.at(operation_id);
      for (const auto& [parameter, nested_region] : operation.regions) {
        static_cast<void>(parameter);
        self(self, nested_region);
      }
    }
  };
  visit(visit, graph_->state->root_region);
  return result;
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

Region Graph::make_region(std::shared_ptr<GraphIdentity> graph,
                          std::uint64_t id) {
  return Region(std::move(graph), id);
}

}  // namespace joggle
