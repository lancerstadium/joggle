#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::string read(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_BUFFER_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_RESNET_BLOCK);
  compiler.add(R"(
joggle 1;
module projected_schema@1.0.0 {
  import tensor@2.0.0;
  import buffer@1.0.0;

  type descriptor(element: type, shape: list<int>, space: string);

  fn describe_tensor<T: tensor.ranked_tensor>(input: T)
    -> descriptor<T.element_type, T.shape, "value">;
  fn describe_buffer<T: buffer.storage>(input: T)
    -> descriptor<T.element_type, T.shape, T.address_space>;

  fn tensor_value(input: tensor.ranked<f32, [2, 3]>)
    -> descriptor<f32, [2, 3], "value"> {
    result = describe_tensor(input);
    return result;
  }

  fn storage_value(input: buffer.buffer<f32, [2, 3], "sram">)
    -> descriptor<f32, [2, 3], "sram"> {
    result = describe_buffer(input);
    return result;
  }

  fn transpose_value(input: tensor.ranked<f32, [2, 3, 4]>)
    -> tensor.ranked<f32, [4, 2, 3]> {
    return tensor.transpose(input, [2, 0, 1]);
  }

  fn constant_value() -> tensor.ranked<f32, [2, 3]> {
    value: tensor.ranked<f32, [2, 3]> = tensor.constant(
      resource: "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
    );
    return value;
  }

  fn dynamic_read(
    input: buffer.buffer<f32, [2, 3], "sram">,
    row: index,
    column: index
  ) -> f32 {
    chain = buffer.start();
    value, chain = buffer.read(chain, input, row, column);
    return value;
  }
}
)",
               "projected-schema.joggle");
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto nn = compiler.module("nn");
  const auto model = compiler.module("resnet18_basic_block");
  const auto function = compiler.materialize("resnet18_basic_block.main");
  const auto tensor_value =
      compiler.materialize("projected_schema.tensor_value");
  const auto storage_value =
      compiler.materialize("projected_schema.storage_value");
  const auto transpose_value =
      compiler.materialize("projected_schema.transpose_value");
  const auto constant_value =
      compiler.materialize("projected_schema.constant_value");
  const auto dynamic_read =
      compiler.materialize("projected_schema.dynamic_read");
  if (!nn || !model || !function || !tensor_value || !storage_value ||
      !transpose_value || !constant_value || !dynamic_read) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto operations = function->ops();
  const auto outputs = function->entry().terminator().returned();
  const auto shape =
      outputs.empty()
          ? std::optional<std::vector<std::int64_t>>{}
          : outputs.front().type().get<std::vector<std::int64_t>>("shape");
  bool ok = true;
  ok &=
      expect(operations.size() == 7U &&
                 operations[0].callee().symbol().qualified_name() ==
                     "nn.conv2d_nchw" &&
                 operations[1].callee().symbol().qualified_name() ==
                     "nn.batch_norm_nchw" &&
                 operations[5].callee().symbol().qualified_name() == "nn.add" &&
                 operations[6].callee().symbol().qualified_name() == "nn.relu",
             "the ResNet-18 basic block resolves to ordinary NN Module "
             "operations");
  ok &= expect(shape && *shape == std::vector<std::int64_t>({1, 64, 56, 56}),
               "convolution formulas and residual addition preserve the "
               "declared output shape");
  const auto tensor_descriptor =
      tensor_value->entry().terminator().returned().front().type();
  const auto storage_descriptor =
      storage_value->entry().terminator().returned().front().type();
  const auto tensor_input = tensor_value->arguments().front().type();
  const auto storage_input = storage_value->arguments().front().type();
  const auto tensor_element = tensor_descriptor.get<joggle::Type>("element");
  const auto tensor_shape =
      tensor_descriptor.get<std::vector<std::int64_t>>("shape");
  const auto storage_space = storage_descriptor.get<std::string>("space");
  const auto derived_element = tensor_input.get<joggle::Type>("element_type");
  const auto identity_shape =
      tensor_input.get<std::vector<std::int64_t>>("shape");
  const auto derived_space = storage_input.get<std::string>("address_space");
  const auto transposed_shape = transpose_value->entry()
                                    .terminator()
                                    .returned()
                                    .front()
                                    .type()
                                    .get<std::vector<std::int64_t>>("shape");
  const auto constants = constant_value->ops();
  const std::string constant_source =
      joggle::format(*constant_value, "constant_value");
  ok &= expect(
      tensor_element &&
          tensor_element->schema().symbol().qualified_name() == "prelude.f32" &&
          tensor_shape == std::optional<std::vector<std::int64_t>>{{2, 3}} &&
          storage_space == std::optional<std::string>{"sram"} &&
          derived_element &&
          derived_element->schema().symbol().qualified_name() ==
              "prelude.f32" &&
          identity_shape == std::optional<std::vector<std::int64_t>>{{2, 3}} &&
          derived_space == std::optional<std::string>{"sram"} &&
          transposed_shape ==
              std::optional<std::vector<std::int64_t>>{{4, 2, 3}},
      "type, list, and string fields flow through one parameter system");
  ok &= expect(
      constants.size() == 1U &&
          constants.front().callee().symbol().qualified_name() ==
              "tensor.constant" &&
          constants.front().property<std::string>("resource") ==
              std::optional<std::string>{
                  "sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"} &&
          constant_value->entry()
                  .terminator()
                  .returned()
                  .front()
                  .type()
                  .get<std::vector<std::int64_t>>("shape") ==
              std::optional<std::vector<std::int64_t>>{{2, 3}} &&
          constant_source.find(
              "tensor.constant(resource: "
              "\"sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\"") !=
              std::string::npos,
      "a stable tensor resource reference is a typed, serializable IR "
      "op");
  const auto memory_ops = dynamic_read->ops();
  ok &= expect(
      memory_ops.size() == 2U &&
          memory_ops.back().callee().symbol().qualified_name() ==
              "buffer.read" &&
          memory_ops.back().arguments().size() == 4U,
      "buffer accesses accept dynamic variadic index values");
  ok &= expect(joggle::format(*nn) == read(JOGGLE_NN_MODULE) &&
                   joggle::format(*model) == read(JOGGLE_RESNET_BLOCK),
               "the NN vocabulary and model fixture are canonical, "
               "maintainable source");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
