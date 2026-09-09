#include <joggle/joggle.h>

#include <cstdint>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3)
    return 2;

  joggle::Env env;
  env.path(argv[1]);
  env.path(argv[2]);
  if (!env.load("probe"))
    return env.print_diags(stderr);
  const std::vector<joggle::Attr> args{joggle::Attr(std::int64_t{21})};
  std::vector<joggle::Attr> returns;
  if (!env.call("probe.twice", args, returns) || returns.size() != 1 ||
      !returns[0].integer() || *returns[0].integer() != 42)
    return 1;
  if (!env.load("nn"))
    return env.print_diags(stderr);

  constexpr std::string_view source =
      "module consumer\n"
      "use nn\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3, 1]>,\n"
      "  right: tensor<f32, [2, 1, 4]>\n"
      ") -> tensor<f32, [2, 3, 4]> {\n"
      "  return nn.add(left, right, \"NONE\")\n"
      "}\n";
  joggle::Mod mod;
  if (!joggle::parse(env, source, mod, "consumer.jog"))
    return mod.print_diags(stderr);
  if (!mod.verify(env))
    return mod.print_diags(stderr);
  for (joggle::Op op : mod.ops()) {
    if (op.callee() == "nn.add" && env.resolve(mod, op))
      return 0;
  }
  return 1;
}
