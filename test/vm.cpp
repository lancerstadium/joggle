#include "joggle/joggle.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
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

void append(joggle::Attr::Bytes& out, std::int64_t value) {
  const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
  for (unsigned shift = 0; shift != 64; shift += 8)
    out.push_back(static_cast<std::uint8_t>(bits >> shift));
}

std::vector<std::int64_t> integers(const joggle::Attr::Bytes& bytes) {
  std::vector<std::int64_t> out;
  if (bytes.size() % 8 != 0)
    return out;
  out.reserve(bytes.size() / 8);
  for (std::size_t offset = 0; offset < bytes.size(); offset += 8) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift != 64; shift += 8)
      value |= std::uint64_t{bytes[offset + shift / 8]} << shift;
    out.push_back(std::bit_cast<std::int64_t>(value));
  }
  return out;
}

bool execute(joggle::Env& env, std::string image, std::string entry,
             std::vector<std::int64_t> inputs, joggle::Attr::Bytes& result,
             std::int64_t& steps) {
  joggle::Attr::Bytes bytes;
  for (const std::int64_t input : inputs)
    append(bytes, input);
  const std::vector<joggle::Attr> args{joggle::Attr(std::move(image)),
                                       joggle::Attr(std::move(entry)),
                                       joggle::Attr(std::move(bytes))};
  std::vector<joggle::Attr> returns;
  if (!env.call("vm.run", args, returns) || returns.size() != 2 ||
      !returns[0].bytes() || !returns[1].integer())
    return false;
  result = *returns[0].bytes();
  steps = *returns[1].integer();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 4);
  std::ifstream input(argv[1]);
  CHECK(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[3]);
  CHECK(env.load("vm"));
  joggle::Mod model;
  CHECK(joggle::parse(env, source.str(), model, argv[1]));
  CHECK(model.verify(env));

  joggle::Attr image;
  CHECK(joggle::query(env, "vm.image", model, image));
  CHECK(image.string());
  CHECK(image.string()->starts_with("joggle-vm 1\nfn main\n"));
  joggle::Attr repeated_image;
  CHECK(joggle::query(env, "vm.image", model, repeated_image));
  CHECK(repeated_image == image);

  joggle::Attr::Bytes result;
  std::int64_t then_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 1}, result,
                then_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{30} && then_steps > 0);
  std::int64_t again_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 1}, result,
                again_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{30} &&
        again_steps == then_steps);

  std::int64_t else_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 0}, result,
                else_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{14} &&
        else_steps == then_steps);

  std::int64_t shift_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "shift", {-8, 2}, result,
                shift_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{-2} && shift_steps > 0);

  CHECK(!execute(env, std::string(*image.string()), "shift", {-8, 64},
                 result, shift_steps));
  CHECK(!execute(env, std::string(*image.string()), "divide", {1, 0}, result,
                 shift_steps));
  CHECK(!execute(env, "not-a-vm", "main", {10, 5, 1}, result, else_steps));
  CHECK(!env.diags().empty());

  std::ifstream tensor_input(argv[2]);
  CHECK(tensor_input);
  std::ostringstream tensor_source;
  tensor_source << tensor_input.rdbuf();
  joggle::Mod tensor_model;
  CHECK(joggle::parse(env, tensor_source.str(), tensor_model, argv[2]));
  CHECK(tensor_model.verify(env));
  const std::vector<joggle::Attr> selection{joggle::Attr("int_add")};
  joggle::Attr tensor_image;
  if (!joggle::query(env, "vm.image", tensor_model, tensor_image, selection)) {
    env.print_diags(stderr);
    return 1;
  }
  CHECK(tensor_image.string());
  CHECK(tensor_image.string()->starts_with("joggle-vm 1\nfn int_add\n"));
  std::int64_t tensor_steps = 0;
  CHECK(execute(env, std::string(*tensor_image.string()), "int_add",
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, result,
                tensor_steps));
  CHECK(integers(result) ==
        (std::vector<std::int64_t>{8, 10, 12, 14, 16, 18}));
  CHECK(tensor_steps > then_steps);

  const std::vector<joggle::Attr> matmul_selection{
      joggle::Attr("int_matmul")};
  joggle::Attr matmul_image;
  CHECK(joggle::query(env, "vm.image", tensor_model, matmul_image,
                      matmul_selection));
  CHECK(matmul_image.string());
  std::int64_t matmul_steps = 0;
  CHECK(execute(env, std::string(*matmul_image.string()), "int_matmul",
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, result,
                matmul_steps));
  CHECK(integers(result) ==
        (std::vector<std::int64_t>{58, 64, 139, 154}));
  CHECK(matmul_steps > tensor_steps);
  std::int64_t repeated_matmul_steps = 0;
  CHECK(execute(env, std::string(*matmul_image.string()), "int_matmul",
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, result,
                repeated_matmul_steps));
  CHECK(integers(result) ==
        (std::vector<std::int64_t>{58, 64, 139, 154}));
  CHECK(repeated_matmul_steps == matmul_steps);
  return 0;
}
