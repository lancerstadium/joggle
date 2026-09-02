#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
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
  const auto tensor_type = module.type("tensor");
  const auto dense = module.attribute("dense");
  const auto constant = module.operation("constant");
  const auto reshape = module.operation("reshape");
  if (!tensor_type || !dense || !constant || !reshape) {
    diagnostics.report("tensor behavior does not match its linked schema");
    return false;
  }

  compiler.bind(
      *tensor_type,
      [tensor_schema = *tensor_type](
          const joggle::Type& type, joggle::Diagnostics& type_diagnostics) {
        const auto dimensions = shape(type, tensor_schema);
        if (!dimensions || dimensions->empty() ||
            !std::all_of(
                dimensions->begin(), dimensions->end(),
                [](std::int64_t dimension) { return dimension > 0; })) {
          type_diagnostics.report(
              "tensor.tensor needs positive dimensions");
          return false;
        }
        return true;
      });
  compiler.bind(*dense,
                [](const joggle::Attribute& data,
                   joggle::Diagnostics& attribute_diagnostics) {
                  const auto values =
                      data.get<std::vector<std::int64_t>>("values");
                  if (!values || values->empty()) {
                    attribute_diagnostics.report(
                        "tensor.dense needs at least one value");
                    return false;
                  }
                  return true;
                });
  compiler.bind(
      *constant,
      [tensor_schema = *tensor_type,
       data_schema = *dense](const joggle::Operation& operation,
                            joggle::Diagnostics& operation_diagnostics) {
        const auto data = operation.get<joggle::Attribute>("value");
        if (!data || data->schema() != data_schema ||
            operation.results().size() != 1U) {
          operation_diagnostics.report(
              "tensor.constant needs dense data and one result");
          return false;
        }
        const auto expected =
            elements(operation.result(0).type(), tensor_schema);
        const auto values = data->get<std::vector<std::int64_t>>("values");
        if (!expected || !values || values->size() != *expected) {
          operation_diagnostics.report(
              "tensor.constant data size does not match its result tensor");
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
              "tensor.reshape needs one input and one result");
          return false;
        }
        const auto input_elements =
            elements(operation.operands().front().type(), tensor_schema);
        const auto output_elements =
            elements(operation.result(0).type(), tensor_schema);
        if (!input_elements || !output_elements ||
            *input_elements != *output_elements) {
          operation_diagnostics.report(
              "tensor.reshape must preserve the element count");
          return false;
        }
        return true;
      });
  return true;
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
