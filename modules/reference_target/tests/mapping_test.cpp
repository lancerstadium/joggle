#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <joggle/joggle.h>

namespace {

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

std::size_t calls(const joggle::Function& function, std::string_view module) {
  const auto ops = function.ops();
  return static_cast<std::size_t>(std::count_if(
      ops.begin(), ops.end(),
      [module](const joggle::Op& op) {
        return op.callee().symbol().module_name() == module;
      }));
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_ARITH_MODULE);
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_NN_MODULE);
  compiler.load(JOGGLE_MEM_MODULE);
  compiler.load(JOGGLE_REFERENCE_TARGET_MODULE);
  compiler.load(JOGGLE_RESNET_BLOCK);
  if (!compiler.link() ||
      !compiler.load_behavior("reference_target",
                              JOGGLE_REFERENCE_TARGET_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = compiler.materialize("resnet18_basic_block.main");
  const auto target = compiler.module("reference_target");
  const auto memory = compiler.module("mem");
  const auto map = target ? target->function("map") : std::nullopt;
  const auto analyze =
      target ? target->function("local_bytes_upper_bound") : std::nullopt;
  const auto reference = memory ? memory->interface("reference") : std::nullopt;
  const auto ref = target ? target->type("ref") : std::nullopt;
  const auto linear = target ? target->type("linear") : std::nullopt;
  const auto tiled = target ? target->type("tiled") : std::nullopt;
  const auto io = target ? target->type("io") : std::nullopt;
  const auto local = target ? target->type("local") : std::nullopt;
  if (!source || !map || !analyze || !reference || !ref || !linear || !tiled ||
      !io || !local) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  joggle::Module model("basic_block", {1, 0, 0});
  joggle::Diagnostics diagnostics;
  if (!model.insert("main", *source, diagnostics)) {
    diagnostics.print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto mapped = compiler.run<joggle::Module>(
      *map, model, std::int64_t{8}, std::int64_t{8});
  const auto mapped_again = compiler.run<joggle::Module>(
      *map, model, std::int64_t{8}, std::int64_t{8});
  const joggle::Function* body =
      mapped && !mapped->functions().empty()
          ? mapped->functions().front().body()
          : nullptr;
  const auto bytes = mapped
                         ? compiler.run<std::int64_t>(*analyze, *mapped)
                         : std::optional<std::int64_t>{};
  if (!mapped || !mapped_again || body == nullptr || !bytes) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto arguments = body->arguments();
  const auto ops = body->ops();
  bool ok = true;
  ok &= expect(calls(*source, "nn") == 7U && calls(*body, "nn") == 0U &&
                   calls(*body, "reference_target") == 7U,
               "mapping replaces the complete basic-block NN vocabulary");
  ok &= expect(arguments.size() == 11U &&
                   std::all_of(arguments.begin(), arguments.end(),
                               [&](const joggle::Value& value) {
                                 const auto space = value.type().get<joggle::Type>(
                                     "space_type");
                                 const auto layout =
                                     value.type().get<joggle::Type>("layout_type");
                                 return value.type().schema() == *ref && space &&
                                        space->schema() == *io && layout &&
                                        layout->schema() == *linear &&
                                        compiler.conforms(value.type().schema(),
                                                          *reference);
                               }),
               "graph inputs become linear IO references through mem.reference");
  ok &= expect(
      std::all_of(ops.begin(), ops.end(),
                  [&](const joggle::Op& op) {
                    const auto space =
                        op.value().type().get<joggle::Type>("space_type");
                    const auto layout =
                        op.value().type().get<joggle::Type>("layout_type");
                    return space && space->schema() == *local && layout &&
                           layout->schema() == *tiled &&
                           layout->get<std::int64_t>("rows") == 8 &&
                           layout->get<std::int64_t>("columns") == 8;
                  }),
      "rank-4 intermediates become explicitly tiled local references");
  ok &= expect(*bytes == 5619712,
               "analysis reports the conservative sum of local SSA storage");
  ok &= expect(joggle::format(*mapped) == joggle::format(*mapped_again),
               "the target mapping is deterministic");
  ok &= expect(calls(*source, "nn") == 7U,
               "mapping leaves the source Function unchanged");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
