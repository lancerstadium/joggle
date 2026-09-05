#include <cstddef>
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

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

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

bool load(joggle::Compiler& compiler, const char* arith, const char* tensor,
          const char* quant, const char* onnx, const char* onnx_native,
          const char* transform, const char* transform_native) {
  compiler.load(arith);
  compiler.load(tensor);
  compiler.load(quant);
  compiler.load(transform);
  compiler.load(onnx);
  return compiler.link() && compiler.load_native("onnx", onnx_native) &&
         compiler.load_native("transform", transform_native);
}

std::string callee_name(const joggle::Op& op) {
  const auto declaration = op.callee().referenced_fn();
  return declaration ? std::string(declaration->name()) : std::string{};
}

using ExpectedCall = std::pair<std::string_view, std::vector<std::int64_t>>;

const std::vector<ExpectedCall> expected_calls{
    {"conv", {1, 64, 111, 111}},       {"relu", {1, 64, 111, 111}},
    {"max_pool", {1, 64, 55, 55}},     {"conv", {1, 16, 55, 55}},
    {"relu", {1, 16, 55, 55}},         {"conv", {1, 64, 55, 55}},
    {"relu", {1, 64, 55, 55}},         {"conv", {1, 64, 55, 55}},
    {"relu", {1, 64, 55, 55}},         {"concat", {1, 128, 55, 55}},
    {"conv", {1, 16, 55, 55}},         {"relu", {1, 16, 55, 55}},
    {"conv", {1, 64, 55, 55}},         {"relu", {1, 64, 55, 55}},
    {"conv", {1, 64, 55, 55}},         {"relu", {1, 64, 55, 55}},
    {"concat", {1, 128, 55, 55}},      {"max_pool", {1, 128, 27, 27}},
    {"conv", {1, 32, 27, 27}},         {"relu", {1, 32, 27, 27}},
    {"conv", {1, 128, 27, 27}},        {"relu", {1, 128, 27, 27}},
    {"conv", {1, 128, 27, 27}},        {"relu", {1, 128, 27, 27}},
    {"concat", {1, 256, 27, 27}},      {"conv", {1, 32, 27, 27}},
    {"relu", {1, 32, 27, 27}},         {"conv", {1, 128, 27, 27}},
    {"relu", {1, 128, 27, 27}},        {"conv", {1, 128, 27, 27}},
    {"relu", {1, 128, 27, 27}},        {"concat", {1, 256, 27, 27}},
    {"max_pool", {1, 256, 13, 13}},    {"conv", {1, 48, 13, 13}},
    {"relu", {1, 48, 13, 13}},         {"conv", {1, 192, 13, 13}},
    {"relu", {1, 192, 13, 13}},        {"conv", {1, 192, 13, 13}},
    {"relu", {1, 192, 13, 13}},        {"concat", {1, 384, 13, 13}},
    {"conv", {1, 48, 13, 13}},         {"relu", {1, 48, 13, 13}},
    {"conv", {1, 192, 13, 13}},        {"relu", {1, 192, 13, 13}},
    {"conv", {1, 192, 13, 13}},        {"relu", {1, 192, 13, 13}},
    {"concat", {1, 384, 13, 13}},      {"conv", {1, 64, 13, 13}},
    {"relu", {1, 64, 13, 13}},         {"conv", {1, 256, 13, 13}},
    {"relu", {1, 256, 13, 13}},        {"conv", {1, 256, 13, 13}},
    {"relu", {1, 256, 13, 13}},        {"concat", {1, 512, 13, 13}},
    {"conv", {1, 64, 13, 13}},         {"relu", {1, 64, 13, 13}},
    {"conv", {1, 256, 13, 13}},        {"relu", {1, 256, 13, 13}},
    {"conv", {1, 256, 13, 13}},        {"relu", {1, 256, 13, 13}},
    {"concat", {1, 512, 13, 13}},      {"dropout", {1, 512, 13, 13}},
    {"conv", {1, 1000, 13, 13}},       {"relu", {1, 1000, 13, 13}},
    {"average_pool", {1, 1000, 1, 1}}, {"reshape", {1, 1000}},
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 8 && argc != 9) {
    return EXIT_FAILURE;
  }

  joggle::Compiler malformed;
  if (!load(malformed, argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
            argv[7])) {
    malformed.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto rejected = malformed.run<joggle::Mod>(
      "onnx.read", joggle::Bytes{std::byte{0x7f}}, std::string{"bad"});
  joggle::Compiler malformed_again;
  if (!load(malformed_again, argv[1], argv[2], argv[3], argv[4], argv[5],
            argv[6], argv[7])) {
    malformed_again.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto rejected_again = malformed_again.run<joggle::Mod>(
      "onnx.read", joggle::Bytes{std::byte{0x7f}}, std::string{"bad"});
  const auto malformed_message = malformed.diag().issues().back().message;
  bool ok = expect(
      !rejected && !rejected_again &&
          malformed_message.find("valid ModelProto") != std::string::npos &&
          malformed_message == malformed_again.diag().issues().back().message,
      "malformed Protobuf input is rejected transactionally");

  joggle::Compiler structural;
  structural.load(argv[1]);
  structural.load(argv[2]);
  structural.load(argv[3]);
  structural.load(argv[4]);
  structural.add(R"(
joggle 1;
mod matmul_use@1.0.0 {
  import onnx@5 as o;
  import tensor@4 as t;

  fn main(
    lhs: t.tensor<f32, [2, 4]>,
    rhs: t.tensor<f32, [4, 3]>
  ) -> t.tensor<f32, [2, 3]> {
    return o.MatMul(lhs, rhs);
  }
}
)",
                 "matmul-use.joggle");
  const bool structural_linked = structural.link();
  const auto matmul_user = structural_linked
                               ? structural.materialize("matmul_use.main")
                               : std::optional<joggle::Fn>{};
  const auto matmul_user_ops =
      matmul_user ? matmul_user->ops() : std::vector<joggle::Op>{};
  const auto matmul = matmul_user_ops.size() == 1U
                          ? structural.materialize(matmul_user_ops.front())
                          : std::optional<joggle::Fn>{};
  const auto matmul_ops = matmul ? matmul->ops() : std::vector<joggle::Op>{};
  const auto output_body =
      matmul_ops.size() == 1U && matmul_ops.front().arguments().size() == 1U
          ? matmul_ops.front().arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  const auto output_ops =
      output_body ? output_body->ops() : std::vector<joggle::Op>{};
  const auto element = output_ops.size() == 1U
                           ? structural.materialize(output_ops.front())
                           : std::optional<joggle::Fn>{};
  const auto element_ops =
      element ? element->ops() : std::vector<joggle::Op>{};
  const auto product_body =
      element_ops.size() == 3U && element_ops[1].arguments().size() == 1U
          ? element_ops[1].arguments().front().inline_fn()
          : std::optional<joggle::Fn>{};
  const auto reduction_body =
      element_ops.size() == 3U && element_ops[2].arguments().size() == 3U
          ? element_ops[2].arguments()[2].inline_fn()
          : std::optional<joggle::Fn>{};
  if (!matmul) {
    structural.diag().print(std::cerr);
  }
  ok &= expect(
      matmul_user && matmul_user_ops.size() == 1U &&
          callee_name(matmul_user_ops.front()) == "MatMul" && matmul &&
          matmul_ops.size() == 1U &&
          callee_name(matmul_ops.front()) == "map" && output_body &&
          output_ops.size() == 1U &&
          callee_name(output_ops.front()) == "matmul_element" && element &&
          element_ops.size() == 3U &&
          callee_name(element_ops[0]) == "zero" &&
          callee_name(element_ops[1]) == "map" &&
          callee_name(element_ops[2]) == "reduce" && product_body &&
          product_body->ops().size() == 9U && reduction_body &&
          reduction_body->ops().size() == 1U &&
          callee_name(reduction_body->ops().front()) == "+" &&
          structural.verify(*matmul) && structural.verify(*output_body) &&
          structural.verify(*element) && structural.verify(*product_body) &&
          structural.verify(*reduction_body),
      "ONNX MatMul expands to a real map/reduce/index/arithmetic Fn body");

  if (argc == 8) {
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  const auto bytes = read_bytes(argv[8]);
  ok &= expect(!bytes.empty(), "reference model bytes are readable");

  joggle::Compiler compiler;
  if (!load(compiler, argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
            argv[7])) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto shape_probe = compiler.run<std::vector<std::int64_t>>(
      "onnx.conv_shape", std::vector<std::int64_t>{1, 3, 224, 224},
      std::vector<std::int64_t>{64, 3, 3, 3}, std::vector<std::int64_t>{3, 3},
      std::vector<std::int64_t>{2, 2}, std::vector<std::int64_t>{0, 0, 0, 0},
      std::vector<std::int64_t>{1, 1});
  ok &= expect(shape_probe == std::vector<std::int64_t>({1, 64, 111, 111}),
               "ONNX shape semantics execute as an ordinary source fn");
  const auto model =
      compiler.run<joggle::Mod>("onnx.read", bytes, std::string{"squeezenet"});
  if (!model) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto repeated =
      compiler.run<joggle::Mod>("onnx.read", bytes, std::string{"squeezenet"});
  ok &= expect(repeated && repeated->digest() == model->digest(),
               "the same bytes and name produce the same Mod identity");
  const auto main = model->fn("main");
  const auto body = main ? main->body() : nullptr;
  if (!body) {
    return EXIT_FAILURE;
  }

  const auto ops = body->ops();
  std::size_t constants = 0;
  std::size_t located = 0;
  for (const auto& op : ops) {
    if (op.callee().referenced_fn()->symbol().mod_name() == "onnx" &&
        op.callee().referenced_fn()->symbol().local_name() == "Constant") {
      ++constants;
      const auto digest = op.callee().binding<std::string>("content");
      ok &= expect(digest && model->data(*digest).has_value(),
                   "constant content names Mod-owned bytes");
    }
    located += static_cast<std::size_t>(op.location().has_value());
  }

  const auto arguments = body->arguments();
  const auto returned = body->entry().terminator().returned();
  ok &= expect(arguments.size() == 1U &&
                   arguments.front().type().get<std::vector<std::int64_t>>(
                       "shape") == std::vector<std::int64_t>({1, 3, 224, 224}),
               "reference model has one typed runtime input");
  ok &= expect(returned.size() == 1U &&
                   returned.front().type().get<std::vector<std::int64_t>>(
                       "shape") == std::vector<std::int64_t>({1, 1000}),
               "static propagation reaches the declared output shape");
  ok &= expect(ops.size() == 118U && constants == 52U &&
                   ops.size() - constants == 66U,
               "reference model has 52 constants and 66 semantic calls");
  ok &= expect(located == ops.size(),
               "every imported call retains deterministic provenance");
  ok &= expect(model->data().size() == 54U,
               "original model and all 53 initializers are retained");

  bool data_digests_match = true;
  bool source_digest_found = false;
  for (const auto& digest : model->data()) {
    const auto data = model->data(digest);
    if (!data) {
      data_digests_match = false;
      continue;
    }
    const std::string_view view{reinterpret_cast<const char*>(data->data()),
                                data->size()};
    data_digests_match &= digest == "sha256:" + joggle::sha256(view);
    source_digest_found |=
        data->size() == 4956208U &&
        joggle::sha256(view) ==
            "1eeff551a67ae8d565ca33b572fc4b66e3ef357b0eb2863bb9ff47a918cc4088";
  }
  ok &= expect(data_digests_match && source_digest_found,
               "every payload digest and the exact source-model digest match");

  bool sequence_matches = ops.size() == constants + expected_calls.size();
  std::size_t convs = 0;
  std::size_t relus = 0;
  std::size_t pools = 0;
  std::size_t concats = 0;
  for (std::size_t index = 0; sequence_matches && index < expected_calls.size();
       ++index) {
    const auto& op = ops[constants + index];
    const auto& expected = expected_calls[index];
    sequence_matches &=
        op.callee().referenced_fn()->symbol().local_name() ==
            (expected.first == "conv"           ? "Conv"
             : expected.first == "relu"         ? "Relu"
             : expected.first == "max_pool"     ? "MaxPool"
             : expected.first == "average_pool" ? "AveragePool"
             : expected.first == "concat"       ? "Concat"
             : expected.first == "reshape"      ? "Reshape"
             : expected.first == "dropout"      ? "Dropout"
                                                : expected.first) &&
        op.value().type().get<std::vector<std::int64_t>>("shape") ==
            expected.second &&
        op.location() && op.location()->source.starts_with("onnx:main/node/");
    if (expected.first == "conv") {
      ++convs;
      const auto strides =
          op.callee().binding<std::vector<std::int64_t>>("strides");
      const auto pads = op.callee().binding<std::vector<std::int64_t>>("pads");
      const auto dilations =
          op.callee().binding<std::vector<std::int64_t>>("dilations");
      sequence_matches &= strides && pads && dilations &&
                          (*strides == std::vector<std::int64_t>{1, 1} ||
                           *strides == std::vector<std::int64_t>{2, 2}) &&
                          (*pads == std::vector<std::int64_t>{0, 0, 0, 0} ||
                           *pads == std::vector<std::int64_t>{1, 1, 1, 1}) &&
                          *dilations == std::vector<std::int64_t>{1, 1} &&
                          op.callee().binding<std::int64_t>("group") == 1;
    } else if (expected.first == "relu") {
      ++relus;
    } else if (expected.first == "max_pool" ||
               expected.first == "average_pool") {
      ++pools;
      sequence_matches &=
          op.callee().binding<std::int64_t>("ceil_mode") == 0 &&
          op.callee().binding<std::vector<std::int64_t>>("pads") ==
              std::vector<std::int64_t>({0, 0, 0, 0});
    } else if (expected.first == "concat") {
      ++concats;
      sequence_matches &= op.callee().binding<std::int64_t>("axis") == 1;
    } else if (expected.first == "reshape") {
      sequence_matches &= op.callee().binding<std::vector<std::int64_t>>(
                              "shape") == std::vector<std::int64_t>({0, -1});
    }
  }
  ok &= expect(sequence_matches && convs == 26U && relus == 26U &&
                   pools == 4U && concats == 8U,
               "all 66 semantic calls, shapes, bindings, and locations "
               "match the independently audited graph");
  ok &= expect(compiler.verify(*model),
               "the imported Mod satisfies ordinary IR verification");

  const auto inlined = compiler.run<joggle::Mod>("transform.inline", *model);
  const auto inlined_main = inlined ? inlined->fn("main") : std::nullopt;
  const joggle::Fn* inlined_body =
      inlined_main ? inlined_main->body() : nullptr;
  if (!inlined_body) {
    compiler.diag().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto inlined_ops = inlined_body->ops();
  std::size_t maps = 0;
  std::size_t remaining_relu = 0;
  bool typed_callbacks = true;
  for (const joggle::Op& op : inlined_ops) {
    const auto declaration = op.callee().referenced_fn();
    if (!declaration || (declaration->symbol().mod_name() != "onnx" &&
                         declaration->symbol().mod_name() != "tensor")) {
      continue;
    }
    remaining_relu += static_cast<std::size_t>(declaration->name() == "Relu");
    if (declaration->name() != "map") {
      continue;
    }
    ++maps;
    const auto arguments = op.arguments();
    typed_callbacks &= arguments.size() == 2U &&
                       arguments[1].inline_fn().has_value() &&
                       arguments[1].captures().empty();
  }
  ok &= expect(inlined_ops.size() + 1U == ops.size() && maps == relus &&
                   remaining_relu == 0U && typed_callbacks &&
                   compiler.verify(*inlined),
               "generic Fn inlining expands every real-model Relu into its "
               "bodyful map form without an operator-name case");

  const auto exposed = compiler.run<joggle::Mod>("transform.inline", *inlined);
  const auto exposed_main = exposed ? exposed->fn("main") : std::nullopt;
  const auto* exposed_body = exposed_main ? exposed_main->body() : nullptr;
  std::size_t domain_maps = 0;
  bool captures_composition = true;
  if (exposed_body) {
    for (const auto& op : exposed_body->ops()) {
      const auto declaration = op.callee().referenced_fn();
      if (!declaration || declaration->symbol().mod_name() != "tensor" ||
          declaration->name() != "map") {
        continue;
      }
      ++domain_maps;
      const auto arguments = op.arguments();
      captures_composition &= arguments.size() == 1U &&
                              arguments.front().inline_fn().has_value() &&
                              arguments.front().captures().size() == 2U;
    }
  }
  ok &= expect(exposed_body && domain_maps == relus && captures_composition &&
                   compiler.verify(*exposed),
               "a second generic expansion exposes domain-map/subscript composition");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
