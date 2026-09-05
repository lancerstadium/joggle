#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

#include "onnx.pb.h"

namespace {

using Shape = std::vector<std::int64_t>;

joggle::Bytes read_bytes(const char* path) {
  std::ifstream input(path, std::ios::binary);
  const std::vector<char> characters{std::istreambuf_iterator<char>(input),
                                     std::istreambuf_iterator<char>()};
  joggle::Bytes result;
  result.reserve(characters.size());
  for (const char value : characters) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

std::optional<std::int32_t> onnx_element(const joggle::Type& type) {
  const auto schema = type.schema();
  const auto name = schema.name();
  if (name == "f32") {
    return onnx::TensorProto_DataType_FLOAT;
  }
  if (name == "u8") {
    return onnx::TensorProto_DataType_UINT8;
  }
  if (name == "i8") {
    return onnx::TensorProto_DataType_INT8;
  }
  if (name == "i32") {
    return onnx::TensorProto_DataType_INT32;
  }
  if (name == "i64") {
    return onnx::TensorProto_DataType_INT64;
  }
  return std::nullopt;
}

bool tensor_info(onnx::ValueInfoProto& info, std::string name,
                 const joggle::Type& type) {
  const auto element = type.get<joggle::Type>("element");
  const auto shape = type.get<Shape>("shape");
  const auto code = element ? onnx_element(*element) : std::nullopt;
  if (!code || !shape) {
    return false;
  }
  info.set_name(std::move(name));
  auto* tensor = info.mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(*code);
  for (const auto dimension : *shape) {
    tensor->mutable_shape()->add_dim()->set_dim_value(dimension);
  }
  return true;
}

std::string
lookup(const std::vector<std::pair<joggle::Val, std::string>>& names,
       const joggle::Val& value) {
  for (const auto& [candidate, name] : names) {
    if (candidate == value) {
      return name;
    }
  }
  return {};
}

void add_int(onnx::NodeProto& node, std::string name, std::int64_t value) {
  auto* attribute = node.add_attribute();
  attribute->set_name(std::move(name));
  attribute->set_type(onnx::AttributeProto_AttributeType_INT);
  attribute->set_i(value);
}

void add_ints(onnx::NodeProto& node, std::string name, const Shape& values) {
  auto* attribute = node.add_attribute();
  attribute->set_name(std::move(name));
  attribute->set_type(onnx::AttributeProto_AttributeType_INTS);
  for (const auto value : values) {
    attribute->add_ints(value);
  }
}

void add_float(onnx::NodeProto& node, std::string name, double value) {
  auto* attribute = node.add_attribute();
  attribute->set_name(std::move(name));
  attribute->set_type(onnx::AttributeProto_AttributeType_FLOAT);
  attribute->set_f(static_cast<float>(value));
}

bool emit(const joggle::Mod& mod, const char* path) {
  const auto main = mod.fn("main");
  const auto fn = main ? main->body() : nullptr;
  if (!fn || fn->arguments().size() != 1U) {
    return false;
  }

  const auto operations = fn->ops();
  const bool qdq =
      std::any_of(operations.begin(), operations.end(), [](const auto& op) {
        const auto symbol = op.callee().referenced_fn()->symbol();
        return symbol.mod_name() == "onnx" &&
               (symbol.local_name() == "QuantizeLinear" ||
                symbol.local_name() == "DequantizeLinear");
      });
  onnx::ModelProto model;
  model.set_ir_version(qdq ? 7 : onnx::IR_VERSION_2017_11_3);
  model.set_producer_name("joggle-onnx-roundtrip-test");
  auto* opset = model.add_opset_import();
  opset->set_domain("");
  opset->set_version(qdq ? 13 : 7);
  auto* graph = model.mutable_graph();
  graph->set_name("joggle_roundtrip");

  std::vector<std::pair<joggle::Val, std::string>> names;
  const auto argument = fn->arguments().front();
  if (!tensor_info(*graph->add_input(), "input", argument.type())) {
    return false;
  }
  names.emplace_back(argument, "input");

  std::size_t constant_index = 0;
  std::size_t call_index = 0;
  for (const auto& op : operations) {
    const auto symbol = op.callee().referenced_fn()->symbol();
    const auto callee = symbol.local_name();
    const auto callee_mod = symbol.mod_name();
    if (callee_mod == "onnx" && callee == "Constant") {
      const auto digest = op.callee().binding<std::string>("content");
      const auto data = digest ? mod.data(*digest) : std::nullopt;
      const auto shape = op.value().type().get<Shape>("shape");
      const auto element = op.value().type().get<joggle::Type>("element");
      const auto code = element ? onnx_element(*element) : std::nullopt;
      if (!data || !shape || !code) {
        return false;
      }
      const auto name = "constant_" + std::to_string(constant_index++);
      auto* initializer = graph->add_initializer();
      initializer->set_name(name);
      initializer->set_data_type(*code);
      for (const auto dimension : *shape) {
        initializer->add_dims(dimension);
      }
      initializer->set_raw_data(reinterpret_cast<const char*>(data->data()),
                                data->size());
      if (!qdq && !tensor_info(*graph->add_input(), name, op.value().type())) {
        return false;
      }
      names.emplace_back(op.value(), name);
      continue;
    }

    auto* node = graph->add_node();
    node->set_name("call_" + std::to_string(call_index));
    if (callee_mod == "onnx" &&
        (callee == "QuantizeLinear" || callee == "DequantizeLinear")) {
      node->set_op_type(std::string(callee));
      const auto axis = op.callee().binding<std::int64_t>("axis");
      if (!axis) {
        return false;
      }
      add_int(*node, "axis", *axis);
    } else if (callee_mod == "onnx" && callee == "Conv") {
      node->set_op_type("Conv");
      const auto strides = op.callee().binding<Shape>("strides");
      const auto pads = op.callee().binding<Shape>("pads");
      const auto dilations = op.callee().binding<Shape>("dilations");
      const auto group = op.callee().binding<std::int64_t>("group");
      if (!strides || !pads || !dilations || !group) {
        return false;
      }
      add_ints(*node, "strides", *strides);
      add_ints(*node, "pads", *pads);
      add_ints(*node, "dilations", *dilations);
      add_int(*node, "group", *group);
    } else if (callee_mod == "onnx" && callee == "Relu") {
      node->set_op_type("Relu");
    } else if (callee_mod == "onnx" &&
               (callee == "MaxPool" || callee == "AveragePool")) {
      node->set_op_type(std::string(callee));
      const auto kernel = op.callee().binding<Shape>("kernel_shape");
      const auto strides = op.callee().binding<Shape>("strides");
      const auto pads = op.callee().binding<Shape>("pads");
      const auto ceil_mode = op.callee().binding<std::int64_t>("ceil_mode");
      if (!kernel || !strides || !pads || !ceil_mode) {
        return false;
      }
      add_ints(*node, "kernel_shape", *kernel);
      add_ints(*node, "strides", *strides);
      add_ints(*node, "pads", *pads);
      if (*ceil_mode != 0) {
        return false;
      }
    } else if (callee_mod == "onnx" && callee == "GlobalAveragePool") {
      node->set_op_type("GlobalAveragePool");
    } else if (callee_mod == "onnx" && callee == "Concat") {
      node->set_op_type("Concat");
      const auto axis = op.callee().binding<std::int64_t>("axis");
      if (!axis) {
        return false;
      }
      add_int(*node, "axis", *axis);
    } else if (callee_mod == "onnx" && callee == "Reshape") {
      node->set_op_type("Reshape");
    } else if (callee_mod == "onnx" && callee == "Flatten") {
      node->set_op_type("Flatten");
      const auto axis = op.callee().binding<std::int64_t>("axis");
      if (!axis) {
        return false;
      }
      add_int(*node, "axis", *axis);
    } else if (callee_mod == "onnx" && callee == "Dropout") {
      node->set_op_type("Dropout");
      const auto ratio = op.callee().binding<double>("ratio");
      if (!ratio) {
        return false;
      }
      add_float(*node, "ratio", *ratio);
    } else if (callee_mod == "onnx" && callee == "Softmax") {
      node->set_op_type("Softmax");
      const auto axis = op.callee().binding<std::int64_t>("axis");
      if (!axis) {
        return false;
      }
      add_int(*node, "axis", *axis);
    } else {
      return false;
    }

    for (const auto& operand : op.arguments()) {
      const auto name = lookup(names, operand);
      if (name.empty()) {
        return false;
      }
      node->add_input(name);
    }
    if (callee_mod == "onnx" && callee == "Reshape") {
      const auto requested = op.callee().binding<Shape>("shape");
      if (!requested) {
        return false;
      }
      const auto shape_name = "reshape_shape_" + std::to_string(call_index);
      auto* initializer = graph->add_initializer();
      initializer->set_name(shape_name);
      initializer->set_data_type(onnx::TensorProto_DataType_INT64);
      initializer->add_dims(static_cast<std::int64_t>(requested->size()));
      for (const auto dimension : *requested) {
        initializer->add_int64_data(dimension);
      }
      if (!qdq) {
        auto* input = graph->add_input();
        input->set_name(shape_name);
        auto* tensor = input->mutable_type()->mutable_tensor_type();
        tensor->set_elem_type(onnx::TensorProto_DataType_INT64);
        tensor->mutable_shape()->add_dim()->set_dim_value(
            static_cast<std::int64_t>(requested->size()));
      }
      node->add_input(shape_name);
    }

    const auto output = "value_" + std::to_string(call_index++);
    node->add_output(output);
    names.emplace_back(op.value(), output);
  }

  const auto returned = fn->entry().terminator().returned();
  if (returned.size() != 1U) {
    return false;
  }
  const auto output = lookup(names, returned.front());
  if (output.empty() ||
      !tensor_info(*graph->add_output(), output, returned.front().type())) {
    return false;
  }

  std::ofstream stream(path, std::ios::binary);
  return stream && model.SerializeToOstream(&stream);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 7) {
    return EXIT_FAILURE;
  }
  const int tensor_index = 1;
  const int onnx_index = 2;
  const int schema_index = 3;
  const int native_index = 4;
  const int model_index = 5;
  const int output_index = 6;
  joggle::Compiler compiler;
  compiler.load(argv[tensor_index]);
  compiler.load(argv[onnx_index]);
  compiler.load(argv[schema_index]);
  if (!compiler.link() || !compiler.load_native("onnx", argv[native_index])) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto bytes = read_bytes(argv[model_index]);
  const auto model = compiler.run<joggle::Mod>(
      "onnx.read", bytes, std::string{"squeezenet_roundtrip"});
  if (!model || !emit(*model, argv[output_index])) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
