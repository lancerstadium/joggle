#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

#include "onnx.proto3.pb.h"

namespace {

std::string module_name(std::string_view source) {
  std::string result(source.empty() ? "onnx_model" : source);
  std::transform(result.begin(), result.end(), result.begin(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return static_cast<char>(std::isalnum(byte) != 0 || value == '_' ? value
                                                                    : '_');
  });
  if (result.empty() ||
      (std::isalpha(static_cast<unsigned char>(result.front())) == 0 &&
       result.front() != '_')) {
    result.insert(result.begin(), '_');
  }
  return result;
}

std::optional<std::vector<std::int64_t>>
shape(const ::onnx::TensorShapeProto& source, std::string_view value,
      joggle::Diagnostics& diagnostics) {
  std::vector<std::int64_t> result;
  result.reserve(static_cast<std::size_t>(source.dim_size()));
  for (int index = 0; index < source.dim_size(); ++index) {
    const auto& dimension = source.dim(index);
    if (!dimension.has_dim_value() || dimension.dim_value() <= 0) {
      diagnostics.report("ONNX value '" + std::string(value) +
                         "' has a dynamic or non-positive dimension");
      return std::nullopt;
    }
    result.push_back(dimension.dim_value());
  }
  return result;
}

std::optional<std::string_view> element_name(std::int32_t element) {
  switch (element) {
  case ::onnx::TensorProto::BOOL:
    return "i1";
  case ::onnx::TensorProto::INT8:
    return "i8";
  case ::onnx::TensorProto::INT16:
    return "i16";
  case ::onnx::TensorProto::INT32:
    return "i32";
  case ::onnx::TensorProto::INT64:
    return "i64";
  case ::onnx::TensorProto::UINT8:
    return "u8";
  case ::onnx::TensorProto::UINT16:
    return "u16";
  case ::onnx::TensorProto::UINT32:
    return "u32";
  case ::onnx::TensorProto::UINT64:
    return "u64";
  case ::onnx::TensorProto::FLOAT16:
    return "f16";
  case ::onnx::TensorProto::BFLOAT16:
    return "bf16";
  case ::onnx::TensorProto::FLOAT:
    return "f32";
  case ::onnx::TensorProto::DOUBLE:
    return "f64";
  default:
    return std::nullopt;
  }
}

std::optional<joggle::Type>
tensor_type(joggle::Compiler& compiler,
            const joggle::Module::TypeDecl& ranked, std::int32_t element,
            std::vector<std::int64_t> dimensions,
            joggle::Diagnostics& diagnostics) {
  const auto name = element_name(element);
  if (!name) {
    diagnostics.report("ONNX tensor element type " +
                       std::to_string(element) + " is unsupported");
    return std::nullopt;
  }
  const auto scalar = compiler.make(*name);
  if (!scalar) {
    return std::nullopt;
  }
  return compiler.make(ranked, *scalar, std::move(dimensions));
}

std::optional<joggle::Type>
tensor_type(joggle::Compiler& compiler,
            const joggle::Module::TypeDecl& ranked,
            const ::onnx::ValueInfoProto& value,
            joggle::Diagnostics& diagnostics) {
  if (!value.has_type() || !value.type().has_tensor_type() ||
      !value.type().tensor_type().has_shape()) {
    diagnostics.report("ONNX value '" + value.name() +
                       "' has no ranked tensor type");
    return std::nullopt;
  }
  const auto dimensions =
      shape(value.type().tensor_type().shape(), value.name(), diagnostics);
  if (!dimensions) {
    return std::nullopt;
  }
  return tensor_type(compiler, ranked, value.type().tensor_type().elem_type(),
                     *dimensions, diagnostics);
}

std::optional<joggle::Type>
tensor_type(joggle::Compiler& compiler,
            const joggle::Module::TypeDecl& ranked,
            const ::onnx::TensorProto& value,
            joggle::Diagnostics& diagnostics) {
  std::vector<std::int64_t> dimensions(value.dims().begin(),
                                        value.dims().end());
  if (std::any_of(dimensions.begin(), dimensions.end(),
                  [](std::int64_t dimension) { return dimension <= 0; })) {
    diagnostics.report("ONNX initializer '" + value.name() +
                       "' has a non-positive dimension");
    return std::nullopt;
  }
  return tensor_type(compiler, ranked, value.data_type(),
                     std::move(dimensions), diagnostics);
}

struct RankedType {
  joggle::Type element;
  std::vector<std::int64_t> shape;
};

std::optional<RankedType>
ranked_type(const joggle::Type& type,
            const joggle::Module::TypeDecl& ranked,
            std::string_view context, joggle::Diagnostics& diagnostics) {
  if (type.schema() != ranked) {
    diagnostics.report(std::string(context) + " requires ranked tensors");
    return std::nullopt;
  }
  const auto element = type.get<joggle::Type>("element");
  const auto shape = type.get<std::vector<std::int64_t>>("shape");
  if (!element || !shape) {
    diagnostics.report(std::string(context) +
                       " received a malformed ranked tensor type");
    return std::nullopt;
  }
  return RankedType{*element, *shape};
}

std::optional<joggle::Type>
make_ranked(joggle::Compiler& compiler,
            const joggle::Module::TypeDecl& ranked,
            const joggle::Type& element, std::vector<std::int64_t> shape) {
  return compiler.make(ranked, element, std::move(shape));
}

std::optional<std::int64_t>
window_extent(std::int64_t input, std::int64_t kernel, std::int64_t stride,
              std::int64_t dilation, std::int64_t before,
              std::int64_t after) {
  if (input <= 0 || kernel <= 0 || stride <= 0 || dilation <= 0 || before < 0 ||
      after < 0) {
    return std::nullopt;
  }
  const std::int64_t numerator =
      input + before + after - dilation * (kernel - 1) - 1;
  if (numerator < 0) {
    return std::nullopt;
  }
  return numerator / stride + 1;
}

std::optional<joggle::Type>
conv_result(joggle::Compiler& compiler,
            const joggle::Module::TypeDecl& ranked,
            const joggle::Type& input_type, const joggle::Type& weight_type,
            const std::vector<std::int64_t>& strides,
            const std::vector<std::int64_t>& dilations,
            const std::vector<std::int64_t>& pads,
            joggle::Diagnostics& diagnostics) {
  const auto input =
      ranked_type(input_type, ranked, "ONNX Conv", diagnostics);
  const auto weight =
      ranked_type(weight_type, ranked, "ONNX Conv", diagnostics);
  if (!input || !weight || input->shape.size() != 4U ||
      weight->shape.size() != 4U || input->element != weight->element) {
    diagnostics.report("ONNX Conv requires compatible rank-4 tensors");
    return std::nullopt;
  }
  const auto height = window_extent(input->shape[2], weight->shape[2],
                                    strides[0], dilations[0], pads[0], pads[2]);
  const auto width = window_extent(input->shape[3], weight->shape[3],
                                   strides[1], dilations[1], pads[1], pads[3]);
  if (!height || !width) {
    diagnostics.report("ONNX Conv has an invalid output extent");
    return std::nullopt;
  }
  return make_ranked(compiler, ranked, input->element,
                     {input->shape[0], weight->shape[0], *height, *width});
}

std::optional<joggle::Type>
pool_result(joggle::Compiler& compiler,
            const joggle::Module::TypeDecl& ranked,
            const joggle::Type& input_type,
            const std::vector<std::int64_t>& kernel,
            const std::vector<std::int64_t>& strides,
            const std::vector<std::int64_t>& dilations,
            const std::vector<std::int64_t>& pads,
            joggle::Diagnostics& diagnostics) {
  const auto input =
      ranked_type(input_type, ranked, "ONNX MaxPool", diagnostics);
  if (!input || input->shape.size() != 4U) {
    diagnostics.report("ONNX MaxPool requires a rank-4 tensor");
    return std::nullopt;
  }
  const auto height = window_extent(input->shape[2], kernel[0], strides[0],
                                    dilations[0], pads[0], pads[2]);
  const auto width = window_extent(input->shape[3], kernel[1], strides[1],
                                   dilations[1], pads[1], pads[3]);
  if (!height || !width) {
    diagnostics.report("ONNX MaxPool has an invalid output extent");
    return std::nullopt;
  }
  return make_ranked(compiler, ranked, input->element,
                     {input->shape[0], input->shape[1], *height, *width});
}

joggle::Bytes bytes(std::string_view source) {
  joggle::Bytes result;
  result.reserve(source.size());
  for (const char value : source) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

std::optional<joggle::Bytes>
initializer_bytes(const ::onnx::TensorProto& initializer,
                  joggle::Diagnostics& diagnostics) {
  if (!initializer.external_data().empty() ||
      initializer.data_location() == ::onnx::TensorProto::EXTERNAL) {
    diagnostics.report("ONNX initializer '" + initializer.name() +
                       "' uses unsupported external tensor data");
    return std::nullopt;
  }
  if (initializer.raw_data().empty()) {
    diagnostics.report("ONNX initializer '" + initializer.name() +
                       "' does not use canonical raw_data storage");
    return std::nullopt;
  }
  std::size_t element_size = 0;
  switch (initializer.data_type()) {
  case ::onnx::TensorProto::BOOL:
  case ::onnx::TensorProto::INT8:
  case ::onnx::TensorProto::UINT8:
    element_size = 1U;
    break;
  case ::onnx::TensorProto::INT16:
  case ::onnx::TensorProto::UINT16:
  case ::onnx::TensorProto::FLOAT16:
  case ::onnx::TensorProto::BFLOAT16:
    element_size = 2U;
    break;
  case ::onnx::TensorProto::INT32:
  case ::onnx::TensorProto::UINT32:
  case ::onnx::TensorProto::FLOAT:
    element_size = 4U;
    break;
  case ::onnx::TensorProto::INT64:
  case ::onnx::TensorProto::UINT64:
  case ::onnx::TensorProto::DOUBLE:
    element_size = 8U;
    break;
  default:
    diagnostics.report("ONNX initializer '" + initializer.name() +
                       "' has an unsupported element encoding");
    return std::nullopt;
  }
  std::size_t expected = element_size;
  for (const std::int64_t dimension : initializer.dims()) {
    if (dimension <= 0 ||
        static_cast<std::uint64_t>(dimension) >
            std::numeric_limits<std::size_t>::max() / expected) {
      diagnostics.report("ONNX initializer '" + initializer.name() +
                         "' has an invalid or overflowing byte size");
      return std::nullopt;
    }
    expected *= static_cast<std::size_t>(dimension);
  }
  if (initializer.raw_data().size() != expected) {
    diagnostics.report("ONNX initializer '" + initializer.name() +
                       "' raw_data size disagrees with its tensor type");
    return std::nullopt;
  }
  return bytes(initializer.raw_data());
}

const ::onnx::AttributeProto*
attribute(const ::onnx::NodeProto& node, std::string_view name) {
  const auto found = std::find_if(
      node.attribute().begin(), node.attribute().end(),
      [&](const auto& value) { return value.name() == name; });
  return found == node.attribute().end() ? nullptr : &*found;
}

bool attributes_are(const ::onnx::NodeProto& node,
                    std::initializer_list<std::string_view> allowed,
                    std::size_t node_index, joggle::Diagnostics& diagnostics) {
  std::set<std::string_view> seen;
  for (const auto& value : node.attribute()) {
    if (std::find(allowed.begin(), allowed.end(), value.name()) ==
            allowed.end() ||
        !seen.insert(value.name()).second) {
      diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                         node.op_type() + "') has unsupported or duplicate "
                         "attribute '" + value.name() + "'");
      return false;
    }
  }
  return true;
}

std::optional<std::int64_t>
integer_attribute(const ::onnx::NodeProto& node, std::string_view name,
                  std::int64_t fallback, std::size_t node_index,
                  joggle::Diagnostics& diagnostics) {
  const auto* value = attribute(node, name);
  if (value == nullptr) {
    return fallback;
  }
  if (value->type() != ::onnx::AttributeProto::INT) {
    diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                       node.op_type() + "') attribute '" + std::string(name) +
                       "' must be an integer");
    return std::nullopt;
  }
  return value->i();
}

std::optional<double>
real_attribute(const ::onnx::NodeProto& node, std::string_view name,
               double fallback, std::size_t node_index,
               joggle::Diagnostics& diagnostics) {
  const auto* value = attribute(node, name);
  if (value == nullptr) {
    return fallback;
  }
  if (value->type() != ::onnx::AttributeProto::FLOAT ||
      !std::isfinite(value->f())) {
    diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                       node.op_type() + "') attribute '" + std::string(name) +
                       "' must be a finite float");
    return std::nullopt;
  }
  return value->f();
}

std::optional<std::vector<std::int64_t>>
integer_list_attribute(const ::onnx::NodeProto& node, std::string_view name,
                       std::vector<std::int64_t> fallback,
                       std::size_t node_index,
                       joggle::Diagnostics& diagnostics) {
  const auto* value = attribute(node, name);
  if (value == nullptr) {
    return fallback;
  }
  if (value->type() != ::onnx::AttributeProto::INTS) {
    diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                       node.op_type() + "') attribute '" + std::string(name) +
                       "' must be an integer list");
    return std::nullopt;
  }
  return std::vector<std::int64_t>(value->ints().begin(), value->ints().end());
}

bool explicit_padding(const ::onnx::NodeProto& node, std::size_t node_index,
                      joggle::Diagnostics& diagnostics) {
  const auto* value = attribute(node, "auto_pad");
  if (value == nullptr) {
    return true;
  }
  if (value->type() != ::onnx::AttributeProto::STRING ||
      value->s() != "NOTSET") {
    diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                       node.op_type() +
                       "') requires explicit padding (auto_pad=NOTSET)");
    return false;
  }
  return true;
}

std::optional<joggle::Module>
read(joggle::Compiler& compiler, joggle::Bytes input,
     joggle::Diagnostics& diagnostics) {
  if (input.size() >
      static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    diagnostics.report("ONNX model is too large for protobuf parsing");
    return std::nullopt;
  }
  ::onnx::ModelProto model;
  if (!model.ParseFromArray(input.data(), static_cast<int>(input.size())) ||
      !model.has_graph()) {
    diagnostics.report("input is not a valid ONNX ModelProto with a graph");
    return std::nullopt;
  }
  std::optional<std::int64_t> opset;
  for (const auto& imported : model.opset_import()) {
    if (imported.domain().empty() || imported.domain() == "ai.onnx") {
      if (opset) {
        diagnostics.report("ONNX model declares the ai.onnx opset twice");
        return std::nullopt;
      }
      opset = imported.version();
    } else {
      diagnostics.report("ONNX operator domain '" + imported.domain() +
                         "' is unsupported");
      return std::nullopt;
    }
  }
  if (opset != std::optional<std::int64_t>{18}) {
    diagnostics.report("ONNX importer currently requires ai.onnx opset 18");
    return std::nullopt;
  }

  const auto tensor = compiler.module("tensor");
  const auto onnx = compiler.module("onnx");
  const auto ranked = tensor ? tensor->type("ranked") : std::nullopt;
  const auto constant = onnx ? onnx->function("constant") : std::nullopt;
  const auto add = onnx ? onnx->function("add") : std::nullopt;
  const auto relu = onnx ? onnx->function("relu") : std::nullopt;
  const auto convs = onnx ? onnx->overloads("conv")
                        : std::vector<joggle::Module::FunctionDecl>{};
  const auto conv = std::find_if(
      convs.begin(), convs.end(),
      [](const auto& candidate) { return candidate.inputs().size() == 6U; });
  const auto biased_conv = std::find_if(
      convs.begin(), convs.end(),
      [](const auto& candidate) { return candidate.inputs().size() == 7U; });
  const auto max_pool = onnx ? onnx->function("max_pool") : std::nullopt;
  const auto global_average_pool =
      onnx ? onnx->function("global_average_pool") : std::nullopt;
  const auto flatten = onnx ? onnx->function("flatten") : std::nullopt;
  const auto gemm = onnx ? onnx->function("gemm") : std::nullopt;
  const auto string_type = compiler.make("string");
  const auto integer_type = compiler.make("int");
  const auto real_type = compiler.make("real");
  const auto prelude = compiler.module("prelude");
  const auto list = prelude ? prelude->type("list") : std::nullopt;
  const auto integer_list = list && integer_type
                                ? compiler.make(*list, *integer_type)
                                : std::nullopt;
  if (!ranked || !constant || !add || !relu || conv == convs.end() ||
      biased_conv == convs.end() || !max_pool ||
      !global_average_pool || !flatten || !gemm || !string_type ||
      !integer_type || !real_type || !integer_list) {
    diagnostics.report("ONNX behavior requires its source schema and tensor@2");
    return std::nullopt;
  }

  auto function = compiler.create_function();
  if (!function) {
    return std::nullopt;
  }
  auto edit = function->edit();
  std::map<std::string, joggle::Value, std::less<>> values;
  std::set<std::string, std::less<>> initializer_names;
  joggle::Module imported(module_name(model.graph().name()), {1, 0, 0});

  for (const auto& initializer : model.graph().initializer()) {
    if (initializer.name().empty() ||
        !initializer_names.insert(initializer.name()).second) {
      diagnostics.report("ONNX initializer names must be unique and non-empty");
      return std::nullopt;
    }
    const auto type = tensor_type(compiler, *ranked, initializer, diagnostics);
    const auto payload = initializer_bytes(initializer, diagnostics);
    if (!type || !payload) {
      return std::nullopt;
    }
    const std::string resource = imported.store(std::move(*payload));
    const auto known = compiler.known(*string_type, resource);
    if (!known) {
      return std::nullopt;
    }
    const auto op = edit.append(*constant, {*known}, {*type});
    values.emplace(initializer.name(), op.value());
  }

  for (const auto& argument : model.graph().input()) {
    if (initializer_names.contains(argument.name())) {
      continue;
    }
    if (argument.name().empty() || values.contains(argument.name())) {
      diagnostics.report("ONNX graph input names must be unique and non-empty");
      return std::nullopt;
    }
    const auto type = tensor_type(compiler, *ranked, argument, diagnostics);
    if (!type) {
      return std::nullopt;
    }
    values.emplace(argument.name(), edit.argument(*type));
  }

  for (int node_index = 0; node_index < model.graph().node_size();
       ++node_index) {
    const auto& node = model.graph().node(node_index);
    if (!node.domain().empty() && node.domain() != "ai.onnx") {
      diagnostics.report("ONNX node " + std::to_string(node_index) +
                         " uses unsupported operator '" + node.domain() +
                         "." + node.op_type() + "'");
      return std::nullopt;
    }
    std::vector<joggle::Value> arguments;
    arguments.reserve(static_cast<std::size_t>(node.input_size()) + 10U);
    for (const std::string& input_name : node.input()) {
      const auto value = values.find(input_name);
      if (input_name.empty() || value == values.end()) {
        diagnostics.report("ONNX node " + std::to_string(node_index) +
                           " references unavailable input '" + input_name +
                           "'");
        return std::nullopt;
      }
      arguments.push_back(value->second);
    }
    const auto arity = [&](std::size_t expected) {
      if (arguments.size() == expected) {
        return true;
      }
      diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                         node.op_type() + "') requires " +
                         std::to_string(expected) + " inputs");
      return false;
    };
    const auto append_integer = [&](std::int64_t value) {
      const auto known = compiler.known(*integer_type, value);
      if (!known) {
        return false;
      }
      arguments.push_back(*known);
      return true;
    };
    const auto append_real = [&](double value) {
      const auto known = compiler.known(*real_type, value);
      if (!known) {
        return false;
      }
      arguments.push_back(*known);
      return true;
    };
    const auto append_integers = [&](const std::vector<std::int64_t>& value) {
      const auto known = compiler.known(*integer_list, value);
      if (!known) {
        return false;
      }
      arguments.push_back(*known);
      return true;
    };
    const auto valid_pair = [&](const std::vector<std::int64_t>& values,
                                std::string_view name) {
      if (values.size() == 2U && values[0] > 0 && values[1] > 0) {
        return true;
      }
      diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                         node.op_type() + "') attribute '" +
                         std::string(name) +
                         "' must contain two positive integers");
      return false;
    };
    const auto valid_pads = [&](const std::vector<std::int64_t>& values) {
      if (values.size() == 4U &&
          std::all_of(values.begin(), values.end(),
                      [](std::int64_t value) { return value >= 0; })) {
        return true;
      }
      diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                         node.op_type() +
                         "') attribute 'pads' must contain four non-negative "
                         "integers");
      return false;
    };

    const joggle::Module::FunctionDecl* declaration = nullptr;
    std::optional<joggle::Type> result_type;
    if (node.op_type() == "Add") {
      if (!arity(2U) ||
          !attributes_are(node, {}, static_cast<std::size_t>(node_index),
                          diagnostics)) {
        return std::nullopt;
      }
      declaration = &*add;
      result_type = arguments.front().type();
    } else if (node.op_type() == "Relu") {
      if (!arity(1U) ||
          !attributes_are(node, {}, static_cast<std::size_t>(node_index),
                          diagnostics)) {
        return std::nullopt;
      }
      declaration = &*relu;
      result_type = arguments.front().type();
    } else if (node.op_type() == "Conv") {
      if ((arguments.size() != 2U && arguments.size() != 3U) ||
          !attributes_are(node,
                          {"auto_pad", "dilations", "group", "kernel_shape",
                           "pads", "strides"},
                          static_cast<std::size_t>(node_index), diagnostics) ||
          !explicit_padding(node, static_cast<std::size_t>(node_index),
                            diagnostics)) {
        if (arguments.size() != 2U && arguments.size() != 3U) {
          diagnostics.report("ONNX node " + std::to_string(node_index) +
                             " ('Conv') requires 2 or 3 inputs");
        }
        return std::nullopt;
      }
      const auto group = integer_attribute(
          node, "group", 1, static_cast<std::size_t>(node_index), diagnostics);
      const auto strides = integer_list_attribute(
          node, "strides", {1, 1}, static_cast<std::size_t>(node_index),
          diagnostics);
      const auto dilations = integer_list_attribute(
          node, "dilations", {1, 1}, static_cast<std::size_t>(node_index),
          diagnostics);
      const auto pads = integer_list_attribute(
          node, "pads", {0, 0, 0, 0},
          static_cast<std::size_t>(node_index), diagnostics);
      const auto kernel = integer_list_attribute(
          node, "kernel_shape", {}, static_cast<std::size_t>(node_index),
          diagnostics);
      if (!group || *group != 1 || !strides || !dilations || !pads ||
          !kernel || (!kernel->empty() && !valid_pair(*kernel, "kernel_shape")) ||
          !valid_pair(*strides, "strides") ||
          !valid_pair(*dilations, "dilations") || !valid_pads(*pads)) {
        if (group && *group != 1) {
          diagnostics.report("ONNX node " + std::to_string(node_index) +
                             " ('Conv') requires group=1");
        }
        return std::nullopt;
      }
      result_type = conv_result(compiler, *ranked, arguments[0].type(),
                                arguments[1].type(), *strides, *dilations,
                                *pads, diagnostics);
      if (!result_type || !append_integers(*strides) ||
          !append_integers(*dilations) || !append_integers(*pads) ||
          !append_integer(*group)) {
        return std::nullopt;
      }
      declaration = node.input_size() == 2 ? &*conv : &*biased_conv;
    } else if (node.op_type() == "MaxPool") {
      if (!arity(1U) ||
          !attributes_are(node,
                          {"auto_pad", "ceil_mode", "dilations",
                           "kernel_shape", "pads", "storage_order",
                           "strides"},
                          static_cast<std::size_t>(node_index), diagnostics) ||
          !explicit_padding(node, static_cast<std::size_t>(node_index),
                            diagnostics)) {
        return std::nullopt;
      }
      const auto kernel = integer_list_attribute(
          node, "kernel_shape", {}, static_cast<std::size_t>(node_index),
          diagnostics);
      const auto strides = integer_list_attribute(
          node, "strides", {1, 1}, static_cast<std::size_t>(node_index),
          diagnostics);
      const auto dilations = integer_list_attribute(
          node, "dilations", {1, 1}, static_cast<std::size_t>(node_index),
          diagnostics);
      const auto pads = integer_list_attribute(
          node, "pads", {0, 0, 0, 0},
          static_cast<std::size_t>(node_index), diagnostics);
      const auto ceil_mode = integer_attribute(
          node, "ceil_mode", 0, static_cast<std::size_t>(node_index),
          diagnostics);
      const auto storage_order = integer_attribute(
          node, "storage_order", 0, static_cast<std::size_t>(node_index),
          diagnostics);
      if (!kernel || !strides || !dilations || !pads || !ceil_mode ||
          !storage_order || *ceil_mode != 0 || *storage_order != 0 ||
          !valid_pair(*kernel, "kernel_shape") ||
          !valid_pair(*strides, "strides") ||
          !valid_pair(*dilations, "dilations") || !valid_pads(*pads)) {
        if (ceil_mode && *ceil_mode != 0) {
          diagnostics.report("ONNX node " + std::to_string(node_index) +
                             " ('MaxPool') requires ceil_mode=0");
        }
        if (storage_order && *storage_order != 0) {
          diagnostics.report("ONNX node " + std::to_string(node_index) +
                             " ('MaxPool') requires storage_order=0");
        }
        return std::nullopt;
      }
      result_type = pool_result(compiler, *ranked, arguments[0].type(),
                                *kernel, *strides, *dilations, *pads,
                                diagnostics);
      if (!result_type || !append_integers(*kernel) ||
          !append_integers(*strides) || !append_integers(*dilations) ||
          !append_integers(*pads) || !append_integer(*ceil_mode) ||
          !append_integer(*storage_order)) {
        return std::nullopt;
      }
      declaration = &*max_pool;
    } else if (node.op_type() == "GlobalAveragePool") {
      if (!arity(1U) ||
          !attributes_are(node, {}, static_cast<std::size_t>(node_index),
                          diagnostics)) {
        return std::nullopt;
      }
      declaration = &*global_average_pool;
      const auto source_type = ranked_type(
          arguments[0].type(), *ranked, "ONNX GlobalAveragePool", diagnostics);
      if (!source_type || source_type->shape.size() != 4U) {
        return std::nullopt;
      }
      result_type = make_ranked(
          compiler, *ranked, source_type->element,
          {source_type->shape[0], source_type->shape[1], 1, 1});
    } else if (node.op_type() == "Flatten") {
      const auto axis = integer_attribute(
          node, "axis", 1, static_cast<std::size_t>(node_index), diagnostics);
      if (!arity(1U) ||
          !attributes_are(node, {"axis"},
                          static_cast<std::size_t>(node_index), diagnostics) ||
          !axis || *axis != 1) {
        if (axis && *axis != 1) {
          diagnostics.report("ONNX node " + std::to_string(node_index) +
                             " ('Flatten') requires axis=1");
        }
        return std::nullopt;
      }
      declaration = &*flatten;
      const auto source_type = ranked_type(arguments[0].type(), *ranked,
                                           "ONNX Flatten", diagnostics);
      if (!source_type || source_type->shape.size() != 4U) {
        return std::nullopt;
      }
      result_type = make_ranked(
          compiler, *ranked, source_type->element,
          {source_type->shape[0], source_type->shape[1] * source_type->shape[2] *
                                      source_type->shape[3]});
      if (!result_type || !append_integer(*axis)) {
        return std::nullopt;
      }
    } else if (node.op_type() == "Gemm") {
      const auto alpha = real_attribute(
          node, "alpha", 1.0, static_cast<std::size_t>(node_index), diagnostics);
      const auto beta = real_attribute(
          node, "beta", 1.0, static_cast<std::size_t>(node_index), diagnostics);
      const auto trans_a = integer_attribute(
          node, "transA", 0, static_cast<std::size_t>(node_index), diagnostics);
      const auto trans_b = integer_attribute(
          node, "transB", 0, static_cast<std::size_t>(node_index), diagnostics);
      if (!arity(3U) ||
          !attributes_are(node, {"alpha", "beta", "transA", "transB"},
                          static_cast<std::size_t>(node_index), diagnostics) ||
          !alpha || !beta || !trans_a || !trans_b || *alpha != 1.0 ||
          *beta != 1.0 || *trans_a != 0 || *trans_b != 1) {
        if (alpha && beta && trans_a && trans_b &&
            (*alpha != 1.0 || *beta != 1.0 || *trans_a != 0 ||
             *trans_b != 1)) {
          diagnostics.report("ONNX node " + std::to_string(node_index) +
                             " ('Gemm') requires alpha=1, beta=1, transA=0, "
                             "and transB=1");
        }
        return std::nullopt;
      }
      declaration = &*gemm;
      const auto a = ranked_type(arguments[0].type(), *ranked, "ONNX Gemm",
                                 diagnostics);
      const auto b = ranked_type(arguments[1].type(), *ranked, "ONNX Gemm",
                                 diagnostics);
      if (!a || !b || a->shape.size() != 2U || b->shape.size() != 2U ||
          a->element != b->element) {
        diagnostics.report("ONNX Gemm requires compatible rank-2 tensors");
        return std::nullopt;
      }
      result_type = make_ranked(compiler, *ranked, a->element,
                                {a->shape[0], b->shape[0]});
      if (!result_type || !append_real(*alpha) || !append_real(*beta) ||
          !append_integer(*trans_a) || !append_integer(*trans_b)) {
        return std::nullopt;
      }
    }
    if (declaration == nullptr) {
      diagnostics.report("ONNX node " + std::to_string(node_index) +
                         " uses unsupported operator '" + node.op_type() +
                         "'");
      return std::nullopt;
    }
    std::optional<joggle::Op> op;
    try {
      op = edit.append(*declaration, std::move(arguments), {*result_type});
    } catch (const std::exception& error) {
      diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                         node.op_type() + "') is ill-typed: " + error.what());
      return std::nullopt;
    }
    if (node.output_size() !=
        static_cast<int>(op->results().size())) {
      diagnostics.report("ONNX node " + std::to_string(node_index) +
                         " has an unsupported result count");
      return std::nullopt;
    }
    for (int output = 0; output < node.output_size(); ++output) {
      if (node.output(output).empty() ||
          !values
               .emplace(node.output(output),
                        op->result(static_cast<std::size_t>(output)))
               .second) {
        diagnostics.report("ONNX value names must be unique and non-empty");
        return std::nullopt;
      }
    }
  }

  std::vector<joggle::Value> returned;
  returned.reserve(static_cast<std::size_t>(model.graph().output_size()));
  for (const auto& output : model.graph().output()) {
    const auto value = values.find(output.name());
    const auto expected = tensor_type(compiler, *ranked, output, diagnostics);
    if (value == values.end() || !expected) {
      diagnostics.report("ONNX graph output '" + output.name() +
                         "' is unavailable");
      return std::nullopt;
    }
    if (value->second.type() != *expected) {
      diagnostics.report("ONNX graph output '" + output.name() +
                         "' disagrees with its inferred type");
      return std::nullopt;
    }
    returned.push_back(value->second);
  }
  edit.ret(function->entry(), std::move(returned));
  if (!edit.commit(diagnostics)) {
    return std::nullopt;
  }

  if (!imported.insert("main", std::move(*function), diagnostics)) {
    return std::nullopt;
  }
  return imported;
}

std::optional<joggle::Module>
to_nn(joggle::Compiler& compiler, joggle::Module input,
      joggle::Diagnostics& diagnostics) {
  const auto tensor = compiler.module("tensor");
  const auto nn = compiler.module("nn");
  const auto constant = tensor ? tensor->function("constant") : std::nullopt;
  const auto add = nn ? nn->function("add") : std::nullopt;
  const auto relu = nn ? nn->function("relu") : std::nullopt;
  const auto convs = nn ? nn->overloads("conv2d_nchw")
                        : std::vector<joggle::Module::FunctionDecl>{};
  const auto conv = std::find_if(
      convs.begin(), convs.end(),
      [](const auto& candidate) { return candidate.inputs().size() == 10U; });
  const auto biased_conv = std::find_if(
      convs.begin(), convs.end(),
      [](const auto& candidate) { return candidate.inputs().size() == 11U; });
  const auto max_pool = nn ? nn->function("max_pool2d_nchw") : std::nullopt;
  const auto global_average_pool =
      nn ? nn->function("global_average_pool_nchw") : std::nullopt;
  const auto flatten = nn ? nn->function("flatten_nchw") : std::nullopt;
  const auto linear = nn ? nn->function("linear") : std::nullopt;
  const auto integer = compiler.make("int");
  if (!constant || !add || !relu || conv == convs.end() ||
      biased_conv == convs.end() || !max_pool || !global_average_pool ||
      !flatten || !linear || !integer) {
    diagnostics.report("onnx.to_nn requires tensor@2 and nn@2");
    return std::nullopt;
  }

  const auto changes = joggle::convert(
      input,
      [&](const joggle::Op& op, joggle::Function::Edit& edit,
          joggle::Diagnostics& rule_diagnostics) {
        const auto symbol = op.callee().symbol();
        if (symbol.module_name() != "onnx") {
          return false;
        }

        const auto fail = [&](std::string message) {
          rule_diagnostics.report("cannot convert '" +
                                  symbol.qualified_name() + "': " + message);
          return false;
        };
        const auto integers = [&](std::string_view name)
            -> std::optional<std::vector<std::int64_t>> {
          return op.property<std::vector<std::int64_t>>(name);
        };
        std::vector<joggle::Value> arguments = op.operands();
        const joggle::Module::FunctionDecl* target = nullptr;
        const auto append_integers =
            [&](const std::vector<std::int64_t>& values) {
              for (const std::int64_t value : values) {
                const auto known = compiler.known(*integer, value);
                if (!known) {
                  return false;
                }
                arguments.push_back(*known);
              }
              return true;
            };

        if (symbol.local_name() == "constant") {
          const auto resource = op.property("resource");
          if (!resource) {
            return fail("missing resource property");
          }
          arguments = {*resource};
          target = &*constant;
        } else if (symbol.local_name() == "add") {
          target = &*add;
        } else if (symbol.local_name() == "relu") {
          target = &*relu;
        } else if (symbol.local_name() == "conv") {
          const auto strides = integers("strides");
          const auto dilations = integers("dilations");
          const auto pads = integers("pads");
          const auto group = op.property<std::int64_t>("group");
          if (!strides || strides->size() != 2U || !dilations ||
              dilations->size() != 2U || !pads || pads->size() != 4U ||
              group != std::optional<std::int64_t>{1}) {
            return fail("nn.conv2d_nchw requires 2-D attributes and group=1");
          }
          if (!append_integers(*strides) || !append_integers(*dilations) ||
              !append_integers(*pads)) {
            return fail("could not materialize an integer property");
          }
          target = arguments.size() == 10U ? &*conv : &*biased_conv;
        } else if (symbol.local_name() == "max_pool") {
          const auto kernel = integers("kernel_shape");
          const auto strides = integers("strides");
          const auto dilations = integers("dilations");
          const auto pads = integers("pads");
          const auto ceil_mode = op.property<std::int64_t>("ceil_mode");
          const auto storage_order =
              op.property<std::int64_t>("storage_order");
          if (!kernel || kernel->size() != 2U || !strides ||
              strides->size() != 2U || !dilations ||
              dilations->size() != 2U || !pads || pads->size() != 4U ||
              ceil_mode != std::optional<std::int64_t>{0} ||
              storage_order != std::optional<std::int64_t>{0}) {
            return fail("nn.max_pool2d_nchw requires floor rounding and "
                        "row-major indices");
          }
          if (!append_integers(*kernel) || !append_integers(*strides) ||
              !append_integers(*dilations) || !append_integers(*pads)) {
            return fail("could not materialize an integer property");
          }
          target = &*max_pool;
        } else if (symbol.local_name() == "global_average_pool") {
          target = &*global_average_pool;
        } else if (symbol.local_name() == "flatten") {
          if (op.property<std::int64_t>("axis") !=
              std::optional<std::int64_t>{1}) {
            return fail("nn.flatten_nchw requires axis=1");
          }
          target = &*flatten;
        } else if (symbol.local_name() == "gemm") {
          if (op.property<double>("alpha") != std::optional<double>{1.0} ||
              op.property<double>("beta") != std::optional<double>{1.0} ||
              op.property<std::int64_t>("trans_a") !=
                  std::optional<std::int64_t>{0} ||
              op.property<std::int64_t>("trans_b") !=
                  std::optional<std::int64_t>{1}) {
            return fail("nn.linear requires alpha=1, beta=1, transA=0, "
                        "and transB=1");
          }
          target = &*linear;
        } else {
          return fail("no nn representation is registered");
        }

        const auto replacement =
            edit.insert(op, *target, std::move(arguments),
                        [&] {
                          std::vector<joggle::Type> types;
                          for (const auto& result : op.results()) {
                            types.push_back(result.type());
                          }
                          return types;
                        }());
        edit.replace(op, replacement.results());
        return true;
      },
      [](const joggle::Op& op) {
        return op.callee().symbol().module_name() != "onnx";
      },
      diagnostics);
  if (!changes) {
    return std::nullopt;
  }
  return input;
}

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics&) {
  compiler.bind(module, "read", read);
  compiler.bind(module, "to_nn", to_nn);
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
