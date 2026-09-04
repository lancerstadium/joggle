#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <joggle/joggle.h>

#include "onnx.pb.h"

namespace {

using Integers = std::vector<std::int64_t>;
using Reals = std::vector<double>;
using Shape = std::vector<std::int64_t>;

void append_u32(joggle::Bytes& output, std::uint32_t value) {
  for (std::size_t byte = 0; byte < 4U; ++byte) {
    output.push_back(static_cast<std::byte>((value >> (byte * 8U)) & 0xffU));
  }
}

joggle::Bytes f32_bytes(const std::vector<float>& values) {
  joggle::Bytes output;
  output.reserve(values.size() * 4U);
  for (const float value : values) {
    append_u32(output, std::bit_cast<std::uint32_t>(value));
  }
  return output;
}

bool write_bytes(const char* path, const joggle::Bytes& bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

void tensor_info(onnx::ValueInfoProto& info, std::string name,
                 std::int32_t element, const Shape& shape) {
  info.set_name(std::move(name));
  auto* tensor = info.mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(element);
  for (const auto extent : shape) {
    tensor->mutable_shape()->add_dim()->set_dim_value(extent);
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    return EXIT_FAILURE;
  }
  joggle::Compiler compiler;
  compiler.load(argv[1]);
  if (!compiler.link() || !compiler.load_native("quant", argv[2])) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto i8 = compiler.make("i8");
  const Shape shape{2, 3};
  const Reals scales{1.0, 2.0, 0.5};
  const Integers zeros{0, 1, -1};
  const std::vector<float> values{-2.5F, -3.0F, -0.25F,
                                  2.5F,  3.0F,  0.25F};
  const auto input = f32_bytes(values);
  const auto quantized =
      i8 ? compiler.run<joggle::Bytes>("quant.quantize", input, scales, zeros,
                                       shape, std::int64_t{-1}, *i8)
         : std::nullopt;
  const auto dequantized =
      quantized && i8
          ? compiler.run<joggle::Bytes>("quant.dequantize", *quantized,
                                        scales, zeros, shape,
                                        std::int64_t{-1}, *i8)
          : std::nullopt;
  if (!quantized || !dequantized || !write_bytes(argv[4], *quantized) ||
      !write_bytes(argv[5], *dequantized)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  onnx::ModelProto model;
  model.set_ir_version(7);
  model.set_producer_name("joggle-quant-reference-test");
  auto* opset = model.add_opset_import();
  opset->set_domain("");
  opset->set_version(13);
  auto* graph = model.mutable_graph();
  graph->set_name("quant_reference");
  tensor_info(*graph->add_input(), "input", onnx::TensorProto_DataType_FLOAT,
              shape);
  tensor_info(*graph->add_output(), "quantized",
              onnx::TensorProto_DataType_INT8, shape);
  tensor_info(*graph->add_output(), "dequantized",
              onnx::TensorProto_DataType_FLOAT, shape);

  auto* scale = graph->add_initializer();
  scale->set_name("scale");
  scale->set_data_type(onnx::TensorProto_DataType_FLOAT);
  scale->add_dims(3);
  const auto scale_bytes =
      f32_bytes({static_cast<float>(scales[0]), static_cast<float>(scales[1]),
                 static_cast<float>(scales[2])});
  scale->set_raw_data(reinterpret_cast<const char*>(scale_bytes.data()),
                      scale_bytes.size());

  auto* zero = graph->add_initializer();
  zero->set_name("zero");
  zero->set_data_type(onnx::TensorProto_DataType_INT8);
  zero->add_dims(3);
  const char zero_bytes[] = {0, 1, static_cast<char>(0xff)};
  zero->set_raw_data(zero_bytes, sizeof(zero_bytes));

  auto* quantize = graph->add_node();
  quantize->set_op_type("QuantizeLinear");
  quantize->add_input("input");
  quantize->add_input("scale");
  quantize->add_input("zero");
  quantize->add_output("quantized");
  auto* quantize_axis = quantize->add_attribute();
  quantize_axis->set_name("axis");
  quantize_axis->set_type(onnx::AttributeProto_AttributeType_INT);
  quantize_axis->set_i(-1);

  auto* dequantize = graph->add_node();
  dequantize->set_op_type("DequantizeLinear");
  dequantize->add_input("quantized");
  dequantize->add_input("scale");
  dequantize->add_input("zero");
  dequantize->add_output("dequantized");
  auto* dequantize_axis = dequantize->add_attribute();
  dequantize_axis->set_name("axis");
  dequantize_axis->set_type(onnx::AttributeProto_AttributeType_INT);
  dequantize_axis->set_i(-1);

  std::ofstream model_output(argv[3], std::ios::binary | std::ios::trunc);
  return model.SerializeToOstream(&model_output) && model_output.good()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
