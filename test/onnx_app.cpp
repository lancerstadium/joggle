#include "joggle/joggle.h"
#include "onnx.pb.h"

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
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

bool write(std::string_view path, std::string_view contents) {
  std::ofstream output(std::string(path), std::ios::binary | std::ios::trunc);
  return output && output.write(contents.data(),
                                static_cast<std::streamsize>(contents.size()));
}

bool write(std::string_view path, const Bytes& contents) {
  return write(path, std::string_view(
                         reinterpret_cast<const char*>(contents.data()),
                         contents.size()));
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
    if (!std::isfinite(value) ||
        std::fabs(value - reference) >
            1.0e-4F + 1.0e-4F * std::fabs(reference))
      return false;
  }
  return true;
}

bool tensor(std::string_view path, std::string_view name,
            const std::vector<std::int64_t>& shape, Bytes& data) {
  const Bytes encoded = read(path);
  if (encoded.empty() ||
      encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return false;
  jogonnx::TensorProto value;
  if (!value.ParseFromArray(encoded.data(), static_cast<int>(encoded.size())) ||
      value.data_type() != 1 || value.name() != name ||
      value.dims_size() != static_cast<int>(shape.size()) ||
      !value.has_raw_data())
    return false;
  std::size_t count = 1;
  for (std::size_t axis = 0; axis < shape.size(); ++axis) {
    const std::int64_t extent = shape[axis];
    if (value.dims(static_cast<int>(axis)) != extent || extent < 0 ||
        static_cast<std::uint64_t>(extent) >
            std::numeric_limits<std::size_t>::max() / count)
      return false;
    count *= static_cast<std::size_t>(extent);
  }
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(float) ||
      value.raw_data().size() != count * sizeof(float))
    return false;
  data.assign(value.raw_data().begin(), value.raw_data().end());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 10);
  const Bytes encoded = read(argv[1]);
  CHECK(!encoded.empty());
  Bytes input;
  Bytes expected;
  CHECK(tensor(argv[2], "data", {1, 3, 224, 224}, input));
  CHECK(tensor(argv[3], "mobilenetv20_output_flatten0_reshape0", {1, 1000},
               expected));

  joggle::Env env;
  env.path(argv[7]);
  CHECK(env.load("onnx"));
  const std::vector<joggle::Attr> read_args{joggle::Attr(encoded)};
  std::vector<joggle::Attr> read_result;
  CHECK(env.call("onnx.read", read_args, read_result));
  CHECK(read_result.size() == 1 && read_result.front().string());

  CHECK(env.load("onnx.nn"));
  CHECK(env.load("opt"));
  CHECK(env.load("vm"));
  CHECK(env.load("c"));
  CHECK(env.load("mem"));
  joggle::Mod model;
  CHECK(joggle::parse(env, *read_result.front().string(), model, argv[1]));
  CHECK(model.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", model));
  const std::vector<joggle::Attr> dce_args{
      joggle::Attr(joggle::Attr::List{})};
  CHECK(joggle::run(env, "opt.dce", model, dce_args));
  CHECK(model.verify(env));
  CHECK(joggle::run(env, "vm.prepare", model));
  CHECK(model.verify(env));
  CHECK(write(argv[8], joggle::print(model)));

  joggle::Attr image;
  if (!joggle::query(env, "vm.image", model, image)) {
    env.print_diags(stderr);
    return 1;
  }
  CHECK(image.string() && write(argv[9], *image.string()));
  const std::vector<joggle::Attr> vm_args{
      joggle::Attr(std::string(*image.string())), joggle::Attr("main"),
      joggle::Attr(input)};
  std::vector<joggle::Attr> vm_result;
  const auto started = std::chrono::steady_clock::now();
  CHECK(env.call("vm.run", vm_args, vm_result));
  const std::chrono::duration<double> elapsed =
      std::chrono::steady_clock::now() - started;
  CHECK(vm_result.size() == 2 && vm_result[0].bytes() &&
        close(*vm_result[0].bytes(), expected) && vm_result[1].integer() &&
        *vm_result[1].integer() > 0);
  std::printf("VM: %lld steps in %.3f seconds\n",
              static_cast<long long>(*vm_result[1].integer()),
              elapsed.count());

  CHECK(joggle::run(env, "c.prepare", model));
  CHECK(model.verify(env));
  CHECK(joggle::run(env, "mem.plan", model));
  const std::vector<joggle::Attr> placement{joggle::Attr("static")};
  CHECK(joggle::run(env, "c.place", model, placement));
  CHECK(model.verify(env));
  joggle::Attr source;
  CHECK(joggle::query(env, "c.source", model, source));
  CHECK(source.string() && source.string()->find("onnx.") == std::string::npos);
  CHECK(source.string()->find("static float jog_mem_f32_") !=
        std::string::npos);
  CHECK(write(argv[4], *source.string()));
  CHECK(write(argv[5], input));
  CHECK(write(argv[6], expected));
  return 0;
}
