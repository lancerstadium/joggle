#include "joggle/joggle.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
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

struct Stats {
  std::size_t tensors = 0;
  std::size_t nodes = 0;
  std::size_t unknown = 0;
  std::set<std::string, std::less<>> calls;
};

Stats inspect(const joggle::Mod& mod) {
  Stats stats;
  for (joggle::Op op : mod.ops()) {
    if (op.kind() != joggle::Op::Kind::call)
      continue;
    stats.calls.emplace(op.callee());
    if (op.callee() == "onnx.tensor") {
      ++stats.tensors;
      continue;
    }
    if (!op.callee().starts_with("onnx.") || op.callee() == "onnx.model")
      continue;
    ++stats.nodes;
    for (joggle::Val output : op.outs())
      stats.unknown += output.type().text() == "_" ? 1 : 0;
  }
  return stats;
}

struct GraphRefs {
  bool valid = true;
  std::size_t count = 0;
};

GraphRefs graph_refs(const joggle::Mod& mod) {
  GraphRefs result;
  const auto check = [&](const joggle::Attr& value, joggle::Op op,
                         const auto& self) -> void {
    if (const joggle::Attr::List* values = value.list()) {
      for (const joggle::Attr& item : *values)
        self(item, op, self);
      return;
    }
    const joggle::Attr::Dict* ref = value.dict();
    if (!ref || !ref->contains("fn"))
      return;
    if (!ref->at("fn").string() || !ref->contains("inputs") ||
        !ref->at("inputs").integer() || !ref->contains("captures") ||
        !ref->at("captures").list()) {
      result.valid = false;
      return;
    }
    const joggle::Fn fn = mod.find_fn(*ref->at("fn").string());
    if (!fn) {
      result.valid = false;
      return;
    }
    const std::int64_t inputs = *ref->at("inputs").integer();
    const joggle::Attr::List& captures = *ref->at("captures").list();
    if (inputs < 0 || static_cast<std::size_t>(inputs) + captures.size() !=
                          fn.params().size()) {
      result.valid = false;
      return;
    }
    for (const joggle::Attr& capture : captures) {
      if (!capture.integer() || *capture.integer() < 0 ||
          static_cast<std::size_t>(*capture.integer()) >= op.args().size()) {
        result.valid = false;
        return;
      }
    }
    ++result.count;
  };
  for (joggle::Op op : mod.ops()) {
    const joggle::Attr* metadata = op.meta("onnx");
    if (!metadata || !metadata->dict())
      continue;
    for (const auto& [name, value] : *metadata->dict()) {
      (void)name;
      check(value, op, check);
    }
  }
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc >= 4);
  const bool import_only = std::string_view(argv[3]) == "--import-only";
  const int first_expected = import_only ? 4 : 3;
  std::ifstream input(argv[1], std::ios::binary);
  CHECK(input);
  const std::vector<unsigned char> raw{std::istreambuf_iterator<char>(input),
                                       std::istreambuf_iterator<char>()};

  joggle::Env env;
  env.path(argv[2]);
  CHECK(env.load("onnx"));
  const std::vector<joggle::Attr> args{
      joggle::Attr(joggle::Attr::Bytes(raw.begin(), raw.end()))};
  std::vector<joggle::Attr> returns;
  CHECK(env.call("onnx.read", args, returns));
  CHECK(returns.size() == 1 && returns.front().string());

  joggle::Mod model;
  CHECK(joggle::parse(env, *returns.front().string(), model, argv[1]));
  CHECK(model.verify(env));
  const joggle::Fn main = model.find_fn("main");
  CHECK(main && !main.params().empty() && !main.returns().empty());
  const Stats source = inspect(model);
  CHECK(source.tensors > 0 && source.nodes > 0);
  for (int index = first_expected; index < argc; ++index)
    CHECK(source.calls.contains(argv[index]));

  const std::string canonical = joggle::print(model);
  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, canonical, roundtrip, "zoo-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(model, roundtrip));

  const GraphRefs graphs = graph_refs(model);
  CHECK(graphs.valid);
  if (import_only) {
    CHECK(graphs.count > 0);
    std::printf("%s: %zu tensors, %zu nodes, %zu nested graphs\n", argv[1],
                source.tensors, source.nodes, graphs.count);
    return 0;
  }

  CHECK(env.load("onnx.nn"));
  CHECK(joggle::run(env, "onnx.nn.infer", model));
  CHECK(model.verify(env));
  const Stats inferred = inspect(model);
  CHECK(inferred.tensors == source.tensors);
  CHECK(inferred.nodes == source.nodes);
  CHECK(inferred.unknown == 0);

  CHECK(joggle::run(env, "onnx.nn.convert", model));
  CHECK(model.verify(env));
  const Stats converted = inspect(model);
  CHECK(converted.nodes == 0);
  const std::string converted_text = joggle::print(model);
  CHECK(joggle::run(env, "onnx.nn.convert", model));
  CHECK(joggle::print(model) == converted_text);

  joggle::Mod converted_roundtrip;
  CHECK(joggle::parse(env, converted_text, converted_roundtrip,
                      "zoo-converted-roundtrip.jog"));
  if (!converted_roundtrip.verify(env))
    return converted_roundtrip.print_diags(stderr);
  CHECK(joggle::structurally_equal(model, converted_roundtrip));

  std::printf("%s: %zu tensors, %zu nodes, %zu unknown before, %zu after\n",
              argv[1], source.tensors, source.nodes, source.unknown,
              inferred.unknown);
  return 0;
}
