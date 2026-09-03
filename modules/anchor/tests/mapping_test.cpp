#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
  compiler.load(JOGGLE_ANCHOR_MODULE);
  compiler.load(JOGGLE_RESNET_BLOCK);
  if (!compiler.link() ||
      !compiler.load_behavior("anchor", JOGGLE_ANCHOR_BEHAVIOR)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto source = compiler.materialize("resnet18_basic_block.main");
  const auto target = compiler.module("anchor");
  const auto memory = compiler.module("mem");
  const auto map = target ? target->function("map") : std::nullopt;
  const auto analyze =
      target ? target->function("local_bytes_upper_bound") : std::nullopt;
  const auto plan =
      target ? target->function("plan_storage") : std::nullopt;
  const auto scratch =
      target ? target->function("scratch_bytes") : std::nullopt;
  const auto reference = memory ? memory->interface("reference") : std::nullopt;
  const auto ref = target ? target->type("ref") : std::nullopt;
  const auto linear = target ? target->type("linear") : std::nullopt;
  const auto tiled = target ? target->type("tiled") : std::nullopt;
  const auto io = target ? target->type("io") : std::nullopt;
  const auto local = target ? target->type("local") : std::nullopt;
  if (!source || !map || !analyze || !plan || !scratch || !reference || !ref ||
      !linear || !tiled || !io || !local) {
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
  const auto planned =
      mapped ? compiler.run<joggle::Module>(*plan, *mapped)
             : std::optional<joggle::Module>{};
  const auto planned_again =
      mapped ? compiler.run<joggle::Module>(*plan, *mapped)
             : std::optional<joggle::Module>{};
  const auto scratch_size =
      planned ? compiler.run<std::int64_t>(*scratch, *planned)
              : std::optional<std::int64_t>{};
  const joggle::Function* planned_body =
      planned && !planned->functions().empty()
          ? planned->functions().front().body()
          : nullptr;
  if (!mapped || !mapped_again || body == nullptr || !bytes || !planned ||
      !planned_again || planned_body == nullptr || !scratch_size) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  const auto arguments = body->arguments();
  const auto ops = body->ops();
  std::vector<std::int64_t> slots;
  for (const auto& op : planned_body->ops()) {
    if (op.callee().symbol().qualified_name() == "anchor.place") {
      const auto slot = op.property<std::int64_t>("slot");
      if (slot) {
        slots.push_back(*slot);
      }
    }
  }
  bool ok = true;
  ok &= expect(calls(*source, "nn") == 7U && calls(*body, "nn") == 0U &&
                   calls(*body, "anchor") == 7U,
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
  ok &= expect(planned_body->ops().size() == 14U &&
                   slots == std::vector<std::int64_t>({0, 1, 0, 1, 0, 1, 0}),
               "storage planning records deterministic first-fit slot reuse");
  ok &= expect(*scratch_size == 1605632,
               "two alternating activation slots replace seven allocations");
  ok &= expect(joggle::format(*mapped) == joggle::format(*mapped_again),
               "the target mapping is deterministic");
  ok &= expect(joggle::format(*planned) == joggle::format(*planned_again),
               "the storage plan is deterministic and serializable");
  ok &= expect(calls(*source, "nn") == 7U,
               "mapping leaves the source Function unchanged");

  joggle::Module overlapping = *planned;
  const auto int_type = compiler.make("int");
  const auto slot_zero =
      int_type ? compiler.known(*int_type, std::int64_t{0})
               : std::optional<joggle::Value>{};
  bool changed_slot = false;
  joggle::Diagnostics rewrite_diagnostics;
  const auto changed = slot_zero
                           ? joggle::rewrite(
                                 overlapping,
                                 [&](const joggle::Op& op,
                                     joggle::Function::Edit& edit,
                                     joggle::Diagnostics&) {
                                   if (changed_slot ||
                                       op.callee().symbol().qualified_name() !=
                                           "anchor.place" ||
                                       op.property<std::int64_t>("slot") != 1) {
                                     return false;
                                   }
                                   auto call_arguments = op.arguments();
                                   call_arguments[1] = *slot_zero;
                                   const auto replacement = edit.insert(
                                       op, op.callee(),
                                       std::move(call_arguments),
                                       {op.value().type()});
                                   edit.replace(op, replacement.results());
                                   changed_slot = true;
                                   return true;
                                 },
                                 rewrite_diagnostics)
                           : std::optional<std::size_t>{};
  const auto invalid_scratch =
      changed && *changed == 1U
          ? compiler.run<std::int64_t>(*scratch, overlapping)
          : std::optional<std::int64_t>{};
  ok &= expect(changed && *changed == 1U && !invalid_scratch,
               "scratch analysis rejects overlapping physical slot reuse");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
