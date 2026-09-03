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
#include <joggle/onnx/onnx.h>

#include "onnx.proto3.pb.h"

namespace {

using Resources = joggle::onnx::Resources;

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

std::optional<std::tuple<joggle::Module, Resources>>
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
  const auto nn = compiler.module("nn");
  const auto ranked = tensor ? tensor->type("ranked") : std::nullopt;
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
  const auto string_type = compiler.make("string");
  const auto integer_type = compiler.make("int");
  if (!ranked || !constant || !add || !relu || conv == convs.end() ||
      biased_conv == convs.end() || !max_pool ||
      !global_average_pool || !flatten || !linear || !string_type ||
      !integer_type) {
    diagnostics.report("ONNX behavior requires tensor@2 and nn@2");
    return std::nullopt;
  }

  auto function = compiler.create_function();
  if (!function) {
    return std::nullopt;
  }
  auto edit = function->edit();
  std::map<std::string, joggle::Value, std::less<>> values;
  std::set<std::string, std::less<>> initializer_names;
  Resources resources;

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
    const std::string raw(reinterpret_cast<const char*>(payload->data()),
                          payload->size());
    const std::string resource = "sha256:" + joggle::sha256(raw);
    resources.try_emplace(resource, *payload);
    const auto known = compiler.known(*string_type, resource);
    if (!known) {
      return std::nullopt;
    }
    const auto instruction = edit.append(*constant, {*known}, {*type});
    values.emplace(initializer.name(), instruction.value());
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
    if (node.op_type() == "Add") {
      if (!arity(2U) ||
          !attributes_are(node, {}, static_cast<std::size_t>(node_index),
                          diagnostics)) {
        return std::nullopt;
      }
      declaration = &*add;
    } else if (node.op_type() == "Relu") {
      if (!arity(1U) ||
          !attributes_are(node, {}, static_cast<std::size_t>(node_index),
                          diagnostics)) {
        return std::nullopt;
      }
      declaration = &*relu;
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
      if (!append_integer((*strides)[0]) ||
          !append_integer((*strides)[1]) ||
          !append_integer((*dilations)[0]) ||
          !append_integer((*dilations)[1]) || !append_integer((*pads)[0]) ||
          !append_integer((*pads)[1]) || !append_integer((*pads)[2]) ||
          !append_integer((*pads)[3])) {
        return std::nullopt;
      }
      declaration = arguments.size() == 2U ? &*conv : &*biased_conv;
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
      if (!append_integer((*kernel)[0]) || !append_integer((*kernel)[1]) ||
          !append_integer((*strides)[0]) ||
          !append_integer((*strides)[1]) ||
          !append_integer((*dilations)[0]) ||
          !append_integer((*dilations)[1]) || !append_integer((*pads)[0]) ||
          !append_integer((*pads)[1]) || !append_integer((*pads)[2]) ||
          !append_integer((*pads)[3])) {
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
      declaration = &*linear;
    }
    if (declaration == nullptr) {
      diagnostics.report("ONNX node " + std::to_string(node_index) +
                         " uses unsupported operator '" + node.op_type() +
                         "'");
      return std::nullopt;
    }
    std::optional<joggle::Instruction> instruction;
    try {
      instruction = edit.append(*declaration, std::move(arguments));
    } catch (const std::exception& error) {
      diagnostics.report("ONNX node " + std::to_string(node_index) + " ('" +
                         node.op_type() + "') is ill-typed: " + error.what());
      return std::nullopt;
    }
    if (node.output_size() !=
        static_cast<int>(instruction->results().size())) {
      diagnostics.report("ONNX node " + std::to_string(node_index) +
                         " has an unsupported result count");
      return std::nullopt;
    }
    for (int output = 0; output < node.output_size(); ++output) {
      if (node.output(output).empty() ||
          !values
               .emplace(node.output(output),
                        instruction->result(static_cast<std::size_t>(output)))
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

  joggle::Module imported(module_name(model.graph().name()), {1, 0, 0});
  if (!imported.insert("main", std::move(*function), diagnostics)) {
    return std::nullopt;
  }
  return std::tuple{std::move(imported), std::move(resources)};
}

void bind(joggle::Compiler& compiler, const joggle::Module& module,
          joggle::Diagnostics& diagnostics) {
  const auto resource_type = module.type("resources");
  if (!resource_type ||
      !compiler.represent<Resources>(*resource_type)) {
    diagnostics.report("ONNX behavior does not match its resources type");
    return;
  }
  compiler.bind(module, "read", read);
  compiler.bind(
      module, "lookup",
      [](const Resources& resources, std::string resource,
         joggle::Diagnostics& reported) -> std::optional<joggle::Bytes> {
        const auto found = resources.find(resource);
        if (found == resources.end()) {
          reported.report("ONNX resource '" + resource + "' is unavailable");
          return std::nullopt;
        }
        return found->second;
      });
}

}  // namespace

JOGGLE_EXPORT_BEHAVIOR(bind)
