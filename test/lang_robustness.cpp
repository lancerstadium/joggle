#include "joggle/joggle.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

class Random {
public:
  explicit Random(std::uint64_t state) : state_(state) {}

  std::uint64_t next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 0x2545f4914f6cdd1dULL;
  }

  std::size_t index(std::size_t bound) {
    return bound == 0 ? 0 : static_cast<std::size_t>(next() % bound);
  }

private:
  std::uint64_t state_;
};

std::string mutate(std::string source, Random& random) {
  const std::size_t changes = 1 + random.index(8);
  for (std::size_t change = 0; change < changes; ++change) {
    switch (random.index(5)) {
    case 0:
      if (!source.empty())
        source[random.index(source.size())] =
            static_cast<char>(random.next() & 0xff);
      break;
    case 1:
      if (source.size() < 4096)
        source.insert(source.begin() + random.index(source.size() + 1),
                      static_cast<char>(random.next() & 0xff));
      break;
    case 2:
      if (!source.empty())
        source.erase(source.begin() + random.index(source.size()));
      break;
    case 3:
      if (!source.empty())
        source.resize(random.index(source.size() + 1));
      break;
    default:
      if (!source.empty())
        source[random.index(source.size())] ^=
            static_cast<char>(1U << random.index(8));
      break;
    }
  }
  return source;
}

bool stable_module(joggle::Env& env, std::string_view source) {
  joggle::Mod first;
  if (!joggle::parse(env, source, first, "mutated.jog"))
    return true;
  const std::string canonical = joggle::print(first);
  joggle::Mod second;
  if (!joggle::parse(env, canonical, second, "roundtrip.jog") ||
      !joggle::structurally_equal(first, second) ||
      joggle::print(second) != canonical) {
    std::fprintf(stderr, "accepted source (%zu bytes):\n%.*s\ncanonical:\n%s\n",
                 source.size(), static_cast<int>(source.size()), source.data(),
                 canonical.c_str());
    return false;
  }
  return true;
}

bool stable_attr(joggle::Env& env, std::string_view source) {
  joggle::Attr first;
  if (!joggle::parse(env, source, first, "mutated.attr"))
    return true;
  const std::string canonical = joggle::print(first);
  joggle::Attr second;
  return joggle::parse(env, canonical, second, "roundtrip.attr") &&
         joggle::print(second) == canonical;
}

bool stable_type(std::string_view source) {
  const joggle::Ty first{std::string(source)};
  if (!first.valid())
    return true;
  const joggle::Ty second{std::string(first.text())};
  return second.valid() && second == first && second.text() == first.text();
}

}  // namespace

int main() {
  constexpr std::array<std::string_view, 5> modules{
      "mod empty\n",
      "mod scalar\nfn add(a: i32, b: i32) -> i32 { return a + b }\n",
      "mod loop\nfn sum(x: tensor<i32, [4]>) -> i32 {\n"
      "  var y: i32 = 0\n  for i in 0..4 { y += x[i] }\n  return y\n}\n",
      "mod branch\nfn choose(x: i32, flag: bool) -> i32 {\n"
      "  var y = x\n  if flag { y += 1 } else { y -= 1 }\n  return y\n}\n",
      "mod generic\nfn id<T: Ty>(x: T) -> T { return x }\n",
  };
  constexpr std::array<std::string_view, 6> attributes{
      "null",
      "true",
      "-9223372036854775808",
      "3.141592653589793",
      R"([1, "two", false, [3]])",
      R"({"name": "value", "rank": 4})",
  };
  constexpr std::array<std::string_view, 8> types{
      "i32",
      "index",
      "tensor<f32, [1, 3, 224, 224]>",
      "sat<7>",
      "packed<u4, [16, 32]>",
      "layout<tensor<i8, [N, C]>, blocked<8>>",
      "list<tensor<f16, [4]>>",
      "[1, 2, width<8>]",
  };

  joggle::Env env;
  for (const std::string_view source : modules)
    CHECK(stable_module(env, source));
  for (const std::string_view source : attributes)
    CHECK(stable_attr(env, source));
  for (const std::string_view source : types)
    CHECK(stable_type(source));

  joggle::Mod visible_constant;
  CHECK(joggle::parse(env, "mod constant\nfn main() -> i32 { 7 return 0 }\n",
                      visible_constant, "constant.jog"));
  CHECK(joggle::print(visible_constant).find("\n  7\n") != std::string::npos);

  joggle::Mod repeated_value;
  CHECK(joggle::parse(
      env, "mod repeat\nfn main() -> i32 { var y: i32 = 0 y return y }\n",
      repeated_value, "repeat.jog"));
  const std::string repeated_text = joggle::print(repeated_value);
  CHECK(repeated_text.find("var y: i32 = 0") != std::string::npos);
  CHECK(stable_module(env, repeated_text));

  Random random(0x4a6f67676c65ULL);
  for (std::size_t iteration = 0; iteration < 10000; ++iteration) {
    const std::string_view module = modules[random.index(modules.size())];
    CHECK(stable_module(env, mutate(std::string(module), random)));
    const std::string_view attribute =
        attributes[random.index(attributes.size())];
    CHECK(stable_attr(env, mutate(std::string(attribute), random)));
    const std::string_view type = types[random.index(types.size())];
    CHECK(stable_type(mutate(std::string(type), random)));
  }
  return 0;
}
