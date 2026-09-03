#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <joggle/joggle.h>

#include "onnx.proto3.pb.h"

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

void tensor_value(::onnx::ValueInfoProto* value, std::string name,
                  std::initializer_list<std::int64_t> shape) {
  value->set_name(std::move(name));
  auto* tensor = value->mutable_type()->mutable_tensor_type();
  tensor->set_elem_type(::onnx::TensorProto::FLOAT);
  for (const std::int64_t dimension : shape) {
    tensor->mutable_shape()->add_dim()->set_dim_value(dimension);
  }
}

std::string floats(std::initializer_list<std::uint32_t> bits) {
  std::string result;
  result.reserve(bits.size() * 4U);
  for (const std::uint32_t value : bits) {
    for (std::uint32_t shift = 0; shift < 32U; shift += 8U) {
      result.push_back(
          static_cast<char>(static_cast<unsigned char>(value >> shift)));
    }
  }
  return result;
}

::onnx::ModelProto model(std::string_view final_operator = "Relu") {
  ::onnx::ModelProto result;
  result.set_ir_version(8);
  result.add_opset_import()->set_version(18);
  auto* graph = result.mutable_graph();
  graph->set_name("tiny_add");
  tensor_value(graph->add_input(), "input", {1, 2});
  tensor_value(graph->add_output(), "output", {1, 2});

  auto* weight = graph->add_initializer();
  weight->set_name("weight");
  weight->set_data_type(::onnx::TensorProto::FLOAT);
  weight->add_dims(1);
  weight->add_dims(2);
  weight->set_raw_data(floats({0x3f800000U, 0x40000000U}));

  auto* add = graph->add_node();
  add->set_op_type("Add");
  add->add_input("input");
  add->add_input("weight");
  add->add_output("sum");

  auto* final = graph->add_node();
  final->set_op_type(std::string(final_operator));
  final->add_input("sum");
  final->add_output("output");
  return result;
}

joggle::Bytes byte_string(std::string_view serialized) {
  joggle::Bytes result;
  result.reserve(serialized.size());
  for (const char value : serialized) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

joggle::Bytes encode(const ::onnx::ModelProto& model) {
  return byte_string(model.SerializeAsString());
}

std::optional<joggle::Bytes> read_file(const char* path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::string data((std::istreambuf_iterator<char>(input)),
                   std::istreambuf_iterator<char>());
  return byte_string(data);
}

}  // namespace

int main(int argc, char** argv) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_ONNX_MODULE);
  compiler.add(R"(
joggle 1;
module onnx_composition@1.0.0 {
  import onnx@2.0.0;

  fn read(input: bytes) -> module {
    return onnx.read(input);
  }

  fn read_nn(input: bytes) -> module {
    return onnx.to_nn(onnx.read(input));
  }
}
)",
               "onnx-composition.joggle");
  if (!compiler.link() ||
      !compiler.load_behavior("onnx", JOGGLE_ONNX_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  using Imported = joggle::Module;
  if (argc == 2) {
    const auto source = read_file(argv[1]);
    ::onnx::ModelProto reference;
    const bool reference_valid =
        source && source->size() <=
                      static_cast<std::size_t>(
                          std::numeric_limits<int>::max()) &&
        reference.ParseFromArray(source->data(),
                                 static_cast<int>(source->size()));
    const auto imported = source
                              ? compiler.run<Imported>("onnx_composition.read",
                                                       *source)
                              : std::optional<Imported>{};
    const auto repeated = source
                              ? compiler.run<Imported>("onnx_composition.read",
                                                       *source)
                              : std::optional<Imported>{};
    const auto converted =
        source ? compiler.run<Imported>("onnx_composition.read_nn", *source)
               : std::optional<Imported>{};
    if (!reference_valid || !imported || !converted || !repeated) {
      if (!source) {
        std::cerr << "cannot read ONNX model '" << argv[1] << "'\n";
      }
      compiler.diagnostics().print(std::cerr);
      return EXIT_FAILURE;
    }
    const auto& source_module = *imported;
    const auto& module = *converted;
    const auto source_main = source_module.function("main");
    const joggle::Function* source_body =
        source_main ? source_main->body() : nullptr;
    const auto main = module.function("main");
    const joggle::Function* body = main ? main->body() : nullptr;
    std::map<std::string, std::size_t, std::less<>> counts;
    if (body) {
      for (const auto& op : body->ops()) {
        ++counts[op.callee().symbol().qualified_name()];
      }
    }
    const std::map<std::string, std::size_t, std::less<>> expected{
        {"nn.add", 8U},
        {"nn.conv2d_nchw", 20U},
        {"nn.flatten_nchw", 1U},
        {"nn.global_average_pool_nchw", 1U},
        {"nn.linear", 1U},
        {"nn.max_pool2d_nchw", 1U},
        {"nn.relu", 17U},
    };
    std::map<std::string, std::size_t, std::less<>> source_counts;
    if (source_body) {
      for (const auto& op : source_body->ops()) {
        ++source_counts[op.callee().symbol().qualified_name()];
      }
    }
    const std::map<std::string, std::size_t, std::less<>> source_expected{
        {"onnx.add", 8U},
        {"onnx.conv", 20U},
        {"onnx.flatten", 1U},
        {"onnx.gemm", 1U},
        {"onnx.global_average_pool", 1U},
        {"onnx.max_pool", 1U},
        {"onnx.relu", 17U},
    };
    bool valid = source_body != nullptr && body != nullptr;
    std::set<std::string, std::less<>> referenced_resources;
    if (body) {
      for (const auto& op : body->ops()) {
        if (op.callee().symbol().qualified_name() ==
            "tensor.constant") {
          const auto resource = op.property<std::string>("resource");
          valid = valid && resource && module.data(*resource);
          if (resource) {
            referenced_resources.insert(*resource);
          }
        }
      }
    }
    for (const auto& [name, count] : expected) {
      valid = valid && counts[name] == count;
      counts.erase(name);
    }
    for (const auto& [name, count] : source_expected) {
      valid = valid && source_counts[name] == count;
      source_counts.erase(name);
    }
    const auto constants = counts.extract("tensor.constant");
    const auto source_constants = source_counts.extract("onnx.constant");
    valid = valid && !constants.empty() && counts.empty() &&
            constants.mapped() ==
                static_cast<std::size_t>(reference.graph().initializer_size()) &&
            !source_constants.empty() && source_counts.empty() &&
            source_constants.mapped() == constants.mapped() &&
            referenced_resources.size() == module.data().size() &&
            !module.data().empty() &&
            source_module.digest() == repeated->digest() &&
            source_module.data() == module.data();
    std::size_t resource_bytes = 0;
    for (const auto& name : module.data()) {
      const auto payload = module.data(name);
      resource_bytes += payload ? payload->size() : 0U;
    }
    std::cout << "module " << module.name() << '#' << module.digest() << '\n'
              << "operators 49\n"
              << "resources " << module.data().size() << '\n'
              << "resource-bytes " << resource_bytes << '\n';
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (argc != 1) {
    std::cerr << "usage: joggle_module_onnx_test [model.onnx]\n";
    return EXIT_FAILURE;
  }
  const joggle::Bytes source = encode(model());
  const auto first =
      compiler.run<Imported>("onnx_composition.read", source);
  const auto second =
      compiler.run<Imported>("onnx_composition.read", source);
  if (!first || !second) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto& source_module = *first;
  const auto& repeated_module = *second;
  const auto converted = compiler.run<Imported>("onnx.to_nn", source_module);
  if (!converted) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto& module = *converted;
  const auto source_main = source_module.function("main");
  const joggle::Function* source_body =
      source_main ? source_main->body() : nullptr;
  const auto source_ops = source_body ? source_body->ops()
                                      : std::vector<joggle::Op>{};
  const auto main = module.function("main");
  const joggle::Function* body = main ? main->body() : nullptr;
  const auto ops = body ? body->ops()
                                 : std::vector<joggle::Op>{};
  const auto data_names = module.data();
  const std::string resource = data_names.empty() ? std::string{}
                                                   : data_names.front();
  const auto found = module.data(resource);

  bool ok = true;
  ok &= expect(
      source_body && source_body->arguments().size() == 1U &&
          source_ops.size() == 3U &&
          source_ops[0].callee().symbol().qualified_name() ==
              "onnx.constant" &&
          source_ops[1].callee().symbol().qualified_name() == "onnx.add" &&
          source_ops[2].callee().symbol().qualified_name() == "onnx.relu",
      "ONNX import preserves source operations before conversion");
  ok &= expect(
      body && body->arguments().size() == 1U && ops.size() == 3U &&
          ops[0].callee().symbol().qualified_name() ==
              "tensor.constant" &&
          ops[1].callee().symbol().qualified_name() == "nn.add" &&
          ops[2].callee().symbol().qualified_name() == "nn.relu",
      "onnx.to_nn transactionally converts source operations to nn");
  ok &= expect(data_names.size() == 1U && resource.starts_with("sha256:") &&
                   resource.size() == 71U && found && found->size() == 8U,
               "initializer bytes are Module-owned behind a content digest");
  ok &= expect(source_module.digest() == repeated_module.digest() &&
                   joggle::format(source_module) ==
                       joggle::format(repeated_module) &&
                   source_module.data() == module.data(),
               "repeated ONNX import is deterministic");

  auto changed_model = model();
  changed_model.mutable_graph()
      ->mutable_initializer(0)
      ->set_raw_data(floats({0x40400000U, 0x40000000U}));
  const auto changed =
      compiler.run<Imported>("onnx_composition.read", encode(changed_model));
  ok &= expect(changed && changed->digest() != source_module.digest() &&
                   changed->data() != source_module.data(),
               "changing initializer bytes changes Module data and identity");

  const auto rejected =
      compiler.run<Imported>("onnx_composition.read", encode(model("Sigmoid")));
  const bool precise_rejection = std::any_of(
      compiler.diagnostics().entries().begin(),
      compiler.diagnostics().entries().end(),
      [](const joggle::Diagnostic& diagnostic) {
        return diagnostic.message.find(
                   "node 1 uses unsupported operator 'Sigmoid'") !=
               std::string::npos;
      });
  ok &= expect(!rejected && precise_rejection,
               "an unsupported ONNX operator is rejected with node context");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
