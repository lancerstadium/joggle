#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

#include "execute.h"
#include "kernel.h"

namespace {

struct Schema {
  joggle::Module target;
  joggle::Module::TypeDecl reference;
  joggle::Module::TypeDecl linear;
  joggle::Module::TypeDecl tiled;
  joggle::Module::TypeDecl io;
  joggle::Module::TypeDecl read_only;
  joggle::Module::TypeDecl local;
  joggle::Module::TypeDecl timeline;
  joggle::Module::InterfaceDecl ranked_tensor;
  joggle::Module::InterfaceDecl memory_reference;
  joggle::Module::InterfaceDecl immutable_data;
  joggle::Module::InterfaceDecl placement;
  joggle::Module::InterfaceDecl machine;
  joggle::Module::FunctionDecl place;
};

std::optional<joggle::Module::FunctionDecl>
function(const joggle::Module& module, std::string_view name,
         std::size_t input_count) {
  for (const auto& candidate : module.overloads(name)) {
    if (candidate.inputs().size() == input_count) {
      return candidate;
    }
  }
  return std::nullopt;
}

bool ranked(joggle::Compiler& compiler, const joggle::Type& type,
            const Schema& schema) {
  return compiler.conforms(type.schema(), schema.ranked_tensor);
}

std::optional<joggle::Type>
map_type(joggle::Compiler& compiler, const joggle::Value& value,
         const Schema& schema, std::int64_t tile_rows,
         std::int64_t tile_columns, joggle::Diagnostics& diagnostics) {
  const joggle::Type source = value.type();
  if (compiler.conforms(source.schema(), schema.memory_reference) ||
      !ranked(compiler, source, schema)) {
    return source;
  }

  const auto element = source.get<joggle::Type>("element_type");
  const auto shape = source.get<std::vector<std::int64_t>>("shape");
  if (!element || !shape) {
    diagnostics.report("ranked tensor is missing element_type or shape");
    return std::nullopt;
  }

  const auto producer = value.defining_op();
  const bool constant =
      producer && compiler.conforms(producer->callee(), schema.immutable_data);
  const bool local = !value.is_function_argument() && !constant;

  const auto space = compiler.make(constant   ? schema.read_only
                                   : local    ? schema.local
                                              : schema.io);
  const auto layout = local && shape->size() == 4U
                          ? compiler.make(schema.tiled, tile_rows, tile_columns)
                          : compiler.make(schema.linear);
  if (!space || !layout) {
    diagnostics.report("anchor could not construct layout or space");
    return std::nullopt;
  }
  const auto mapped =
      compiler.make(schema.reference, *element, *shape, *layout, *space);
  if (!mapped) {
    diagnostics.report("anchor could not construct a reference type");
  }
  return mapped;
}

std::optional<joggle::Module::FunctionDecl>
map_callee(joggle::Compiler& compiler, const joggle::Op& op,
           const Schema& schema, joggle::Diagnostics& diagnostics) {
  const auto symbol = op.callee().symbol();
  if (symbol.module_name() == schema.target.name()) {
    return op.callee();
  }

  std::string_view name;
  if (compiler.conforms(op.callee(), schema.immutable_data)) {
    name = "constant";
  } else if (symbol.module_name() == "nn") {
    name = symbol.local_name();
  } else {
    bool touches_tensor = false;
    for (const auto& argument : op.arguments()) {
      touches_tensor = touches_tensor || ranked(compiler, argument.type(), schema);
    }
    for (const auto& result : op.results()) {
      touches_tensor = touches_tensor || ranked(compiler, result.type(), schema);
    }
    if (!touches_tensor) {
      return op.callee();
    }
    diagnostics.report("anchor does not map tensor call '" +
                       symbol.qualified_name() + "'");
    return std::nullopt;
  }

  const auto mapped = function(schema.target, name, op.callee().inputs().size());
  if (!mapped) {
    diagnostics.report("anchor has no matching implementation for '" +
                       symbol.qualified_name() + "'");
  }
  return mapped;
}

std::optional<joggle::Module>
map_module(joggle::Compiler& compiler, joggle::Module input,
           std::int64_t tile_rows, std::int64_t tile_columns,
           const Schema& schema, joggle::Diagnostics& diagnostics) {
  if (tile_rows <= 0 || tile_columns <= 0) {
    diagnostics.report("tile dimensions must be positive");
    return std::nullopt;
  }

  joggle::Module output(std::string(input.name()), input.version());
  for (const std::string& name : input.data()) {
    const auto payload = input.data(name);
    if (!payload || output.store(joggle::Bytes(payload->begin(), payload->end())) !=
                        name) {
      diagnostics.report("anchor could not preserve Module data '" +
                         name + "'");
      return std::nullopt;
    }
  }

  for (const auto& member : input.functions()) {
    const joggle::Function* source = member.body();
    if (source == nullptr) {
      diagnostics.report("anchor requires materialized Functions");
      return std::nullopt;
    }
    for (const auto& op : source->ops()) {
      if (op.callee().symbol().module_name() == input.name()) {
        diagnostics.report(
            "anchor currently rejects calls between input Functions");
        return std::nullopt;
      }
    }

    auto mapped = joggle::clone(
        compiler, *source,
        [&](const joggle::Value& value) {
          return map_type(compiler, value, schema, tile_rows, tile_columns,
                          diagnostics);
        },
        [&](const joggle::Op& op) {
          return map_callee(compiler, op, schema, diagnostics);
        },
        diagnostics);
    if (!mapped ||
        !output.insert(std::string(member.name()), std::move(*mapped),
                       diagnostics)) {
      return std::nullopt;
    }
  }

  return compiler.verify(output) ? std::optional<joggle::Module>{output}
                                 : std::nullopt;
}

std::optional<joggle::Module>
fuse_relu(joggle::Module input, const Schema& schema,
          joggle::Diagnostics& diagnostics) {
  const auto normalization =
      function(schema.target, "batch_norm_nchw", 6U);
  const auto activation = function(schema.target, "relu", 1U);
  const auto fused_normalization =
      function(schema.target, "batch_norm_relu_nchw", 6U);
  const auto convolution = function(schema.target, "conv2d_nchw", 11U);
  const auto fused_convolution =
      function(schema.target, "conv_relu_nchw", 11U);
  if (!normalization || !activation || !fused_normalization ||
      !convolution || !fused_convolution) {
    diagnostics.report("anchor fusion does not match its Module schema");
    return std::nullopt;
  }

  const auto changed = joggle::rewrite(
      input,
      [&](const joggle::Op& op, joggle::Function::Edit& edit,
          joggle::Diagnostics&) {
        if (op.callee() != *activation) {
          return false;
        }
        const auto operands = op.operands();
        const auto producer =
            operands.size() == 1U ? operands.front().defining_op()
                                  : std::optional<joggle::Op>{};
        if (!producer || producer->results().size() != 1U ||
            producer->parent() != op.parent() ||
            producer->value().users() != std::vector<joggle::Op>{op}) {
          return false;
        }
        const auto replacement_callee =
            producer->callee() == *normalization
                ? fused_normalization
                : producer->callee() == *convolution
                      ? fused_convolution
                      : std::optional<joggle::Module::FunctionDecl>{};
        if (!replacement_callee) {
          return false;
        }
        const auto replacement = edit.insert(
            op, *replacement_callee, producer->arguments(),
            {op.value().type()});
        edit.replace(op, replacement.results());
        edit.erase(*producer);
        return true;
      },
      diagnostics);
  return changed ? std::optional<joggle::Module>{std::move(input)}
                 : std::nullopt;
}

std::optional<std::uint64_t>
reference_bytes(joggle::Compiler& compiler, const joggle::Type& type,
                const Schema& schema, joggle::Diagnostics& diagnostics) {
  if (!compiler.conforms(type.schema(), schema.memory_reference)) {
    return std::uint64_t{0};
  }
  const auto element = type.get<joggle::Type>("element_type");
  const auto shape = type.get<std::vector<std::int64_t>>("shape");
  const auto bits = element
                        ? element->get<std::int64_t>("storage_bits")
                        : std::optional<std::int64_t>{};
  if (!element || !shape || !bits || *bits <= 0) {
    diagnostics.report("reference has no positive storage_bits or shape");
    return std::nullopt;
  }

  std::uint64_t total_bits = static_cast<std::uint64_t>(*bits);
  for (const std::int64_t dimension : *shape) {
    if (dimension <= 0 ||
        static_cast<std::uint64_t>(dimension) >
            std::numeric_limits<std::uint64_t>::max() / total_bits) {
      diagnostics.report("reference size overflows");
      return std::nullopt;
    }
    total_bits *= static_cast<std::uint64_t>(dimension);
  }
  return total_bits / 8U + (total_bits % 8U == 0U ? 0U : 1U);
}

std::optional<std::uint64_t>
local_bytes(joggle::Compiler& compiler, const joggle::Type& type,
            const Schema& schema, joggle::Diagnostics& diagnostics) {
  if (!compiler.conforms(type.schema(), schema.memory_reference)) {
    return std::uint64_t{0};
  }
  const auto space = type.get<joggle::Type>("space_type");
  return !space || space->schema() != schema.local
             ? std::optional<std::uint64_t>{0}
             : reference_bytes(compiler, type, schema, diagnostics);
}

std::optional<std::int64_t>
local_bytes_upper_bound(joggle::Compiler& compiler, const joggle::Module& input,
                        const Schema& schema,
                        joggle::Diagnostics& diagnostics) {
  std::uint64_t total = 0;
  const auto add = [&](const joggle::Value& value) -> bool {
    const auto bytes = local_bytes(compiler, value.type(), schema, diagnostics);
    if (!bytes || *bytes >
                      static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max()) -
                          total) {
      if (bytes) {
        diagnostics.report("local byte upper bound overflows int");
      }
      return false;
    }
    total += *bytes;
    return true;
  };

  for (const auto& member : input.functions()) {
    const joggle::Function* function_body = member.body();
    if (function_body == nullptr) {
      diagnostics.report("local byte analysis requires materialized Functions");
      return std::nullopt;
    }
    for (const auto& block : function_body->blocks()) {
      for (const auto& argument : block.arguments()) {
        if (!add(argument)) {
          return std::nullopt;
        }
      }
    }
    for (const auto& op : function_body->ops()) {
      for (const auto& result : op.results()) {
        if (!add(result)) {
          return std::nullopt;
        }
      }
    }
  }
  return static_cast<std::int64_t>(total);
}

bool is_local(joggle::Compiler& compiler, const joggle::Type& type,
              const Schema& schema) {
  if (!compiler.conforms(type.schema(), schema.memory_reference)) {
    return false;
  }
  const auto space = type.get<joggle::Type>("space_type");
  return space && space->schema() == schema.local;
}

std::optional<std::size_t> position(const std::vector<joggle::Op>& ops,
                                    const joggle::Op& target) {
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (ops[index] == target) {
      return index;
    }
  }
  return std::nullopt;
}

bool returned(const joggle::Function& function, const joggle::Value& value) {
  for (const auto& block : function.blocks()) {
    for (const auto& result : block.terminator().returned()) {
      if (result == value) {
        return true;
      }
    }
  }
  return false;
}

struct Assignment {
  joggle::Value value;
  std::int64_t slot = 0;
};

struct Slot {
  joggle::Type type;
  std::size_t available_after = 0;
};

std::optional<std::vector<Assignment>>
assign_slots(joggle::Compiler& compiler, const joggle::Function& function_body,
             const Schema& schema, joggle::Diagnostics& diagnostics) {
  const auto blocks = function_body.blocks();
  if (blocks.size() != 1U || !blocks.front().arguments().empty() ||
      blocks.front().terminator().kind() != joggle::Terminator::Kind::Return) {
    diagnostics.report(
        "storage planning currently requires one straight-line Block");
    return std::nullopt;
  }

  const auto ops = function_body.ops();
  std::vector<Assignment> assignments;
  std::vector<Slot> slots;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    if (compiler.conforms(ops[index].callee(), schema.placement)) {
      diagnostics.report("storage planning received an already placed Function");
      return std::nullopt;
    }
    for (const auto& result : ops[index].results()) {
      if (!is_local(compiler, result.type(), schema)) {
        continue;
      }
      std::size_t last_use = index;
      for (const auto& user : result.users()) {
        const auto user_position = position(ops, user);
        if (!user_position) {
          diagnostics.report("storage planning lost a local SSA user");
          return std::nullopt;
        }
        if (*user_position > last_use) {
          last_use = *user_position;
        }
      }
      if (returned(function_body, result)) {
        last_use = ops.size();
      }

      std::optional<std::size_t> selected;
      for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        if (slots[slot].type == result.type() &&
            slots[slot].available_after < index) {
          selected = slot;
          break;
        }
      }
      if (!selected) {
        selected = slots.size();
        slots.push_back(Slot{result.type(), last_use});
      } else {
        slots[*selected].available_after = last_use;
      }
      if (*selected > static_cast<std::size_t>(
                          std::numeric_limits<std::int64_t>::max())) {
        diagnostics.report("storage slot id overflows int");
        return std::nullopt;
      }
      assignments.push_back(
          Assignment{result, static_cast<std::int64_t>(*selected)});
    }
  }
  return assignments;
}

std::optional<std::int64_t>
assigned_slot(const std::vector<Assignment>& assignments,
              const joggle::Value& value) {
  for (const auto& assignment : assignments) {
    if (assignment.value == value) {
      return assignment.slot;
    }
  }
  return std::nullopt;
}

std::optional<joggle::Value>
mapped_value(joggle::Function::Edit& edit,
             std::vector<std::pair<joggle::Value, joggle::Value>>& values,
             const joggle::Value& source) {
  if (source.known()) {
    return source;
  }
  for (const auto& [from, to] : values) {
    if (from == source) {
      return to;
    }
  }
  const auto reference = source.referenced_function();
  if (reference) {
    const auto mapped = edit.reference(*reference, source.type());
    values.emplace_back(source, mapped);
    return mapped;
  }
  return std::nullopt;
}

std::optional<joggle::Function>
plan_function(joggle::Compiler& compiler, const joggle::Function& source,
              const Schema& schema, const joggle::Type& int_type,
              joggle::Diagnostics& diagnostics) {
  const auto assignments = assign_slots(compiler, source, schema, diagnostics);
  auto output = compiler.create_function();
  if (!assignments || !output) {
    return std::nullopt;
  }

  auto edit = output->edit();
  std::vector<std::pair<joggle::Value, joggle::Value>> values;
  for (const auto& argument : source.arguments()) {
    const auto mapped = edit.argument(argument.type());
    values.emplace_back(argument, mapped);
  }

  for (const auto& op : source.ops()) {
    std::vector<joggle::Value> arguments;
    for (const auto& argument : op.arguments()) {
      const auto mapped = mapped_value(edit, values, argument);
      if (!mapped) {
        diagnostics.report("storage planning encountered a value before its "
                           "definition");
        return std::nullopt;
      }
      arguments.push_back(*mapped);
    }
    std::vector<joggle::Type> result_types;
    for (const auto& result : op.results()) {
      result_types.push_back(result.type());
    }
    const auto rebuilt =
        edit.append(op.callee(), std::move(arguments), std::move(result_types));
    const auto source_results = op.results();
    const auto rebuilt_results = rebuilt.results();
    for (std::size_t index = 0; index < source_results.size(); ++index) {
      joggle::Value mapped = rebuilt_results[index];
      const auto slot = assigned_slot(*assignments, source_results[index]);
      if (slot) {
        const auto known_slot = compiler.known(int_type, *slot);
        if (!known_slot) {
          diagnostics.report("storage planning could not materialize a slot");
          return std::nullopt;
        }
        mapped = edit
                     .append(schema.place, {mapped, *known_slot},
                             {mapped.type()})
                     .value();
      }
      values.emplace_back(source_results[index], mapped);
    }
  }

  std::vector<joggle::Value> results;
  for (const auto& value : source.entry().terminator().returned()) {
    const auto mapped = mapped_value(edit, values, value);
    if (!mapped) {
      diagnostics.report("storage planning lost a returned value");
      return std::nullopt;
    }
    results.push_back(*mapped);
  }
  edit.ret(output->entry(), std::move(results));
  return edit.commit(diagnostics) ? std::move(output) : std::nullopt;
}

std::optional<joggle::Module>
plan_storage(joggle::Compiler& compiler, joggle::Module input,
             const Schema& schema, joggle::Diagnostics& diagnostics) {
  const auto int_type = compiler.make("int");
  if (!int_type) {
    diagnostics.report("storage planning requires the Prelude int type");
    return std::nullopt;
  }

  joggle::Module output(std::string(input.name()), input.version());
  for (const std::string& name : input.data()) {
    const auto payload = input.data(name);
    if (!payload || output.store(joggle::Bytes(payload->begin(), payload->end())) !=
                        name) {
      diagnostics.report("storage planning could not preserve Module data '" +
                         name + "'");
      return std::nullopt;
    }
  }
  for (const auto& member : input.functions()) {
    const joggle::Function* source = member.body();
    if (source == nullptr) {
      diagnostics.report("storage planning requires materialized Functions");
      return std::nullopt;
    }
    auto planned =
        plan_function(compiler, *source, schema, *int_type, diagnostics);
    if (!planned ||
        !output.insert(std::string(member.name()), std::move(*planned),
                       diagnostics)) {
      return std::nullopt;
    }
  }
  return compiler.verify(output) ? std::optional<joggle::Module>{output}
                                 : std::nullopt;
}

struct SlotUse {
  std::int64_t id = 0;
  joggle::Type type;
  std::uint64_t bytes = 0;
  std::size_t last_use = 0;
};

std::optional<std::uint64_t>
function_scratch_bytes(joggle::Compiler& compiler,
                       const joggle::Function& function_body,
                       const Schema& schema,
                       joggle::Diagnostics& diagnostics) {
  const auto ops = function_body.ops();
  std::vector<SlotUse> slots;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const auto& op = ops[index];
    const bool placement = compiler.conforms(op.callee(), schema.placement);
    for (const auto& result : op.results()) {
      if (!is_local(compiler, result.type(), schema)) {
        continue;
      }
      if (!placement) {
        const auto users = result.users();
        if (users.size() != 1U ||
            !compiler.conforms(users.front().callee(), schema.placement)) {
          diagnostics.report("storage plan contains an unplaced local value");
          return std::nullopt;
        }
        continue;
      }

      const auto slot = op.property<std::int64_t>("slot");
      const auto bytes = local_bytes(compiler, result.type(), schema, diagnostics);
      const auto input = op.operand("input");
      const auto producer = input ? input->defining_op() : std::nullopt;
      const auto definition = producer ? position(ops, *producer) : std::nullopt;
      if (!slot || *slot < 0 || !bytes || !definition ||
          *definition >= index) {
        diagnostics.report("storage placement has an invalid slot or type");
        return std::nullopt;
      }
      std::size_t last_use = index;
      for (const auto& user : result.users()) {
        const auto user_position = position(ops, user);
        if (!user_position) {
          diagnostics.report("storage validation lost a placed SSA user");
          return std::nullopt;
        }
        if (*user_position > last_use) {
          last_use = *user_position;
        }
      }
      if (returned(function_body, result)) {
        last_use = ops.size();
      }

      SlotUse* existing = nullptr;
      for (auto& candidate : slots) {
        if (candidate.id == *slot) {
          existing = &candidate;
          break;
        }
      }
      if (existing != nullptr) {
        if (existing->type != result.type() ||
            existing->last_use >= *definition) {
          diagnostics.report("storage plan reuses an incompatible or live slot");
          return std::nullopt;
        }
        existing->last_use = last_use;
      } else {
        slots.push_back(SlotUse{*slot, result.type(), *bytes, last_use});
      }
    }
  }

  std::uint64_t total = 0;
  for (const auto& slot : slots) {
    if (slot.bytes > std::numeric_limits<std::uint64_t>::max() - total) {
      diagnostics.report("scratch byte count overflows");
      return std::nullopt;
    }
    total += slot.bytes;
  }
  return total;
}

std::optional<std::int64_t>
scratch_bytes(joggle::Compiler& compiler, const joggle::Module& input,
              const Schema& schema, joggle::Diagnostics& diagnostics) {
  std::uint64_t required = 0;
  for (const auto& member : input.functions()) {
    const joggle::Function* function_body = member.body();
    if (function_body == nullptr) {
      diagnostics.report("scratch analysis requires materialized Functions");
      return std::nullopt;
    }
    const auto bytes =
        function_scratch_bytes(compiler, *function_body, schema, diagnostics);
    if (!bytes) {
      return std::nullopt;
    }
    if (*bytes > required) {
      required = *bytes;
    }
  }
  if (required > static_cast<std::uint64_t>(
                     std::numeric_limits<std::int64_t>::max())) {
    diagnostics.report("scratch byte count does not fit in int");
    return std::nullopt;
  }
  return static_cast<std::int64_t>(required);
}

struct Machine {
  std::uint64_t lanes = 0;
  std::uint64_t macs_per_lane = 0;
  std::uint64_t local_bytes_per_cycle = 0;
  std::uint64_t external_bytes_per_cycle = 0;
  std::uint64_t scratch_capacity = 0;
  std::uint64_t launch_cycles = 0;
};

struct Event {
  std::string function;
  std::size_t op_index = 0;
  std::string operation;
  std::uint64_t start = 0;
  std::uint64_t end = 0;
  std::uint64_t compute = 0;
  std::uint64_t local = 0;
  std::uint64_t external = 0;
  std::uint64_t launch = 0;
};

struct Timeline {
  std::string module;
  std::string digest;
  Machine machine;
  std::uint64_t scratch = 0;
  std::uint64_t cycles = 0;
  std::vector<Event> events;
};

std::optional<Machine> machine(joggle::Compiler& compiler,
                               const joggle::Type& type,
                               const Schema& schema,
                               joggle::Diagnostics& diagnostics) {
  if (!compiler.conforms(type.schema(), schema.machine)) {
    diagnostics.report("anchor cycle model requires a machine type");
    return std::nullopt;
  }
  const auto lanes = type.get<std::int64_t>("lanes");
  const auto macs = type.get<std::int64_t>("macs_per_lane");
  const auto local = type.get<std::int64_t>("local_bytes_per_cycle");
  const auto external = type.get<std::int64_t>("external_bytes_per_cycle");
  const auto scratch = type.get<std::int64_t>("scratch_capacity");
  const auto launch = type.get<std::int64_t>("launch_cycles");
  if (!lanes || !macs || !local || !external || !scratch || !launch ||
      *lanes <= 0 || *macs <= 0 || *local <= 0 || *external <= 0 ||
      *scratch < 0 || *launch < 0) {
    diagnostics.report("anchor machine fields are missing or invalid");
    return std::nullopt;
  }
  return Machine{static_cast<std::uint64_t>(*lanes),
                 static_cast<std::uint64_t>(*macs),
                 static_cast<std::uint64_t>(*local),
                 static_cast<std::uint64_t>(*external),
                 static_cast<std::uint64_t>(*scratch),
                 static_cast<std::uint64_t>(*launch)};
}

std::optional<std::uint64_t>
elements(const joggle::Type& type, joggle::Diagnostics& diagnostics) {
  const auto shape = type.get<std::vector<std::int64_t>>("shape");
  if (!shape) {
    diagnostics.report("anchor operation has no shaped reference result");
    return std::nullopt;
  }
  std::uint64_t count = 1;
  for (const std::int64_t dimension : *shape) {
    if (dimension <= 0 ||
        static_cast<std::uint64_t>(dimension) >
            std::numeric_limits<std::uint64_t>::max() / count) {
      diagnostics.report("anchor element count overflows");
      return std::nullopt;
    }
    count *= static_cast<std::uint64_t>(dimension);
  }
  return count;
}

bool multiply(std::uint64_t& value, std::uint64_t factor,
              joggle::Diagnostics& diagnostics, std::string_view context) {
  if (factor != 0U &&
      value > std::numeric_limits<std::uint64_t>::max() / factor) {
    diagnostics.report(std::string(context) + " overflows");
    return false;
  }
  value *= factor;
  return true;
}

bool add(std::uint64_t& value, std::uint64_t amount,
         joggle::Diagnostics& diagnostics, std::string_view context) {
  if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
    diagnostics.report(std::string(context) + " overflows");
    return false;
  }
  value += amount;
  return true;
}

struct Work {
  std::uint64_t macs = 0;
  std::uint64_t lanes = 0;
};

std::optional<Work>
work(const joggle::Op& op, joggle::Diagnostics& diagnostics) {
  const std::string_view name = op.callee().name();
  if (name == "flatten_nchw") {
    return Work{};
  }
  const auto output = elements(op.value().type(), diagnostics);
  if (!output) {
    return std::nullopt;
  }
  std::uint64_t amount = *output;
  if (name == "conv2d_nchw" || name == "conv_relu_nchw") {
    const auto weight = op.operand("weight");
    const auto shape = weight
                           ? weight->type().get<std::vector<std::int64_t>>(
                                 "shape")
                           : std::optional<std::vector<std::int64_t>>{};
    if (!shape || shape->size() != 4U) {
      diagnostics.report("anchor convolution has no four-dimensional weight");
      return std::nullopt;
    }
    for (std::size_t axis = 1; axis < shape->size(); ++axis) {
      if ((*shape)[axis] <= 0 ||
          !multiply(amount, static_cast<std::uint64_t>((*shape)[axis]),
                    diagnostics, "convolution work")) {
        return std::nullopt;
      }
    }
    return Work{.macs = amount,
                .lanes = name == "conv_relu_nchw" ? *output : 0U};
  } else if (name == "linear") {
    const auto weight = op.operand("weight");
    const auto shape = weight
                           ? weight->type().get<std::vector<std::int64_t>>(
                                 "shape")
                           : std::optional<std::vector<std::int64_t>>{};
    if (!shape || shape->size() != 2U || (*shape)[1] <= 0 ||
        !multiply(amount, static_cast<std::uint64_t>((*shape)[1]),
                  diagnostics, "linear work")) {
      diagnostics.report("anchor linear operation has an invalid weight");
      return std::nullopt;
    }
    return Work{.macs = amount};
  } else if (name == "batch_norm_nchw") {
    if (!multiply(amount, 5U, diagnostics, "batch normalization work")) {
      return std::nullopt;
    }
  } else if (name == "batch_norm_relu_nchw") {
    if (!multiply(amount, 6U, diagnostics,
                  "fused batch normalization and ReLU work")) {
      return std::nullopt;
    }
  } else if (name == "max_pool2d_nchw") {
    const auto height = op.property<std::int64_t>("kernel_h");
    const auto width = op.property<std::int64_t>("kernel_w");
    if (!height || !width || *height <= 0 || *width <= 0 ||
        !multiply(amount, static_cast<std::uint64_t>(*height), diagnostics,
                  "pooling work") ||
        !multiply(amount, static_cast<std::uint64_t>(*width), diagnostics,
                  "pooling work")) {
      return std::nullopt;
    }
  } else if (name == "global_average_pool_nchw") {
    const auto input = op.operand("input");
    if (!input) {
      diagnostics.report("anchor global pooling has no input");
      return std::nullopt;
    }
    const auto input_elements = elements(input->type(), diagnostics);
    if (!input_elements) {
      return std::nullopt;
    }
    amount = *input_elements;
  } else if (name != "relu" && name != "add") {
    diagnostics.report("anchor has no cycle model for '" + std::string(name) +
                       "'");
    return std::nullopt;
  }
  return Work{.lanes = amount};
}

struct Traffic {
  std::uint64_t local = 0;
  std::uint64_t external = 0;
};

std::optional<Traffic>
traffic(joggle::Compiler& compiler, const joggle::Op& op,
        const Schema& schema, joggle::Diagnostics& diagnostics) {
  Traffic result;
  const auto account = [&](const joggle::Type& type) -> bool {
    if (!compiler.conforms(type.schema(), schema.memory_reference)) {
      return true;
    }
    const auto bytes = reference_bytes(compiler, type, schema, diagnostics);
    const auto space = type.get<joggle::Type>("space_type");
    if (!bytes || !space) {
      return false;
    }
    return space->schema() == schema.local
               ? add(result.local, *bytes, diagnostics, "local traffic")
               : add(result.external, *bytes, diagnostics,
                     "external traffic");
  };
  for (const auto& operand : op.operands()) {
    if (!account(operand.type())) {
      return std::nullopt;
    }
  }
  for (const auto& output : op.results()) {
    if (!account(output.type())) {
      return std::nullopt;
    }
  }
  return result;
}

std::uint64_t ceil_div(std::uint64_t value, std::uint64_t divisor) {
  return value / divisor + (value % divisor == 0U ? 0U : 1U);
}

std::optional<Timeline>
simulate(joggle::Compiler& compiler, const joggle::Module& input,
         const joggle::Type& target, const Schema& schema,
         joggle::Diagnostics& diagnostics) {
  const auto config = machine(compiler, target, schema, diagnostics);
  const auto required = scratch_bytes(compiler, input, schema, diagnostics);
  if (!config || !required) {
    return std::nullopt;
  }
  if (static_cast<std::uint64_t>(*required) > config->scratch_capacity) {
    diagnostics.report("anchor storage plan exceeds scratch capacity");
    return std::nullopt;
  }
  std::uint64_t macs_per_cycle = config->lanes;
  if (!multiply(macs_per_cycle, config->macs_per_lane, diagnostics,
                "machine MAC throughput")) {
    return std::nullopt;
  }

  Timeline result{.module = std::string(input.name()),
                  .digest = std::string(input.digest()),
                  .machine = *config,
                  .scratch = static_cast<std::uint64_t>(*required)};
  for (const auto& member : input.functions()) {
    const joggle::Function* function_body = member.body();
    if (function_body == nullptr) {
      diagnostics.report("anchor simulation requires materialized Functions");
      return std::nullopt;
    }
    std::size_t op_index = 0;
    for (const auto& op : function_body->ops()) {
      const std::string_view name = op.callee().name();
      if (compiler.conforms(op.callee(), schema.placement) ||
          name == "constant") {
        ++op_index;
        continue;
      }
      if (op.callee().symbol().module_name() != schema.target.name()) {
        diagnostics.report("anchor simulation encountered a foreign call");
        return std::nullopt;
      }
      const auto operation_work = work(op, diagnostics);
      const auto operation_traffic = traffic(compiler, op, schema, diagnostics);
      if (!operation_work || !operation_traffic) {
        return std::nullopt;
      }
      std::uint64_t compute =
          ceil_div(operation_work->macs, macs_per_cycle);
      if (!add(compute, ceil_div(operation_work->lanes, config->lanes),
               diagnostics, "operation compute cycle count")) {
        return std::nullopt;
      }
      const std::uint64_t local =
          ceil_div(operation_traffic->local, config->local_bytes_per_cycle);
      const std::uint64_t external = ceil_div(
          operation_traffic->external, config->external_bytes_per_cycle);
      std::uint64_t active = compute;
      if (local > active) {
        active = local;
      }
      if (external > active) {
        active = external;
      }
      std::uint64_t operation_cycles = active;
      if (!add(operation_cycles, config->launch_cycles, diagnostics,
               "operation cycle count") ||
          !add(result.cycles, operation_cycles, diagnostics,
               "module cycle count")) {
        return std::nullopt;
      }
      result.events.push_back(
          Event{.function = std::string(member.name()),
                .op_index = op_index,
                .operation = op.callee().symbol().qualified_name(),
                .start = result.cycles - operation_cycles,
                .end = result.cycles,
                .compute = compute,
                .local = local,
                .external = external,
                .launch = config->launch_cycles});
      ++op_index;
    }
  }
  if (result.cycles > static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max())) {
    diagnostics.report("module cycle count does not fit in int");
    return std::nullopt;
  }
  return result;
}

std::int64_t duration(const Timeline& input) {
  return static_cast<std::int64_t>(input.cycles);
}

joggle::Bytes encode(std::string_view input) {
  joggle::Bytes result;
  result.reserve(input.size());
  for (const char value : input) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

joggle::Bytes trace(const Timeline& input) {
  std::string output = "anchor timeline 1\nmodule ";
  output += input.module;
  output += '#';
  output += input.digest;
  output += "\nscratch-bytes ";
  output += std::to_string(input.scratch);
  output += "\nlanes ";
  output += std::to_string(input.machine.lanes);
  output += "\nmacs-per-lane ";
  output += std::to_string(input.machine.macs_per_lane);
  output += "\nlocal-bytes-per-cycle ";
  output += std::to_string(input.machine.local_bytes_per_cycle);
  output += "\nexternal-bytes-per-cycle ";
  output += std::to_string(input.machine.external_bytes_per_cycle);
  output += "\nscratch-capacity ";
  output += std::to_string(input.machine.scratch_capacity);
  output += "\nlaunch-cycles ";
  output += std::to_string(input.machine.launch_cycles);
  output += "\ncycles ";
  output += std::to_string(input.cycles);
  output += "\nevents ";
  output += std::to_string(input.events.size());
  output += "\ncolumns function op-index operation start end compute-cycles "
            "local-cycles external-cycles launch-cycles\n";
  for (const Event& event : input.events) {
    output += "event ";
    output += event.function;
    output += ' ';
    output += std::to_string(event.op_index);
    output += ' ';
    output += event.operation;
    output += ' ';
    output += std::to_string(event.start);
    output += ' ';
    output += std::to_string(event.end);
    output += ' ';
    output += std::to_string(event.compute);
    output += ' ';
    output += std::to_string(event.local);
    output += ' ';
    output += std::to_string(event.external);
    output += ' ';
    output += std::to_string(event.launch);
    output += '\n';
  }
  return encode(output);
}

std::optional<joggle::Bytes>
emit(joggle::Compiler& compiler, const joggle::Module& input,
     const joggle::Type& target, const Schema& schema,
     joggle::Diagnostics& diagnostics) {
  const auto timeline = simulate(compiler, input, target, schema, diagnostics);
  if (!timeline) {
    return std::nullopt;
  }
  const auto bundle = joggle::anchor::kernel_bundle(compiler, input,
                                                     diagnostics);
  if (!bundle) {
    return std::nullopt;
  }
  std::string output = "anchor 2\nsource ";
  output += input.name();
  output += '#';
  output += input.digest();
  output += "\nbundle ";
  output += bundle->name();
  output += '#';
  output += bundle->digest();
  output += "\nlanes ";
  output += std::to_string(timeline->machine.lanes);
  output += "\nmacs-per-lane ";
  output += std::to_string(timeline->machine.macs_per_lane);
  output += "\nscratch-bytes ";
  output += std::to_string(timeline->scratch);
  output += "\ncycles ";
  output += std::to_string(timeline->cycles);
  output += "\n---\n";
  output += joggle::format(*bundle);
  return encode(output);
}

std::optional<Schema> schema(joggle::Compiler& compiler,
                             joggle::Diagnostics& diagnostics) {
  const auto target = compiler.module("anchor");
  const auto tensor = compiler.module("tensor");
  const auto memory = compiler.module("mem");
  if (!target || !tensor || !memory) {
    diagnostics.report("anchor behavior requires its Module imports");
    return std::nullopt;
  }
  const auto reference = target->type("ref");
  const auto linear = target->type("linear");
  const auto tiled = target->type("tiled");
  const auto io = target->type("io");
  const auto read_only = target->type("read_only");
  const auto local = target->type("local");
  const auto timeline = target->type("timeline");
  const auto ranked_tensor = tensor->interface("ranked_tensor");
  const auto memory_reference = memory->interface("reference");
  const auto immutable_data = tensor->interface("immutable_data");
  const auto placement = target->interface("placement");
  const auto machine_interface = target->interface("machine");
  const auto place = target->function("place");
  if (!reference || !linear || !tiled || !io || !read_only || !local ||
      !timeline || !ranked_tensor || !memory_reference || !immutable_data ||
      !placement || !machine_interface || !place) {
    diagnostics.report("anchor behavior does not match its schema");
    return std::nullopt;
  }
  return Schema{*target,           *reference,       *linear,
                *tiled,           *io,              *read_only,
                *local,           *timeline,        *ranked_tensor,
                *memory_reference, *immutable_data, *placement,
                *machine_interface, *place};
}

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto resolved = schema(compiler, diagnostics);
  if (!resolved) {
    return;
  }
  if (!compiler.represent<Timeline>(resolved->timeline)) {
    return;
  }
  compiler.bind(
      module, "map",
      [resolved](joggle::Compiler& bound, joggle::Module input,
                 std::int64_t rows, std::int64_t columns,
                 joggle::Diagnostics& reported) {
        return map_module(bound, std::move(input), rows, columns, *resolved,
                          reported);
      });
  compiler.bind(
      module, "local_bytes_upper_bound",
      [resolved](joggle::Compiler& bound, const joggle::Module& input,
                 joggle::Diagnostics& reported) {
        return local_bytes_upper_bound(bound, input, *resolved, reported);
      });
  compiler.bind(
      module, "fuse_relu",
      [resolved](joggle::Module input, joggle::Diagnostics& reported) {
        return fuse_relu(std::move(input), *resolved, reported);
      });
  compiler.bind(
      module, "plan_storage",
      [resolved](joggle::Compiler& bound, joggle::Module input,
                 joggle::Diagnostics& reported) {
        return plan_storage(bound, std::move(input), *resolved, reported);
      });
  compiler.bind(
      module, "scratch_bytes",
      [resolved](joggle::Compiler& bound, const joggle::Module& input,
                 joggle::Diagnostics& reported) {
        return scratch_bytes(bound, input, *resolved, reported);
      });
  compiler.bind(
      module, "simulate",
      [resolved](joggle::Compiler& bound, const joggle::Module& input,
                 const joggle::Type& target,
                 joggle::Diagnostics& reported) {
        return simulate(bound, input, target, *resolved, reported);
      });
  compiler.bind(module, "duration", duration);
  compiler.bind(module, "trace", trace);
  compiler.bind(module, "bundle", joggle::anchor::kernel_bundle);
  compiler.bind(module, "kernel_report", joggle::anchor::kernel_report);
  compiler.bind(
      module, "execute_f32",
      [resolved](joggle::Compiler& bound, const joggle::Module& program,
                 const joggle::Bytes& input,
                 joggle::Diagnostics& reported) {
        const joggle::anchor::ExecutionSchema execution{
            resolved->target, resolved->memory_reference, resolved->placement};
        return joggle::anchor::execute_f32(bound, program, input, execution,
                                           reported);
      });
  compiler.bind(
      module, "emit",
      [resolved](joggle::Compiler& bound, const joggle::Module& input,
                 const joggle::Type& target,
                 joggle::Diagnostics& reported) {
        return emit(bound, input, target, *resolved, reported);
      });
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
