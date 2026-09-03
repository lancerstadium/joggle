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
    diagnostics.report("reference target could not construct layout or space");
    return std::nullopt;
  }
  const auto mapped =
      compiler.make(schema.reference, *element, *shape, *layout, *space);
  if (!mapped) {
    diagnostics.report("reference target could not construct a reference type");
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
    diagnostics.report("reference target does not map tensor call '" +
                       symbol.qualified_name() + "'");
    return std::nullopt;
  }

  const auto mapped = function(schema.target, name, op.callee().inputs().size());
  if (!mapped) {
    diagnostics.report("reference target has no matching implementation for '" +
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
      diagnostics.report("reference target could not preserve Module data '" +
                         name + "'");
      return std::nullopt;
    }
  }

  for (const auto& member : input.functions()) {
    const joggle::Function* source = member.body();
    if (source == nullptr) {
      diagnostics.report("reference target requires materialized Functions");
      return std::nullopt;
    }
    for (const auto& op : source->ops()) {
      if (op.callee().symbol().module_name() == input.name()) {
        diagnostics.report(
            "reference target currently rejects calls between input Functions");
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

std::optional<Schema> schema(joggle::Compiler& compiler,
                             joggle::Diagnostics& diagnostics) {
  const auto target = compiler.module("reference_target");
  const auto tensor = compiler.module("tensor");
  const auto memory = compiler.module("mem");
  if (!target || !tensor || !memory) {
    diagnostics.report("reference_target behavior requires its Module imports");
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
  if (!reference || !linear || !tiled || !io || !read_only || !local ||
      !ranked_tensor || !memory_reference || !immutable_data) {
    diagnostics.report("reference_target behavior does not match its schema");
    return std::nullopt;
  }
  return Schema{*target,         *reference,      *linear,
                *tiled,         *io,             *read_only,
                *local,         *ranked_tensor,  *memory_reference,
                *immutable_data};
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
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
