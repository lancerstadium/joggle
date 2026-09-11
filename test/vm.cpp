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

std::int64_t integer(const joggle::Attr::Bytes& bytes) {
  if (bytes.size() != 8)
    return 0;
  std::uint64_t value = 0;
  for (unsigned shift = 0; shift != 64; shift += 8)
    value |= std::uint64_t{bytes[shift / 8]} << shift;
  return std::bit_cast<std::int64_t>(value);
}

bool execute(joggle::Env& env, std::string image, std::string entry,
             std::vector<std::int64_t> inputs, std::int64_t& result,
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
  result = integer(*returns[0].bytes());
  steps = *returns[1].integer();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 3);
  std::ifstream input(argv[1]);
  CHECK(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[2]);
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

  std::int64_t result = 0;
  std::int64_t then_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 1}, result,
                then_steps));
  CHECK(result == 30 && then_steps > 0);
  std::int64_t again_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 1}, result,
                again_steps));
  CHECK(result == 30 && again_steps == then_steps);

  std::int64_t else_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 0}, result,
                else_steps));
  CHECK(result == 14 && else_steps == then_steps);

  std::int64_t shift_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "shift", {-8, 2}, result,
                shift_steps));
  CHECK(result == -2 && shift_steps > 0);

  CHECK(!execute(env, std::string(*image.string()), "shift", {-8, 64},
                 result, shift_steps));
  CHECK(!execute(env, std::string(*image.string()), "divide", {1, 0}, result,
                 shift_steps));
  CHECK(!execute(env, "not-a-vm", "main", {10, 5, 1}, result, else_steps));
  CHECK(!env.diags().empty());
  return 0;
}
