#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

namespace {

struct Schema {
  joggle::Module target;
  joggle::Module::TypeDecl reference;
  joggle::Module::TypeDecl linear;
  joggle::Module::TypeDecl tiled;
  joggle::Module::TypeDecl io;
  joggle::Module::TypeDecl read_only;
  joggle::Module::TypeDecl local;
  joggle::Module::InterfaceDecl ranked_tensor;
  joggle::Module::InterfaceDecl memory_reference;
  joggle::Module::InterfaceDecl immutable_data;
  joggle::Module::InterfaceDecl placement;
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

std::optional<std::uint64_t>
local_bytes(joggle::Compiler& compiler, const joggle::Type& type,
            const Schema& schema, joggle::Diagnostics& diagnostics) {
  if (!compiler.conforms(type.schema(), schema.memory_reference)) {
    return std::uint64_t{0};
  }
  const auto space = type.get<joggle::Type>("space_type");
  if (!space || space->schema() != schema.local) {
    return std::uint64_t{0};
  }
  const auto element = type.get<joggle::Type>("element_type");
  const auto shape = type.get<std::vector<std::int64_t>>("shape");
  const auto bits = element
                        ? element->get<std::int64_t>("storage_bits")
                        : std::optional<std::int64_t>{};
  if (!element || !shape || !bits || *bits <= 0) {
    diagnostics.report("local reference has no positive storage_bits or shape");
    return std::nullopt;
  }

  std::uint64_t total_bits = static_cast<std::uint64_t>(*bits);
  for (const std::int64_t dimension : *shape) {
    if (dimension <= 0 ||
        static_cast<std::uint64_t>(dimension) >
            std::numeric_limits<std::uint64_t>::max() / total_bits) {
      diagnostics.report("local reference size overflows");
      return std::nullopt;
    }
    total_bits *= static_cast<std::uint64_t>(dimension);
  }
  return total_bits / 8U + (total_bits % 8U == 0U ? 0U : 1U);
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
  const auto ranked_tensor = tensor->interface("ranked_tensor");
  const auto memory_reference = memory->interface("reference");
  const auto immutable_data = tensor->interface("immutable_data");
  const auto placement = target->interface("placement");
  const auto place = target->function("place");
  if (!reference || !linear || !tiled || !io || !read_only || !local ||
      !ranked_tensor || !memory_reference || !immutable_data || !placement ||
      !place) {
    diagnostics.report("anchor behavior does not match its schema");
    return std::nullopt;
  }
  return Schema{*target,         *reference,      *linear,
                *tiled,         *io,             *read_only,
                *local,         *ranked_tensor,  *memory_reference,
                *immutable_data, *placement,     *place};
}

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto resolved = schema(compiler, diagnostics);
  if (!resolved) {
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
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
