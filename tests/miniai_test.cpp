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
  compiler.load(JOGGLE_BITMATH_MODULE);
  compiler.load(JOGGLE_MINIAI_MODULE);
  compiler.load(JOGGLE_FIXED_MODULE);
  compiler.load(JOGGLE_FEEDFORWARD_MODULE);
  if (!compiler.link()) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto bitmath = compiler.module("bitmath");
  const auto miniai = compiler.module("miniai");
  if (!bitmath || !miniai) {
    return EXIT_FAILURE;
  }
  if (!compiler.load_behavior("bitmath", JOGGLE_BITMATH_BEHAVIOR) ||
      !compiler.load_behavior("miniai", JOGGLE_MINIAI_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto word_schema = bitmath->type("word");
  const auto tensor_schema = miniai->type("tensor");
  const auto data_schema = miniai->attribute("tensor_data");
  const auto input_schema = miniai->operation("input");
  const auto parameter_schema = miniai->operation("parameter");
  const auto reshape_schema = miniai->operation("reshape");
  const auto dense_schema = miniai->operation("dense");
  const auto relu_schema = miniai->operation("relu");
  if (!word_schema || !tensor_schema || !data_schema || !input_schema ||
      !parameter_schema || !reshape_schema || !dense_schema || !relu_schema) {
    return EXIT_FAILURE;
  }

  const auto compute_nodes = [](const joggle::Graph& graph) {
    return graph.all_operations().size();
  };

  const auto word = compiler.make(*word_schema, std::int64_t{8});
  if (!word) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto tensor = [&](std::initializer_list<std::int64_t> dimensions) {
    return compiler.make(
        *tensor_schema, *word,
        std::vector<std::int64_t>(dimensions.begin(), dimensions.end()));
  };
  const auto flat = tensor({4});
  const auto activation = tensor({1, 4});
  const auto weight = tensor({3, 4});
  const auto bias = tensor({3});
  const auto output = tensor({1, 3});
  const auto six_elements = tensor({2, 3});
  const auto tensor_data = [&](std::initializer_list<std::int64_t> values) {
    return compiler.make(
        *data_schema, std::vector<std::int64_t>(values.begin(), values.end()));
  };
  const auto weight_data = tensor_data({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
  const auto bias_data = tensor_data({1, 2, 3});
  auto graph = compiler.graph();
  if (!flat || !activation || !weight || !bias || !output || !six_elements ||
      !weight_data || !bias_data || !graph) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  {
    auto edit = graph->edit();
    const auto input = edit.append(*input_schema, {}, {*flat});
    edit.set(input, "name", std::string{"input"});
    const auto weights = edit.append(*parameter_schema, {}, {*weight});
    edit.set(weights, "name", std::string{"weight"});
    edit.set(weights, "data", *weight_data);
    const auto biases = edit.append(*parameter_schema, {}, {*bias});
    edit.set(biases, "name", std::string{"bias"});
    edit.set(biases, "data", *bias_data);
    const auto reshaped = edit.append(
        *reshape_schema, {input.result(0)},
        joggle::property("shape", std::vector<std::int64_t>{1, 4}));
    const auto dense = edit.append(
        *dense_schema,
        {reshaped.result(0), weights.result(0), biases.result(0)});
    edit.append(*relu_schema, {dense.result(0)});

    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      diagnostics.print(std::cerr);
      return EXIT_FAILURE;
    }
  }

  bool ok = true;
  ok &= expect(compiler.verify(*graph), "normal feed-forward graph verifies");
  const auto constructed = graph->operations();
  const auto reshape_shape =
      constructed.size() > 3U
          ? constructed[3].get<std::vector<std::int64_t>>("shape")
          : std::nullopt;
  ok &= expect(constructed.size() == 6U && reshape_shape &&
                   *reshape_shape == std::vector<std::int64_t>({1, 4}) &&
                   constructed[3].result(0).type() == *activation &&
                   constructed[4].result(0).type() == *output &&
                   constructed[5].result(0).type() == *output,
               "C++ construction shares reshape, dense, and relu contracts "
               "with the text graph");
  const auto nodes = compiler.query(*graph, compute_nodes);
  ok &= expect(nodes && *nodes == 6U, "MiniAI query sees six nodes");
  ok &= expect(compiler.run(*graph, "miniai.simplify"),
               "MiniAI contraction pass executes");
  ok &= expect(graph->operations().size() == 6U,
               "a non-matching contraction preserves the graph");

  const auto parsed_graph = compiler.graph("feedforward.main");
  if (!parsed_graph) {
    compiler.diagnostics().print(std::cerr);
  }
  ok &= expect(parsed_graph && parsed_graph->operations().size() == 6U,
               "a packaged graph instantiates and verifies");

  auto nested_relu = compiler.graph();
  if (!nested_relu) {
    return EXIT_FAILURE;
  }
  auto nested_edit = nested_relu->edit();
  const auto nested_input = nested_edit.argument(*activation);
  const auto relu1 = nested_edit.append(*relu_schema, {nested_input});
  const auto relu2 = nested_edit.append(*relu_schema, {relu1.result(0)});
  nested_edit.append(*relu_schema, {relu2.result(0)});
  joggle::Diagnostics nested_diagnostics;
  if (!nested_edit.commit(nested_diagnostics)) {
    nested_diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }
  ok &= expect(compiler.run(*nested_relu, "miniai.simplify") &&
                   nested_relu->operations().size() == 1U,
               "a nested pass term contracts three ReLUs to one");

  auto invalid = compiler.graph();
  if (!invalid) {
    return EXIT_FAILURE;
  }
  bool invalid_dense_rejected = false;
  {
    auto invalid_edit = invalid->edit();
    const auto invalid_input = invalid_edit.argument(*activation);
    const auto invalid_weight = invalid_edit.argument(*weight);
    const auto invalid_bias = invalid_edit.argument(*bias);
    invalid_edit.append(*dense_schema,
                        {invalid_input, invalid_weight, invalid_bias},
                        {*activation});
    joggle::Diagnostics invalid_diagnostics;
    invalid_dense_rejected =
        !invalid_edit.commit(invalid_diagnostics) && !invalid_diagnostics.ok();
  }
  ok &= expect(invalid_dense_rejected && invalid->inputs().empty() &&
                   invalid->operations().empty(),
               "the shared op contract rejects and rolls back an incompatible "
               "dense shape at commit");

  auto invalid_reshape = compiler.graph();
  if (!invalid_reshape) {
    return EXIT_FAILURE;
  }
  {
    auto edit = invalid_reshape->edit();
    const auto input = edit.argument(*flat);
    const auto reshaped = edit.append(
        *reshape_schema, {input},
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
               "MiniAI behavior adds the non-structural reshape invariant");

  joggle::Compiler invalid_data;
  invalid_data.load(JOGGLE_BITMATH_MODULE);
  invalid_data.load(JOGGLE_MINIAI_MODULE);
  invalid_data.add(R"(
joggle 1;

module invalid_data@1.0.0 {
  import bitmath@1 as math;
  import miniai@1 as nn;

  graph main() -> nn.tensor<math.word<8>, [3, 4]> {
    %weight: nn.tensor<math.word<8>, [3, 4]> = nn.parameter(
      name = "weight",
      data = nn.tensor_data<[1, 2, 3]>
    );
    return %weight;
  }
}
)",
                   "invalid-data.joggle");
  const bool invalid_linked = invalid_data.link();
  const bool invalid_behaviors =
      invalid_linked &&
      invalid_data.load_behavior("bitmath", JOGGLE_BITMATH_BEHAVIOR) &&
      invalid_data.load_behavior("miniai", JOGGLE_MINIAI_BEHAVIOR);
  const auto invalid_data_module = invalid_data.module("invalid_data");
  const auto invalid_data_graph = invalid_behaviors && invalid_data_module
                                      ? invalid_data.graph("invalid_data.main")
                                      : std::optional<joggle::Graph>{};
  const auto data_diagnostics = invalid_data.diagnostics().entries();
  ok &= expect(
      !invalid_data_graph && !invalid_data.ok() && !data_diagnostics.empty() &&
          data_diagnostics.back().message.find("data size does not match") !=
              std::string::npos &&
          data_diagnostics.back().source &&
          data_diagnostics.back().source->source == "invalid-data.joggle" &&
          data_diagnostics.back().source->begin.line == 9U,
      "MiniAI rejects bad parameter data at its source operation");

  joggle::Compiler invalid_attribute;
  invalid_attribute.load(JOGGLE_BITMATH_MODULE);
  invalid_attribute.load(JOGGLE_MINIAI_MODULE);
  invalid_attribute.add(R"(
joggle 1;

module invalid_attribute@1.0.0 {
  import bitmath@1 as math;
  import miniai@1 as nn;

  graph main() -> nn.tensor<math.word<8>, [1]> {
    %weight: nn.tensor<math.word<8>, [1]> = nn.parameter(
      name = "weight",
      data = nn.tensor_data<[]>
    );
    return %weight;
  }
}
)",
                        "invalid-attribute.joggle");
  const bool attribute_linked = invalid_attribute.link();
  const bool attribute_behaviors =
      attribute_linked &&
      invalid_attribute.load_behavior("bitmath", JOGGLE_BITMATH_BEHAVIOR) &&
      invalid_attribute.load_behavior("miniai", JOGGLE_MINIAI_BEHAVIOR);
  const auto invalid_attribute_module =
      invalid_attribute.module("invalid_attribute");
  const auto invalid_attribute_graph =
      attribute_behaviors && invalid_attribute_module
          ? invalid_attribute.graph("invalid_attribute.main")
          : std::optional<joggle::Graph>{};
  const auto attribute_diagnostics = invalid_attribute.diagnostics().entries();
  ok &= expect(!invalid_attribute_graph && !attribute_diagnostics.empty() &&
                   attribute_diagnostics.back().message.find(
                       "tensor_data needs at least one value") !=
                       std::string::npos &&
                   attribute_diagnostics.back().source &&
                   attribute_diagnostics.back().source->source ==
                       "invalid-attribute.joggle" &&
                   attribute_diagnostics.back().source->begin.line == 11U,
               "attribute behavior diagnostics retain the graph source range");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
