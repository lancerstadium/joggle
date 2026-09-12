#include "joggle/joggle.h"
#include "onnx.pb.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

using Bytes = joggle::Attr::Bytes;

Bytes read(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input)
    return {};
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

bool tensor(std::string_view path, std::string_view name,
            std::initializer_list<std::int64_t> shape, Bytes& data) {
  const Bytes encoded = read(path);
  if (encoded.empty() ||
      encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return false;
  auto value = std::make_unique<jogonnx::TensorProto>();
  if (!value->ParseFromArray(encoded.data(), static_cast<int>(encoded.size())) ||
      value->data_type() != 1 || value->name() != name ||
      value->dims_size() != static_cast<int>(shape.size()) ||
      !value->has_raw_data())
    return false;
  std::size_t count = 1;
  std::size_t axis = 0;
  for (const std::int64_t extent : shape) {
    if (value->dims(static_cast<int>(axis++)) != extent || extent < 0 ||
        static_cast<std::uint64_t>(extent) >
            std::numeric_limits<std::size_t>::max() / count)
      return false;
    count *= static_cast<std::size_t>(extent);
  }
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(float) ||
      value->raw_data().size() != count * sizeof(float))
    return false;
  data.assign(value->raw_data().begin(), value->raw_data().end());
  return true;
}

bool write(std::string_view path, std::string_view contents) {
  std::ofstream output(std::string(path), std::ios::binary | std::ios::trunc);
  return output && output.write(contents.data(),
                                static_cast<std::streamsize>(contents.size()));
}

bool close(const Bytes& actual, const Bytes& expected) {
  if (actual.size() != expected.size() || actual.size() % sizeof(float) != 0)
    return false;
  for (std::size_t offset = 0; offset < actual.size(); offset += sizeof(float)) {
    std::uint32_t actual_bits = 0;
    std::uint32_t expected_bits = 0;
    for (unsigned shift = 0; shift != 32; shift += 8) {
      actual_bits |= std::uint32_t{actual[offset + shift / 8]} << shift;
      expected_bits |= std::uint32_t{expected[offset + shift / 8]} << shift;
    }
    const float value = std::bit_cast<float>(actual_bits);
    const float reference = std::bit_cast<float>(expected_bits);
    if (std::fabs(value - reference) >
        1.0e-5F + 1.0e-5F * std::fabs(reference))
      return false;
  }
  return true;
}

std::string array(std::string_view name, const Bytes& data) {
  std::ostringstream out;
  out << "static const unsigned char " << name << '[' << data.size()
      << "] = {";
  for (std::size_t index = 0; index < data.size(); ++index) {
    if (index)
      out << ',';
    out << static_cast<unsigned>(data[index]);
  }
  return out.str() + "};\n";
}

std::string harness(const Bytes& left, const Bytes& right,
                    const Bytes& expected) {
  std::string out =
      "#include <math.h>\n"
      "#include <stddef.h>\n"
      "#include <string.h>\n"
      "void model_main(const float*, const float*, float*);\n";
  out += array("left_bytes", left);
  out += array("right_bytes", right);
  out += array("expected_bytes", expected);
  out +=
      "int main(void) {\n"
      "  float left[12], right[12], expected[9], result[9];\n"
      "  memcpy(left, left_bytes, sizeof left);\n"
      "  memcpy(right, right_bytes, sizeof right);\n"
      "  memcpy(expected, expected_bytes, sizeof expected);\n"
      "  model_main(left, right, result);\n"
      "  for (size_t i = 0; i < 9; ++i)\n"
      "    if (fabsf(result[i] - expected[i]) >\n"
      "        1.0e-5f + 1.0e-5f * fabsf(expected[i]))\n"
      "      return 1;\n"
      "  return 0;\n"
      "}\n";
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 9);
  const Bytes model_data = read(argv[1]);
  CHECK(!model_data.empty());
  Bytes left;
  Bytes right;
  Bytes expected;
  CHECK(tensor(argv[2], "a", {3, 4}, left));
  CHECK(tensor(argv[3], "b", {4, 3}, right));
  CHECK(tensor(argv[4], "c", {3, 3}, expected));

  joggle::Env env;
  env.path(argv[7]);
  env.path(argv[8]);
  CHECK(env.load("onnx"));
  const std::vector<joggle::Attr> read_args{joggle::Attr(model_data)};
  std::vector<joggle::Attr> read_result;
  CHECK(env.call("onnx.read", read_args, read_result));
  CHECK(read_result.size() == 1 && read_result.front().string());

  CHECK(env.load("onnx.nn"));
  CHECK(env.load("opt"));
  CHECK(env.load("vm"));
  CHECK(env.load("c"));
  CHECK(env.load("ikj"));
  joggle::Mod model;
  CHECK(joggle::parse(env, *read_result.front().string(), model, argv[1]));
  CHECK(model.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", model));
  const std::vector<joggle::Attr> dce_args{
      joggle::Attr(joggle::Attr::List{})};
  CHECK(joggle::run(env, "opt.dce", model, dce_args));
  CHECK(joggle::run(env, "ikj.apply", model));
  CHECK(joggle::run(env, "vm.prepare", model));
  CHECK(joggle::run(env, "c.prepare", model));
  CHECK(model.verify(env));
  CHECK(joggle::print(model).find("for i in 0..3, k in 0..4, j in 0..3") !=
        std::string::npos);
  for (joggle::Op op : model.ops()) {
    CHECK(!op.callee().starts_with("onnx."));
    CHECK(!op.meta("onnx"));
  }

  const std::vector<joggle::Attr> selection{joggle::Attr("main")};
  joggle::Attr image;
  if (!joggle::query(env, "vm.image", model, image, selection)) {
    env.print_diags(stderr);
    return 1;
  }
  CHECK(image.string());
  Bytes input = left;
  input.insert(input.end(), right.begin(), right.end());
  const std::vector<joggle::Attr> vm_args{
      joggle::Attr(std::string(*image.string())), joggle::Attr("main"),
      joggle::Attr(input)};
  std::vector<joggle::Attr> vm_result;
  CHECK(env.call("vm.run", vm_args, vm_result));
  CHECK(vm_result.size() == 2 && vm_result[0].bytes() &&
        close(*vm_result[0].bytes(), expected) && vm_result[1].integer() &&
        *vm_result[1].integer() > 0);
  std::vector<joggle::Attr> repeated;
  CHECK(env.call("vm.run", vm_args, repeated));
  CHECK(repeated == vm_result);

  joggle::Attr c_source;
  if (!joggle::query(env, "c.source", model, c_source)) {
    env.print_diags(stderr);
    return 1;
  }
  CHECK(c_source.string() && c_source.string()->find("onnx") == std::string::npos);
  CHECK(write(argv[5], *c_source.string()));
  CHECK(write(argv[6], harness(left, right, expected)));
  return 0;
}
