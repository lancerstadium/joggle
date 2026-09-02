#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <joggle/joggle.h>

namespace {

std::optional<std::vector<std::int64_t>>
shape(const joggle::Type& type, const joggle::Module::TypeDecl& schema) {
  if (type.schema() != schema) {
    return std::nullopt;
  }
  return type.get<std::vector<std::int64_t>>("shape");
}

std::optional<std::size_t> elements(const joggle::Type& type,
                                    const joggle::Module::TypeDecl& schema) {
  const auto dimensions = shape(type, schema);
  if (!dimensions) {
    return std::nullopt;
  }
  std::size_t count = 1U;
  for (const std::int64_t dimension : *dimensions) {
    if (dimension <= 0 || static_cast<std::uint64_t>(dimension) >
                              std::numeric_limits<std::size_t>::max() / count) {
      return std::nullopt;
    }
    count *= static_cast<std::size_t>(dimension);
  }
  return count;
}

bool bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto bitmath = compiler.module("bitmath");
  const auto format =
      bitmath ? bitmath->interface("numeric_format") : std::nullopt;
  const auto tensor_type = module.type("tensor");
  const auto tensor_data = module.attribute("tensor_data");
  const auto input = module.operation("input");
  const auto parameter = module.operation("parameter");
  const auto reshape = module.operation("reshape");
  if (!format || !tensor_type || !tensor_data || !input || !parameter ||
      !reshape) {
    diagnostics.report("miniai behavior does not match its linked schema");
    return false;
  }

  compiler.bind(
      *tensor_type,
      [&compiler, format = *format, tensor_schema = *tensor_type](
          const joggle::Type& type, joggle::Diagnostics& type_diagnostics) {
        const auto element = type.get<joggle::Type>("element");
        const auto dimensions = shape(type, tensor_schema);
        if (!element || !dimensions || dimensions->empty() ||
            !std::all_of(
                dimensions->begin(), dimensions->end(),
                [](std::int64_t dimension) { return dimension > 0; }) ||
            !compiler.conforms(element->schema(), format)) {
          type_diagnostics.report(
              "miniai.tensor needs a numeric element format and "
              "positive dimensions");
          return false;
        }
        return true;
      });
  compiler.bind(*tensor_data,
                [](const joggle::Attribute& data,
                   joggle::Diagnostics& attribute_diagnostics) {
                  const auto values =
                      data.get<std::vector<std::int64_t>>("values");
                  if (!values || values->empty()) {
                    attribute_diagnostics.report(
                        "miniai.tensor_data needs at least one value");
                    return false;
                  }
                  return true;
                });
  compiler.bind(*input,
                [](const joggle::Operation& operation,
                   joggle::Diagnostics& operation_diagnostics) {
                  const auto name = operation.get<std::string>("name");
                  if (!name || name->empty()) {
                    operation_diagnostics.report(
                        "miniai.input needs a non-empty name");
                    return false;
                  }
                  return true;
                });
  compiler.bind(
      *parameter,
      [tensor_schema = *tensor_type,
       data_schema = *tensor_data](const joggle::Operation& operation,
                                   joggle::Diagnostics& operation_diagnostics) {
        const auto name = operation.get<std::string>("name");
        const auto data = operation.get<joggle::Attribute>("data");
        if (!name || name->empty() || !data || data->schema() != data_schema ||
            operation.results().size() != 1U) {
          operation_diagnostics.report(
              "miniai.parameter needs a name, tensor_data, and one result");
          return false;
        }
        const auto expected =
            elements(operation.result(0).type(), tensor_schema);
        const auto values = data->get<std::vector<std::int64_t>>("values");
        if (!expected || !values || values->size() != *expected) {
          operation_diagnostics.report(
              "miniai.parameter data size does not match its result tensor");
          return false;
        }
        return true;
      });
  compiler.bind(
      *reshape,
      [tensor_schema = *tensor_type](
          const joggle::Operation& operation,
          joggle::Diagnostics& operation_diagnostics) {
        if (operation.operands().size() != 1U ||
            operation.results().size() != 1U) {
          operation_diagnostics.report(
              "miniai.reshape needs one input and one result");
          return false;
        }
        const auto input_elements =
            elements(operation.operands().front().type(), tensor_schema);
        const auto output_elements =
            elements(operation.result(0).type(), tensor_schema);
        if (!input_elements || !output_elements ||
            *input_elements != *output_elements) {
          operation_diagnostics.report(
              "miniai.reshape must preserve the element count");
          return false;
        }
        return true;
      });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
