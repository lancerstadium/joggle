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
  joggle::Val value;
  std::int32_t element = 0;
  Shape shape;
};

struct Initializer {
  std::int32_t element = 0;
  Shape shape;
  joggle::Bytes bytes;
  std::vector<std::int64_t> integers;
};

struct Schema {
  joggle::Mod::TypeDecl tensor;
  joggle::Mod::FnDecl constant;
  joggle::Mod::FnDecl conv;
  joggle::Mod::FnDecl conv_bias;
  joggle::Mod::FnDecl relu;
  joggle::Mod::FnDecl max_pool;
  joggle::Mod::FnDecl average_pool;
  joggle::Mod::FnDecl concat;
  joggle::Mod::FnDecl reshape;
  joggle::Mod::FnDecl softmax;
  std::optional<joggle::Mod::FnDecl> quantize;
  std::optional<joggle::Mod::FnDecl> dequantize;
  std::map<std::int32_t, joggle::Type> elements;
  joggle::Type integer;
  joggle::Type boolean;
  joggle::Type string;
  joggle::Type integer_list;
};

bool fail(joggle::Diag& diagnostics, std::string message) {
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

std::optional<std::size_t> element_count(std::span<const std::int64_t> shape) {
  std::size_t count = 1;
  for (const auto dimension : shape) {
    if (dimension < 0) {
      return std::nullopt;
    }
    const auto size = static_cast<std::uint64_t>(dimension);
    if (size > std::numeric_limits<std::size_t>::max() ||
        (size != 0 && count > std::numeric_limits<std::size_t>::max() /
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

std::optional<Initializer> decode_initializer(const onnx::TensorProto& tensor,
                                              joggle::Diag& diagnostics) {
  Initializer result;
  result.element = tensor.data_type();
  result.shape.assign(tensor.dims().begin(), tensor.dims().end());
  const auto count = element_count(result.shape);
  if (!count) {
    fail(diagnostics, "initializer '" + tensor.name() +
                          "' has an invalid or overflowing shape");
    return std::nullopt;
  }
  if (tensor.has_segment() ||
      tensor.data_location() == onnx::TensorProto_DataLocation_EXTERNAL ||
      tensor.external_data_size() != 0) {
    fail(diagnostics, "initializer '" + tensor.name() +
                          "' uses segmented or external storage");
    return std::nullopt;
  }

  const bool has_raw = !tensor.raw_data().empty();
  const auto byte_count = [&](std::size_t width) -> std::optional<std::size_t> {
    if (*count > std::numeric_limits<std::size_t>::max() / width) {
      fail(diagnostics,
           "initializer '" + tensor.name() + "' byte count overflows");
      return std::nullopt;
    }
    return *count * width;
  };
  const auto copy_raw = [&](std::size_t width) {
    const auto expected = byte_count(width);
    if (!expected || tensor.raw_data().size() != *expected) {
      if (!expected) {
        return false;
      }
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' raw byte count does not match its shape");
      return false;
    }
    result.bytes.reserve(tensor.raw_data().size());
    for (const char value : tensor.raw_data()) {
      result.bytes.push_back(
          static_cast<std::byte>(static_cast<unsigned char>(value)));
    }
    return true;
  };
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
      if (!copy_raw(sizeof(float))) {
        return std::nullopt;
      }
    } else {
      if (static_cast<std::size_t>(tensor.float_data_size()) != *count) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' FLOAT value count does not match shape");
        return std::nullopt;
      }
      const auto expected = byte_count(sizeof(float));
      if (!expected) {
        return std::nullopt;
      }
      result.bytes.reserve(*expected);
      for (const float value : tensor.float_data()) {
        append_u32(result.bytes, std::bit_cast<std::uint32_t>(value));
      }
    }
    return result;
  }

  if (result.element == onnx::TensorProto_DataType_UINT8 ||
      result.element == onnx::TensorProto_DataType_INT8 ||
      result.element == onnx::TensorProto_DataType_INT32) {
    if (tensor.float_data_size() != 0 || tensor.int64_data_size() != 0 ||
        tensor.double_data_size() != 0 || tensor.uint64_data_size() != 0 ||
        tensor.string_data_size() != 0 ||
        (has_raw && tensor.int32_data_size() != 0)) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' has conflicting integer storage fields");
      return std::nullopt;
    }
    const std::size_t width =
        result.element == onnx::TensorProto_DataType_INT32 ? 4U : 1U;
    if (has_raw) {
      if (!copy_raw(width)) {
        return std::nullopt;
      }
      return result;
    }
    if (static_cast<std::size_t>(tensor.int32_data_size()) != *count) {
      fail(diagnostics, "initializer '" + tensor.name() +
                            "' integer value count does not match shape");
      return std::nullopt;
    }
    const auto expected = byte_count(width);
    if (!expected) {
      return std::nullopt;
    }
    result.bytes.reserve(*expected);
    for (const auto value : tensor.int32_data()) {
      if ((result.element == onnx::TensorProto_DataType_UINT8 &&
           (value < 0 || value > 255)) ||
          (result.element == onnx::TensorProto_DataType_INT8 &&
           (value < -128 || value > 127))) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' has an out-of-range 8-bit value");
        return std::nullopt;
      }
      const auto bits = static_cast<std::uint32_t>(value);
      for (std::size_t byte = 0; byte < width; ++byte) {
        result.bytes.push_back(
            static_cast<std::byte>((bits >> (byte * 8U)) & 0xffU));
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
      if (!copy_raw(sizeof(std::int64_t))) {
        return std::nullopt;
      }
      result.integers.reserve(*count);
      for (std::size_t index = 0; index < *count; ++index) {
        std::uint64_t bits = 0;
        for (unsigned byte = 0; byte < 8; ++byte) {
          bits |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(
                      result.bytes[index * 8 + byte]))
                  << (byte * 8);
        }
        result.integers.push_back(std::bit_cast<std::int64_t>(bits));
      }
    } else {
      if (static_cast<std::size_t>(tensor.int64_data_size()) != *count) {
        fail(diagnostics, "initializer '" + tensor.name() +
                              "' INT64 value count does not match shape");
        return std::nullopt;
      }
      result.integers.assign(tensor.int64_data().begin(),
                             tensor.int64_data().end());
      const auto expected = byte_count(sizeof(std::int64_t));
      if (!expected) {
        return std::nullopt;
      }
      result.bytes.reserve(*expected);
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
value_type(const onnx::ValueInfoProto& value, joggle::Diag& diagnostics) {
  if (!value.has_type() || !value.type().has_tensor_type()) {
    fail(diagnostics, "value '" + value.name() + "' is not a typed tensor");
    return std::nullopt;
  }
  const auto& tensor = value.type().tensor_type();
  if (!tensor.has_shape()) {
    fail(diagnostics, "value '" + value.name() + "' has no static shape");
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

std::optional<joggle::Mod::FnDecl>
overload(const joggle::Mod& mod, std::string_view name, std::size_t inputs) {
  const auto declarations = mod.overloads(name);
  const auto found = std::find_if(
      declarations.begin(), declarations.end(),
      [inputs](const auto& fn) { return fn.inputs().size() == inputs; });
  return found == declarations.end()
             ? std::nullopt
             : std::optional<joggle::Mod::FnDecl>{*found};
}

std::optional<Schema> load_schema(joggle::Compiler& compiler, bool needs_quant,
                                  joggle::Diag& diagnostics) {
  const auto mod = compiler.mod("tensor");
  const auto quant = compiler.mod("quant");
  const auto integer = compiler.make("int");
  const auto boolean = compiler.make("bool");
  const auto string = compiler.make("string");
  const auto integer_list_decl = compiler.mod("prelude")
                                     ? compiler.mod("prelude")->type("list")
                                     : std::nullopt;
  const auto integer_list = integer && integer_list_decl
                                ? compiler.make(*integer_list_decl, *integer)
                                : std::nullopt;
  if (!mod || !integer || !boolean || !string || !integer_list) {
    fail(diagnostics, "linked tensor and Prelude schemas are unavailable");
    return std::nullopt;
  }
  if (needs_quant && !quant) {
    fail(diagnostics, "the QDQ profile requires the quant Mod");
    return std::nullopt;
  }
  const auto tensor = mod->type("tensor");
  const auto constant = overload(*mod, "constant", 1);
  const auto conv = overload(*mod, "conv", 6);
  const auto conv_bias = overload(*mod, "conv", 7);
  const auto relu = overload(*mod, "relu", 1);
  const auto max_pool = overload(*mod, "max_pool", 5);
  const auto average_pool = overload(*mod, "average_pool", 5);
  const auto concat = overload(*mod, "concat", 3);
  const auto reshape = overload(*mod, "reshape", 2);
  const auto softmax = overload(*mod, "softmax", 2);
  const auto quantize = quant ? overload(*quant, "quantize", 4) : std::nullopt;
  const auto dequantize =
      quant ? overload(*quant, "dequantize", 4) : std::nullopt;
  if (!tensor || !constant || !conv || !conv_bias || !relu || !max_pool ||
      !average_pool || !concat || !reshape || !softmax ||
      (needs_quant && (!quantize || !dequantize))) {
    fail(diagnostics,
         "tensor or quant Mod declarations do not match ONNX importer");
    return std::nullopt;
  }
  std::map<std::int32_t, joggle::Type> elements;
  for (const auto& [code, name] :
       std::vector<std::pair<std::int32_t, std::string_view>>{
           {onnx::TensorProto_DataType_FLOAT, "f32"},
           {onnx::TensorProto_DataType_UINT8, "u8"},
           {onnx::TensorProto_DataType_INT8, "i8"},
           {onnx::TensorProto_DataType_INT32, "i32"},
           {onnx::TensorProto_DataType_INT64, "i64"}}) {
    const auto type = compiler.make(name);
    if (!type) {
      fail(diagnostics, "Prelude tensor element Types are unavailable");
      return std::nullopt;
    }
    elements.emplace(code, *type);
  }
  return Schema{*tensor,
                *constant,
                *conv,
                *conv_bias,
                *relu,
                *max_pool,
                *average_pool,
                *concat,
                *reshape,
                *softmax,
                quantize,
                dequantize,
                std::move(elements),
                *integer,
                *boolean,
                *string,
                *integer_list};
}

std::optional<joggle::Type> tensor_type(joggle::Compiler& compiler,
                                        const Schema& schema,
                                        std::int32_t element,
                                        const Shape& shape) {
  const auto found = schema.elements.find(element);
  return found == schema.elements.end()
             ? std::nullopt
             : compiler.make(schema.tensor, found->second, shape);
}

std::optional<joggle::Val> known(joggle::Compiler& compiler,
                                 const joggle::Type& type, std::int64_t value) {
  return compiler.known(type, value);
}

std::optional<joggle::Val> known(joggle::Compiler& compiler,
                                 const joggle::Type& type, bool value) {
  return compiler.known(type, value);
}

std::optional<joggle::Val> known(joggle::Compiler& compiler,
                                 const joggle::Type& type,
                                 const std::string& value) {
  return compiler.known(type, value);
}

std::optional<joggle::Val> known(joggle::Compiler& compiler,
                                 const joggle::Type& type, const Shape& value) {
  return compiler.known(type, value);
}

std::optional<std::map<std::string, const onnx::AttributeProto*>>
attributes(const onnx::NodeProto& node, const std::set<std::string>& allowed,
           joggle::Diag& diagnostics) {
  std::map<std::string, const onnx::AttributeProto*> result;
  for (const auto& attribute : node.attribute()) {
    if (!allowed.contains(attribute.name())) {
      fail(diagnostics, "node '" + node.name() + "' (" + node.op_type() +
                            ") has unsupported attribute '" + attribute.name() +
                            "'");
      return std::nullopt;
    }
    if (!result.emplace(attribute.name(), &attribute).second) {
      fail(diagnostics, "node '" + node.name() + "' has duplicate attribute '" +
                            attribute.name() + "'");
      return std::nullopt;
    }
  }
  return result;
}

std::optional<std::int64_t> integer_attribute(
    const std::map<std::string, const onnx::AttributeProto*>& attrs,
    std::string_view name, std::int64_t fallback, joggle::Diag& diagnostics) {
  const auto found = attrs.find(std::string(name));
  if (found == attrs.end()) {
    return fallback;
  }
  if (found->second->type() != onnx::AttributeProto_AttributeType_INT ||
      !found->second->has_i()) {
    fail(diagnostics, "attribute '" + std::string(name) + "' must be INT");
    return std::nullopt;
  }
  return found->second->i();
}

std::optional<Shape> integers_attribute(
    const std::map<std::string, const onnx::AttributeProto*>& attrs,
    std::string_view name, Shape fallback, joggle::Diag& diagnostics) {
  const auto found = attrs.find(std::string(name));
  if (found == attrs.end()) {
    return fallback;
  }
  if (found->second->type() != onnx::AttributeProto_AttributeType_INTS) {
    fail(diagnostics, "attribute '" + std::string(name) + "' must be INTS");
    return std::nullopt;
  }
  return Shape(found->second->ints().begin(), found->second->ints().end());
}

std::optional<std::string> string_attribute(
    const std::map<std::string, const onnx::AttributeProto*>& attrs,
    std::string_view name, std::string fallback, joggle::Diag& diagnostics) {
  const auto found = attrs.find(std::string(name));
  if (found == attrs.end()) {
    return fallback;
  }
  if (found->second->type() != onnx::AttributeProto_AttributeType_STRING ||
      !found->second->has_s()) {
    fail(diagnostics, "attribute '" + std::string(name) + "' must be STRING");
    return std::nullopt;
  }
  return found->second->s();
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

std::optional<Shape> spatial_shape(const Shape& input, const Shape& kernel,
                                   const Shape& strides, const Shape& pads,
                                   const Shape& dilations,
                                   joggle::Diag& diagnostics,
                                   std::string_view operation) {
  if (input.size() != 4 || !positive_pair(kernel) || !positive_pair(strides) ||
      !positive_pair(dilations) || !nonnegative_pads(pads)) {
    fail(diagnostics, std::string(operation) +
                          " requires NCHW rank 4 and two-dimensional static "
                          "kernel/stride/dilation/pad values");
    return std::nullopt;
  }
  Shape result{input[0], input[1], 0, 0};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    if (input[axis + 2] > maximum - pads[axis] ||
        input[axis + 2] + pads[axis] > maximum - pads[axis + 2] ||
        kernel[axis] - 1 > (maximum - 1) / dilations[axis]) {
      fail(diagnostics, std::string(operation) + " spatial extent overflows");
      return std::nullopt;
    }
    const auto padded = input[axis + 2] + pads[axis] + pads[axis + 2];
    const auto receptive = dilations[axis] * (kernel[axis] - 1) + 1;
    if (padded < receptive) {
      fail(diagnostics,
           std::string(operation) + " kernel exceeds padded input");
      return std::nullopt;
    }
    result[axis + 2] = (padded - receptive) / strides[axis] + 1;
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

std::optional<Shape> reshape_shape(const Shape& input, const Shape& requested,
                                   joggle::Diag& diagnostics) {
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
    result[*inferred] = static_cast<std::int64_t>(*input_count / known_count);
  } else if (known_count != *input_count) {
    fail(diagnostics, "Reshape changes the number of elements");
    return std::nullopt;
  }
  return result;
}

enum class Profile {
  Opset7,
  Qdq13,
};

std::optional<Profile> profile(const onnx::ModelProto& source,
                               joggle::Diag& diagnostics) {
  std::optional<std::int64_t> standard;
  for (const auto& imported : source.opset_import()) {
    if (imported.domain().empty() || imported.domain() == "ai.onnx") {
      if (standard) {
        fail(diagnostics, "the ai.onnx opset is imported more than once");
        return std::nullopt;
      }
      standard = imported.version();
    }
  }
  if (source.ir_version() == 3 && standard == 7) {
    return Profile::Opset7;
  }
  if (source.ir_version() == 7 && standard == 13) {
    return Profile::Qdq13;
  }
  fail(diagnostics,
       "supported profiles are IR 3/opset 7 and IR 7/opset 13 QDQ");
  return std::nullopt;
}

}  // namespace

std::optional<joggle::Mod> read(joggle::Compiler& compiler,
                                const joggle::Bytes& input, std::string name,
                                joggle::Diag& diagnostics) {
  if (!valid_identifier(name)) {
    fail(diagnostics, "mod name '" + name + "' is not an identifier");
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
  const auto selected = profile(source, diagnostics);
  if (!selected) {
    return std::nullopt;
  }
  if (!source.has_graph() || source.functions_size() != 0 ||
      source.training_info_size() != 0) {
    fail(diagnostics,
         "a single inference graph without fns or training data is "
         "required");
    return std::nullopt;
  }
  const auto& graph = source.graph();
  if (graph.output_size() != 1 || graph.sparse_initializer_size() != 0 ||
      graph.quantization_annotation_size() != 0) {
    fail(diagnostics,
         "one graph output and no sparse initializers or quantization "
         "annotations are required");
    return std::nullopt;
  }

  if (*selected == Profile::Qdq13) {
    const bool has_quantize = std::any_of(
        graph.node().begin(), graph.node().end(),
        [](const auto& node) { return node.op_type() == "QuantizeLinear"; });
    const bool has_dequantize = std::any_of(
        graph.node().begin(), graph.node().end(),
        [](const auto& node) { return node.op_type() == "DequantizeLinear"; });
    if (!has_quantize || !has_dequantize) {
      fail(diagnostics,
           "the IR 7/opset 13 profile requires both QuantizeLinear and "
           "DequantizeLinear");
      return std::nullopt;
    }
  }

  const auto schema =
      load_schema(compiler, *selected == Profile::Qdq13, diagnostics);
  if (!schema) {
    return std::nullopt;
  }

  using Declared = std::pair<std::int32_t, Shape>;
  std::map<std::string, Declared> declared_values;
  std::set<std::string> graph_inputs;
  const auto declare = [&](const onnx::ValueInfoProto& value) {
    if (value.name().empty() || declared_values.contains(value.name())) {
      fail(diagnostics, "value metadata names must be non-empty and unique");
      return false;
    }
    const auto declared = value_type(value, diagnostics);
    return declared && declared_values.emplace(value.name(), *declared).second;
  };
  for (const auto& value : graph.input()) {
    if (!graph_inputs.insert(value.name()).second || !declare(value)) {
      return std::nullopt;
    }
  }
  for (const auto& value : graph.value_info()) {
    if (!declare(value)) {
      return std::nullopt;
    }
  }
  for (const auto& value : graph.output()) {
    if (!declare(value)) {
      return std::nullopt;
    }
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

  for (const auto& [initializer_name, initializer] : initializers) {
    const auto found = declared_values.find(initializer_name);
    const bool legacy_match = graph_inputs.contains(initializer_name) &&
                              found != declared_values.end() &&
                              found->second.first == initializer.element &&
                              found->second.second == initializer.shape;
    if ((*selected == Profile::Opset7 && !legacy_match) ||
        (*selected == Profile::Qdq13 &&
         graph_inputs.contains(initializer_name))) {
      fail(diagnostics, "initializer '" + initializer_name +
                            "' violates its ONNX IR profile's input rule");
      return std::nullopt;
    }
  }

  joggle::Mod model(std::move(name), {1, 0, 0});
  static_cast<void>(model.store(joggle::Bytes(input)));
  for (const auto& [initializer_name, initializer] : initializers) {
    static_cast<void>(initializer_name);
    static_cast<void>(model.store(initializer.bytes));
  }

  auto fn = compiler.create_fn();
  if (!fn) {
    fail(diagnostics, "could not create a Fn in the linked compiler");
    return std::nullopt;
  }
  auto edit = fn->edit();
  std::map<std::string, Tensor> values;

  for (const auto& input_value : graph.input()) {
    if (initializers.contains(input_value.name())) {
      continue;
    }
    const auto& declared = declared_values.at(input_value.name());
    if (declared.first != onnx::TensorProto_DataType_FLOAT) {
      fail(diagnostics, "only FLOAT runtime graph inputs are supported");
      return std::nullopt;
    }
    const auto type =
        tensor_type(compiler, *schema, declared.first, declared.second);
    if (!type) {
      fail(diagnostics, "could not construct graph input tensor Type");
      return std::nullopt;
    }
    values.emplace(input_value.name(), Tensor{edit.argument(*type),
                                              declared.first, declared.second});
  }

  std::size_t initializer_ordinal = 0;
  for (const auto& tensor : graph.initializer()) {
    const auto& initializer = initializers.at(tensor.name());
    if (initializer.element == onnx::TensorProto_DataType_INT64) {
      ++initializer_ordinal;
      continue;
    }
    const auto type =
        tensor_type(compiler, *schema, initializer.element, initializer.shape);
    const auto digest = model.store(initializer.bytes);
    const auto content = known(compiler, schema->string, digest);
    if (!type || !content) {
      fail(diagnostics,
           "could not materialize initializer '" + tensor.name() + "'");
      return std::nullopt;
    }
    const auto constant = edit.append(schema->constant, {*content}, {*type});
    edit.locate(constant, location(graph, "initializer", tensor.name(),
                                   initializer_ordinal));
    values.emplace(tensor.name(), Tensor{constant.value(), initializer.element,
                                         initializer.shape});
    ++initializer_ordinal;
  }

  const auto require_value =
      [&](std::string_view value_name) -> std::optional<Tensor> {
    const auto found = values.find(std::string(value_name));
    if (found == values.end()) {
      fail(diagnostics,
           "value '" + std::string(value_name) + "' is unavailable at its use");
      return std::nullopt;
    }
    return found->second;
  };

  const auto publish = [&](std::string_view value_name, Tensor tensor) {
    const auto declared = declared_values.find(std::string(value_name));
    if (declared != declared_values.end() &&
        (declared->second.first != tensor.element ||
         declared->second.second != tensor.shape)) {
      return fail(diagnostics, "inferred value '" + std::string(value_name) +
                                   "' disagrees with ONNX metadata");
    }
    return values.emplace(std::string(value_name), std::move(tensor)).second;
  };

  const std::set<std::string_view> legacy_ops{"AveragePool", "Concat",  "Conv",
                                              "Dropout",     "MaxPool", "Relu",
                                              "Reshape",     "Softmax"};
  const std::set<std::string_view> qdq_ops{"Concat",
                                           "Conv",
                                           "DequantizeLinear",
                                           "Flatten",
                                           "GlobalAveragePool",
                                           "MaxPool",
                                           "QuantizeLinear",
                                           "Reshape",
                                           "Softmax"};
  const auto& supported_ops =
      *selected == Profile::Opset7 ? legacy_ops : qdq_ops;

  for (std::size_t node_index = 0;
       node_index < static_cast<std::size_t>(graph.node_size()); ++node_index) {
    const auto& node = graph.node(static_cast<int>(node_index));
    if (!node.domain().empty() && node.domain() != "ai.onnx") {
      fail(diagnostics,
           "node '" + node.name() + "' uses an unsupported domain");
      return std::nullopt;
    }
    if (!supported_ops.contains(node.op_type())) {
      fail(diagnostics, "operator '" + node.op_type() +
                            "' is unsupported by the selected profile");
      return std::nullopt;
    }
    if (node.output_size() != 1 || node.output(0).empty() ||
        values.contains(node.output(0))) {
      fail(diagnostics, "each node must define one unique non-empty output");
      return std::nullopt;
    }

    std::optional<joggle::Op> call;
    std::int32_t output_element = 0;
    Shape output_shape;
    const auto append_call =
        [&](joggle::Mod::FnDecl declaration, std::vector<joggle::Val> arguments,
            std::int32_t element,
            const Shape& shape) -> std::optional<joggle::Op> {
      const auto type = tensor_type(compiler, *schema, element, shape);
      if (!type) {
        fail(diagnostics, "could not construct result tensor Type for node '" +
                              node.name() + "'");
        return std::nullopt;
      }
      return edit.append(declaration, std::move(arguments), {*type});
    };

    if (node.op_type() == "QuantizeLinear" ||
        node.op_type() == "DequantizeLinear") {
      const bool quantizing = node.op_type() == "QuantizeLinear";
      const auto attrs = attributes(node, {"axis"}, diagnostics);
      const auto input_tensor = node.input_size() == 3
                                    ? require_value(node.input(0))
                                    : std::optional<Tensor>{};
      const auto scale = node.input_size() == 3 ? require_value(node.input(1))
                                                : std::optional<Tensor>{};
      const auto zero = node.input_size() == 3 ? require_value(node.input(2))
                                               : std::optional<Tensor>{};
      const auto axis = attrs
                            ? integer_attribute(*attrs, "axis", 1, diagnostics)
                            : std::nullopt;
      if (!attrs || !input_tensor || !scale || !zero || !axis ||
          scale->element != onnx::TensorProto_DataType_FLOAT ||
          scale->shape != zero->shape || input_tensor->shape.empty()) {
        if (node.input_size() != 3 && diagnostics.ok()) {
          fail(diagnostics,
               node.op_type() + " requires input, scale, and zero point");
        }
        return std::nullopt;
      }
      auto normalized_axis = *axis;
      if (normalized_axis < 0) {
        normalized_axis +=
            static_cast<std::int64_t>(input_tensor->shape.size());
      }
      const auto parameter_count = element_count(scale->shape);
      const bool scalar_parameters = parameter_count && *parameter_count == 1U;
      const bool per_axis_parameters =
          normalized_axis >= 0 &&
          normalized_axis <
              static_cast<std::int64_t>(input_tensor->shape.size()) &&
          scale->shape ==
              Shape{input_tensor
                        ->shape[static_cast<std::size_t>(normalized_axis)]};
      const bool storage_type =
          zero->element == onnx::TensorProto_DataType_UINT8 ||
          zero->element == onnx::TensorProto_DataType_INT8 ||
          (!quantizing && zero->element == onnx::TensorProto_DataType_INT32);
      if ((!scalar_parameters && !per_axis_parameters) || !storage_type ||
          (quantizing &&
           input_tensor->element != onnx::TensorProto_DataType_FLOAT) ||
          (!quantizing && input_tensor->element != zero->element)) {
        fail(diagnostics,
             node.op_type() + " has inconsistent tensor or parameter Types");
        return std::nullopt;
      }
      const auto axis_value = known(compiler, schema->integer, *axis);
      output_element =
          quantizing ? zero->element : onnx::TensorProto_DataType_FLOAT;
      output_shape = input_tensor->shape;
      const auto declaration =
          quantizing ? schema->quantize : schema->dequantize;
      if (!axis_value || !declaration) {
        fail(diagnostics, "quant Mod declarations are unavailable");
        return std::nullopt;
      }
      call = append_call(
          *declaration,
          {input_tensor->value, scale->value, zero->value, *axis_value},
          output_element, output_shape);
    } else if (node.op_type() == "Conv") {
      if (node.input_size() != 2 && node.input_size() != 3) {
        fail(diagnostics, "Conv requires weight and optional bias");
        return std::nullopt;
      }
      const auto input_tensor = require_value(node.input(0));
      const auto weight = require_value(node.input(1));
      const auto bias = node.input_size() == 3 ? require_value(node.input(2))
                                               : std::optional<Tensor>{};
      const auto attrs = attributes(
          node,
          {"auto_pad", "dilations", "group", "kernel_shape", "pads", "strides"},
          diagnostics);
      if (!input_tensor || !weight || (node.input_size() == 3 && !bias) ||
          !attrs || weight->shape.size() != 4 ||
          input_tensor->element != onnx::TensorProto_DataType_FLOAT ||
          weight->element != onnx::TensorProto_DataType_FLOAT ||
          (bias && bias->element != onnx::TensorProto_DataType_FLOAT)) {
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
      const auto group = integer_attribute(*attrs, "group", 1, diagnostics);
      const auto auto_pad =
          string_attribute(*attrs, "auto_pad", "NOTSET", diagnostics);
      const bool channel_product_overflows =
          group && *group > 0 && weight->shape[1] != 0 &&
          *group > std::numeric_limits<std::int64_t>::max() / weight->shape[1];
      if (!strides || !pads || !dilations || !kernel || !group || !auto_pad ||
          *auto_pad != "NOTSET" ||
          *kernel != Shape({weight->shape[2], weight->shape[3]}) ||
          *group <= 0 || channel_product_overflows ||
          input_tensor->shape.size() != 4 ||
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
      const auto strides_value =
          known(compiler, schema->integer_list, *strides);
      const auto pads_value = known(compiler, schema->integer_list, *pads);
      const auto dilations_value =
          known(compiler, schema->integer_list, *dilations);
      const auto group_value = known(compiler, schema->integer, *group);
      if (!strides_value || !pads_value || !dilations_value || !group_value) {
        fail(diagnostics, "could not encode Conv properties");
        return std::nullopt;
      }
      std::vector<joggle::Val> arguments{input_tensor->value, weight->value};
      if (bias) {
        arguments.push_back(bias->value);
      }
      arguments.insert(arguments.end(), {*strides_value, *pads_value,
                                         *dilations_value, *group_value});
      call = append_call(bias ? schema->conv_bias : schema->conv,
                         std::move(arguments), onnx::TensorProto_DataType_FLOAT,
                         output_shape);
      output_element = onnx::TensorProto_DataType_FLOAT;
    } else if (node.op_type() == "Relu") {
      const auto attrs = attributes(node, {}, diagnostics);
      const auto input_tensor = node.input_size() == 1
                                    ? require_value(node.input(0))
                                    : std::optional<Tensor>{};
      if (!attrs || !input_tensor) {
        if (node.input_size() != 1 && diagnostics.ok()) {
          fail(diagnostics, "Relu requires one input");
        }
        return std::nullopt;
      }
      output_shape = input_tensor->shape;
      output_element = input_tensor->element;
      call = append_call(schema->relu, {input_tensor->value}, output_element,
                         output_shape);
    } else if (node.op_type() == "MaxPool" || node.op_type() == "AveragePool") {
      const bool average = node.op_type() == "AveragePool";
      const auto attrs = attributes(
          node,
          average ? std::set<std::string>{"ceil_mode", "count_include_pad",
                                          "auto_pad", "kernel_shape", "pads",
                                          "strides"}
                  : std::set<std::string>{"ceil_mode", "auto_pad", "dilations",
                                          "kernel_shape", "pads",
                                          "storage_order", "strides"},
          diagnostics);
      const auto input_tensor = node.input_size() == 1
                                    ? require_value(node.input(0))
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
      const auto dilations =
          integers_attribute(*attrs, "dilations", {1, 1}, diagnostics);
      const auto auto_pad =
          string_attribute(*attrs, "auto_pad", "NOTSET", diagnostics);
      const auto ceil_mode =
          integer_attribute(*attrs, "ceil_mode", 0, diagnostics);
      const auto extra = integer_attribute(
          *attrs, average ? "count_include_pad" : "storage_order", 0,
          diagnostics);
      if (!kernel || !strides || !pads || !dilations || !auto_pad ||
          !ceil_mode || !extra || *auto_pad != "NOTSET" ||
          *dilations != Shape({1, 1}) || *ceil_mode != 0 || *extra != 0) {
        if (diagnostics.ok()) {
          fail(diagnostics,
               node.op_type() + " only supports floor-mode NCHW inference");
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
      output_element = input_tensor->element;
      const auto kernel_value = known(compiler, schema->integer_list, *kernel);
      const auto strides_value =
          known(compiler, schema->integer_list, *strides);
      const auto pads_value = known(compiler, schema->integer_list, *pads);
      const auto ceil_value = known(compiler, schema->boolean, false);
      if (!kernel_value || !strides_value || !pads_value || !ceil_value) {
        fail(diagnostics, "could not encode Pool properties");
        return std::nullopt;
      }
      call = append_call(average ? schema->average_pool : schema->max_pool,
                         {input_tensor->value, *kernel_value, *strides_value,
                          *pads_value, *ceil_value},
                         output_element, output_shape);
    } else if (node.op_type() == "GlobalAveragePool") {
      const auto attrs = attributes(node, {}, diagnostics);
      const auto input_tensor = node.input_size() == 1
                                    ? require_value(node.input(0))
                                    : std::optional<Tensor>{};
      if (!attrs || !input_tensor || input_tensor->shape.size() != 4U) {
        if (node.input_size() != 1 && diagnostics.ok()) {
          fail(diagnostics, "GlobalAveragePool requires one NCHW input");
        }
        return std::nullopt;
      }
      const Shape kernel{input_tensor->shape[2], input_tensor->shape[3]};
      const Shape strides{1, 1};
      const Shape pads{0, 0, 0, 0};
      const auto kernel_value = known(compiler, schema->integer_list, kernel);
      const auto strides_value = known(compiler, schema->integer_list, strides);
      const auto pads_value = known(compiler, schema->integer_list, pads);
      const auto ceil_value = known(compiler, schema->boolean, false);
      if (!kernel_value || !strides_value || !pads_value || !ceil_value) {
        fail(diagnostics, "could not encode GlobalAveragePool properties");
        return std::nullopt;
      }
      output_element = input_tensor->element;
      output_shape = {input_tensor->shape[0], input_tensor->shape[1], 1, 1};
      call = append_call(schema->average_pool,
                         {input_tensor->value, *kernel_value, *strides_value,
                          *pads_value, *ceil_value},
                         output_element, output_shape);
    } else if (node.op_type() == "Flatten") {
      const auto attrs = attributes(node, {"axis"}, diagnostics);
      const auto input_tensor = node.input_size() == 1
                                    ? require_value(node.input(0))
                                    : std::optional<Tensor>{};
      const auto axis = attrs
                            ? integer_attribute(*attrs, "axis", 1, diagnostics)
                            : std::nullopt;
      if (!attrs || !input_tensor || !axis) {
        return std::nullopt;
      }
      auto normalized = *axis;
      if (normalized < 0) {
        normalized += static_cast<std::int64_t>(input_tensor->shape.size());
      }
      if (normalized < 0 ||
          normalized > static_cast<std::int64_t>(input_tensor->shape.size())) {
        fail(diagnostics, "Flatten axis is out of range");
        return std::nullopt;
      }
      const auto split = static_cast<std::size_t>(normalized);
      const auto outer = element_count(
          std::span<const std::int64_t>(input_tensor->shape.data(), split));
      const auto inner = element_count(
          std::span<const std::int64_t>(input_tensor->shape.data() + split,
                                        input_tensor->shape.size() - split));
      if (!outer || !inner ||
          *outer > static_cast<std::size_t>(
                       std::numeric_limits<std::int64_t>::max()) ||
          *inner > static_cast<std::size_t>(
                       std::numeric_limits<std::int64_t>::max())) {
        fail(diagnostics, "Flatten shape overflows");
        return std::nullopt;
      }
      output_element = input_tensor->element;
      output_shape = {static_cast<std::int64_t>(*outer),
                      static_cast<std::int64_t>(*inner)};
      const auto shape_value =
          known(compiler, schema->integer_list, output_shape);
      if (!shape_value) {
        fail(diagnostics, "could not encode Flatten shape");
        return std::nullopt;
      }
      call = append_call(schema->reshape, {input_tensor->value, *shape_value},
                         output_element, output_shape);
    } else if (node.op_type() == "Softmax") {
      const auto attrs = attributes(node, {"axis"}, diagnostics);
      const auto input_tensor = node.input_size() == 1
                                    ? require_value(node.input(0))
                                    : std::optional<Tensor>{};
      const auto fallback = *selected == Profile::Opset7 ? 1 : -1;
      const auto axis =
          attrs ? integer_attribute(*attrs, "axis", fallback, diagnostics)
                : std::nullopt;
      if (!attrs || !input_tensor || !axis ||
          input_tensor->element != onnx::TensorProto_DataType_FLOAT) {
        return std::nullopt;
      }
      auto normalized = *axis;
      if (normalized < 0) {
        normalized += static_cast<std::int64_t>(input_tensor->shape.size());
      }
      if (normalized < 0 ||
          normalized >= static_cast<std::int64_t>(input_tensor->shape.size())) {
        fail(diagnostics, "Softmax axis is out of range");
        return std::nullopt;
      }
      const auto axis_value = known(compiler, schema->integer, *axis);
      if (!axis_value) {
        fail(diagnostics, "could not encode Softmax axis");
        return std::nullopt;
      }
      output_element = input_tensor->element;
      output_shape = input_tensor->shape;
      call = append_call(schema->softmax, {input_tensor->value, *axis_value},
                         output_element, output_shape);
    } else if (node.op_type() == "Concat") {
      const auto attrs = attributes(node, {"axis"}, diagnostics);
      const auto lhs = node.input_size() == 2 ? require_value(node.input(0))
                                              : std::optional<Tensor>{};
      const auto rhs = node.input_size() == 2 ? require_value(node.input(1))
                                              : std::optional<Tensor>{};
      const auto axis = attrs
                            ? integer_attribute(*attrs, "axis", 0, diagnostics)
                            : std::nullopt;
      if (!attrs || !lhs || !rhs || !axis ||
          lhs->shape.size() != rhs->shape.size() ||
          lhs->element != rhs->element) {
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
          if (output_shape[index] >
              std::numeric_limits<std::int64_t>::max() - rhs->shape[index]) {
            fail(diagnostics, "Concat axis extent overflows");
            return std::nullopt;
          }
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
      call = append_call(schema->concat, {lhs->value, rhs->value, *axis_value},
                         lhs->element, output_shape);
      output_element = lhs->element;
    } else if (node.op_type() == "Dropout") {
      const auto attrs = attributes(node, {"ratio"}, diagnostics);
      const auto input_tensor = node.input_size() == 1
                                    ? require_value(node.input(0))
                                    : std::optional<Tensor>{};
      if (!attrs || !input_tensor) {
        if (node.input_size() != 1 && diagnostics.ok()) {
          fail(diagnostics, "Dropout-7 requires one input");
        }
        return std::nullopt;
      }
      const auto ratio = attrs->find("ratio");
      if (ratio != attrs->end() &&
          (ratio->second->type() != onnx::AttributeProto_AttributeType_FLOAT ||
           !ratio->second->has_f())) {
        fail(diagnostics, "Dropout ratio must be FLOAT");
        return std::nullopt;
      }
      if (!publish(node.output(0), *input_tensor)) {
        return std::nullopt;
      }
      continue;
    } else if (node.op_type() == "Reshape") {
      const auto attrs = attributes(node, {}, diagnostics);
      const auto input_tensor = node.input_size() == 2
                                    ? require_value(node.input(0))
                                    : std::optional<Tensor>{};
      const auto shape_initializer = node.input_size() == 2
                                         ? initializers.find(node.input(1))
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
      output_element = input_tensor->element;
      const auto shape_value = known(compiler, schema->integer_list, requested);
      if (!shape_value) {
        fail(diagnostics, "could not encode Reshape shape");
        return std::nullopt;
      }
      call = append_call(schema->reshape, {input_tensor->value, *shape_value},
                         output_element, output_shape);
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
    if (!publish(node.output(0),
                 Tensor{call->value(), output_element, output_shape})) {
      return std::nullopt;
    }
  }

  const auto output = values.find(graph.output(0).name());
  const auto declared_output = value_type(graph.output(0), diagnostics);
  if (output == values.end() || !declared_output ||
      declared_output->first != onnx::TensorProto_DataType_FLOAT ||
      output->second.element != declared_output->first ||
      output->second.shape != declared_output->second) {
    if (diagnostics.ok()) {
      fail(diagnostics, "inferred graph output does not match declaration");
    }
    return std::nullopt;
  }
  edit.ret(fn->entry(), {output->second.value});
  if (!edit.commit(diagnostics) ||
      !model.insert("main", std::move(*fn), diagnostics)) {
    return std::nullopt;
  }
  return model;
}

}  // namespace joggle_onnx
