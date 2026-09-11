#include "joggle/joggle.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <fstream>
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

void append(joggle::Attr::Bytes& out, std::int64_t value) {
  const std::uint64_t bits = static_cast<std::uint64_t>(value);
  for (unsigned shift = 0; shift != 64; shift += 8)
    out.push_back(static_cast<std::uint8_t>(bits >> shift));
}

std::vector<std::int64_t> integers(const joggle::Attr::Bytes& bytes) {
  std::vector<std::int64_t> out;
  if (bytes.size() % 8 != 0)
    return out;
  for (std::size_t offset = 0; offset < bytes.size(); offset += 8) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift != 64; shift += 8)
      value |= std::uint64_t{bytes[offset + shift / 8]} << shift;
    out.push_back(std::bit_cast<std::int64_t>(value));
  }
  return out;
}

bool call(joggle::Env& env, std::string_view name,
          std::vector<joggle::Attr> args, std::vector<joggle::Attr>& returns) {
  env.clear_diags();
  return env.call(name, args, returns);
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
  CHECK(env.load("sat"));
  CHECK(env.load("sat.c"));

  joggle::Mod typed;
  constexpr std::string_view typed_source =
      "module typed\n"
      "use sat\n"
      "fn inferred(a: sat<8>, b: sat<8>) -> sat<8> {\n"
      "  return sat.add(a, b)\n"
      "}\n"
      "fn explicit(a: sat<16>, b: sat<16>) -> sat<16> {\n"
      "  return sat.add<16>(a, b)\n"
      "}\n";
  CHECK(joggle::parse(env, typed_source, typed, "typed.jog"));
  CHECK(typed.verify(env));
  for (const std::string_view name : {"inferred", "explicit"}) {
    const joggle::Op ret = typed.find_fn(name).body().ops().back();
    CHECK(ret.args().size() == 1);
    CHECK(ret.args().front().type().name() == "sat");
  }

  joggle::Mod wrong_arity;
  CHECK(joggle::parse(env,
                      "module wrong\nuse sat\n"
                      "fn f(a: sat<8>) -> sat<8> { return sat.add(a) }\n",
                      wrong_arity, "wrong-arity.jog"));
  CHECK(!wrong_arity.verify(env));
  CHECK(!wrong_arity.diags().empty());
  CHECK(wrong_arity.diags().front().message.find("expects 2 arguments") !=
        std::string::npos);

  joggle::Mod wrong_type;
  CHECK(joggle::parse(
      env,
      "module wrong\nuse sat\n"
      "fn f(a: sat<8>, b: sat<16>) -> sat<8> { return sat.add(a, b) }\n",
      wrong_type, "wrong-type.jog"));
  CHECK(!wrong_type.verify(env));
  CHECK(!wrong_type.diags().empty());
  CHECK(wrong_type.diags().front().message.find("expected 'sat<8>'") !=
        std::string::npos);

  joggle::Mod missing_use;
  CHECK(joggle::parse(
      env,
      "module wrong\n"
      "fn f(a: sat<8>, b: sat<8>) -> sat<8> { return sat.add(a, b) }\n",
      missing_use, "missing-use.jog"));
  CHECK(!missing_use.verify(env));
  CHECK(!missing_use.diags().empty());
  CHECK(missing_use.diags().front().message.find("requires 'use sat'") !=
        std::string::npos);

  joggle::Mod wrong_constructor;
  CHECK(joggle::parse(env,
                      "module wrong\nuse sat\n"
                      "fn f(x: sat<8, 16>) -> sat<8, 16> { return x }\n",
                      wrong_constructor, "wrong-constructor.jog"));
  CHECK(!wrong_constructor.verify(env));
  CHECK(!wrong_constructor.diags().empty());
  CHECK(wrong_constructor.diags().front().message.find(
            "expects 1 type argument") != std::string::npos);

  joggle::Mod wrong_kind;
  CHECK(joggle::parse(env,
                      "module wrong\nuse sat\n"
                      "fn f(x: sat<f32>) -> sat<f32> { return x }\n",
                      wrong_kind, "wrong-kind.jog"));
  CHECK(!wrong_kind.verify(env));
  CHECK(!wrong_kind.diags().empty());
  CHECK(wrong_kind.diags().front().message.find("expected 'int'") !=
        std::string::npos);

  joggle::Mod mod;
  CHECK(joggle::parse(env, source.str(), mod, argv[1]));
  CHECK(mod.verify(env));
  const joggle::Op abstract8 =
      mod.find_fn("add8").body().ops().back().args().front().def();
  const joggle::Op abstract32 =
      mod.find_fn("add32").body().ops().back().args().front().def();
  const joggle::Op abstract1 =
      mod.find_fn("add1").body().ops().back().args().front().def();
  const joggle::Op abstract64 =
      mod.find_fn("add64").body().ops().back().args().front().def();
  CHECK(env.resolve(mod, abstract8).module() == "sat");
  CHECK(env.resolve(mod, abstract1).module() == "sat");
  CHECK(env.resolve(mod, abstract64).module() == "sat");
  CHECK(env.resolve(mod, abstract32).module() == "base");
  CHECK(joggle::run(env, "sat.select", mod));
  CHECK(mod.verify(env));

  const joggle::Op selected =
      mod.find_fn("add8").body().ops().back().args().front().def();
  const joggle::Op untouched =
      mod.find_fn("add32").body().ops().back().args().front().def();
  const joggle::Op too_narrow =
      mod.find_fn("add1").body().ops().back().args().front().def();
  const joggle::Op too_wide =
      mod.find_fn("add64").body().ops().back().args().front().def();
  CHECK(selected.callee() == "sat.add");
  CHECK(untouched.callee() == "operator +");
  CHECK(too_narrow.callee() == "operator +");
  CHECK(too_wide.callee() == "operator +");
  const std::string once = joggle::print(mod);
  CHECK(joggle::run(env, "sat.select", mod));
  CHECK(joggle::print(mod) == once);

  std::vector<joggle::Attr> returns;
  CHECK(call(env, "sat.sim",
             {joggle::Attr(std::int64_t{8}), joggle::Attr(std::int64_t{100}),
              joggle::Attr(std::int64_t{100})},
             returns));
  CHECK(returns.size() == 1 && returns[0].integer() == 127);
  CHECK(call(env, "sat.sim",
             {joggle::Attr(std::int64_t{8}), joggle::Attr(std::int64_t{-100}),
              joggle::Attr(std::int64_t{-100})},
             returns));
  CHECK(returns.size() == 1 && returns[0].integer() == -128);

  CHECK(call(env, "sat.emit", {joggle::Attr(std::int64_t{8})}, returns));
  CHECK(returns.size() == 1 && returns[0].string());
  CHECK(returns[0].string()->find("module sat_add_8") !=
        std::string_view::npos);
  CHECK(returns[0].string()->find("[7:0]") != std::string_view::npos);
  CHECK(returns[0].string()->find("{a[7], a} + {b[7], b}") !=
        std::string_view::npos);

  joggle::Mod collision;
  constexpr std::string_view collision_source =
      "module collision\n"
      "use sat\n"
      "[sat.width: 5]\n"
      "fn sat_add_5(a: i16, b: i16) -> i16 { return a }\n"
      "fn add5(a: sat<5>, b: sat<5>) -> sat<5> {\n"
      "  return sat.add(a, b)\n"
      "}\n";
  CHECK(joggle::parse(env, collision_source, collision, "collision.jog"));
  CHECK(collision.verify(env));
  const std::string before_collision = joggle::print(collision);
  const std::uint64_t before_collision_revision = collision.revision();
  CHECK(!joggle::run(env, "sat.c.lower", collision));
  CHECK(joggle::print(collision) == before_collision);
  CHECK(collision.revision() == before_collision_revision);

  joggle::Mod nested_format;
  constexpr std::string_view nested_format_source =
      "module nested_format\n"
      "use sat\n"
      "use tensor\n"
      "fn keep(x: tensor<sat<5>, [2]>) -> tensor<sat<5>, [2]> {\n"
      "  return x\n"
      "}\n";
  CHECK(joggle::parse(env, nested_format_source, nested_format,
                      "nested-format.jog"));
  CHECK(nested_format.verify(env));
  CHECK(joggle::run(env, "sat.c.lower", nested_format));
  CHECK(nested_format.verify(env));
  const joggle::Fn keep = nested_format.find_fn("keep");
  const joggle::Ty lowered_tensor("tensor<i8, [2]>");
  CHECK(keep.params().front().type() == lowered_tensor);
  CHECK(keep.returns() == std::vector<joggle::Ty>{lowered_tensor});
  const std::string once_lowered = joggle::print(nested_format);
  CHECK(joggle::run(env, "sat.c.lower", nested_format));
  CHECK(joggle::print(nested_format) == once_lowered);

  CHECK(env.load("sat.vm"));
  std::ifstream vm_input_file(argv[2]);
  CHECK(vm_input_file);
  std::ostringstream vm_source;
  vm_source << vm_input_file.rdbuf();
  joggle::Mod vm_case;
  CHECK(joggle::parse(env, vm_source.str(), vm_case, argv[2]));
  CHECK(vm_case.verify(env));
  CHECK(joggle::run(env, "sat.vm.prepare", vm_case));
  CHECK(vm_case.verify(env));
  const std::string prepared_vm = joggle::print(vm_case);
  CHECK(prepared_vm.find("sat<") == std::string::npos);
  CHECK(joggle::run(env, "sat.vm.prepare", vm_case));
  CHECK(joggle::print(vm_case) == prepared_vm);
  CHECK(vm_case.find_fn("add5").params()[0].type() == joggle::Ty("i64"));
  CHECK(vm_case.find_fn("add_vec5").params()[0].type() ==
        joggle::Ty("tensor<i64, [4]>"));

  const std::vector<joggle::Attr> image_args{joggle::Attr("add_vec5")};
  joggle::Attr image;
  CHECK(joggle::query(env, "vm.image", vm_case, image, image_args));
  CHECK(image.string());
  joggle::Attr::Bytes vm_input;
  for (const std::int64_t value : {15, 10, -16, -10, 1, 10, -1, -10})
    append(vm_input, value);
  std::vector<joggle::Attr> vm_returns;
  CHECK(call(env, "vm.run",
             {joggle::Attr(std::string(*image.string())),
              joggle::Attr("add_vec5"), joggle::Attr(std::move(vm_input))},
             vm_returns));
  CHECK(vm_returns.size() == 2 && vm_returns[0].bytes() &&
        integers(*vm_returns[0].bytes()) ==
            (std::vector<std::int64_t>{15, 15, -16, -16}) &&
        vm_returns[1].integer() && *vm_returns[1].integer() > 0);
  return 0;
}
