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
#include <tuple>

#include <joggle/joggle.h>
#include <joggle/onnx/onnx.h>

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
  if (!compiler.link() ||
      !compiler.load_behavior("onnx", JOGGLE_ONNX_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  using Imported = std::tuple<joggle::Module, joggle::onnx::Resources>;
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
                              ? compiler.run<Imported>("onnx.read", *source)
                              : std::optional<Imported>{};
    const auto repeated = source
                              ? compiler.run<Imported>("onnx.read", *source)
                              : std::optional<Imported>{};
    if (!reference_valid || !imported || !repeated) {
      if (!source) {
        std::cerr << "cannot read ONNX model '" << argv[1] << "'\n";
      }
      compiler.diagnostics().print(std::cerr);
      return EXIT_FAILURE;
    }
    const auto& [module, resources] = *imported;
    const auto main = module.function("main");
    const joggle::Function* body = main ? main->body() : nullptr;
    std::map<std::string, std::size_t, std::less<>> counts;
    if (body) {
      for (const auto& instruction : body->instructions()) {
        ++counts[instruction.callee().symbol().qualified_name()];
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
    bool valid = body != nullptr;
    std::set<std::string, std::less<>> referenced_resources;
    if (body) {
      for (const auto& instruction : body->instructions()) {
        if (instruction.callee().symbol().qualified_name() ==
            "tensor.constant") {
          const auto resource = instruction.get<std::string>("resource");
          valid = valid && resource && resources.contains(*resource);
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
    const auto constants = counts.extract("tensor.constant");
    valid = valid && !constants.empty() && counts.empty() &&
            constants.mapped() ==
                static_cast<std::size_t>(reference.graph().initializer_size()) &&
            referenced_resources.size() == resources.size() &&
            !resources.empty() &&
            module.digest() == std::get<0>(*repeated).digest() &&
            resources == std::get<1>(*repeated);
    std::size_t resource_bytes = 0;
    for (const auto& [name, payload] : resources) {
      static_cast<void>(name);
      resource_bytes += payload.size();
    }
    std::cout << "module " << module.name() << '#' << module.digest() << '\n'
              << "operators 49\n"
              << "resources " << resources.size() << '\n'
              << "resource-bytes " << resource_bytes << '\n';
    return valid ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  if (argc != 1) {
    std::cerr << "usage: joggle_onnx_test [model.onnx]\n";
    return EXIT_FAILURE;
  }
  const joggle::Bytes source = encode(model());
  const auto first = compiler.run<Imported>("onnx.read", source);
  const auto second = compiler.run<Imported>("onnx.read", source);
  if (!first || !second) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto& [module, resources] = *first;
  const auto& [repeated_module, repeated_resources] = *second;
  const auto main = module.function("main");
  const joggle::Function* body = main ? main->body() : nullptr;
  const auto instructions = body ? body->instructions()
                                 : std::vector<joggle::Instruction>{};
  const std::string resource = resources.empty() ? std::string{}
                                                  : resources.begin()->first;
  const auto payload = resource.empty()
                           ? std::optional<joggle::Bytes>{}
                           : compiler.run<joggle::Bytes>(
                                 "onnx.lookup", resources, resource);

  bool ok = true;
  ok &= expect(
      body && body->arguments().size() == 1U && instructions.size() == 3U &&
          instructions[0].callee().symbol().qualified_name() ==
              "tensor.constant" &&
          instructions[1].callee().symbol().qualified_name() == "nn.add" &&
          instructions[2].callee().symbol().qualified_name() == "nn.relu",
      "ONNX dataflow becomes ordinary typed Joggle instructions");
  ok &= expect(resources.size() == 1U && resource.starts_with("sha256:") &&
                   resource.size() == 71U && payload && payload->size() == 8U,
               "initializer bytes are detached behind a content digest");
  ok &= expect(module.digest() == repeated_module.digest() &&
                   resources == repeated_resources &&
                   joggle::format(module) == joggle::format(repeated_module),
               "repeated ONNX import is deterministic");

  auto changed_model = model();
  changed_model.mutable_graph()
      ->mutable_initializer(0)
      ->set_raw_data(floats({0x40400000U, 0x40000000U}));
  const auto changed =
      compiler.run<Imported>("onnx.read", encode(changed_model));
  ok &= expect(changed && std::get<0>(*changed).digest() != module.digest() &&
                   std::get<1>(*changed) != resources,
               "changing initializer bytes changes resource and Module "
               "identity");

  const auto rejected =
      compiler.run<Imported>("onnx.read", encode(model("Sigmoid")));
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
