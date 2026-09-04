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
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

bool tensor_info(onnx::ValueInfoProto& info, std::string name,
                 const joggle::Type& type) {
  const auto element = type.get<joggle::Type>("element");
  const auto shape = type.get<Shape>("shape");
  if (!element || !shape || element->schema().name() != "f32") {
    return false;
  }
  info.set_name(std::move(name));
  auto* tensor = info.mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(onnx::TensorProto_DataType_FLOAT);
  for (const auto dimension : *shape) {
    tensor->mutable_shape()->add_dim()->set_dim_value(dimension);
  }
  return true;
}

std::string lookup(
    const std::vector<std::pair<joggle::Value, std::string>>& names,
    const joggle::Value& value) {
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

void add_ints(onnx::NodeProto& node, std::string name,
              const Shape& values) {
  auto* attribute = node.add_attribute();
  attribute->set_name(std::move(name));
  attribute->set_type(onnx::AttributeProto_AttributeType_INTS);
  for (const auto value : values) {
    attribute->add_ints(value);
  }
}

bool emit(const joggle::Module& module, const char* path) {
  const auto main = module.function("main");
  const auto function = main ? main->body() : nullptr;
  if (!function || function->arguments().size() != 1U) {
    return false;
  }

  onnx::ModelProto model;
  model.set_ir_version(onnx::IR_VERSION_2017_11_3);
  model.set_producer_name("joggle-onnx-roundtrip-test");
  auto* opset = model.add_opset_import();
  opset->set_domain("");
  opset->set_version(7);
  auto* graph = model.mutable_graph();
  graph->set_name("joggle_roundtrip");

  std::vector<std::pair<joggle::Value, std::string>> names;
  const auto argument = function->arguments().front();
  if (!tensor_info(*graph->add_input(), "input", argument.type())) {
    return false;
  }
  names.emplace_back(argument, "input");

  std::size_t constant_index = 0;
  std::size_t call_index = 0;
  for (const auto& op : function->ops()) {
    const auto callee = op.callee().symbol().local_name();
    if (callee == "constant") {
      const auto digest = op.property<std::string>("content");
      const auto data = digest ? module.data(*digest) : std::nullopt;
      const auto shape = op.value().type().get<Shape>("shape");
      if (!data || !shape) {
        return false;
      }
      const auto name = "constant_" + std::to_string(constant_index++);
      auto* initializer = graph->add_initializer();
      initializer->set_name(name);
      initializer->set_data_type(onnx::TensorProto_DataType_FLOAT);
      for (const auto dimension : *shape) {
        initializer->add_dims(dimension);
      }
      initializer->set_raw_data(
          reinterpret_cast<const char*>(data->data()), data->size());
      if (!tensor_info(*graph->add_input(), name, op.value().type())) {
        return false;
      }
      names.emplace_back(op.value(), name);
      continue;
    }

    auto* node = graph->add_node();
    node->set_name("call_" + std::to_string(call_index));
    if (callee == "conv") {
      node->set_op_type("Conv");
      const auto strides = op.property<Shape>("strides");
      const auto pads = op.property<Shape>("pads");
      const auto dilations = op.property<Shape>("dilations");
      const auto group = op.property<std::int64_t>("group");
      if (!strides || !pads || !dilations || !group) {
        return false;
      }
      add_ints(*node, "strides", *strides);
      add_ints(*node, "pads", *pads);
      add_ints(*node, "dilations", *dilations);
      add_int(*node, "group", *group);
    } else if (callee == "relu") {
      node->set_op_type("Relu");
    } else if (callee == "max_pool" || callee == "average_pool") {
      node->set_op_type(callee == "max_pool" ? "MaxPool" : "AveragePool");
      const auto kernel = op.property<Shape>("kernel");
      const auto strides = op.property<Shape>("strides");
      const auto pads = op.property<Shape>("pads");
      const auto ceil_mode = op.property<bool>("ceil_mode");
      if (!kernel || !strides || !pads || !ceil_mode) {
        return false;
      }
      add_ints(*node, "kernel_shape", *kernel);
      add_ints(*node, "strides", *strides);
      add_ints(*node, "pads", *pads);
      if (*ceil_mode) {
        return false;
      }
    } else if (callee == "concat") {
      node->set_op_type("Concat");
      const auto axis = op.property<std::int64_t>("axis");
      if (!axis) {
        return false;
      }
      add_int(*node, "axis", *axis);
    } else if (callee == "reshape") {
      node->set_op_type("Reshape");
    } else {
      return false;
    }

    for (const auto& operand : op.operands()) {
      const auto name = lookup(names, operand);
      if (name.empty()) {
        return false;
      }
      node->add_input(name);
    }
    if (callee == "reshape") {
      const auto requested = op.property<Shape>("shape");
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
      auto* input = graph->add_input();
      input->set_name(shape_name);
      auto* tensor = input->mutable_type()->mutable_tensor_type();
      tensor->set_elem_type(onnx::TensorProto_DataType_INT64);
      tensor->mutable_shape()->add_dim()->set_dim_value(
          static_cast<std::int64_t>(requested->size()));
      node->add_input(shape_name);
    }

    const auto output = "value_" + std::to_string(call_index++);
    node->add_output(output);
    names.emplace_back(op.value(), output);
  }

  const auto returned = function->entry().terminator().returned();
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
  if (argc != 6) {
    return EXIT_FAILURE;
  }
  joggle::Compiler compiler;
  compiler.load(argv[1]);
  compiler.load(argv[2]);
  if (!compiler.link() || !compiler.load_native("onnx", argv[3])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto bytes = read_bytes(argv[4]);
  const auto model =
      compiler.run<joggle::Module>("onnx.read", bytes,
                                   std::string{"squeezenet_roundtrip"});
  if (!model || !emit(*model, argv[5])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
