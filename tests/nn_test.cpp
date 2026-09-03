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
  import tensor@1.0.0;
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
  const auto tensor_value = compiler.materialize("projected_schema.tensor_value");
  const auto storage_value = compiler.materialize("projected_schema.storage_value");
  const auto dynamic_read = compiler.materialize("projected_schema.dynamic_read");
  if (!nn || !model || !function || !tensor_value || !storage_value ||
      !dynamic_read) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto operations = function->instructions();
  const auto outputs = function->entry().terminator().returned();
  const auto shape =
      outputs.empty()
          ? std::optional<std::vector<std::int64_t>>{}
          : outputs.front().type().get<std::vector<std::int64_t>>("shape");
  bool ok = true;
  ok &= expect(operations.size() == 7U &&
                   operations[0].callee().symbol().qualified_name() ==
                       "nn.conv2d_nchw" &&
                   operations[1].callee().symbol().qualified_name() ==
                       "nn.batch_norm_nchw" &&
                   operations[5].callee().symbol().qualified_name() ==
                       "nn.add" &&
                   operations[6].callee().symbol().qualified_name() ==
                       "nn.relu",
               "the ResNet-18 basic block resolves to ordinary NN Module "
               "operations");
  ok &= expect(shape &&
                   *shape == std::vector<std::int64_t>({1, 64, 56, 56}),
               "convolution formulas and residual addition preserve the "
               "declared output shape");
  const auto tensor_descriptor = tensor_value->entry().terminator().returned().front().type();
  const auto storage_descriptor = storage_value->entry().terminator().returned().front().type();
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
  ok &= expect(
      tensor_element &&
          tensor_element->schema().symbol().qualified_name() == "prelude.f32" &&
          tensor_shape ==
              std::optional<std::vector<std::int64_t>>{{2, 3}} &&
          storage_space == std::optional<std::string>{"sram"} &&
          derived_element &&
          derived_element->schema().symbol().qualified_name() ==
              "prelude.f32" &&
          identity_shape ==
              std::optional<std::vector<std::int64_t>>{{2, 3}} &&
          derived_space == std::optional<std::string>{"sram"},
      "type, list, and string fields flow through one parameter system");
  const auto memory_instructions = dynamic_read->instructions();
  ok &= expect(
      memory_instructions.size() == 2U &&
          memory_instructions.back().callee().symbol().qualified_name() ==
              "buffer.read" &&
          memory_instructions.back().arguments().size() == 4U,
      "buffer accesses accept dynamic variadic index values");
  ok &= expect(joggle::format(*nn) == read(JOGGLE_NN_MODULE) &&
                   joggle::format(*model) == read(JOGGLE_RESNET_BLOCK),
               "the NN vocabulary and model fixture are canonical, "
               "maintainable source");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
