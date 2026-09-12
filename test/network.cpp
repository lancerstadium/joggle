#include "joggle/joggle.h"

#include <cstdio>
#include <set>
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

std::size_t count(const joggle::Mod& mod, std::string_view callee) {
  std::size_t result = 0;
  for (joggle::Op op : mod.ops())
    result += op.callee() == callee;
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 3);
  joggle::Env env;
  env.path(argv[1]);
  env.path(argv[2]);
  CHECK(env.load("nn"));
  CHECK(env.load("script"));

  constexpr std::string_view legal_source =
      "module legal.network\n"
      "use nn\n"
      "fn main(x: tensor<i8, [4]>) -> tensor<i8, [4]> {\n"
      "  return relu(x)\n"
      "}\n";
  joggle::Mod legal;
  CHECK(joggle::parse(env, legal_source, legal, "legal-network.jog"));
  CHECK(legal.verify(env));
  const std::string before_keep = joggle::print(legal);
  CHECK(joggle::run(env, "script.keep_relu", legal));
  CHECK(joggle::print(legal) == before_keep);
  joggle::Attr supported_frontier;
  CHECK(joggle::query(env, "script.typed_frontier", legal,
                      supported_frontier));
  CHECK(supported_frontier.list() && supported_frontier.list()->empty());
  joggle::Attr open_frontier;
  CHECK(joggle::query(env, "script.network_frontier", legal,
                      open_frontier));
  CHECK(open_frontier.list() && open_frontier.list()->size() == 1);
  CHECK(open_frontier.list()->front().string() == "nn.relu");
  CHECK(joggle::run(env, "script.expose_network", legal));
  CHECK(legal.verify(env));
  for (joggle::Op op : legal.ops())
    CHECK(op.callee() != "relu" && op.callee() != "nn.relu");
  CHECK(joggle::query(env, "script.network_frontier", legal,
                      open_frontier));
  CHECK(open_frontier.list() && !open_frontier.list()->empty());
  joggle::Mod legal_roundtrip;
  CHECK(joggle::parse(env, joggle::print(legal), legal_roundtrip,
                      "legal-network-roundtrip.jog"));
  CHECK(legal_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(legal, legal_roundtrip));

  constexpr std::string_view typed_source =
      "module typed.network\n"
      "use nn\n"
      "fn main(a: tensor<i8, [2, 2]>, b: tensor<f32, [4]>) "
      "-> (tensor<i8, [2, 2]>, tensor<f32, [4]>) {\n"
      "  let x = nn.relu(a)\n"
      "  let y = nn.relu(b)\n"
      "  return x, y\n"
      "}\n";
  joggle::Mod typed;
  CHECK(joggle::parse(env, typed_source, typed, "typed-network.jog"));
  CHECK(typed.verify(env));
  const std::vector<joggle::Fn> script_fns = env.fns("script");
  joggle::Fn relu_cap;
  for (joggle::Fn fn : script_fns)
    if (fn.name() == "nn.relu" && !fn.generics().empty())
      relu_cap = fn;
  CHECK(relu_cap);
  std::vector<joggle::Op> typed_relus;
  for (joggle::Op op : typed.ops())
    if (op.callee() == "nn.relu")
      typed_relus.push_back(op);
  CHECK(typed_relus.size() == 2);
  CHECK(env.accepts(typed_relus[0], relu_cap));
  CHECK(!env.accepts(typed_relus[1], relu_cap));
  const std::string before_failed_impl = joggle::print(typed);
  const std::uint64_t before_failed_revision = typed.revision();
  CHECK(!env.expand(typed, typed_relus[1], relu_cap));
  CHECK(joggle::print(typed) == before_failed_impl);
  CHECK(typed.revision() == before_failed_revision);
  for (const std::string& dependency : typed.uses())
    CHECK(dependency != "script");
  typed.clear_diags();
  joggle::Attr typed_frontier;
  CHECK(joggle::query(env, "script.typed_frontier", typed, typed_frontier));
  CHECK(typed_frontier.list() && typed_frontier.list()->size() == 1);
  CHECK(typed_frontier.list()->front().string() == "nn.relu");
  CHECK(joggle::run(env, "script.typed_legalize", typed));
  CHECK(typed.verify(env));
  typed_relus.clear();
  for (joggle::Op op : typed.ops())
    if (op.callee() == "nn.relu")
      typed_relus.push_back(op);
  CHECK(typed_relus.size() == 1);
  CHECK(env.accepts(typed_relus.front(), relu_cap));
  joggle::Mod typed_roundtrip;
  CHECK(joggle::parse(env, joggle::print(typed), typed_roundtrip,
                      "typed-network-roundtrip.jog"));
  CHECK(typed_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(typed, typed_roundtrip));

  constexpr std::string_view implementation_source =
      "module implementation.network\n"
      "use nn\n"
      "fn main(a: tensor<i8, [4]>, b: tensor<i8, [8]>) "
      "-> (tensor<i8, [4]>, tensor<i8, [8]>) {\n"
      "  let x = relu(a)\n"
      "  let y = relu(b)\n"
      "  return x, y\n"
      "}\n";
  joggle::Mod implementation;
  CHECK(joggle::parse(env, implementation_source, implementation,
                      "implementation-network.jog"));
  CHECK(implementation.verify(env));
  std::vector<joggle::Fn> relu_impls;
  for (joggle::Fn fn : script_fns)
    if (fn.name() == "nn.relu")
      relu_impls.push_back(fn);
  CHECK(relu_impls.size() == 2);
  std::vector<joggle::Op> implementation_calls;
  for (joggle::Op op : implementation.ops())
    if (op.callee() == "relu")
      implementation_calls.push_back(op);
  CHECK(implementation_calls.size() == 2);
  bool ambiguous = true;
  const joggle::Fn vector_impl =
      env.match(implementation_calls[0], relu_impls, &ambiguous);
  CHECK(vector_impl && !ambiguous && vector_impl.generics().empty());
  const joggle::Fn generic_impl =
      env.match(implementation_calls[1], relu_impls, &ambiguous);
  CHECK(generic_impl && !ambiguous && !generic_impl.generics().empty());
  joggle::Attr implementation_report;
  CHECK(joggle::run(env, "script.apply_impls", implementation,
                    implementation_report));
  CHECK(implementation.verify(env));
  CHECK(count(implementation, "relu") == 0);
  CHECK(count(implementation, "mid.relu") == 0);
  CHECK(count(implementation, "edge.relu4") == 1);
  CHECK(count(implementation, "edge.relu") == 1);
  bool uses_script = false;
  for (const std::string& dependency : implementation.uses())
    uses_script = dependency == "script" || uses_script;
  CHECK(uses_script);
  const joggle::Attr::Dict* implementation_summary =
      implementation_report.dict();
  CHECK(implementation_summary);
  const joggle::Attr::List* implementation_steps =
      implementation_summary->at("steps").list();
  CHECK(implementation_steps);
  std::size_t expansion_events = 0;
  std::size_t nn_expansions = 0;
  std::size_t mid_expansions = 0;
  bool saw_generic = false;
  bool saw_specialized = false;
  for (const joggle::Attr& step : *implementation_steps) {
    const joggle::Attr::Dict* event = step.dict();
    if (!event || event->at("kind").string() != "expand")
      continue;
    ++expansion_events;
    CHECK(event->at("edits").integer() &&
          *event->at("edits").integer() > 0);
    const auto source = event->at("source").string();
    nn_expansions += source == "nn.relu" ? 1 : 0;
    mid_expansions += source == "mid.relu" ? 1 : 0;
    const auto impl = event->at("impl").string();
    CHECK(impl);
    if (source == "nn.relu")
      CHECK(impl == "script.nn.relu");
    if (source == "mid.relu")
      CHECK(impl == "script.mid.relu");
    const joggle::Attr::List* params = event->at("params").list();
    CHECK(params && params->size() == 1 && params->front().string());
    const joggle::Attr::List* returns = event->at("returns").list();
    CHECK(returns && returns->size() == 1 && returns->front().string());
    const std::string_view param = *params->front().string();
    if (source == "nn.relu") {
      saw_generic =
          param.find(", S>") != std::string_view::npos || saw_generic;
      saw_specialized =
          param.find("[4]") != std::string_view::npos || saw_specialized;
    }
  }
  CHECK(expansion_events == 3);
  CHECK(nn_expansions == 2);
  CHECK(mid_expansions == 1);
  CHECK(saw_generic && saw_specialized);
  joggle::Mod implementation_roundtrip;
  CHECK(joggle::parse(env, joggle::print(implementation),
                      implementation_roundtrip,
                      "implementation-network-roundtrip.jog"));
  CHECK(implementation_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(implementation,
                                   implementation_roundtrip));
  joggle::Mod ambiguous_impl;
  CHECK(joggle::parse(env, implementation_source, ambiguous_impl,
                      "ambiguous-implementation-network.jog"));
  CHECK(ambiguous_impl.verify(env));
  const std::string before_ambiguous = joggle::print(ambiguous_impl);
  const std::uint64_t ambiguous_revision = ambiguous_impl.revision();
  CHECK(!joggle::run(env, "script.apply_ambiguous", ambiguous_impl));
  CHECK(joggle::print(ambiguous_impl) == before_ambiguous);
  CHECK(ambiguous_impl.revision() == ambiguous_revision);

  constexpr std::string_view cyclic_source =
      "module cyclic.network\n"
      "use nn\n"
      "fn main(x: tensor<i8, [4]>) -> tensor<i8, [4]> {\n"
      "  return loop.relu(x)\n"
      "}\n";
  joggle::Mod cyclic;
  CHECK(joggle::parse(env, cyclic_source, cyclic, "cyclic-network.jog"));
  CHECK(cyclic.verify(env));
  const std::string before_cycle = joggle::print(cyclic);
  const std::uint64_t cycle_revision = cyclic.revision();
  CHECK(!joggle::run(env, "script.apply_cycle", cyclic));
  CHECK(joggle::print(cyclic) == before_cycle);
  CHECK(cyclic.revision() == cycle_revision);
  bool diagnosed_cycle = false;
  for (const joggle::Diag& diag : env.diags())
    diagnosed_cycle =
        diag.message.find("did not converge") != std::string::npos ||
        diagnosed_cycle;
  CHECK(diagnosed_cycle);

  constexpr std::string_view annotated_legal_source =
      "module annotated.legal\n"
      "use nn\n"
      "fn main(x: tensor<f32, [4]>) -> tensor<f32, [4]> {\n"
      "  [schedule: {width: 4}]\n"
      "  let y = relu(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod annotated_legal;
  CHECK(joggle::parse(env, annotated_legal_source, annotated_legal,
                      "annotated-legal.jog"));
  CHECK(annotated_legal.verify(env));
  const std::string before_annotated = joggle::print(annotated_legal);
  CHECK(joggle::run(env, "script.expose_network", annotated_legal));
  CHECK(joggle::print(annotated_legal) == before_annotated);
  CHECK(joggle::query(env, "script.network_frontier", annotated_legal,
                      open_frontier));
  CHECK(open_frontier.list() && open_frontier.list()->size() == 1);
  CHECK(open_frontier.list()->front().string() == "nn.relu");

  constexpr std::string_view dynamic_source =
      "module dynamic.network\n"
      "use tensor\n"
      "fn keep(x: tensor<f32, [_, 3]>) -> tensor<f32, [_, 3]> {\n"
      "  return x\n"
      "}\n";
  joggle::Mod dynamic;
  CHECK(joggle::parse(env, dynamic_source, dynamic, "dynamic.jog"));
  CHECK(dynamic.verify(env));
  joggle::Mod dynamic_roundtrip;
  CHECK(joggle::parse(env, joggle::print(dynamic), dynamic_roundtrip,
                      "dynamic-roundtrip.jog"));
  CHECK(dynamic_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(dynamic, dynamic_roundtrip));

  constexpr std::string_view max_pool_source =
      "module max.pool\n"
      "use nn\n"
      "fn main(x: tensor<f32, [1, 1, 4, 4]>) "
      "-> tensor<f32, [1, 1, 2, 2]> {\n"
      "  return nn.max_pool2d(\n"
      "    x, [2, 2], [2, 2], [0, 0, 0, 0], [1, 1], [0, 1, 2, 3]\n"
      "  )\n"
      "}\n";
  joggle::Mod max_pool;
  CHECK(joggle::parse(env, max_pool_source, max_pool, "max-pool.jog"));
  CHECK(max_pool.verify(env));
  joggle::Op max_pool_call;
  for (joggle::Op op : max_pool.ops())
    if (op.callee() == "nn.max_pool2d")
      max_pool_call = op;
  const joggle::Fn max_pool_fn = env.resolve(max_pool, max_pool_call);
  CHECK(max_pool_fn && env.expand(max_pool, max_pool_call, max_pool_fn));
  CHECK(max_pool.verify(env));
  for (joggle::Op op : max_pool.ops())
    CHECK(op.callee() != "nn.max_pool2d");
  joggle::Mod max_pool_roundtrip;
  CHECK(joggle::parse(env, joggle::print(max_pool), max_pool_roundtrip,
                      "max-pool-roundtrip.jog"));
  CHECK(max_pool_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(max_pool, max_pool_roundtrip));

  constexpr std::string_view broadcast_source =
      "module broadcast.network\n"
      "use nn\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3, 1]>,\n"
      "  right: tensor<f32, [2, 1, 4]>\n"
      ") -> tensor<f32, [2, 3, 4]> {\n"
      "  return nn.add(left, right, \"NONE\")\n"
      "}\n";
  joggle::Mod broadcast;
  CHECK(joggle::parse(env, broadcast_source, broadcast, "broadcast.jog"));
  CHECK(broadcast.verify(env));
  joggle::Op add;
  for (joggle::Op op : broadcast.ops())
    if (op.callee() == "nn.add")
      add = op;
  CHECK(add);
  const joggle::Fn add_fn = env.resolve(broadcast, add);
  CHECK(add_fn && env.expand(broadcast, add, add_fn));
  CHECK(broadcast.verify(env));
  std::vector<joggle::Op> copies;
  for (joggle::Op op : broadcast.ops())
    if (op.callee() == "tensor.broadcast")
      copies.push_back(op);
  CHECK(copies.size() == 2);
  for (joggle::Op op : copies) {
    const joggle::Fn callee = env.resolve(broadcast, op);
    CHECK(callee && env.expand(broadcast, op, callee));
  }
  CHECK(broadcast.verify(env));
  std::size_t loops = 0;
  for (joggle::Op op : broadcast.ops()) {
    CHECK(op.callee() != "nn.add" && op.callee() != "tensor.broadcast");
    loops += op.kind() == joggle::Op::Kind::loop ? 1 : 0;
  }
  CHECK(loops == 4);
  joggle::Mod broadcast_roundtrip;
  CHECK(joggle::parse(env, joggle::print(broadcast), broadcast_roundtrip,
                      "broadcast-roundtrip.jog"));
  CHECK(broadcast_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(broadcast, broadcast_roundtrip));

  constexpr std::string_view residual_source =
      "module residual.network\n"
      "use nn\n"
      "fn main(\n"
      "  left: tensor<f32, [2, 3]>, right: tensor<f32, [2, 3]>\n"
      ") -> tensor<f32, [2, 3]> {\n"
      "  return nn.add(left, right, \"NONE\")\n"
      "}\n";
  joggle::Mod residual;
  CHECK(joggle::parse(env, residual_source, residual, "residual.jog"));
  CHECK(residual.verify(env));
  joggle::Op residual_add;
  for (joggle::Op op : residual.ops())
    if (op.callee() == "nn.add")
      residual_add = op;
  const joggle::Fn residual_fn = env.resolve(residual, residual_add);
  CHECK(residual_fn && residual_fn.generics().size() == 2);
  CHECK(env.expand(residual, residual_add, residual_fn));
  CHECK(residual.verify(env));
  for (joggle::Op op : residual.ops())
    CHECK(op.callee() != "tensor.broadcast");

  CHECK(env.load("onnx.nn"));
  constexpr std::string_view bridge_source =
      "module broadcast.bridge\n"
      "use onnx\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3, 1]>,\n"
      "  right: tensor<f32, [2, 1, 4]>\n"
      ") -> tensor<f32, [2, 3, 4]> {\n"
      "  let sum = onnx.Add(left, right)\n"
      "  let out: tensor<f32, [2, 3, 4]> = onnx.Relu(sum)\n"
      "  return out\n"
      "}\n";
  joggle::Mod bridge;
  CHECK(joggle::parse(env, bridge_source, bridge, "broadcast-bridge.jog"));
  CHECK(bridge.verify(env));
  joggle::Mod direct_bridge;
  CHECK(joggle::parse(env, bridge_source, direct_bridge,
                      "direct-broadcast-bridge.jog"));
  CHECK(direct_bridge.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", direct_bridge));
  CHECK(direct_bridge.verify(env));
  CHECK(count(direct_bridge, "onnx.Add") == 0);
  CHECK(count(direct_bridge, "onnx.Relu") == 0);
  CHECK(count(direct_bridge, "nn.add") == 1);
  CHECK(count(direct_bridge, "nn.relu") == 1);
  if (!joggle::run(env, "onnx.nn.infer", bridge))
    return env.print_diags(stderr);
  CHECK(bridge.verify(env));
  joggle::Op source_add;
  for (joggle::Op op : bridge.ops())
    if (op.callee() == "onnx.Add")
      source_add = op;
  CHECK(source_add && source_add.outs().size() == 1);
  CHECK(source_add.outs()[0].type() ==
        joggle::Ty("tensor<f32, [2, 3, 4]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", bridge));
  CHECK(bridge.verify(env));
  CHECK(joggle::structurally_equal(direct_bridge, bridge));
  joggle::Op semantic_add;
  for (joggle::Op op : bridge.ops())
    if (op.callee() == "nn.add")
      semantic_add = op;
  CHECK(semantic_add && semantic_add.args().size() == 2);
  const joggle::Fn semantic_fn = env.resolve(bridge, semantic_add);
  CHECK(semantic_fn && env.expand(bridge, semantic_add, semantic_fn));
  CHECK(bridge.verify(env));

  constexpr std::string_view binary_bridge_source =
      "module binary.bridge\n"
      "use onnx\n"
      "fn main(\n"
      "  left: tensor<f32, [1, 3, 1]>,\n"
      "  right: tensor<f32, [2, 1, 4]>\n"
      ") -> tensor<f32, [2, 3, 4]> {\n"
      "  let product = onnx.Mul(left, right)\n"
      "  let out: tensor<f32, [2, 3, 4]> = onnx.Sub(product, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod binary_bridge;
  CHECK(joggle::parse(env, binary_bridge_source, binary_bridge,
                      "binary-bridge.jog"));
  CHECK(binary_bridge.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", binary_bridge));
  CHECK(binary_bridge.verify(env));
  joggle::Op source_mul;
  for (joggle::Op op : binary_bridge.ops())
    if (op.callee() == "onnx.Mul")
      source_mul = op;
  CHECK(source_mul && source_mul.outs().size() == 1);
  CHECK(source_mul.outs()[0].type() ==
        joggle::Ty("tensor<f32, [2, 3, 4]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", binary_bridge));
  CHECK(binary_bridge.verify(env));
  std::size_t binaries = 0;
  for (joggle::Op op : binary_bridge.ops()) {
    if (op.callee() != "nn.mul" && op.callee() != "nn.sub")
      continue;
    CHECK(op.args().size() == 2 && op.meta().empty());
    const joggle::Fn fn = env.resolve(binary_bridge, op);
    CHECK(fn && env.expand(binary_bridge, op, fn));
    ++binaries;
  }
  CHECK(binaries == 2 && binary_bridge.verify(env));
  for (joggle::Op op : binary_bridge.ops())
    CHECK(op.callee() != "nn.mul" && op.callee() != "nn.sub");

  constexpr std::string_view scalar_bridge_source =
      "module scalar.bridge\n"
      "use onnx\n"
      "fn main(values: tensor<f32, [768]>) -> tensor<f32, [768]> {\n"
      "  let scalar: tensor<f32, []> = source()\n"
      "  let out: tensor<f32, [768]> = onnx.Mul(scalar, values)\n"
      "  return out\n"
      "}\n";
  joggle::Mod scalar_bridge;
  CHECK(joggle::parse(env, scalar_bridge_source, scalar_bridge,
                      "scalar-bridge.jog"));
  CHECK(scalar_bridge.verify(env));
  joggle::Op scalar_mul;
  for (joggle::Op op : scalar_bridge.ops())
    if (op.callee() == "onnx.Mul")
      scalar_mul = op;
  CHECK(scalar_mul && scalar_bridge.use(env, "nn"));
  const std::string before_bad_retarget = joggle::print(scalar_bridge);
  CHECK(!scalar_bridge.retarget(env, scalar_mul, "nn.relu",
                                scalar_mul.args()));
  CHECK(joggle::print(scalar_bridge) == before_bad_retarget);
  CHECK(joggle::run(env, "onnx.nn.convert", scalar_bridge));
  CHECK(scalar_bridge.verify(env));
  CHECK(scalar_mul.callee() == "nn.mul");

  constexpr std::string_view broadcast_relations_source =
      "module broadcast.relations\n"
      "use onnx\n"
      "fn main(\n"
      "  scalar: tensor<f32, []>, left: tensor<f32, [1, 3]>,\n"
      "  right: tensor<f32, [2, 1]>\n"
      ") -> (tensor<f32, [2, 3]>, tensor<bool, [2, 3]>) {\n"
      "  let maximum = onnx.Max(scalar, left, right)\n"
      "  let less = onnx.Less(maximum, right)\n"
      "  return maximum, less\n"
      "}\n";
  joggle::Mod broadcast_relations;
  CHECK(joggle::parse(env, broadcast_relations_source, broadcast_relations,
                      "broadcast-relations.jog"));
  CHECK(broadcast_relations.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", broadcast_relations));
  CHECK(broadcast_relations.verify(env));
  for (joggle::Op op : broadcast_relations.ops()) {
    if (op.callee() == "onnx.Max")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [2, 3]>"));
    if (op.callee() == "onnx.Less")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<bool, [2, 3]>"));
  }

  constexpr std::string_view unary_bridge_source =
      "module unary.bridge\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [2, 3]>) -> (_, _, _, _, _, _, _) {\n"
      "  let sigmoid = onnx.Sigmoid(x)\n"
      "  let exponent = onnx.Exp(sigmoid)\n"
      "  let floor = onnx.Floor(exponent)\n"
      "  let logarithm = onnx.Log(exponent)\n"
      "  let error = onnx.Erf(sigmoid)\n"
      "  let ceiling = onnx.Ceil(exponent)\n"
      "  let rounded = onnx.Round(ceiling)\n"
      "  return sigmoid, exponent, floor, logarithm, error, ceiling, rounded\n"
      "}\n";
  joggle::Mod unary_bridge;
  CHECK(joggle::parse(env, unary_bridge_source, unary_bridge,
                      "unary-bridge.jog"));
  CHECK(unary_bridge.verify(env));
  joggle::Attr untyped;
  CHECK(joggle::query(env, "opt.untyped", unary_bridge, untyped));
  CHECK(untyped.list() && untyped.list()->size() == 7);
  CHECK(joggle::run(env, "onnx.nn.infer", unary_bridge));
  CHECK(unary_bridge.verify(env));
  CHECK(joggle::query(env, "opt.untyped", unary_bridge, untyped));
  CHECK(untyped.list() && untyped.list()->empty());
  for (joggle::Op op : unary_bridge.ops())
    if (op.callee().starts_with("onnx.")) {
      CHECK(op.outs().size() == 1);
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [2, 3]>"));
    }
  CHECK(joggle::run(env, "onnx.nn.convert", unary_bridge));
  CHECK(unary_bridge.verify(env));
  CHECK(count(unary_bridge, "nn.sigmoid") == 1);
  CHECK(count(unary_bridge, "nn.exp") == 1);
  CHECK(count(unary_bridge, "nn.floor") == 1);
  CHECK(count(unary_bridge, "nn.log") == 1);
  CHECK(count(unary_bridge, "nn.erf") == 1);
  CHECK(count(unary_bridge, "nn.ceil") == 1);
  CHECK(count(unary_bridge, "nn.round_even") == 1);
  for (joggle::Op op : unary_bridge.ops()) {
    if (!op.callee().starts_with("nn."))
      continue;
    const joggle::Fn fn = env.resolve(unary_bridge, op);
    CHECK(fn && env.expand(unary_bridge, op, fn));
  }
  CHECK(unary_bridge.verify(env));
  CHECK(count(unary_bridge, "nn.sigmoid") == 0);
  CHECK(count(unary_bridge, "nn.exp") == 0);
  CHECK(count(unary_bridge, "nn.floor") == 0);
  CHECK(count(unary_bridge, "nn.log") == 0);
  CHECK(count(unary_bridge, "nn.erf") == 0);
  CHECK(count(unary_bridge, "nn.ceil") == 0);
  CHECK(count(unary_bridge, "nn.round_even") == 0);

  constexpr std::string_view matrix_bridge_source =
      "module matrix.bridge\n"
      "use onnx\n"
      "fn main(\n"
      "  x: tensor<f32, [2, 3, 4]>, w: tensor<f32, [4, 5]>\n"
      ") -> tensor<f32, [5, 6]> {\n"
      "  [onnx: {axis: -1}]\n"
      "  let flat = onnx.Flatten(x)\n"
      "  let product = onnx.MatMul(flat, w)\n"
      "  [onnx: {perm: [1, 0]}]\n"
      "  let out = onnx.Transpose(product)\n"
      "  return out\n"
      "}\n";
  joggle::Mod matrix_bridge;
  CHECK(joggle::parse(env, matrix_bridge_source, matrix_bridge,
                      "matrix-bridge.jog"));
  CHECK(matrix_bridge.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", matrix_bridge));
  CHECK(matrix_bridge.verify(env));
  CHECK(matrix_bridge.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [5, 6]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", matrix_bridge));
  CHECK(matrix_bridge.verify(env));
  std::size_t matrix_calls = 0;
  for (joggle::Op op : matrix_bridge.ops()) {
    if (op.callee() != "tensor.reshape" && op.callee() != "tensor.matmul" &&
        op.callee() != "tensor.permute")
      continue;
    const joggle::Fn fn = env.resolve(matrix_bridge, op);
    CHECK(fn && env.expand(matrix_bridge, op, fn));
    ++matrix_calls;
  }
  CHECK(matrix_calls == 3 && matrix_bridge.verify(env));

  constexpr std::string_view pool_bridge_source =
      "module pool.bridge\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [1, 1, 4, 4]>) "
      "-> tensor<f32, [1, 1, 2, 2]> {\n"
      "  [onnx: {kernel_shape: [2, 2], strides: [2, 2]}]\n"
      "  let y: tensor<f32, [1, 1, 2, 2]> = onnx.MaxPool(x)\n"
      "  return y\n"
      "}\n";
  joggle::Mod pool_bridge;
  CHECK(joggle::parse(env, pool_bridge_source, pool_bridge,
                      "pool-bridge.jog"));
  CHECK(pool_bridge.verify(env));
  if (!joggle::run(env, "onnx.nn.convert", pool_bridge))
    return env.print_diags(stderr);
  CHECK(pool_bridge.verify(env));
  joggle::Op semantic_pool;
  for (joggle::Op op : pool_bridge.ops())
    if (op.callee() == "nn.max_pool2d")
      semantic_pool = op;
  CHECK(semantic_pool && semantic_pool.args().size() == 6);
  CHECK(semantic_pool.meta().empty());
  const joggle::Fn semantic_pool_fn = env.resolve(pool_bridge, semantic_pool);
  CHECK(semantic_pool_fn &&
        env.expand(pool_bridge, semantic_pool, semantic_pool_fn));
  CHECK(pool_bridge.verify(env));

  constexpr std::string_view zoo_slice_source =
      "module zoo.slice\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 3, 8, 8]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>,\n"
      "  matrix: tensor<f32, [5, 16]>, bias: tensor<f32, [5]>\n"
      ") -> tensor<f32, [N, 5]> {\n"
      "  [onnx: {auto_pad: \"SAME_UPPER\", strides: [2, 2]}]\n"
      "  let convolved = onnx.Conv(x, weight)\n"
      "  [onnx: {alpha: 0.1}]\n"
      "  let activated = onnx.LeakyRelu(convolved)\n"
      "  [onnx: {auto_pad: \"SAME_UPPER\", kernel_shape: [2, 2], "
      "strides: [2, 2]}]\n"
      "  let pooled = onnx.MaxPool(activated)\n"
      "  [onnx: {ratio: 0.5}]\n"
      "  let kept = onnx.Dropout(pooled)\n"
      "  let flat = onnx.Flatten(kept)\n"
      "  [onnx: {alpha: 1.0, beta: 1.0, transB: 1}]\n"
      "  let out = onnx.Gemm(flat, matrix, bias)\n"
      "  return out\n"
      "}\n";
  joggle::Mod zoo_slice;
  CHECK(joggle::parse(env, zoo_slice_source, zoo_slice, "zoo-slice.jog"));
  CHECK(zoo_slice.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", zoo_slice));
  CHECK(zoo_slice.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", zoo_slice));
  CHECK(zoo_slice.verify(env));
  const std::set<std::string, std::less<>> zoo_calls{
      "nn.conv2d",       "nn.leaky_relu", "nn.max_pool2d",
      "base.copy",       "tensor.reshape", "nn.gemm"};
  std::set<std::string, std::less<>> seen_zoo_calls;
  for (joggle::Op op : zoo_slice.ops()) {
    CHECK(!op.callee().starts_with("onnx."));
    if (zoo_calls.contains(op.callee()))
      seen_zoo_calls.emplace(op.callee());
  }
  CHECK(seen_zoo_calls == zoo_calls);
  joggle::Mod zoo_slice_roundtrip;
  CHECK(joggle::parse(env, joggle::print(zoo_slice), zoo_slice_roundtrip,
                      "zoo-slice-roundtrip.jog"));
  CHECK(zoo_slice_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(zoo_slice, zoo_slice_roundtrip));

  constexpr std::string_view symbolic_source =
      "module symbolic.bridge\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  left: tensor<f32, [N, 3]>, right: tensor<f32, [1, 3]>\n"
      ") -> tensor<f32, [N, 3]> {\n"
      "  let unknown = onnx.Unknown(left)\n"
      "  let pending: tensor<f32, [N, 3]> = onnx.Add(unknown, right)\n"
      "  let out: tensor<f32, [N, 3]> = onnx.Mul(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod symbolic;
  CHECK(joggle::parse(env, symbolic_source, symbolic, "symbolic-bridge.jog"));
  CHECK(symbolic.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", symbolic));
  CHECK(symbolic.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", symbolic));
  CHECK(symbolic.verify(env));
  std::size_t retained_binaries = 0;
  joggle::Op symbolic_mul;
  for (joggle::Op op : symbolic.ops()) {
    if (op.callee() == "onnx.Add" || op.callee() == "onnx.Mul")
      ++retained_binaries;
    if (op.callee() == "nn.mul")
      symbolic_mul = op;
  }
  CHECK(retained_binaries == 1);
  CHECK(symbolic_mul);
  const joggle::Fn symbolic_mul_fn = env.resolve(symbolic, symbolic_mul);
  CHECK(symbolic_mul_fn &&
        env.expand(symbolic, symbolic_mul, symbolic_mul_fn));
  CHECK(symbolic.verify(env));

  constexpr std::string_view qdq_source =
      "module qdq.shape\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 3]>, scale: tensor<f32, [3]>,\n"
      "  zero: tensor<u8, [3]>\n"
      ") -> tensor<f32, [N, 3]> {\n"
      "  [onnx: {axis: 1}]\n"
      "  let q = onnx.QuantizeLinear(x, scale, zero)\n"
      "  [onnx: {axis: 1}]\n"
      "  let out: tensor<f32, [N, 3]> = "
      "onnx.DequantizeLinear(q, scale, zero)\n"
      "  return out\n"
      "}\n";
  joggle::Mod qdq;
  CHECK(joggle::parse(env, qdq_source, qdq, "qdq-shape.jog"));
  CHECK(qdq.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", qdq));
  CHECK(qdq.verify(env));
  joggle::Op quant;
  joggle::Op dequant;
  for (joggle::Op op : qdq.ops()) {
    if (op.callee() == "onnx.QuantizeLinear")
      quant = op;
    if (op.callee() == "onnx.DequantizeLinear")
      dequant = op;
  }
  CHECK(quant && quant.outs()[0].type() ==
                     joggle::Ty("tensor<u8, [N, 3]>"));
  CHECK(dequant && dequant.outs()[0].type() ==
                       joggle::Ty("tensor<f32, [N, 3]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", qdq));
  CHECK(qdq.verify(env));
  CHECK(quant.callee() == "quant.quantize");
  CHECK(dequant.callee() == "quant.dequantize");
  const joggle::Fn quant_fn = env.resolve(qdq, quant);
  const joggle::Fn dequant_fn = env.resolve(qdq, dequant);
  CHECK(quant_fn && dequant_fn);
  CHECK(env.expand(qdq, quant, quant_fn));
  CHECK(env.expand(qdq, dequant, dequant_fn));
  CHECK(qdq.verify(env));
  joggle::Mod qdq_roundtrip;
  CHECK(joggle::parse(env, joggle::print(qdq), qdq_roundtrip,
                      "qdq-roundtrip.jog"));
  if (!qdq_roundtrip.verify(env))
    return env.print_diags(stderr);
  CHECK(joggle::structurally_equal(qdq, qdq_roundtrip));

  constexpr std::string_view dynamic_quant_source =
      "module dynamic.quant\n"
      "use onnx\n"
      "fn main(\n"
      "  a: tensor<f32, [2, 3]>, b: tensor<u8, [3, 4]>,\n"
      "  b_zero: tensor<u8, [4]>\n"
      ") -> tensor<f32, [2, 4]> {\n"
      "  let aq, scale, a_zero = onnx.DynamicQuantizeLinear(a)\n"
      "  let out = onnx.MatMulInteger(aq, b, a_zero, b_zero)\n"
      "  [onnx: {to: 1}]\n"
      "  let cast = onnx.Cast(out)\n"
      "  return cast\n"
      "}\n";
  joggle::Mod dynamic_quant;
  CHECK(joggle::parse(env, dynamic_quant_source, dynamic_quant,
                      "dynamic-quant.jog"));
  CHECK(dynamic_quant.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", dynamic_quant));
  CHECK(joggle::run(env, "onnx.nn.convert", dynamic_quant));
  CHECK(dynamic_quant.verify(env));
  joggle::Op dynamic_call;
  joggle::Op integer_matmul;
  joggle::Op cast;
  for (joggle::Op op : dynamic_quant.ops()) {
    if (op.callee() == "quant.dynamic")
      dynamic_call = op;
    if (op.callee() == "quant.matmul")
      integer_matmul = op;
    if (op.callee() == "tensor.cast")
      cast = op;
  }
  CHECK(dynamic_call && dynamic_call.outs().size() == 3);
  CHECK(integer_matmul && integer_matmul.outs()[0].type() ==
                              joggle::Ty("tensor<i32, [2, 4]>"));
  CHECK(cast && cast.outs()[0].type() ==
                    joggle::Ty("tensor<f32, [2, 4]>"));
  const joggle::Fn dynamic_fn = env.resolve(dynamic_quant, dynamic_call);
  const joggle::Fn integer_matmul_fn =
      env.resolve(dynamic_quant, integer_matmul);
  const joggle::Fn cast_fn = env.resolve(dynamic_quant, cast);
  CHECK(dynamic_fn && integer_matmul_fn && cast_fn);
  CHECK(env.expand(dynamic_quant, dynamic_call, dynamic_fn));
  CHECK(env.expand(dynamic_quant, integer_matmul, integer_matmul_fn));
  CHECK(env.expand(dynamic_quant, cast, cast_fn));
  CHECK(dynamic_quant.verify(env));
  joggle::Mod dynamic_quant_roundtrip;
  CHECK(joggle::parse(env, joggle::print(dynamic_quant),
                      dynamic_quant_roundtrip,
                      "dynamic-quant-roundtrip.jog"));
  CHECK(dynamic_quant_roundtrip.verify(env));
  CHECK(joggle::structurally_equal(dynamic_quant,
                                   dynamic_quant_roundtrip));

  constexpr std::string_view symbolic_conv_source =
      "module symbolic.conv\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 3, 8, 8]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>,\n"
      "  bias: tensor<f32, [4]>\n"
      ") -> tensor<f32, [N, 4, 1, 1]> {\n"
      "  [onnx: {auto_pad: \"NOTSET\", pads: [1, 1, 1, 1]}]\n"
      "  let convolved = onnx.Conv(x, weight, bias)\n"
      "  let out = onnx.GlobalAveragePool(convolved)\n"
      "  return out\n"
      "}\n";
  joggle::Mod symbolic_conv;
  CHECK(joggle::parse(env, symbolic_conv_source, symbolic_conv,
                      "symbolic-conv.jog"));
  CHECK(symbolic_conv.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", symbolic_conv));
  CHECK(symbolic_conv.verify(env));
  joggle::Op source_conv;
  for (joggle::Op op : symbolic_conv.ops())
    if (op.callee() == "onnx.Conv")
      source_conv = op;
  CHECK(source_conv && source_conv.outs()[0].type() ==
                           joggle::Ty("tensor<f32, [N, 4, 8, 8]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", symbolic_conv));
  CHECK(symbolic_conv.verify(env));
  joggle::Op semantic_conv;
  joggle::Op semantic_global_pool;
  for (joggle::Op op : symbolic_conv.ops()) {
    if (op.callee() == "nn.conv2d")
      semantic_conv = op;
    if (op.callee() == "nn.global_avg_pool2d")
      semantic_global_pool = op;
  }
  CHECK(semantic_conv && semantic_conv.args().size() == 11);
  CHECK(semantic_global_pool);
  const joggle::Fn symbolic_conv_fn = env.resolve(symbolic_conv, semantic_conv);
  CHECK(symbolic_conv_fn &&
        env.expand(symbolic_conv, semantic_conv, symbolic_conv_fn));
  CHECK(symbolic_conv.verify(env));
  const joggle::Fn symbolic_pool_fn =
      env.resolve(symbolic_conv, semantic_global_pool);
  CHECK(symbolic_pool_fn &&
        env.expand(symbolic_conv, semantic_global_pool, symbolic_pool_fn));
  CHECK(symbolic_conv.verify(env));

  constexpr std::string_view partial_conv_source =
      "module partial.conv\n"
      "use onnx\n"
      "fn main<N: int, C: int, H: int, W: int>(\n"
      "  x: tensor<f32, [N, C, H, W]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>\n"
      ") -> tensor<f32, [N, 4, _, _]> {\n"
      "  [onnx: {pads: [0, 0, 1, 1], strides: [2, 2]}]\n"
      "  let out = onnx.Conv(x, weight)\n"
      "  return out\n"
      "}\n";
  joggle::Mod partial_conv;
  CHECK(joggle::parse(env, partial_conv_source, partial_conv,
                      "partial-conv.jog"));
  CHECK(partial_conv.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", partial_conv));
  CHECK(partial_conv.verify(env));
  joggle::Op partial_call;
  for (joggle::Op op : partial_conv.ops())
    if (op.callee() == "onnx.Conv")
      partial_call = op;
  CHECK(partial_call && partial_call.outs()[0].type() ==
                            joggle::Ty("tensor<f32, [N, 4, _, _]>"));

  constexpr std::string_view resize_source =
      "module resize.shape\n"
      "use onnx\n"
      "fn main<N: int, H: int, W: int>(\n"
      "  x: tensor<f32, [N, 3, H, W]>\n"
      ") -> tensor<f32, [1, 3, 300, 300]> {\n"
      "  let roi: tensor<f32, [0]> = onnx.tensor(1, [0], hex\"\")\n"
      "  let scales: tensor<f32, [0]> = onnx.tensor(1, [0], hex\"\")\n"
      "  let sizes: tensor<i64, [4]> = onnx.tensor(\n"
      "    7, [4],\n"
      "    hex\"01000000000000000300000000000000"
      "2c010000000000002c01000000000000\"\n"
      "  )\n"
      "  let out = onnx.Resize(x, roi, scales, sizes)\n"
      "  return out\n"
      "}\n";
  joggle::Mod resize;
  CHECK(joggle::parse(env, resize_source, resize, "resize-shape.jog"));
  CHECK(resize.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", resize));
  CHECK(resize.verify(env));
  joggle::Op resize_call;
  for (joggle::Op op : resize.ops())
    if (op.callee() == "onnx.Resize")
      resize_call = op;
  CHECK(resize_call && resize_call.outs()[0].type() ==
                           joggle::Ty("tensor<f32, [1, 3, 300, 300]>"));

  constexpr std::string_view shape_relations_source =
      "module shape.relations\n"
      "use onnx\n"
      "fn nonzero(x: tensor<bool, [2, 3]>) -> tensor<i64, [2, _]> {\n"
      "  let out = onnx.NonZero(x)\n"
      "  return out\n"
      "}\n"
      "fn range(\n"
      "  start: tensor<f32, []>, stop: tensor<f32, []>,\n"
      "  step: tensor<f32, []>\n"
      ") -> tensor<f32, [_]> {\n"
      "  let out = onnx.Range(start, stop, step)\n"
      "  return out\n"
      "}\n"
      "fn nms(\n"
      "  boxes: tensor<f32, [1, 10, 4]>,\n"
      "  scores: tensor<f32, [1, 2, 10]>\n"
      ") -> tensor<i64, [_, 3]> {\n"
      "  let out = onnx.NonMaxSuppression(boxes, scores)\n"
      "  return out\n"
      "}\n"
      "fn expand(x: tensor<f32, [1, 3]>) -> tensor<f32, [2, 3]> {\n"
      "  let shape: tensor<i64, [2]> = onnx.tensor(\n"
      "    7, [2], hex\"02000000000000000300000000000000\"\n"
      "  )\n"
      "  let out = onnx.Expand(x, shape)\n"
      "  return out\n"
      "}\n"
      "fn tile(x: tensor<f32, [1, 3]>) -> tensor<f32, [2, 3]> {\n"
      "  let repeats: tensor<i64, [2]> = onnx.tensor(\n"
      "    7, [2], hex\"02000000000000000100000000000000\"\n"
      "  )\n"
      "  let out = onnx.Tile(x, repeats)\n"
      "  return out\n"
      "}\n"
      "fn topk(x: tensor<f32, [2, 5]>) "
      "-> (tensor<f32, [2, 3]>, tensor<i64, [2, 3]>) {\n"
      "  let count: tensor<i64, [1]> = onnx.tensor(\n"
      "    7, [1], hex\"0300000000000000\"\n"
      "  )\n"
      "  let values, indices = onnx.TopK(x, count)\n"
      "  return values, indices\n"
      "}\n"
      "fn dynamic_reshape(\n"
      "  x: tensor<f32, [2, 3, 4]>, empty: tensor<i64, [0]>,\n"
      "  shape: tensor<i64, [2]>\n"
      ") -> tensor<f32, [_, _]> {\n"
      "  [onnx: {axis: 0}]\n"
      "  let joined = onnx.Concat(empty, shape)\n"
      "  let out = onnx.Reshape(x, joined)\n"
      "  return out\n"
      "}\n"
      "fn dynamic_tile(\n"
      "  x: tensor<f32, [1, 3, 4]>, repeats: tensor<i64, [3]>\n"
      ") -> tensor<f32, [_, _, _]> {\n"
      "  let out = onnx.Tile(x, repeats)\n"
      "  return out\n"
      "}\n"
      "fn partial_slice(\n"
      "  x: tensor<f32, [_, 1917, 91]>\n"
      ") -> tensor<f32, [_, 1917, 90]> {\n"
      "  let starts: tensor<i64, [3]> = onnx.tensor(\n"
      "    7, [3],\n"
      "    hex\"00000000000000000000000000000000"
      "0100000000000000\"\n"
      "  )\n"
      "  let ends: tensor<i64, [3]> = onnx.tensor(\n"
      "    7, [3],\n"
      "    hex\"ffffffffffffff7fffffffffffffff7f"
      "ffffffffffffff7f\"\n"
      "  )\n"
      "  let out = onnx.Slice(x, starts, ends)\n"
      "  return out\n"
      "}\n"
      "fn dynamic_slice(\n"
      "  x: tensor<f32, [2, 3, 4]>, starts: tensor<i32, [2]>,\n"
      "  ends: tensor<i32, [2]>\n"
      ") -> tensor<f32, [_, _, 4]> {\n"
      "  let out = onnx.Slice(x, starts, ends)\n"
      "  return out\n"
      "}\n"
      "fn dynamic_topk(\n"
      "  x: tensor<f32, [2, 5]>, count: tensor<i64, [1]>\n"
      ") -> (tensor<f32, [2, _]>, tensor<i64, [2, _]>) {\n"
      "  let values, indices = onnx.TopK(x, count)\n"
      "  return values, indices\n"
      "}\n"
      "fn dropout(\n"
      "  x: tensor<f32, [2, 3]>, ratio: tensor<f32, []>\n"
      ") -> (tensor<f32, [2, 3]>, tensor<bool, [2, 3]>) {\n"
      "  let value, mask = onnx.Dropout(x, ratio)\n"
      "  return value, mask\n"
      "}\n";
  joggle::Mod shape_relations;
  CHECK(joggle::parse(env, shape_relations_source, shape_relations,
                      "shape-relations.jog"));
  CHECK(shape_relations.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", shape_relations));
  CHECK(shape_relations.verify(env));
  for (joggle::Op op : shape_relations.ops()) {
    if (op.callee() == "onnx.NonZero")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<i64, [2, _]>"));
    if (op.callee() == "onnx.Range")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [_]>"));
    if (op.callee() == "onnx.NonMaxSuppression")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<i64, [_, 3]>"));
    if (op.callee() == "onnx.Expand" ||
        (op.callee() == "onnx.Tile" && op.blk().fn().name() == "tile"))
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [2, 3]>"));
    if (op.callee() == "onnx.TopK") {
      if (op.blk().fn().name() == "topk") {
        CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [2, 3]>"));
        CHECK(op.outs()[1].type() == joggle::Ty("tensor<i64, [2, 3]>"));
      }
      if (op.blk().fn().name() == "dynamic_topk") {
        CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [2, _]>"));
        CHECK(op.outs()[1].type() == joggle::Ty("tensor<i64, [2, _]>"));
      }
    }
    if (op.callee() == "onnx.Dropout" &&
        op.blk().fn().name() == "dropout") {
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [2, 3]>"));
      CHECK(op.outs()[1].type() == joggle::Ty("tensor<bool, [2, 3]>"));
    }
    if (op.callee() == "onnx.Reshape" &&
        op.blk().fn().name() == "dynamic_reshape")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [_, _]>"));
    if (op.callee() == "onnx.Tile" &&
        op.blk().fn().name() == "dynamic_tile")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [_, _, _]>"));
    if (op.callee() == "onnx.Slice" &&
        op.blk().fn().name() == "partial_slice")
      CHECK(op.outs()[0].type() ==
            joggle::Ty("tensor<f32, [_, 1917, 90]>"));
    if (op.callee() == "onnx.Slice" &&
        op.blk().fn().name() == "dynamic_slice")
      CHECK(op.outs()[0].type() == joggle::Ty("tensor<f32, [_, _, 4]>"));
  }

  constexpr std::string_view invalid_conv_source =
      "module invalid.conv\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 3, 8, 8]>,\n"
      "  weight: tensor<f32, [4, 3, 3, 3]>,\n"
      "  bias: tensor<f32, [5]>\n"
      ") -> tensor<f32, [N, 4, 8, 8]> {\n"
      "  let out: tensor<f32, [N, 4, 8, 8]> = "
      "onnx.Conv(x, weight, bias)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid_conv;
  CHECK(joggle::parse(env, invalid_conv_source, invalid_conv,
                      "invalid-conv.jog"));
  CHECK(invalid_conv.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", invalid_conv));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid_conv));
  CHECK(invalid_conv.verify(env));
  bool retained_conv = false;
  for (joggle::Op op : invalid_conv.ops())
    retained_conv = retained_conv || op.callee() == "onnx.Conv";
  CHECK(retained_conv);

  constexpr std::string_view symbolic_matrix_source =
      "module symbolic.matrix\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 2, 3]>, w: tensor<f32, [6, 4]>\n"
      ") -> tensor<f32, [4, N]> {\n"
      "  [onnx: {axis: 1}]\n"
      "  let flat = onnx.Flatten(x)\n"
      "  let product = onnx.MatMul(flat, w)\n"
      "  [onnx: {perm: [1, 0]}]\n"
      "  let out = onnx.Transpose(product)\n"
      "  return out\n"
      "}\n";
  joggle::Mod symbolic_matrix;
  CHECK(joggle::parse(env, symbolic_matrix_source, symbolic_matrix,
                      "symbolic-matrix.jog"));
  CHECK(symbolic_matrix.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", symbolic_matrix));
  CHECK(symbolic_matrix.verify(env));
  CHECK(symbolic_matrix.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [4, N]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", symbolic_matrix));
  CHECK(symbolic_matrix.verify(env));
  std::size_t symbolic_matrix_calls = 0;
  for (joggle::Op op : symbolic_matrix.ops()) {
    if (op.callee() != "tensor.reshape" &&
        op.callee() != "tensor.matmul" &&
        op.callee() != "tensor.permute")
      continue;
    const joggle::Fn fn = env.resolve(symbolic_matrix, op);
    CHECK(fn && env.expand(symbolic_matrix, op, fn));
    ++symbolic_matrix_calls;
  }
  CHECK(symbolic_matrix_calls == 3 && symbolic_matrix.verify(env));

  constexpr std::string_view shape_program_source =
      "module shape.program\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 3, 4]>) "
      "-> tensor<f32, [N, 12]> {\n"
      "  let inferred_target: tensor<i64, [3]> = onnx.tensor(\n"
      "    7, [3], "
      "hex\"ffffffffffffffff03000000000000000400000000000000\"\n"
      "  )\n"
      "  let expanded = onnx.Reshape(x, inferred_target)\n"
      "  let shape = onnx.Shape(expanded)\n"
      "  let index: tensor<i64, []> = "
      "onnx.tensor(7, [], hex\"0000000000000000\")\n"
      "  [onnx: {axis: 0}]\n"
      "  let batch = onnx.Gather(shape, index)\n"
      "  [onnx: {axes: [0]}]\n"
      "  let vector = onnx.Unsqueeze(batch)\n"
      "  let tail_data: tensor<i64, [2]> = onnx.tensor(\n"
      "    7, [2], hex\"0c000000000000006300000000000000\"\n"
      "  )\n"
      "  let start: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0000000000000000\")\n"
      "  let end: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0100000000000000\")\n"
      "  let axis: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0000000000000000\")\n"
      "  let tail = onnx.Slice(tail_data, start, end, axis)\n"
      "  [place: \"edge\", schedule: {width: 4}, onnx: {axis: 0}]\n"
      "  let target = onnx.Concat(vector, tail)\n"
      "  [onnx: {to: 7}]\n"
      "  let cast = onnx.Cast(target)\n"
      "  let out = onnx.Reshape(expanded, cast)\n"
      "  return out\n"
      "}\n";
  joggle::Mod shape_program;
  CHECK(joggle::parse(env, shape_program_source, shape_program,
                      "shape-program.jog"));
  CHECK(shape_program.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", shape_program));
  CHECK(shape_program.verify(env));
  CHECK(shape_program.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [N, 12]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", shape_program));
  CHECK(shape_program.verify(env));
  joggle::Op semantic_reshape;
  std::vector<joggle::Op> structure_calls;
  for (joggle::Op op : shape_program.ops()) {
    if (op.callee() == "tensor.reshape")
      semantic_reshape = op;
    if (op.callee() == "tensor.shape" ||
        op.callee() == "tensor.gather" ||
        op.callee() == "tensor.slice" ||
        op.callee() == "tensor.concat")
      structure_calls.push_back(op);
    if (op.callee() == "tensor.concat") {
      CHECK(!op.meta("onnx"));
      CHECK(op.meta("place") && op.meta("place")->string() == "edge");
      CHECK(op.meta("schedule") && op.meta("schedule")->dict());
      CHECK(shape_program.unset(op, "place"));
      CHECK(shape_program.unset(op, "schedule"));
    }
    CHECK(op.callee() != "onnx.Shape" && op.callee() != "onnx.Gather" &&
          op.callee() != "onnx.Slice" && op.callee() != "onnx.Concat" &&
          op.callee() != "onnx.Unsqueeze");
  }
  CHECK(structure_calls.size() == 4);
  for (joggle::Op op : structure_calls) {
    const joggle::Fn fn = env.resolve(shape_program, op);
    CHECK(fn && env.expand(shape_program, op, fn));
  }
  CHECK(shape_program.verify(env));
  CHECK(semantic_reshape && semantic_reshape.args().size() == 1);
  const joggle::Fn shape_reshape_fn =
      env.resolve(shape_program, semantic_reshape);
  CHECK(shape_reshape_fn &&
        env.expand(shape_program, semantic_reshape, shape_reshape_fn));
  CHECK(shape_program.verify(env));

  constexpr std::string_view transformer_source =
      "module transformer.shape\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  query: tensor<f32, [N, 12, 256, 64]>,\n"
      "  key: tensor<f32, [N, 12, 256, 64]>\n"
      ") -> tensor<f32, [N, 12, 256, 256]> {\n"
      "  let shape = onnx.Shape(query)\n"
      "  [onnx: {value: {data: hex\"0000803f\", shape: [1], type: 1}}]\n"
      "  let filled: tensor<f32, [_, 12, 256, 64]> = "
      "onnx.ConstantOfShape(shape)\n"
      "  [onnx: {alpha: 0.125, transA: 0, transB: 1}]\n"
      "  let scores = com_microsoft.FusedMatMul(query, key)\n"
      "  return scores\n"
      "}\n";
  joggle::Mod transformer;
  CHECK(joggle::parse(env, transformer_source, transformer,
                      "transformer-shape.jog"));
  CHECK(transformer.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", transformer));
  CHECK(transformer.verify(env));
  joggle::Op filled;
  joggle::Op scores;
  for (joggle::Op op : transformer.ops()) {
    if (op.callee() == "onnx.ConstantOfShape")
      filled = op;
    if (op.callee() == "com_microsoft.FusedMatMul")
      scores = op;
  }
  CHECK(filled && filled.outs()[0].type() ==
                      joggle::Ty("tensor<f32, [N, 12, 256, 64]>"));
  CHECK(scores && scores.outs()[0].type() ==
                      joggle::Ty("tensor<f32, [N, 12, 256, 256]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", transformer));
  CHECK(transformer.verify(env));
  CHECK(scores.callee() == "tensor.matmul");
  joggle::Op semantic_fill;
  for (joggle::Op op : transformer.ops())
    if (op.callee() == "tensor.fill")
      semantic_fill = op;
  CHECK(semantic_fill && semantic_fill.outs()[0].type() ==
                             joggle::Ty(
                                 "tensor<f32, [N, 12, 256, 64]>"));
  const joggle::Fn fill_fn = env.resolve(transformer, semantic_fill);
  CHECK(fill_fn && env.expand(transformer, semantic_fill, fill_fn));
  CHECK(transformer.verify(env));
  const joggle::Fn scaled_matmul = env.resolve(transformer, scores);
  CHECK(scaled_matmul && env.expand(transformer, scores, scaled_matmul));
  CHECK(transformer.verify(env));

  constexpr std::string_view default_fill_source =
      "module default.fill\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 4]>) "
      "-> tensor<f32, [N, 4]> {\n"
      "  let shape = onnx.Shape(x)\n"
      "  let out = onnx.ConstantOfShape(shape)\n"
      "  return out\n"
      "}\n";
  joggle::Mod default_fill;
  CHECK(joggle::parse(env, default_fill_source, default_fill,
                      "default-fill.jog"));
  CHECK(default_fill.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", default_fill));
  CHECK(joggle::run(env, "onnx.nn.convert", default_fill));
  CHECK(default_fill.verify(env));
  bool default_tensor = false;
  for (joggle::Op op : default_fill.ops()) {
    CHECK(op.callee() != "onnx.Shape" &&
          op.callee() != "onnx.ConstantOfShape");
    default_tensor = default_tensor || op.callee() == "tensor.tensor";
  }
  CHECK(default_tensor);

  constexpr std::string_view split_source =
      "module split.shape\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [2, N, 256]>) "
      "-> (tensor<f32, [1, N, 256]>, tensor<f32, [1, N, 256]>) {\n"
      "  [onnx: {axis: 0}]\n"
      "  let left, right = onnx.Split(x)\n"
      "  return left, right\n"
      "}\n";
  joggle::Mod split;
  CHECK(joggle::parse(env, split_source, split, "split-shape.jog"));
  CHECK(split.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", split));
  CHECK(split.verify(env));
  joggle::Op split_call;
  for (joggle::Op op : split.ops())
    if (op.callee() == "onnx.Split")
      split_call = op;
  CHECK(split_call && split_call.outs().size() == 2);
  for (joggle::Val value : split_call.outs())
    CHECK(value.type() == joggle::Ty("tensor<f32, [1, N, 256]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", split));
  CHECK(split.verify(env));
  std::vector<joggle::Op> split_slices;
  for (joggle::Op op : split.ops()) {
    CHECK(op.callee() != "onnx.Split");
    if (op.callee() == "tensor.slice")
      split_slices.push_back(op);
  }
  CHECK(split_slices.size() == 2);
  CHECK(split_slices[0].outs()[0].name() == "left");
  CHECK(split_slices[1].outs()[0].name() == "right");
  for (joggle::Op op : split_slices) {
    const joggle::Fn fn = env.resolve(split, op);
    CHECK(fn && env.expand(split, op, fn));
  }
  CHECK(split.verify(env));

  constexpr std::string_view symbolic_split_source =
      "module symbolic.split\n"
      "use onnx\n"
      "fn main<N: int, K: int>(x: tensor<f32, [N, K]>) "
      "-> tensor<f32, [N]> {\n"
      "  [onnx: {axis: 1}]\n"
      "  let first, second, third = onnx.Split(x)\n"
      "  [onnx: {axes: [1]}]\n"
      "  let out = onnx.Squeeze(first)\n"
      "  return out\n"
      "}\n";
  joggle::Mod symbolic_split;
  CHECK(joggle::parse(env, symbolic_split_source, symbolic_split,
                      "symbolic-split.jog"));
  CHECK(symbolic_split.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", symbolic_split));
  CHECK(symbolic_split.verify(env));
  joggle::Op symbolic_split_call;
  joggle::Op symbolic_squeeze_call;
  for (joggle::Op op : symbolic_split.ops()) {
    if (op.callee() == "onnx.Split")
      symbolic_split_call = op;
    if (op.callee() == "onnx.Squeeze")
      symbolic_squeeze_call = op;
  }
  CHECK(symbolic_split_call && symbolic_split_call.outs().size() == 3);
  for (joggle::Val value : symbolic_split_call.outs())
    CHECK(value.type() == joggle::Ty("tensor<f32, [N, _]>"));
  CHECK(symbolic_squeeze_call && symbolic_squeeze_call.outs()[0].type() ==
                                     joggle::Ty("tensor<f32, [N]>"));

  constexpr std::string_view one_hot_source =
      "module one.hot\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<i64, [N]>) "
      "-> tensor<f32, [N, 3]> {\n"
      "  let depth: tensor<i64, [1]> = "
      "onnx.tensor(7, [1], hex\"0300000000000000\")\n"
      "  let values: tensor<f32, [2]> = "
      "onnx.tensor(1, [2], hex\"000000000000803f\")\n"
      "  [onnx: {axis: -1}]\n"
      "  let out = onnx.OneHot(x, depth, values)\n"
      "  return out\n"
      "}\n";
  joggle::Mod one_hot;
  CHECK(joggle::parse(env, one_hot_source, one_hot, "one-hot.jog"));
  CHECK(one_hot.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", one_hot));
  CHECK(joggle::run(env, "onnx.nn.convert", one_hot));
  CHECK(one_hot.verify(env));
  joggle::Op semantic_one_hot;
  for (joggle::Op op : one_hot.ops())
    if (op.callee() == "tensor.one_hot")
      semantic_one_hot = op;
  CHECK(semantic_one_hot && semantic_one_hot.args().size() == 4);
  const joggle::Fn one_hot_fn = env.resolve(one_hot, semantic_one_hot);
  CHECK(one_hot_fn &&
        env.expand(one_hot, semantic_one_hot, one_hot_fn));
  CHECK(one_hot.verify(env));

  constexpr std::string_view invalid_reshape_source =
      "module invalid.reshape\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [2, 3]>) -> tensor<f32, [5, 5]> {\n"
      "  let shape: tensor<i64, [2]> = onnx.tensor(\n"
      "    7, [2], hex\"05000000000000000500000000000000\"\n"
      "  )\n"
      "  let out: tensor<f32, [5, 5]> = onnx.Reshape(x, shape)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid_reshape;
  CHECK(joggle::parse(env, invalid_reshape_source, invalid_reshape,
                      "invalid-reshape.jog"));
  CHECK(invalid_reshape.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", invalid_reshape));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid_reshape));
  bool retained_reshape = false;
  for (joggle::Op op : invalid_reshape.ops())
    retained_reshape = retained_reshape || op.callee() == "onnx.Reshape";
  CHECK(retained_reshape && invalid_reshape.verify(env));

  constexpr std::string_view batched_matmul_source =
      "module batched.matmul\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  left: tensor<f32, [N, 3, 4]>,\n"
      "  right: tensor<f32, [2, 1, 4, 5]>\n"
      ") -> tensor<f32, [2, N, 3, 5]> {\n"
      "  let out = onnx.MatMul(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod batched_matmul;
  CHECK(joggle::parse(env, batched_matmul_source, batched_matmul,
                      "batched-matmul.jog"));
  CHECK(batched_matmul.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", batched_matmul));
  CHECK(batched_matmul.verify(env));
  joggle::Op source_batched_matmul;
  for (joggle::Op op : batched_matmul.ops())
    if (op.callee() == "onnx.MatMul")
      source_batched_matmul = op;
  CHECK(source_batched_matmul && source_batched_matmul.outs()[0].type() ==
                                       joggle::Ty(
                                           "tensor<f32, [2, N, 3, 5]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", batched_matmul));
  CHECK(batched_matmul.verify(env));
  joggle::Op semantic_batched_matmul;
  for (joggle::Op op : batched_matmul.ops())
    if (op.callee() == "tensor.matmul")
      semantic_batched_matmul = op;
  CHECK(semantic_batched_matmul);
  const joggle::Fn batched_matmul_fn =
      env.resolve(batched_matmul, semantic_batched_matmul);
  CHECK(batched_matmul_fn &&
        env.expand(batched_matmul, semantic_batched_matmul,
                   batched_matmul_fn));
  CHECK(batched_matmul.verify(env));

  constexpr std::string_view mixed_matmul_source =
      "module mixed.matmul\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  left: tensor<f32, [3, 4]>,\n"
      "  right: tensor<f32, [N, 4, 5]>\n"
      ") -> tensor<f32, [N, 3, 5]> {\n"
      "  return onnx.MatMul(left, right)\n"
      "}\n";
  joggle::Mod mixed_matmul;
  CHECK(joggle::parse(env, mixed_matmul_source, mixed_matmul,
                      "mixed-matmul.jog"));
  CHECK(mixed_matmul.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", mixed_matmul));
  CHECK(joggle::run(env, "onnx.nn.convert", mixed_matmul));
  CHECK(mixed_matmul.verify(env));
  joggle::Op mixed_call;
  for (joggle::Op op : mixed_matmul.ops())
    if (op.callee() == "tensor.matmul")
      mixed_call = op;
  CHECK(mixed_call && mixed_call.outs()[0].type() ==
                          joggle::Ty("tensor<f32, [N, 3, 5]>"));

  constexpr std::string_view softmax_source =
      "module axis.softmax\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 2, 3]>) "
      "-> tensor<f32, [N, 2, 3]> {\n"
      "  onnx.model({opsets: [{domain: \"\", version: 13}]})\n"
      "  [onnx: {axis: 1}]\n"
      "  let out = onnx.Softmax(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod softmax;
  CHECK(joggle::parse(env, softmax_source, softmax, "axis-softmax.jog"));
  CHECK(softmax.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", softmax));
  CHECK(joggle::run(env, "onnx.nn.convert", softmax));
  CHECK(softmax.verify(env));
  joggle::Op semantic_softmax;
  for (joggle::Op op : softmax.ops())
    if (op.callee() == "nn.softmax")
      semantic_softmax = op;
  CHECK(semantic_softmax && semantic_softmax.args().size() == 3);
  CHECK(semantic_softmax.args()[1].constant().integer() == 1);
  const joggle::Fn softmax_fn = env.resolve(softmax, semantic_softmax);
  CHECK(softmax_fn && env.expand(softmax, semantic_softmax, softmax_fn));
  CHECK(softmax.verify(env));

  constexpr std::string_view legacy_softmax_source =
      "module legacy.softmax\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 2, 3]>) "
      "-> tensor<f32, [N, 2, 3]> {\n"
      "  onnx.model({opsets: [{domain: \"\", version: 12}]})\n"
      "  let out = onnx.Softmax(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod legacy_softmax;
  CHECK(joggle::parse(env, legacy_softmax_source, legacy_softmax,
                      "legacy-softmax.jog"));
  CHECK(legacy_softmax.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", legacy_softmax));
  CHECK(joggle::run(env, "onnx.nn.convert", legacy_softmax));
  CHECK(legacy_softmax.verify(env));
  joggle::Op legacy_call;
  for (joggle::Op op : legacy_softmax.ops())
    if (op.callee() == "nn.softmax")
      legacy_call = op;
  CHECK(legacy_call && legacy_call.args().size() == 3);
  CHECK(legacy_call.args()[1].type() == joggle::Ty("list<int>"));
  const joggle::Fn legacy_fn = env.resolve(legacy_softmax, legacy_call);
  CHECK(legacy_fn && env.expand(legacy_softmax, legacy_call, legacy_fn));
  CHECK(legacy_softmax.verify(env));

  constexpr std::string_view mean_source =
      "module axis.mean\n"
      "use onnx\n"
      "fn main<N: int>(x: tensor<f32, [N, 2, 3, 4]>) "
      "-> tensor<f32, [N, 3]> {\n"
      "  [onnx: {axes: [1, -1], keepdims: 0}]\n"
      "  let out = onnx.ReduceMean(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod mean;
  CHECK(joggle::parse(env, mean_source, mean, "axis-mean.jog"));
  CHECK(mean.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", mean));
  CHECK(mean.verify(env));
  CHECK(mean.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [N, 3]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", mean));
  CHECK(mean.verify(env));
  joggle::Op semantic_mean;
  for (joggle::Op op : mean.ops())
    if (op.callee() == "tensor.mean")
      semantic_mean = op;
  CHECK(semantic_mean && semantic_mean.args().size() == 2);
  const joggle::Fn mean_fn = env.resolve(mean, semantic_mean);
  CHECK(mean_fn && env.expand(mean, semantic_mean, mean_fn));
  CHECK(mean.verify(env));

  constexpr std::string_view invalid_mean_source =
      "module invalid.mean\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [2, 3]>) -> tensor<f32, [2, 1]> {\n"
      "  [onnx: {axes: [1, 1]}]\n"
      "  let out: tensor<f32, [2, 1]> = onnx.ReduceMean(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid_mean;
  CHECK(joggle::parse(env, invalid_mean_source, invalid_mean,
                      "invalid-mean.jog"));
  CHECK(invalid_mean.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", invalid_mean));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid_mean));
  bool retained_mean = false;
  for (joggle::Op op : invalid_mean.ops())
    retained_mean = retained_mean || op.callee() == "onnx.ReduceMean";
  CHECK(retained_mean && invalid_mean.verify(env));

  constexpr std::string_view norm_source =
      "module norm.chain\n"
      "use onnx\n"
      "fn main<N: int>(\n"
      "  x: tensor<f32, [N, 2, 4]>, epsilon: tensor<f32, [1]>,\n"
      "  exponent: tensor<f32, [1]>\n"
      ") -> tensor<f32, [N, 2, 4]> {\n"
      "  [onnx: {axes: [-1], keepdims: 1}]\n"
      "  let mean = onnx.ReduceMean(x)\n"
      "  let centered = onnx.Sub(x, mean)\n"
      "  let squared = onnx.Mul(centered, centered)\n"
      "  [onnx: {axes: [-1], keepdims: 1}]\n"
      "  let variance = onnx.ReduceMean(squared)\n"
      "  let shifted = onnx.Add(variance, epsilon)\n"
      "  let scale = onnx.Sqrt(shifted)\n"
      "  let normalized = onnx.Div(centered, scale)\n"
      "  let powered = onnx.Pow(normalized, exponent)\n"
      "  let out = onnx.Tanh(powered)\n"
      "  return out\n"
      "}\n";
  joggle::Mod norm;
  CHECK(joggle::parse(env, norm_source, norm, "norm-chain.jog"));
  CHECK(norm.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", norm));
  CHECK(norm.verify(env));
  CHECK(norm.find_fn("main").body().ops().back().args()[0].type() ==
        joggle::Ty("tensor<f32, [N, 2, 4]>"));
  CHECK(joggle::run(env, "onnx.nn.convert", norm));
  CHECK(norm.verify(env));
  const std::string norm_once = joggle::print(norm);
  CHECK(joggle::run(env, "onnx.nn.convert", norm));
  CHECK(joggle::print(norm) == norm_once);
  std::vector<joggle::Op> norm_calls;
  for (joggle::Op op : norm.ops()) {
    const std::string_view callee = op.callee();
    if (callee == "tensor.mean" || callee == "nn.sub" ||
        callee == "nn.mul" || callee == "nn.add" ||
        callee == "nn.sqrt" || callee == "nn.div" ||
        callee == "nn.pow" || callee == "nn.tanh")
      norm_calls.push_back(op);
  }
  CHECK(norm_calls.size() == 9);
  for (joggle::Op op : norm_calls) {
    const joggle::Fn fn = env.resolve(norm, op);
    CHECK(fn && env.expand(norm, op, fn));
  }
  CHECK(norm.verify(env));

  constexpr std::string_view implicit_softmax_source =
      "module implicit.softmax\n"
      "use onnx\n"
      "fn main(x: tensor<f32, [2, 3]>) -> tensor<f32, [2, 3]> {\n"
      "  let out: tensor<f32, [2, 3]> = onnx.Softmax(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod implicit_softmax;
  CHECK(joggle::parse(env, implicit_softmax_source, implicit_softmax,
                      "implicit-softmax.jog"));
  CHECK(implicit_softmax.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", implicit_softmax));
  bool retained_softmax = false;
  for (joggle::Op op : implicit_softmax.ops())
    retained_softmax = retained_softmax || op.callee() == "onnx.Softmax";
  CHECK(retained_softmax && implicit_softmax.verify(env));

  constexpr std::string_view unresolved_shape_source =
      "module unresolved.shape\n"
      "use onnx\n"
      "fn main<N: int, M: int>(\n"
      "  x: tensor<f32, [N, M, 3]>\n"
      ") -> tensor<f32, [_, 3]> {\n"
      "  [onnx: {axis: 2}]\n"
      "  let out: tensor<f32, [_, 3]> = onnx.Flatten(x)\n"
      "  return out\n"
      "}\n";
  joggle::Mod unresolved_shape;
  CHECK(joggle::parse(env, unresolved_shape_source, unresolved_shape,
                      "unresolved-shape.jog"));
  CHECK(unresolved_shape.verify(env));
  CHECK(joggle::run(env, "onnx.nn.infer", unresolved_shape));
  CHECK(joggle::run(env, "onnx.nn.convert", unresolved_shape));
  CHECK(unresolved_shape.verify(env));
  bool retained_flatten = false;
  for (joggle::Op op : unresolved_shape.ops())
    retained_flatten = retained_flatten || op.callee() == "onnx.Flatten";
  CHECK(retained_flatten);

  constexpr std::string_view invalid_source =
      "module invalid.broadcast\n"
      "use onnx\n"
      "fn main(\n"
      "  left: tensor<f32, [2, 3]>, right: tensor<f32, [4, 1]>\n"
      ") -> tensor<f32, [2, 3]> {\n"
      "  let out: tensor<f32, [2, 3]> = onnx.Add(left, right)\n"
      "  return out\n"
      "}\n";
  joggle::Mod invalid;
  CHECK(joggle::parse(env, invalid_source, invalid, "invalid-broadcast.jog"));
  CHECK(invalid.verify(env));
  CHECK(joggle::run(env, "onnx.nn.convert", invalid));
  CHECK(invalid.verify(env));
  bool retained = false;
  for (joggle::Op op : invalid.ops()) {
    retained = retained || op.callee() == "onnx.Add";
    CHECK(op.callee() != "nn.add");
  }
  CHECK(retained);
  return 0;
}
