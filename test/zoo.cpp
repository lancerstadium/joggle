#include "joggle/joggle.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
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
  std::map<std::string, std::size_t, std::less<>> source_calls;
  std::map<std::string, std::size_t, std::less<>> open;
  std::map<std::string, std::size_t, std::less<>> ready_open;
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
    ++stats.source_calls[std::string(op.callee())];
    bool inputs_known = true;
    for (joggle::Val input : op.args()) {
      const joggle::Ty type = input.type();
      inputs_known = type.valid() && type.text() != "_" && inputs_known;
    }
    for (joggle::Val output : op.outs()) {
      const joggle::Ty type = output.type();
      if (!type.valid() || type.text() == "_") {
        ++stats.unknown;
        ++stats.open[std::string(op.callee())];
        if (inputs_known)
          ++stats.ready_open[std::string(op.callee())];
      }
    }
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

void print_ready_open(const joggle::Mod& mod) {
  for (joggle::Op op : mod.ops()) {
    if (!op.callee().starts_with("onnx.") ||
        op.callee() == "onnx.tensor" || op.callee() == "onnx.model")
      continue;
    bool open = false;
    for (joggle::Val output : op.outs()) {
      const joggle::Ty type = output.type();
      open = !type.valid() || type.text() == "_" || open;
    }
    bool ready = !op.args().empty();
    for (joggle::Val input : op.args()) {
      const joggle::Ty type = input.type();
      ready = type.valid() && type.text() != "_" && ready;
    }
    if (!open || !ready)
      continue;
    std::printf("  %.*s(", static_cast<int>(op.callee().size()),
                op.callee().data());
    const std::vector<joggle::Val> inputs = op.args();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      const joggle::Ty input_type = inputs[index].type();
      const std::string_view type = input_type.text();
      std::printf("%s%.*s", index == 0 ? "" : ", ",
                  static_cast<int>(type.size()), type.data());
    }
    std::printf(")\n");
  }
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc >= 4);
  const bool roundtrip_only = std::string_view(argv[3]) == "--roundtrip";
  const bool from_signature =
      std::string_view(argv[3]) == "--frontier-from-signature";
  const bool convert_frontier =
      std::string_view(argv[3]) == "--convert-frontier";
  const bool frontier =
      std::string_view(argv[3]) == "--frontier" || from_signature;
  CHECK((!frontier && !convert_frontier) || argc >= 5);
  std::size_t expected_frontier = 0;
  if (frontier || convert_frontier) {
    const std::string_view text(argv[4]);
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(),
                                        expected_frontier);
    CHECK(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size());
  }
  const int first_expected =
      (frontier || convert_frontier) ? 5 : (roundtrip_only ? 4 : 3);
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
  if (!joggle::parse(env, *returns.front().string(), model, argv[1]))
    return model.print_diags(stderr);
  CHECK(model.verify(env));
  const joggle::Fn main = model.find_fn("main");
  CHECK(main && !main.params().empty() && !main.returns().empty());
  Stats source = inspect(model);
  CHECK(source.tensors > 0 && source.nodes > 0);
  for (int index = first_expected; index < argc; ++index)
    CHECK(source.calls.contains(argv[index]));

  const std::string canonical = joggle::print(model);
  joggle::Mod roundtrip;
  CHECK(joggle::parse(env, canonical, roundtrip, "zoo-roundtrip.jog"));
  CHECK(roundtrip.verify(env));
  CHECK(joggle::structurally_equal(model, roundtrip));

  if (from_signature) {
    for (joggle::Op op : model.ops()) {
      if (op.kind() != joggle::Op::Kind::call ||
          op.callee() == "onnx.tensor" || op.callee() == "onnx.model")
        continue;
      for (joggle::Val output : op.outs())
        (void)model.type(output, joggle::Ty("_"));
    }
    CHECK(model.verify(env));
    source = inspect(model);
  }

  const GraphRefs graphs = graph_refs(model);
  CHECK(graphs.valid);
  if (roundtrip_only) {
    std::printf("%s: %zu tensors, %zu nodes, %zu nested graphs\n", argv[1],
                source.tensors, source.nodes, graphs.count);
    return 0;
  }

  CHECK(env.load("onnx.nn"));
  CHECK(joggle::run(env, "onnx.nn.infer", model));
  CHECK(model.verify(env));
  const Stats inferred = inspect(model);
  std::printf("%s: %zu tensors, %zu nodes, %zu unknown before, %zu after\n",
              argv[1], source.tensors, source.nodes, source.unknown,
              inferred.unknown);
  for (const auto& [callee, count] : inferred.open)
    std::printf("  %s: %zu\n", callee.c_str(), count);
  if (!inferred.ready_open.empty()) {
    std::printf("ready open results:\n");
    for (const auto& [callee, count] : inferred.ready_open)
      std::printf("  %s: %zu\n", callee.c_str(), count);
    print_ready_open(model);
  }
  CHECK(inferred.tensors == source.tensors);
  CHECK(inferred.nodes == source.nodes);
  if (frontier) {
    CHECK(inferred.unknown == expected_frontier);
    return 0;
  }
  CHECK(inferred.unknown == 0);

  CHECK(joggle::run(env, "onnx.nn.convert", model));
  CHECK(model.verify(env));
  const Stats converted = inspect(model);
  if (converted.nodes != 0) {
    std::printf("remaining source calls after conversion:\n");
    for (const auto& [callee, count] : converted.source_calls)
      std::printf("  %s: %zu\n", callee.c_str(), count);
  }
  if (convert_frontier) {
    CHECK(converted.nodes == expected_frontier);
    return 0;
  }
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

  return 0;
}
