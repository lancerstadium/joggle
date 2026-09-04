#include "import.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "onnx.pb.h"

namespace joggle_onnx {
namespace {

using Shape = std::vector<std::int64_t>;

struct Tensor {
  joggle::Value value;
  Shape shape;
};

struct Initializer {
  std::int32_t element = 0;
  Shape shape;
  joggle::Bytes bytes;
  std::vector<std::int64_t> integers;
};

struct TensorSchema {
  joggle::Module::TypeDecl tensor;
  joggle::Module::FunctionDecl constant;
  joggle::Module::FunctionDecl conv;
  joggle::Module::FunctionDecl conv_bias;
  joggle::Module::FunctionDecl relu;
  joggle::Module::FunctionDecl max_pool;
  joggle::Module::FunctionDecl average_pool;
  joggle::Module::FunctionDecl concat;
  joggle::Module::FunctionDecl reshape;
  joggle::Type f32;
  joggle::Type integer;
  joggle::Type boolean;
  joggle::Type string;
  joggle::Type integer_list;
};

bool fail(joggle::Diagnostics& diagnostics, std::string message) {
  diagnostics.report("onnx.read: " + std::move(message));
  return false;
}

bool valid_identifier(std::string_view name) {
  if (name.empty()) {
    return false;
  }
  const auto first = static_cast<unsigned char>(name.front());
  if (std::isalpha(first) == 0 && name.front() != '_') {
    return false;
  }
  return std::all_of(name.begin() + 1, name.end(), [](char value) {
    const auto character = static_cast<unsigned char>(value);
    return std::isalnum(character) != 0 || value == '_';
  });
}

std::optional<std::size_t> element_count(
    std::span<const std::int64_t> shape) {
  std::size_t count = 1;
  for (const auto dimension : shape) {
    if (dimension < 0) {
      return std::nullopt;
    }
    const auto size = static_cast<std::uint64_t>(dimension);
    if (size > std::numeric_limits<std::size_t>::max() ||
        (size != 0 &&
         count > std::numeric_limits<std::size_t>::max() /
                     static_cast<std::size_t>(size))) {
      return std::nullopt;
    }
    count *= static_cast<std::size_t>(size);
  }
  return count;
}

void append_u32(joggle::Bytes& output, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8) {
    output.push_back(
        static_cast<std::byte>((value >> shift) & std::uint32_t{0xff}));
  }
}

void append_u64(joggle::Bytes& output, std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    output.push_back(
        static_cast<std::byte>((value >> shift) & std::uint64_t{0xff}));
  }
}

std::optional<Initializer>
decode_initializer(const onnx::TensorProto& tensor,
                   joggle::Diagnostics& diagnostics) {
  Initializer result;
  result.element = tensor.data_type();
  result.shape.assign(tensor.dims().begin(), tensor.dims().end());
  const auto count = element_count(result.shape);
  if (!count) {
    fail(diagnostics, "initializer '" + tensor.name() +
                          "' has an invalid or overflowing shape");
    return std::nullopt;
  }
  if (tensor.has_segment() || tensor.data_location() ==
                                  onnx::TensorProto_DataLocation_EXTERNAL ||
      tensor.external_data_size() != 0) {
    fail(diagnostics, "initializer '" + tensor.name() +
                          "' uses segmented or external storage");
    return std::nullopt;
  }

  const bool has_raw = !tensor.raw_data().empty();
  if (result.element == onnx::TensorProto_DataType_FLOAT) {
    if (tensor.int32_data_size() != 0 || tensor.int64_data_size() != 0 ||
        tensor.double_data_size() != 0 || tensor.uint64_data_size() != 0 ||
        tensor.string_data_size() != 0 ||
        (has_raw && tensor.float_data_size() != 0)) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' has conflicting FLOAT storage fields");
      return std::nullopt;
    }
    if (has_raw) {
      if (tensor.raw_data().size() != *count * sizeof(float)) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' raw FLOAT byte count does not match shape");
        return std::nullopt;
      }
      result.bytes.reserve(tensor.raw_data().size());
      for (const char value : tensor.raw_data()) {
        result.bytes.push_back(
            static_cast<std::byte>(static_cast<unsigned char>(value)));
      }
    } else {
      if (static_cast<std::size_t>(tensor.float_data_size()) != *count) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' FLOAT value count does not match shape");
        return std::nullopt;
      }
      result.bytes.reserve(*count * sizeof(float));
      for (const float value : tensor.float_data()) {
        append_u32(result.bytes, std::bit_cast<std::uint32_t>(value));
      }
    }
    return result;
  }

  if (result.element == onnx::TensorProto_DataType_INT64) {
    if (tensor.int32_data_size() != 0 || tensor.float_data_size() != 0 ||
        tensor.double_data_size() != 0 || tensor.uint64_data_size() != 0 ||
        tensor.string_data_size() != 0 ||
        (has_raw && tensor.int64_data_size() != 0)) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' has conflicting INT64 storage fields");
      return std::nullopt;
    }
    if (has_raw) {
      if (tensor.raw_data().size() != *count * sizeof(std::int64_t)) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' raw INT64 byte count does not match shape");
        return std::nullopt;
      }
      result.bytes.reserve(tensor.raw_data().size());
      for (const char value : tensor.raw_data()) {
        result.bytes.push_back(
            static_cast<std::byte>(static_cast<unsigned char>(value)));
      }
      result.integers.reserve(*count);
      for (std::size_t index = 0; index < *count; ++index) {
        std::uint64_t bits = 0;
        for (unsigned byte = 0; byte < 8; ++byte) {
          bits |= static_cast<std::uint64_t>(
                      std::to_integer<unsigned char>(
                          result.bytes[index * 8 + byte]))
                  << (byte * 8);
        }
        result.integers.push_back(static_cast<std::int64_t>(bits));
      }
    } else {
      if (static_cast<std::size_t>(tensor.int64_data_size()) != *count) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' INT64 value count does not match shape");
        return std::nullopt;
      }
      result.integers.assign(tensor.int64_data().begin(),
                             tensor.int64_data().end());
      result.bytes.reserve(*count * sizeof(std::int64_t));
      for (const auto value : result.integers) {
        append_u64(result.bytes, static_cast<std::uint64_t>(value));
      }
    }
    return result;
  }

  fail(diagnostics, "initializer '" + tensor.name() +
                        "' has unsupported element type " +
                        std::to_string(result.element));
  return std::nullopt;
}

std::optional<std::pair<std::int32_t, Shape>>
value_type(const onnx::ValueInfoProto& value,
           joggle::Diagnostics& diagnostics) {
  if (!value.has_type() || !value.type().has_tensor_type()) {
    fail(diagnostics, "value '" + value.name() +
                          "' is not a typed tensor");
    return std::nullopt;
  }
  const auto& tensor = value.type().tensor_type();
  if (!tensor.has_shape()) {
    fail(diagnostics, "value '" + value.name() +
                          "' has no static shape");
    return std::nullopt;
  }
  Shape shape;
  shape.reserve(static_cast<std::size_t>(tensor.shape().dim_size()));
  for (const auto& dimension : tensor.shape().dim()) {
    if (!dimension.has_dim_value() || dimension.dim_value() < 0) {
      fail(diagnostics, "value '" + value.name() +
                            "' has an absent, symbolic, or negative dimension");
      return std::nullopt;
    }
    shape.push_back(dimension.dim_value());
  }
  return std::pair{tensor.elem_type(), std::move(shape)};
}

std::optional<joggle::Module::FunctionDecl>
overload(const joggle::Module& module, std::string_view name,
         std::size_t inputs) {
  const auto declarations = module.overloads(name);
  const auto found = std::find_if(
      declarations.begin(), declarations.end(), [inputs](const auto& fn) {
        return fn.inputs().size() == inputs;
      });
  return found == declarations.end()
             ? std::nullopt
             : std::optional<joggle::Module::FunctionDecl>{*found};
}

std::optional<TensorSchema>
load_schema(joggle::Compiler& compiler, joggle::Diagnostics& diagnostics) {
  const auto module = compiler.module("tensor");
  const auto f32 = compiler.make("f32");
  const auto integer = compiler.make("int");
  const auto boolean = compiler.make("bool");
  const auto string = compiler.make("string");
  const auto integer_list_decl =
      compiler.module("prelude")
          ? compiler.module("prelude")->type("list")
          : std::nullopt;
  const auto integer_list =
      integer && integer_list_decl
          ? compiler.make(*integer_list_decl, *integer)
          : std::nullopt;
  if (!module || !f32 || !integer || !boolean || !string || !integer_list) {
    fail(diagnostics, "linked tensor and Prelude schemas are unavailable");
    return std::nullopt;
  }
  const auto tensor = module->type("tensor");
  const auto constant = overload(*module, "constant", 1);
  const auto conv = overload(*module, "conv", 6);
  const auto conv_bias = overload(*module, "conv", 7);
  const auto relu = overload(*module, "relu", 1);
  const auto max_pool = overload(*module, "max_pool", 5);
  const auto average_pool = overload(*module, "average_pool", 5);
  const auto concat = overload(*module, "concat", 3);
  const auto reshape = overload(*module, "reshape", 2);
  if (!tensor || !constant || !conv || !conv_bias || !relu || !max_pool ||
      !average_pool || !concat || !reshape) {
    fail(diagnostics, "tensor Module declarations do not match ONNX importer");
    return std::nullopt;
  }
  return TensorSchema{*tensor,       *constant,    *conv,
                      *conv_bias,    *relu,        *max_pool,
                      *average_pool, *concat,      *reshape,
                      *f32,          *integer,     *boolean,
                      *string,       *integer_list};
}

std::optional<joggle::Type>
tensor_type(joggle::Compiler& compiler, const TensorSchema& schema,
            const Shape& shape) {
  return compiler.make(schema.tensor, schema.f32, shape);
}

std::optional<joggle::Value>
known(joggle::Compiler& compiler, const joggle::Type& type,
      std::int64_t value) {
  return compiler.known(type, value);
}

std::optional<joggle::Value>
known(joggle::Compiler& compiler, const joggle::Type& type, bool value) {
  return compiler.known(type, value);
}

std::optional<joggle::Value>
known(joggle::Compiler& compiler, const joggle::Type& type,
      const std::string& value) {
  return compiler.known(type, value);
}

std::optional<joggle::Value>
known(joggle::Compiler& compiler, const joggle::Type& type,
      const Shape& value) {
  return compiler.known(type, value);
}

std::optional<std::map<std::string, const onnx::AttributeProto*>>
attributes(const onnx::NodeProto& node, const std::set<std::string>& allowed,
           joggle::Diagnostics& diagnostics) {
  std::map<std::string, const onnx::AttributeProto*> result;
  for (const auto& attribute : node.attribute()) {
    if (!allowed.contains(attribute.name())) {
      fail(diagnostics, "node '" + node.name() + "' (" + node.op_type() +
                            ") has unsupported attribute '" +
                            attribute.name() + "'");
      return std::nullopt;
    }
    if (!result.emplace(attribute.name(), &attribute).second) {
      fail(diagnostics, "node '" + node.name() +
                            "' has duplicate attribute '" + attribute.name() +
                            "'");
      return std::nullopt;
    }
  }
  return result;
}

std::optional<std::int64_t>
integer_attribute(const std::map<std::string, const onnx::AttributeProto*>& attrs,
                  std::string_view name, std::int64_t fallback,
                  joggle::Diagnostics& diagnostics) {
  const auto found = attrs.find(std::string(name));
  if (found == attrs.end()) {
    return fallback;
  }
  if (found->second->type() != onnx::AttributeProto_AttributeType_INT ||
      !found->second->has_i()) {
    fail(diagnostics, "attribute '" + std::string(name) +
                          "' must be INT");
    return std::nullopt;
  }
  return found->second->i();
}

std::optional<Shape>
integers_attribute(const std::map<std::string, const onnx::AttributeProto*>& attrs,
                   std::string_view name, Shape fallback,
                   joggle::Diagnostics& diagnostics) {
  const auto found = attrs.find(std::string(name));
  if (found == attrs.end()) {
    return fallback;
  }
  if (found->second->type() != onnx::AttributeProto_AttributeType_INTS) {
    fail(diagnostics, "attribute '" + std::string(name) +
                          "' must be INTS");
    return std::nullopt;
  }
  return Shape(found->second->ints().begin(), found->second->ints().end());
}

bool positive_pair(const Shape& values) {
  return values.size() == 2 &&
         std::all_of(values.begin(), values.end(),
                     [](std::int64_t value) { return value > 0; });
}

bool nonnegative_pads(const Shape& values) {
  return values.size() == 4 &&
         std::all_of(values.begin(), values.end(),
                     [](std::int64_t value) { return value >= 0; });
}

std::optional<Shape>
spatial_shape(const Shape& input, const Shape& kernel, const Shape& strides,
              const Shape& pads, const Shape& dilations,
              joggle::Diagnostics& diagnostics, std::string_view operation) {
  if (input.size() != 4 || !positive_pair(kernel) ||
      !positive_pair(strides) || !positive_pair(dilations) ||
      !nonnegative_pads(pads)) {
    fail(diagnostics, std::string(operation) +
                          " requires NCHW rank 4 and two-dimensional static "
                          "kernel/stride/dilation/pad values");
    return std::nullopt;
  }
  Shape result{input[0], input[1], 0, 0};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto extent =
        input[axis + 2] + pads[axis] + pads[axis + 2] -
        dilations[axis] * (kernel[axis] - 1) - 1;
    if (extent < 0) {
      fail(diagnostics, std::string(operation) +
                            " kernel exceeds padded input");
      return std::nullopt;
    }
    result[axis + 2] = extent / strides[axis] + 1;
  }
  return result;
}

joggle::SourceRange location(const onnx::GraphProto& graph,
                             std::string_view kind, std::string_view name,
                             std::size_t ordinal) {
  const auto line = ordinal + 1;
  return {"onnx:" + graph.name() + "/" + std::string(kind) + "/" +
              std::string(name),
          {line, 1},
          {line, 2}};
}

std::optional<Shape>
reshape_shape(const Shape& input, const Shape& requested,
              joggle::Diagnostics& diagnostics) {
  const auto input_count = element_count(input);
  if (!input_count) {
    fail(diagnostics, "Reshape input has invalid element count");
    return std::nullopt;
  }
  Shape result;
  result.reserve(requested.size());
  std::optional<std::size_t> inferred;
  std::size_t known_count = 1;
  for (std::size_t index = 0; index < requested.size(); ++index) {
    auto dimension = requested[index];
    if (dimension == 0) {
      if (index >= input.size()) {
        fail(diagnostics, "Reshape zero dimension has no input counterpart");
        return std::nullopt;
      }
      dimension = input[index];
    } else if (dimension == -1) {
      if (inferred) {
        fail(diagnostics, "Reshape has more than one inferred dimension");
        return std::nullopt;
      }
      inferred = index;
      result.push_back(-1);
      continue;
    } else if (dimension < 0) {
      fail(diagnostics, "Reshape has an invalid negative dimension");
      return std::nullopt;
    }
    if (dimension != 0 &&
        known_count > std::numeric_limits<std::size_t>::max() /
                          static_cast<std::size_t>(dimension)) {
      fail(diagnostics, "Reshape element count overflows");
      return std::nullopt;
    }
    known_count *= static_cast<std::size_t>(dimension);
    result.push_back(dimension);
  }
  if (inferred) {
    if (known_count == 0 || *input_count % known_count != 0) {
      fail(diagnostics, "Reshape inferred dimension is not integral");
      return std::nullopt;
    }
    result[*inferred] =
        static_cast<std::int64_t>(*input_count / known_count);
  } else if (known_count != *input_count) {
    fail(diagnostics, "Reshape changes the number of elements");
    return std::nullopt;
  }
  return result;
}

}  // namespace

std::optional<joggle::Module>
read(joggle::Compiler& compiler, const joggle::Bytes& input, std::string name,
     joggle::Diagnostics& diagnostics) {
  if (!valid_identifier(name)) {
    fail(diagnostics, "module name '" + name + "' is not an identifier");
    return std::nullopt;
  }
  if (input.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    fail(diagnostics, "input exceeds Protobuf parser size");
    return std::nullopt;
  }

  onnx::ModelProto source;
  if (!source.ParseFromArray(input.data(), static_cast<int>(input.size()))) {
    fail(diagnostics, "input is not a valid ModelProto");
    return std::nullopt;
  }
  if (source.ir_version() != onnx::IR_VERSION_2017_11_3) {
    fail(diagnostics, "only ONNX IR version 3 is supported");
    return std::nullopt;
  }
  if (source.opset_import_size() != 1 ||
      (source.opset_import(0).domain() != "" &&
       source.opset_import(0).domain() != "ai.onnx") ||
      source.opset_import(0).version() != 7) {
    fail(diagnostics, "exactly ai.onnx opset 7 is required");
    return std::nullopt;
  }
  if (!source.has_graph() || source.functions_size() != 0 ||
      source.training_info_size() != 0) {
    fail(diagnostics,
         "a single inference graph without functions or training data is "
         "required");
    return std::nullopt;
  }
  const auto& graph = source.graph();
  if (graph.output_size() != 1 || graph.sparse_initializer_size() != 0 ||
      graph.value_info_size() != 0 ||
      graph.quantization_annotation_size() != 0) {
    fail(diagnostics,
         "one graph output and no sparse, quantized, or intermediate value "
         "metadata are required");
    return std::nullopt;
  }

  const auto schema = load_schema(compiler, diagnostics);
  if (!schema) {
    return std::nullopt;
  }

  std::map<std::string, Initializer> initializers;
  for (const auto& tensor : graph.initializer()) {
    if (tensor.name().empty() || initializers.contains(tensor.name())) {
      fail(diagnostics, "initializer names must be non-empty and unique");
      return std::nullopt;
    }
    auto decoded = decode_initializer(tensor, diagnostics);
    if (!decoded) {
      return std::nullopt;
    }
    initializers.emplace(tensor.name(), std::move(*decoded));
  }

  std::map<std::string, std::pair<std::int32_t, Shape>> graph_inputs;
  for (const auto& input_value : graph.input()) {
    if (input_value.name().empty() ||
        graph_inputs.contains(input_value.name())) {
      fail(diagnostics, "graph input names must be non-empty and unique");
      return std::nullopt;
    }
    const auto declared = value_type(input_value, diagnostics);
    if (!declared) {
      return std::nullopt;
    }
    graph_inputs.emplace(input_value.name(), *declared);
  }
  for (const auto& [initializer_name, initializer] : initializers) {
    const auto found = graph_inputs.find(initializer_name);
    if (found == graph_inputs.end() ||
        found->second.first != initializer.element ||
        found->second.second != initializer.shape) {
      fail(diagnostics, "IR 3 initializer '" + initializer_name +
                            "' does not match its graph input declaration");
      return std::nullopt;
    }
  }

  joggle::Module model(std::move(name), {1, 0, 0});
  static_cast<void>(model.store(joggle::Bytes(input)));
  for (const auto& [initializer_name, initializer] : initializers) {
    static_cast<void>(initializer_name);
    static_cast<void>(model.store(initializer.bytes));
  }

  auto function = compiler.create_function();
  if (!function) {
    fail(diagnostics, "could not create a Function in the linked compiler");
    return std::nullopt;
  }
  auto edit = function->edit();
  std::map<std::string, Tensor> values;

  for (const auto& input_value : graph.input()) {
    if (initializers.contains(input_value.name())) {
      continue;
    }
    const auto& declared = graph_inputs.at(input_value.name());
    if (declared.first != onnx::TensorProto_DataType_FLOAT) {
      fail(diagnostics, "only FLOAT runtime graph inputs are supported");
      return std::nullopt;
    }
    const auto type = tensor_type(compiler, *schema, declared.second);
    if (!type) {
      fail(diagnostics, "could not construct graph input tensor Type");
      return std::nullopt;
    }
    values.emplace(input_value.name(),
                   Tensor{edit.argument(*type), declared.second});
  }

  std::size_t initializer_ordinal = 0;
  for (const auto& tensor : graph.initializer()) {
    const auto& initializer = initializers.at(tensor.name());
    if (initializer.element != onnx::TensorProto_DataType_FLOAT) {
      ++initializer_ordinal;
      continue;
    }
    const auto type = tensor_type(compiler, *schema, initializer.shape);
    const auto digest = model.store(initializer.bytes);
    const auto content = known(compiler, schema->string, digest);
    if (!type || !content) {
      fail(diagnostics, "could not materialize initializer '" +
                            tensor.name() + "'");
      return std::nullopt;
    }
    const auto constant =
        edit.append(schema->constant, {*content}, {*type});
    edit.locate(constant, location(graph, "initializer", tensor.name(),
                                   initializer_ordinal));
    values.emplace(tensor.name(),
                   Tensor{constant.value(), initializer.shape});
    ++initializer_ordinal;
  }

  const auto require_value =
      [&](std::string_view value_name) -> std::optional<Tensor> {
    const auto found = values.find(std::string(value_name));
    if (found == values.end()) {
      fail(diagnostics, "value '" + std::string(value_name) +
                            "' is unavailable at its use");
      return std::nullopt;
    }
    return found->second;
  };

  for (std::size_t node_index = 0;
       node_index < static_cast<std::size_t>(graph.node_size());
       ++node_index) {
    const auto& node = graph.node(static_cast<int>(node_index));
    if (!node.domain().empty() && node.domain() != "ai.onnx") {
      fail(diagnostics, "node '" + node.name() +
                            "' uses an unsupported domain");
      return std::nullopt;
    }
    if (node.output_size() != 1 || node.output(0).empty() ||
        values.contains(node.output(0))) {
      fail(diagnostics, "each node must define one unique non-empty output");
      return std::nullopt;
    }

    std::optional<joggle::Op> call;
    Shape output_shape;
    const auto append_call =
        [&](joggle::Module::FunctionDecl declaration,
            std::vector<joggle::Value> arguments,
            const Shape& shape) -> std::optional<joggle::Op> {
      const auto type = tensor_type(compiler, *schema, shape);
      if (!type) {
        fail(diagnostics, "could not construct result tensor Type for node '" +
                              node.name() + "'");
        return std::nullopt;
      }
      return edit.append(declaration, std::move(arguments), {*type});
    };

    if (node.op_type() == "Conv") {
      if (node.input_size() != 2 && node.input_size() != 3) {
        fail(diagnostics, "Conv requires weight and optional bias");
        return std::nullopt;
      }
      const auto input_tensor = require_value(node.input(0));
      const auto weight = require_value(node.input(1));
      const auto bias =
          node.input_size() == 3 ? require_value(node.input(2))
                                 : std::optional<Tensor>{};
      const auto attrs = attributes(
          node, {"dilations", "group", "kernel_shape", "pads", "strides"},
          diagnostics);
      if (!input_tensor || !weight || (node.input_size() == 3 && !bias) ||
          !attrs || weight->shape.size() != 4) {
        return std::nullopt;
      }
      const auto strides =
          integers_attribute(*attrs, "strides", {1, 1}, diagnostics);
      const auto pads =
          integers_attribute(*attrs, "pads", {0, 0, 0, 0}, diagnostics);
      const auto dilations =
          integers_attribute(*attrs, "dilations", {1, 1}, diagnostics);
      const auto kernel =
          integers_attribute(*attrs, "kernel_shape",
                             {weight->shape[2], weight->shape[3]}, diagnostics);
      const auto group =
          integer_attribute(*attrs, "group", 1, diagnostics);
      if (!strides || !pads || !dilations || !kernel || !group ||
          *kernel != Shape({weight->shape[2], weight->shape[3]}) ||
          *group <= 0 || input_tensor->shape.size() != 4 ||
          weight->shape[1] * *group != input_tensor->shape[1] ||
          (bias && (bias->shape != Shape{weight->shape[0]}))) {
        if (diagnostics.ok()) {
          fail(diagnostics, "Conv tensor shapes or kernel/group attributes "
                            "are inconsistent");
        }
        return std::nullopt;
      }
      const auto spatial =
          spatial_shape(input_tensor->shape, *kernel, *strides, *pads,
                        *dilations, diagnostics, "Conv");
      if (!spatial) {
        return std::nullopt;
      }
      output_shape = *spatial;
      output_shape[1] = weight->shape[0];
      const auto strides_value = known(compiler, schema->integer_list, *strides);
      const auto pads_value = known(compiler, schema->integer_list, *pads);
      const auto dilations_value =
          known(compiler, schema->integer_list, *dilations);
      const auto group_value = known(compiler, schema->integer, *group);
      if (!strides_value || !pads_value || !dilations_value || !group_value) {
        fail(diagnostics, "could not encode Conv properties");
        return std::nullopt;
      }
      std::vector<joggle::Value> arguments{input_tensor->value, weight->value};
      if (bias) {
        arguments.push_back(bias->value);
      }
      arguments.insert(arguments.end(),
                       {*strides_value, *pads_value, *dilations_value,
                        *group_value});
      call = append_call(bias ? schema->conv_bias : schema->conv,
                         std::move(arguments), output_shape);
    } else if (node.op_type() == "Relu") {
      const auto attrs = attributes(node, {}, diagnostics);
      const auto input_tensor =
          node.input_size() == 1 ? require_value(node.input(0))
                                 : std::optional<Tensor>{};
      if (!attrs || !input_tensor) {
        if (node.input_size() != 1 && diagnostics.ok()) {
          fail(diagnostics, "Relu requires one input");
        }
        return std::nullopt;
      }
      output_shape = input_tensor->shape;
      call = append_call(schema->relu, {input_tensor->value}, output_shape);
    } else if (node.op_type() == "MaxPool" ||
               node.op_type() == "AveragePool") {
      const bool average = node.op_type() == "AveragePool";
      const auto attrs = attributes(
          node,
          average ? std::set<std::string>{"ceil_mode", "count_include_pad",
                                          "kernel_shape", "pads", "strides"}
                  : std::set<std::string>{"ceil_mode", "kernel_shape", "pads",
                                          "storage_order", "strides"},
          diagnostics);
      const auto input_tensor =
          node.input_size() == 1 ? require_value(node.input(0))
                                 : std::optional<Tensor>{};
      if (!attrs || !input_tensor) {
        if (node.input_size() != 1 && diagnostics.ok()) {
          fail(diagnostics, node.op_type() + " requires one input");
        }
        return std::nullopt;
      }
      const auto kernel =
          integers_attribute(*attrs, "kernel_shape", {}, diagnostics);
      const auto strides =
          integers_attribute(*attrs, "strides", {1, 1}, diagnostics);
      const auto pads =
          integers_attribute(*attrs, "pads", {0, 0, 0, 0}, diagnostics);
      const auto ceil_mode =
          integer_attribute(*attrs, "ceil_mode", 0, diagnostics);
      const auto extra =
          integer_attribute(*attrs,
                            average ? "count_include_pad" : "storage_order", 0,
                            diagnostics);
      if (!kernel || !strides || !pads || !ceil_mode || !extra ||
          *ceil_mode != 0 || *extra != 0) {
        if (diagnostics.ok()) {
          fail(diagnostics, node.op_type() +
                                " only supports floor-mode NCHW inference");
        }
        return std::nullopt;
      }
      const auto spatial =
          spatial_shape(input_tensor->shape, *kernel, *strides, *pads,
                        Shape{1, 1}, diagnostics, node.op_type());
      if (!spatial) {
        return std::nullopt;
      }
      output_shape = *spatial;
      const auto kernel_value = known(compiler, schema->integer_list, *kernel);
      const auto strides_value = known(compiler, schema->integer_list, *strides);
      const auto pads_value = known(compiler, schema->integer_list, *pads);
      const auto ceil_value = known(compiler, schema->boolean, false);
      if (!kernel_value || !strides_value || !pads_value || !ceil_value) {
        fail(diagnostics, "could not encode Pool properties");
        return std::nullopt;
      }
      call = append_call(average ? schema->average_pool : schema->max_pool,
                         {input_tensor->value, *kernel_value, *strides_value,
                          *pads_value, *ceil_value},
                         output_shape);
    } else if (node.op_type() == "Concat") {
      const auto attrs = attributes(node, {"axis"}, diagnostics);
      const auto lhs =
          node.input_size() == 2 ? require_value(node.input(0))
                                 : std::optional<Tensor>{};
      const auto rhs =
          node.input_size() == 2 ? require_value(node.input(1))
                                 : std::optional<Tensor>{};
      const auto axis =
          attrs ? integer_attribute(*attrs, "axis", 0, diagnostics)
                : std::nullopt;
      if (!attrs || !lhs || !rhs || !axis ||
          lhs->shape.size() != rhs->shape.size()) {
        if (diagnostics.ok()) {
          fail(diagnostics, "Concat requires two equal-rank inputs");
        }
        return std::nullopt;
      }
      auto normalized = *axis;
      if (normalized < 0) {
        normalized += static_cast<std::int64_t>(lhs->shape.size());
      }
      if (normalized < 0 ||
          normalized >= static_cast<std::int64_t>(lhs->shape.size())) {
        fail(diagnostics, "Concat axis is out of range");
        return std::nullopt;
      }
      output_shape = lhs->shape;
      for (std::size_t index = 0; index < output_shape.size(); ++index) {
        if (index == static_cast<std::size_t>(normalized)) {
          output_shape[index] += rhs->shape[index];
        } else if (output_shape[index] != rhs->shape[index]) {
          fail(diagnostics, "Concat non-axis dimensions differ");
          return std::nullopt;
        }
      }
      const auto axis_value = known(compiler, schema->integer, *axis);
      if (!axis_value) {
        fail(diagnostics, "could not encode Concat axis");
        return std::nullopt;
      }
      call = append_call(schema->concat,
                         {lhs->value, rhs->value, *axis_value}, output_shape);
    } else if (node.op_type() == "Dropout") {
      const auto attrs = attributes(node, {"ratio"}, diagnostics);
      const auto input_tensor =
          node.input_size() == 1 ? require_value(node.input(0))
                                 : std::optional<Tensor>{};
      if (!attrs || !input_tensor) {
        if (node.input_size() != 1 && diagnostics.ok()) {
          fail(diagnostics, "Dropout-7 requires one input");
        }
        return std::nullopt;
      }
      const auto ratio = attrs->find("ratio");
      if (ratio != attrs->end() &&
          (ratio->second->type() !=
               onnx::AttributeProto_AttributeType_FLOAT ||
           !ratio->second->has_f())) {
        fail(diagnostics, "Dropout ratio must be FLOAT");
        return std::nullopt;
      }
      values.emplace(node.output(0), *input_tensor);
      continue;
    } else if (node.op_type() == "Reshape") {
      const auto attrs = attributes(node, {}, diagnostics);
      const auto input_tensor =
          node.input_size() == 2 ? require_value(node.input(0))
                                 : std::optional<Tensor>{};
      const auto shape_initializer =
          node.input_size() == 2 ? initializers.find(node.input(1))
                                 : initializers.end();
      if (!attrs || !input_tensor || shape_initializer == initializers.end() ||
          shape_initializer->second.element !=
              onnx::TensorProto_DataType_INT64) {
        if (diagnostics.ok()) {
          fail(diagnostics,
               "Reshape requires one tensor and one constant INT64 shape");
        }
        return std::nullopt;
      }
      const auto requested = shape_initializer->second.integers;
      const auto resolved =
          reshape_shape(input_tensor->shape, requested, diagnostics);
      if (!resolved) {
        return std::nullopt;
      }
      output_shape = *resolved;
      const auto shape_value =
          known(compiler, schema->integer_list, requested);
      if (!shape_value) {
        fail(diagnostics, "could not encode Reshape shape");
        return std::nullopt;
      }
      call = append_call(schema->reshape,
                         {input_tensor->value, *shape_value}, output_shape);
    } else {
      fail(diagnostics, "operator '" + node.op_type() + "' is unsupported");
      return std::nullopt;
    }

    if (!call) {
      return std::nullopt;
    }
    edit.locate(*call,
                location(graph, "node",
                         node.name().empty() ? node.output(0) : node.name(),
                         node_index));
    values.emplace(node.output(0), Tensor{call->value(), output_shape});
  }

  const auto output = values.find(graph.output(0).name());
  const auto declared_output = value_type(graph.output(0), diagnostics);
  if (output == values.end() || !declared_output ||
      declared_output->first != onnx::TensorProto_DataType_FLOAT ||
      output->second.shape != declared_output->second) {
    if (diagnostics.ok()) {
      fail(diagnostics, "inferred graph output does not match declaration");
    }
    return std::nullopt;
  }
  edit.ret(function->entry(), {output->second.value});
  if (!edit.commit(diagnostics) ||
      !model.insert("main", std::move(*function), diagnostics)) {
    return std::nullopt;
  }
  return model;
}

}  // namespace joggle_onnx
