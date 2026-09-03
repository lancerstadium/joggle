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

std::string decode(const joggle::Bytes& bytes) {
  std::string result;
  result.reserve(bytes.size());
  for (const std::byte value : bytes) {
    result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
  }
  return result;
}

std::size_t calls(const joggle::Function& function, std::string_view module) {
  const auto ops = function.ops();
  return static_cast<std::size_t>(std::count_if(
      ops.begin(), ops.end(),
      [module](const joggle::Op& op) {
        return op.callee().symbol().module_name() == module;
      }));
}

std::size_t calls_named(const joggle::Function& function,
                        std::string_view qualified_name) {
  const auto ops = function.ops();
  return static_cast<std::size_t>(std::count_if(
      ops.begin(), ops.end(),
      [qualified_name](const joggle::Op& op) {
        return op.callee().symbol().qualified_name() == qualified_name;
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
  compiler.add(R"(
joggle 1;
module anchor_kernel@1.0.0 {
  import anchor@1;

  fn linear_f32(
    input: anchor.ref<f32, [2, 3], anchor.linear, anchor.io>,
    weight: anchor.ref<f32, [4, 3], anchor.linear, anchor.read_only>,
    bias: anchor.ref<f32, [4], anchor.linear, anchor.read_only>
  ) -> anchor.ref<f32, [2, 4], anchor.linear, anchor.local> {
    return anchor.linear(input, weight, bias);
  }
}
)",
               "anchor-kernel.joggle");
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
  const auto cycles = target ? target->function("cycles") : std::nullopt;
  const auto emit = target ? target->function("emit") : std::nullopt;
  const auto reference = memory ? memory->interface("reference") : std::nullopt;
  const auto ref = target ? target->type("ref") : std::nullopt;
  const auto linear = target ? target->type("linear") : std::nullopt;
  const auto tiled = target ? target->type("tiled") : std::nullopt;
  const auto io = target ? target->type("io") : std::nullopt;
  const auto local = target ? target->type("local") : std::nullopt;
  const auto config = target ? target->type("config") : std::nullopt;
  const auto load = target ? target->function("load") : std::nullopt;
  const auto store = target ? target->function("store") : std::nullopt;
  const auto relu = target ? target->function("relu") : std::nullopt;
  const auto add = target ? target->function("add") : std::nullopt;
  const auto linear_function = compiler.materialize("anchor_kernel.linear_f32");
  const auto linear_call =
      linear_function && linear_function->ops().size() == 1U
          ? std::optional<joggle::Op>{linear_function->ops().front()}
          : std::nullopt;
  const auto linear_body =
      linear_call ? compiler.materialize(*linear_call)
                  : std::optional<joggle::Function>{};
  const auto read = memory ? memory->interface("read") : std::nullopt;
  const auto write = memory ? memory->interface("write") : std::nullopt;
  if (!source || !map || !analyze || !plan || !scratch || !cycles || !emit ||
      !reference || !ref || !linear || !tiled || !io || !local || !config ||
      !load || !store || !relu || !add || !linear_function || !linear_call ||
      !linear_body || !read || !write) {
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
  const auto machine = compiler.make(*config, std::int64_t{16},
                                     std::int64_t{4}, std::int64_t{32},
                                     std::int64_t{8}, std::int64_t{2000000},
                                     std::int64_t{3});
  const auto cycle_count =
      planned && machine
          ? compiler.run<std::int64_t>(*cycles, *planned, *machine)
          : std::optional<std::int64_t>{};
  const auto emitted = planned && machine
                           ? compiler.run<joggle::Bytes>(*emit, *planned,
                                                        *machine)
                           : std::optional<joggle::Bytes>{};
  const auto emitted_again = planned && machine
                                 ? compiler.run<joggle::Bytes>(*emit, *planned,
                                                              *machine)
                                 : std::optional<joggle::Bytes>{};
  const joggle::Function* planned_body =
      planned && !planned->functions().empty()
          ? planned->functions().front().body()
          : nullptr;
  if (!mapped || !mapped_again || body == nullptr || !bytes || !planned ||
      !planned_again || planned_body == nullptr || !scratch_size ||
      !cycle_count || !emitted || !emitted_again) {
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
  ok &= expect(load->form() == joggle::Module::FunctionDecl::Form::External &&
                   store->form() ==
                       joggle::Module::FunctionDecl::Form::External &&
                   compiler.conforms(*load, *read) &&
                   compiler.conforms(*store, *write) &&
                   relu->form() == joggle::Module::FunctionDecl::Form::Body &&
                   add->form() == joggle::Module::FunctionDecl::Form::Body,
               "target primitives remain residual while ReLU and Add have "
               "ordinary Joggle bodies");
  ok &= expect(compiler.verify(*linear_body) &&
                   linear_body->blocks().size() == 13U &&
                   linear_body->arguments().size() == 3U &&
                   linear_body->result_types().size() == 1U &&
                   calls_named(*linear_body, "mem.alloc") == 1U &&
                   calls_named(*linear_body, "anchor.load") == 3U &&
                   calls_named(*linear_body, "anchor.store") == 1U &&
                   calls_named(*linear_body, "anchor.linear") == 0U,
               "a concrete Linear call specializes and materializes the "
               "generic Joggle body as three residual counted loops");
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
  const std::string manifest = decode(*emitted);
  ok &= expect(*cycle_count > 0 && *emitted == *emitted_again &&
                   manifest.starts_with("anchor 1\nmodule basic_block#") &&
                   manifest.find("\nscratch-bytes 1605632\n") !=
                       std::string::npos &&
                   manifest.find("\ncycles " +
                                 std::to_string(*cycle_count) + "\n") !=
                       std::string::npos &&
                   manifest.ends_with(joggle::format(*planned)),
               "cycle analysis and manifest emission consume the same plan");
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

  const auto undersized = compiler.make(
      *config, std::int64_t{16}, std::int64_t{4}, std::int64_t{32},
      std::int64_t{8}, std::int64_t{1605631}, std::int64_t{3});
  const auto impossible = undersized
                              ? compiler.run<std::int64_t>(
                                    *cycles, *planned, *undersized)
                              : std::optional<std::int64_t>{};
  ok &= expect(undersized && !impossible,
               "cycle analysis rejects a machine below the planned scratch "
               "requirement");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
