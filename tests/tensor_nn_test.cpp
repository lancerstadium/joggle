#include <cstdint>
#include <cstdlib>
#include <iostream>
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

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_FIXED_MODULE);
  compiler.load(JOGGLE_MLP_MODULE);
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto arith = compiler.module("arith");
  const auto tensor = compiler.module("tensor");
  const auto nn = compiler.module("nn");
  if (!arith || !tensor || !nn ||
      !compiler.load_behavior("arith", JOGGLE_ARITH_BEHAVIOR) ||
      !compiler.load_behavior("tensor", JOGGLE_TENSOR_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto integer_decl = arith->type("integer");
  const auto tensor_decl = tensor->type("tensor");
  const auto dense_decl = tensor->attribute("dense");
  const auto constant_decl = tensor->operation("constant");
  const auto reshape_decl = tensor->operation("reshape");
  const auto linear_decl = nn->operation("linear");
  const auto relu_decl = nn->operation("relu");
  if (!integer_decl || !tensor_decl || !dense_decl || !constant_decl ||
      !reshape_decl || !linear_decl || !relu_decl) {
    return EXIT_FAILURE;
  }

  const auto integer = compiler.make(*integer_decl, std::int64_t{8});
  if (!integer) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto tensor_type = [&](std::initializer_list<std::int64_t> dimensions) {
    return compiler.make(
        *tensor_decl, *integer,
        std::vector<std::int64_t>(dimensions.begin(), dimensions.end()));
  };
  const auto flat = tensor_type({4});
  const auto activation = tensor_type({1, 4});
  const auto weight = tensor_type({3, 4});
  const auto bias = tensor_type({3});
  const auto output = tensor_type({1, 3});
  const auto six_elements = tensor_type({2, 3});
  const auto dense = [&](std::initializer_list<std::int64_t> values) {
    return compiler.make(
        *dense_decl, std::vector<std::int64_t>(values.begin(), values.end()));
  };
  const auto weight_data = dense({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
  const auto bias_data = dense({1, 2, 3});
  auto graph = compiler.graph();
  if (!flat || !activation || !weight || !bias || !output || !six_elements ||
      !weight_data || !bias_data || !graph) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  {
    auto edit = graph->edit();
    const auto input = edit.argument(*flat);
    const auto weights = edit.append(*constant_decl, {}, {*weight});
    edit.set(weights, "value", *weight_data);
    const auto biases = edit.append(*constant_decl, {}, {*bias});
    edit.set(biases, "value", *bias_data);
    const auto reshaped = edit.append(
        *reshape_decl, {input},
        joggle::property("shape", std::vector<std::int64_t>{1, 4}));
    const auto linear = edit.append(
        *linear_decl,
        {reshaped.result(0), weights.result(0), biases.result(0)});
    edit.append(*relu_decl, {linear.result(0)});

    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  bool ok = true;
  ok &= expect(compiler.verify(*graph), "tensor and NN graph verifies");
  const auto operations = graph->operations();
  const auto reshape_shape =
      operations.size() > 2U
          ? operations[2].get<std::vector<std::int64_t>>("shape")
          : std::nullopt;
  ok &= expect(operations.size() == 5U && graph->inputs().size() == 1U &&
                   reshape_shape &&
                   *reshape_shape == std::vector<std::int64_t>({1, 4}) &&
                   operations[2].result(0).type() == *activation &&
                   operations[3].result(0).type() == *output &&
                   operations[4].result(0).type() == *output,
               "C++ and text construction share tensor and NN contracts");
  const auto nodes = compiler.query(
      *graph, [](const joggle::Graph& current) {
        return current.all_operations().size();
      });
  ok &= expect(nodes && *nodes == 5U,
               "queries inspect the same graph used by passes");
  ok &= expect(compiler.run(*graph, "nn.canonicalize") &&
                   graph->operations().size() == 5U,
               "a non-matching NN canonicalization preserves the graph");

  const auto parsed = compiler.graph("mlp.main");
  ok &= expect(parsed && parsed->inputs().size() == 1U &&
                   parsed->operations().size() == 5U,
               "the packaged MLP opens as an ordinary graph");

  auto nested_relu = compiler.graph();
  if (!nested_relu) {
    return EXIT_FAILURE;
  }
  {
    auto edit = nested_relu->edit();
    const auto input = edit.argument(*activation);
    const auto first = edit.append(*relu_decl, {input});
    const auto second = edit.append(*relu_decl, {first.result(0)});
    edit.append(*relu_decl, {second.result(0)});
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  ok &= expect(compiler.run(*nested_relu, "nn.canonicalize") &&
                   nested_relu->operations().size() == 1U,
               "NN canonicalization contracts nested ReLUs to one");

  auto invalid_linear = compiler.graph();
  if (!invalid_linear) {
    return EXIT_FAILURE;
  }
  {
    auto edit = invalid_linear->edit();
    const auto input = edit.argument(*activation);
    const auto weights = edit.argument(*weight);
    const auto biases = edit.argument(*bias);
    edit.append(*linear_decl, {input, weights, biases}, {*activation});
    joggle::Diagnostics diagnostics;
    ok &= expect(!edit.commit(diagnostics) &&
                     invalid_linear->inputs().empty() &&
                     invalid_linear->operations().empty(),
                 "an incompatible linear result is rejected atomically");
  }

  auto invalid_reshape = compiler.graph();
  if (!invalid_reshape) {
    return EXIT_FAILURE;
  }
  {
    auto edit = invalid_reshape->edit();
    const auto input = edit.argument(*flat);
    const auto reshaped = edit.append(
        *reshape_decl, {input},
        joggle::property("shape", std::vector<std::int64_t>{2, 3}));
    edit.output(reshaped.result(0));
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  ok &= expect(!compiler.verify(*invalid_reshape) &&
                   compiler.diagnostics().entries().back().message.find(
                       "preserve the element count") != std::string::npos,
               "tensor behavior checks nonlinear reshape invariants");

  joggle::Compiler invalid_data;
  invalid_data.load(JOGGLE_ARITH_MODULE);
  invalid_data.load(JOGGLE_TENSOR_MODULE);
  invalid_data.add(R"(
joggle 1;

module invalid_data@1.0.0 {
  import arith@1 as arith;
  import tensor@1 as tensor;

  graph main() -> tensor.tensor<arith.integer<8>, [3, 4]> {
    %weight: tensor.tensor<arith.integer<8>, [3, 4]> = tensor.constant(
      value = tensor.dense<[1, 2, 3]>
    );
    return %weight;
  }
}
)", "invalid-data.joggle");
  const bool invalid_ready =
      invalid_data.link() &&
      invalid_data.load_behavior("arith", JOGGLE_ARITH_BEHAVIOR) &&
      invalid_data.load_behavior("tensor", JOGGLE_TENSOR_BEHAVIOR);
  const auto invalid_graph =
      invalid_ready ? invalid_data.graph("invalid_data.main")
                    : std::optional<joggle::Graph>{};
  const auto data_diagnostics = invalid_data.diagnostics().entries();
  ok &= expect(!invalid_graph && !data_diagnostics.empty() &&
                   data_diagnostics.back().message.find(
                       "data size does not match") != std::string::npos &&
                   data_diagnostics.back().source &&
                   data_diagnostics.back().source->source ==
                       "invalid-data.joggle",
               "tensor.constant rejects data inconsistent with its result");

  joggle::Compiler invalid_attribute;
  invalid_attribute.load(JOGGLE_ARITH_MODULE);
  invalid_attribute.load(JOGGLE_TENSOR_MODULE);
  invalid_attribute.add(R"(
joggle 1;

module invalid_attribute@1.0.0 {
  import arith@1 as arith;
  import tensor@1 as tensor;

  graph main() -> tensor.tensor<arith.integer<8>, [1]> {
    %value: tensor.tensor<arith.integer<8>, [1]> = tensor.constant(
      value = tensor.dense<[]>
    );
    return %value;
  }
}
)", "invalid-attribute.joggle");
  const bool attribute_ready =
      invalid_attribute.link() &&
      invalid_attribute.load_behavior("arith", JOGGLE_ARITH_BEHAVIOR) &&
      invalid_attribute.load_behavior("tensor", JOGGLE_TENSOR_BEHAVIOR);
  const auto invalid_attribute_graph =
      attribute_ready ? invalid_attribute.graph("invalid_attribute.main")
                      : std::optional<joggle::Graph>{};
  const auto attribute_diagnostics =
      invalid_attribute.diagnostics().entries();
  ok &= expect(!invalid_attribute_graph && !attribute_diagnostics.empty() &&
                   attribute_diagnostics.back().message.find(
                       "tensor.dense needs at least one value") !=
                       std::string::npos &&
                   attribute_diagnostics.back().source &&
                   attribute_diagnostics.back().source->source ==
                       "invalid-attribute.joggle",
               "attribute diagnostics retain their source file");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
