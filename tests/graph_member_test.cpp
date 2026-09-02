#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

constexpr std::string_view source = R"(
joggle 1;

module logic@1.0.0 {
  type word(width: i64);
  type tensor(element: type, shape: list<i64>);
  attr payload(scale: f64, labels: list<string>, element: type);

  op source<T: type>(name: string, meta: attr) -> T;
  op identity<T: type>(input: T) -> T;
  op add<T: type>(lhs: T, rhs: T) -> T;
  op scope<T: type>(body: region, tag: string) -> T;
  op branch<T: type>(left: region, right: region) -> T;

  pass simplify {
    identity($input) => $input;
  }

  graph main(%lhs: tensor<word<8>, [2, 4]>) -> tensor<word<8>, [2, 4]> {
    %input: tensor<word<8>, [2, 4]> = source(
      name = "input } // still a string",
      meta = payload<1.5, ["alpha", "beta"], word<8>>
    );
    %sum = add(%lhs, %input);
    return %sum;
  }

  graph structured(%x: tensor<word<8>, [2, 4]>) -> tensor<word<8>, [2, 4]> {
    %scoped: tensor<word<8>, [2, 4]> = scope(tag = "nested") {
      body(%nested: tensor<word<8>, [2, 4]>) {
        %sum = add(%nested, %nested);
      }
    };
    return %scoped;
  }

  graph lexical(%item: word<8>, %outer: word<8>) -> word<8> {
    %result: word<8> = branch() {
      left(%item: word<8>) {
        %local = add(%item, %outer);
      }
      right(%item: word<8>) {
        %local = add(%item, %outer);
      }
    };
    return %result;
  }

  graph nested_pass(%outer: word<8>) -> word<8> {
    %result: word<8> = scope(tag = "pass") {
      body {
        %first = identity(%outer);
        %second = identity(%first);
      }
    };
    return %result;
  }
}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::optional<joggle::Graph> load_graph(joggle::Compiler& compiler,
                                        std::string_view text) {
  compiler.add(text, "logic.joggle");
  if (!compiler.link()) {
    return std::nullopt;
  }
  return compiler.graph("logic.main");
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  const auto graph = load_graph(compiler, source);
  if (!graph) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto module = compiler.module("logic");
  const auto operations = graph->operations();
  bool ok = true;
  ok &= expect(module.has_value(), "the graph owner remains available");
  const auto main_symbol =
      module ? module->symbol(joggle::Module::SymbolKind::Graph, "main")
             : std::nullopt;
  const auto reflected_graph =
      main_symbol ? compiler.graph(*main_symbol) : std::nullopt;
  ok &= expect(reflected_graph && reflected_graph->operations().size() == 2U,
               "a reflected graph symbol opens without rebuilding its name");
  ok &= expect(graph->inputs().size() == 1U && operations.size() == 2U &&
                   graph->outputs().size() == 1U &&
                   graph->outputs().front() == operations.back().result(0) &&
                   operations.back().result(0).type() ==
                       graph->inputs().front().type(),
               "type variables infer a graph operation and its SSA boundary");
  ok &= expect(operations.front().get<std::string>("name") ==
                   "input } // still a string",
               "graph boundaries are parsed by the real string grammar");

  const auto structured = compiler.graph("logic.structured");
  ok &= expect(structured && structured->operations().size() == 1U &&
                   structured->all_operations().size() == 2U &&
                   structured->outputs().size() == 1U &&
                   structured->operations().front().regions().size() ==
                       1U &&
                   structured->operations()
                           .front()
                           .regions()
                           .front()
                           .arguments()
                           .size() == 1U,
               "structured region arguments share the text and C++ model");

  const auto lexical = compiler.graph("logic.lexical");
  const auto lexical_root =
      lexical ? lexical->operations() : std::vector<joggle::Operation>{};
  const auto lexical_regions =
      lexical_root.empty() ? std::vector<joggle::Region>{}
                           : lexical_root.front().regions();
  ok &= expect(lexical && lexical->inputs().size() == 2U &&
                   lexical->all_operations().size() == 3U &&
                   lexical_regions.size() == 2U &&
                   lexical_regions[0].arguments().size() == 1U &&
                   lexical_regions[1].arguments().size() == 1U &&
                   lexical_regions[0].operations().size() == 1U &&
                   lexical_regions[1].operations().size() == 1U &&
                   lexical_regions[0].operations().front().operands()[1] ==
                       lexical->inputs()[1] &&
                   lexical_regions[1].operations().front().operands()[1] ==
                       lexical->inputs()[1],
               "sibling regions reuse local SSA names, shadow an outer name, "
               "and capture an outer value");

  auto nested_pass = compiler.graph("logic.nested_pass");
  const bool nested_simplified =
      nested_pass && compiler.run(*nested_pass, "logic.simplify");
  const auto nested_root = nested_pass ? nested_pass->operations()
                                       : std::vector<joggle::Operation>{};
  const auto nested_regions =
      nested_root.empty() ? std::vector<joggle::Region>{}
                          : nested_root.front().regions();
  ok &= expect(nested_simplified && nested_root.size() == 1U &&
                   nested_regions.size() == 1U &&
                   nested_regions.front().operations().empty(),
               "a text pass contracts operations inside a nested region");

  const std::string nested_emitted =
      nested_pass ? joggle::format(*nested_pass, "nested_compiled")
                  : std::string{};
  joggle::Compiler nested_compiler;
  nested_compiler.add(source, "logic.joggle");
  nested_compiler.add("joggle 1;\nmodule nested_artifact@1.0.0 {\n"
                      "  import logic@1;\n" +
                          nested_emitted + "}\n",
                      "nested-artifact.joggle");
  const bool nested_linked = nested_compiler.link();
  const auto nested_reloaded =
      nested_linked
          ? nested_compiler.graph("nested_artifact.nested_compiled")
          : std::optional<joggle::Graph>{};
  ok &= expect(nested_reloaded &&
                   joggle::format(*nested_reloaded, "nested_compiled") ==
                       nested_emitted,
               "a transformed nested region formats and reloads canonically");

  const std::string emitted =
      structured ? joggle::format(*structured, "compiled") : std::string{};
  joggle::Compiler emitted_compiler;
  emitted_compiler.add(source, "logic.joggle");
  emitted_compiler.add("joggle 1;\nmodule artifact@1.0.0 {\n"
                       "  import logic@1;\n" +
                           emitted + "}\n",
                       "artifact.joggle");
  const bool emitted_linked = emitted_compiler.link();
  const auto emitted_graph = emitted_linked
                                 ? emitted_compiler.graph("artifact.compiled")
                                 : std::optional<joggle::Graph>{};
  ok &= expect(emitted_graph &&
                   joggle::format(*emitted_graph, "compiled") == emitted,
               "a committed Graph formats to round-trippable canonical DSL");
  bool rejected_graph_name = false;
  try {
    static_cast<void>(joggle::format(*structured, "not.a.name"));
  } catch (const std::invalid_argument&) {
    rejected_graph_name = true;
  }
  ok &= expect(rejected_graph_name,
               "Graph formatting rejects a non-DSL member name");

  joggle::Diagnostics roundtrip_diagnostics;
  const std::string canonical = joggle::format(*module);
  const auto roundtrip = joggle::parse_module(canonical, roundtrip_diagnostics,
                                              "canonical.joggle");
  ok &= expect(roundtrip && joggle::format(*roundtrip) == canonical,
               "one module formatter owns schema and graph syntax");

  constexpr std::string_view undefined = R"(
joggle 1;
module logic@1.0.0 {
  type word(width: i64);
  op add<T: type>(lhs: T, rhs: T) -> T;
  graph main() -> word<8> {
    %sum: word<8> = add(%missing, %missing);
    return %sum;
  }
}
)";
  joggle::Compiler invalid;
  const auto invalid_graph = load_graph(invalid, undefined);
  const auto invalid_diagnostics = invalid.diagnostics().entries();
  ok &=
      expect(!invalid_graph && !invalid.ok() && !invalid_diagnostics.empty() &&
                 invalid_diagnostics.front().source.has_value() &&
                 invalid_diagnostics.front().source->source == "logic.joggle" &&
                 invalid_diagnostics.front().source->begin.line == 7U,
             "undefined SSA diagnostics point into the graph");

  constexpr std::string_view leaked = R"(
joggle 1;
module leaked@1.0.0 {
  type word();
  op source<T: type>() -> T;
  op identity<T: type>(input: T) -> T;
  op scope<T: type>(body: region) -> T;
  graph main() -> word {
    %scoped: word = scope() {
      body {
        %local: word = source();
      }
    };
    %result = identity(%local);
    return %result;
  }
}
)";
  joggle::Compiler leaked_compiler;
  leaked_compiler.add(leaked, "leaked.joggle");
  const bool leaked_linked = leaked_compiler.link();
  const auto leaked_graph =
      leaked_linked ? leaked_compiler.graph("leaked.main") : std::nullopt;
  const auto leaked_diagnostics = leaked_compiler.diagnostics().entries();
  ok &= expect(!leaked_graph && !leaked_diagnostics.empty() &&
                   leaked_diagnostics.back().message.find(
                       "undefined SSA value '%local'") != std::string::npos,
               "a region-local SSA value is not visible after the region");

  joggle::Compiler unknown;
  unknown.add(R"(
joggle 1;
module logic@1.0.0 {
  type different();
}
)",
              "unknown.joggle");
  if (!unknown.link()) {
    return EXIT_FAILURE;
  }
  const auto foreign = unknown.graph("logic.main");
  ok &= expect(!foreign && !unknown.ok(),
               "a named graph keeps its module identity");

  joggle::Compiler unqualified;
  unqualified.add(source, "logic.joggle");
  const bool unqualified_linked = unqualified.link();
  const auto unqualified_graph = unqualified_linked
                                     ? unqualified.graph("main")
                                     : std::optional<joggle::Graph>{};
  const auto unqualified_diagnostics = unqualified.diagnostics().entries();
  ok &= expect(!unqualified_graph && !unqualified_diagnostics.empty() &&
                   unqualified_diagnostics.back().message.find(
                       "module.member") != std::string::npos,
               "graph lookup requires one unambiguous qualified member name");

  joggle::Compiler mismatch;
  mismatch.add(R"(
joggle 1;
module mismatch@1.0.0 {
  type a();
  type b();
  op same<T: type>(lhs: T, rhs: T) -> T;
  graph main(%lhs: a, %rhs: b) -> a {
    %result = same(%lhs, %rhs);
    return %result;
  }
}
)",
               "mismatch.joggle");
  const bool mismatch_linked = mismatch.link();
  const auto mismatch_graph = mismatch_linked ? mismatch.graph("mismatch.main")
                                              : std::optional<joggle::Graph>{};
  ok &= expect(!mismatch_graph && !mismatch.ok(),
               "one type variable rejects operands with different types");

  joggle::Compiler return_inferred;
  return_inferred.add(R"(
joggle 1;
module return_inferred@1.0.0 {
  type a();
  op source<T: type>() -> T;
  graph main() -> a {
    %result = source();
    return %result;
  }
}
)",
                      "return-inferred.joggle");
  const bool return_inferred_linked = return_inferred.link();
  const auto return_inferred_graph =
      return_inferred_linked ? return_inferred.graph("return_inferred.main")
                             : std::optional<joggle::Graph>{};
  ok &= expect(return_inferred_graph &&
                   return_inferred_graph->outputs().size() == 1U,
               "a graph result constrains an output-only type variable");

  joggle::Compiler unbound;
  unbound.add(R"(
joggle 1;
module unbound@1.0.0 {
  type a();
  op source<T: type>() -> T;
  graph main() {
    %result = source();
    return;
  }
}
)",
              "unbound.joggle");
  const bool unbound_linked = unbound.link();
  const auto unbound_graph = unbound_linked ? unbound.graph("unbound.main")
                                            : std::optional<joggle::Graph>{};
  ok &=
      expect(!unbound_graph && !unbound.ok(),
             "an unconstrained output-only type variable needs an annotation");

  constexpr std::string_view dependent_source = R"(
joggle 1;
module dependent@1.0.0 {
  type word(width: i64);
  op input<N: i64>(width: N) -> word<N>;
  op default_input<N: i64>(width: N = 8) -> word<N>;

  graph inferred() {
    %value = input(width = 8);
    return;
  }
}
)";
  joggle::Compiler dependent;
  dependent.add(dependent_source, "dependent.joggle");
  const bool dependent_linked = dependent.link();
  const auto dependent_module = dependent.module("dependent");
  auto dependent_graph =
      dependent_linked ? dependent.graph("dependent.inferred") : std::nullopt;
  const auto dependent_operations =
      dependent_graph ? dependent_graph->operations()
                      : std::vector<joggle::Operation>{};
  const auto dependent_width =
      dependent_operations.empty()
          ? std::optional<std::int64_t>{}
          : dependent_operations.front().result(0).type().get<std::int64_t>(
                "width");
  ok &= expect(dependent_module && dependent_graph && dependent_width &&
                   *dependent_width == 8 &&
                   joggle::format(*dependent_module).find(
                       "op input<N: i64>(width: N) -> word<N>;") !=
                       std::string::npos &&
                   joggle::format(*dependent_module).find(
                       "op default_input<N: i64>(width: N = 8) -> word<N>;") !=
                       std::string::npos,
               "a named property binds a generic and infers a text graph "
               "result without another annotation");

  joggle::Compiler inconsistent;
  inconsistent.add(dependent_source, "dependent.joggle");
  const bool inconsistent_linked = inconsistent.link();
  const auto inconsistent_module = inconsistent.module("dependent");
  const auto inconsistent_word =
      inconsistent_module ? inconsistent_module->type("word") : std::nullopt;
  const auto inconsistent_input =
      inconsistent_module ? inconsistent_module->operation("input")
                          : std::nullopt;
  const auto default_input =
      inconsistent_module ? inconsistent_module->operation("default_input")
                          : std::nullopt;
  const auto word8 = inconsistent_word
                         ? inconsistent.make(*inconsistent_word, std::int64_t{8})
                         : std::nullopt;
  auto inconsistent_graph = inconsistent.graph();
  if (!inconsistent_linked || !inconsistent_input || !default_input || !word8 ||
      !inconsistent_graph) {
    return EXIT_FAILURE;
  }
  bool inconsistent_rejected = false;
  {
    auto edit = inconsistent_graph->edit();
    const auto value = edit.append(*inconsistent_input, {}, {*word8});
    edit.set(value, "width", std::int64_t{7});
    edit.output(value.result(0));
    joggle::Diagnostics diagnostics;
    inconsistent_rejected = !edit.commit(diagnostics) && !diagnostics.ok();
  }
  ok &= expect(inconsistent_rejected && inconsistent_graph->inputs().empty() &&
                   inconsistent_graph->operations().empty(),
               "commit validates an explicit result against its dependent "
               "property and rolls back on mismatch");

  joggle::Compiler defaulted;
  defaulted.add(dependent_source, "dependent.joggle");
  const bool defaulted_linked = defaulted.link();
  const auto defaulted_module = defaulted.module("dependent");
  const auto defaulted_input =
      defaulted_module ? defaulted_module->operation("default_input")
                       : std::nullopt;
  const auto named_input =
      defaulted_module ? defaulted_module->operation("input") : std::nullopt;
  auto defaulted_graph = defaulted.graph();
  if (!defaulted_linked || !defaulted_input || !named_input ||
      !defaulted_graph) {
    return EXIT_FAILURE;
  }
  {
    auto edit = defaulted_graph->edit();
    const auto value = edit.append(*defaulted_input);
    edit.output(value.result(0));
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  const auto defaulted_width =
      defaulted_graph->outputs().front().type().get<std::int64_t>("width");
  ok &= expect(defaulted_width && *defaulted_width == 8 &&
                   defaulted.verify(*defaulted_graph),
               "C++ append infers a result from a schema-owned property "
               "default without repeating the type");

  auto named_graph = defaulted.graph();
  if (!named_graph) {
    return EXIT_FAILURE;
  }
  {
    auto edit = named_graph->edit();
    auto width_property =
        joggle::property("width", std::int64_t{12});
    ok &= expect(width_property.name() == "width",
                 "a C++ property retains its schema name");
    const auto value = edit
                           .append(*named_input, {},
                                   std::move(width_property))
                           .value();
    edit.output(value);
    joggle::Diagnostics diagnostics;
    if (!edit.commit(diagnostics)) {
      return EXIT_FAILURE;
    }
  }
  const auto named_width =
      named_graph->outputs().front().type().get<std::int64_t>("width");
  ok &= expect(named_width && *named_width == 12 &&
                   defaulted.verify(*named_graph),
               "a named C++ property participates in result inference at "
               "operation creation");

  bool unknown_property_rejected = false;
  try {
    auto edit = named_graph->edit();
    edit.append(*named_input, {},
                joggle::property("unknown", std::int64_t{1}));
  } catch (const std::invalid_argument& error) {
    unknown_property_rejected =
        std::string_view(error.what()).find("has no property named 'unknown'") !=
        std::string_view::npos;
  }
  ok &= expect(unknown_property_rejected,
               "a misspelled named C++ property is rejected immediately");

  bool duplicate_property_rejected = false;
  try {
    auto edit = named_graph->edit();
    edit.append(*named_input, {},
                joggle::property("width", std::int64_t{1}),
                joggle::property("width", std::int64_t{2}));
  } catch (const std::invalid_argument& error) {
    duplicate_property_rejected =
        std::string_view(error.what()).find("provided more than once") !=
        std::string_view::npos;
  }
  ok &= expect(duplicate_property_rejected,
               "a duplicate named C++ property is rejected immediately");

  bool wrong_property_kind_rejected = false;
  try {
    auto edit = named_graph->edit();
    edit.append(*named_input, {}, joggle::property("width", "wide"));
  } catch (const std::invalid_argument& error) {
    wrong_property_kind_rejected =
        std::string_view(error.what()).find("has the wrong kind") !=
        std::string_view::npos;
  }
  ok &= expect(wrong_property_kind_rejected,
               "a wrong-kind named C++ property is rejected immediately");

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
