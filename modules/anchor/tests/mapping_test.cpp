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

  fn conv_padded_f32(
    input: anchor.ref<f32, [1, 1, 3, 3], anchor.linear, anchor.io>,
    weight: anchor.ref<f32, [1, 1, 2, 2], anchor.linear, anchor.read_only>
  ) -> anchor.ref<f32, [1, 1, 4, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.conv2d_nchw(
      input,
      weight,
      pad_top = 1,
      pad_left = 1,
      pad_bottom = 1,
      pad_right = 1
    );
  }

  fn conv_strided_f32(
    input: anchor.ref<f32, [1, 2, 7, 7], anchor.linear, anchor.io>,
    weight: anchor.ref<f32, [3, 2, 3, 3], anchor.linear, anchor.read_only>
  ) -> anchor.ref<f32, [1, 3, 4, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.conv2d_nchw(
      input,
      weight,
      stride_h = 2,
      stride_w = 2,
      pad_top = 1,
      pad_left = 1,
      pad_bottom = 1,
      pad_right = 1
    );
  }

  fn conv_biased_f32(
    input: anchor.ref<f32, [1, 2, 7, 7], anchor.linear, anchor.io>,
    weight: anchor.ref<f32, [3, 2, 3, 3], anchor.linear, anchor.read_only>,
    bias: anchor.ref<f32, [3], anchor.linear, anchor.read_only>
  ) -> anchor.ref<f32, [1, 3, 4, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.conv2d_nchw(
      input,
      weight,
      bias,
      stride_h = 2,
      stride_w = 2,
      pad_top = 1,
      pad_left = 1,
      pad_bottom = 1,
      pad_right = 1
    );
  }

  fn conv_relu_f32(
    input: anchor.ref<f32, [1, 2, 7, 7], anchor.linear, anchor.io>,
    weight: anchor.ref<f32, [3, 2, 3, 3], anchor.linear, anchor.read_only>,
    bias: anchor.ref<f32, [3], anchor.linear, anchor.read_only>
  ) -> anchor.ref<f32, [1, 3, 4, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.conv_relu_nchw(
      input,
      weight,
      bias,
      stride_h = 2,
      stride_w = 2,
      pad_top = 1,
      pad_left = 1,
      pad_bottom = 1,
      pad_right = 1
    );
  }

  fn max_pool_padded_f32(
    input: anchor.ref<f32, [1, 1, 3, 3], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 1, 4, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.max_pool2d_nchw(
      input,
      2,
      2,
      pad_top = 1,
      pad_left = 1,
      pad_bottom = 1,
      pad_right = 1
    );
  }

  fn max_pool_strided_f32(
    input: anchor.ref<f32, [1, 2, 7, 7], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 2, 4, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.max_pool2d_nchw(
      input,
      3,
      3,
      stride_h = 2,
      stride_w = 2,
      pad_top = 1,
      pad_left = 1,
      pad_bottom = 1,
      pad_right = 1
    );
  }

  fn global_average_f32(
    input: anchor.ref<f32, [1, 2, 3, 4], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 2, 1, 1], anchor.linear, anchor.local> {
    return anchor.global_average_pool_nchw(input);
  }

  fn batch_norm_f32(
    input: anchor.ref<f32, [1, 2, 3, 4], anchor.linear, anchor.io>,
    scale: anchor.ref<f32, [2], anchor.linear, anchor.read_only>,
    bias: anchor.ref<f32, [2], anchor.linear, anchor.read_only>,
    mean: anchor.ref<f32, [2], anchor.linear, anchor.read_only>,
    variance: anchor.ref<f32, [2], anchor.linear, anchor.read_only>
  ) -> anchor.ref<f32, [1, 2, 3, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.batch_norm_nchw(input, scale, bias, mean, variance);
  }

  fn batch_norm_relu_f32(
    input: anchor.ref<f32, [1, 2, 3, 4], anchor.linear, anchor.io>,
    scale: anchor.ref<f32, [2], anchor.linear, anchor.read_only>,
    bias: anchor.ref<f32, [2], anchor.linear, anchor.read_only>,
    mean: anchor.ref<f32, [2], anchor.linear, anchor.read_only>,
    variance: anchor.ref<f32, [2], anchor.linear, anchor.read_only>
  ) -> anchor.ref<f32, [1, 2, 3, 4], anchor.tiled<2, 2>, anchor.local> {
    return anchor.batch_norm_relu_nchw(
      input,
      scale,
      bias,
      mean,
      variance
    );
  }

  fn flatten_f32(
    input: anchor.ref<f32, [1, 2, 3, 4], anchor.linear, anchor.io>
  ) -> anchor.ref<f32, [1, 24], anchor.linear, anchor.local> {
    return anchor.flatten_nchw(input);
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
  const auto fuse =
      target ? target->function("fuse_relu") : std::nullopt;
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
  const auto timeline = target ? target->type("timeline") : std::nullopt;
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
  const auto materialize_call_body = [&](std::string_view name) {
    const auto wrapper = compiler.materialize(name);
    const auto ops = wrapper ? wrapper->ops() : std::vector<joggle::Op>{};
    return ops.size() == 1U ? compiler.materialize(ops.front())
                            : std::optional<joggle::Function>{};
  };
  const auto padded_conv_body =
      materialize_call_body("anchor_kernel.conv_padded_f32");
  const auto strided_conv_body =
      materialize_call_body("anchor_kernel.conv_strided_f32");
  const auto biased_conv_body =
      materialize_call_body("anchor_kernel.conv_biased_f32");
  const auto biased_conv_ops =
      biased_conv_body ? biased_conv_body->ops() : std::vector<joggle::Op>{};
  const auto nested_conv = std::find_if(
      biased_conv_ops.begin(), biased_conv_ops.end(),
      [](const joggle::Op& op) {
        return op.callee().symbol().qualified_name() ==
               "anchor.conv2d_nchw";
      });
  const auto nested_conv_body =
      nested_conv != biased_conv_ops.end()
          ? compiler.materialize(*nested_conv)
          : std::optional<joggle::Function>{};
  const auto fused_conv_body =
      materialize_call_body("anchor_kernel.conv_relu_f32");
  const auto fused_conv_ops =
      fused_conv_body ? fused_conv_body->ops() : std::vector<joggle::Op>{};
  const auto nested_fused_conv = std::find_if(
      fused_conv_ops.begin(), fused_conv_ops.end(),
      [](const joggle::Op& op) {
        return op.callee().symbol().qualified_name() ==
               "anchor.conv2d_nchw";
      });
  const auto nested_fused_conv_body =
      nested_fused_conv != fused_conv_ops.end()
          ? compiler.materialize(*nested_fused_conv)
          : std::optional<joggle::Function>{};
  const auto padded_pool_body =
      materialize_call_body("anchor_kernel.max_pool_padded_f32");
  const auto strided_pool_body =
      materialize_call_body("anchor_kernel.max_pool_strided_f32");
  const auto global_average_body =
      materialize_call_body("anchor_kernel.global_average_f32");
  const auto batch_norm_body =
      materialize_call_body("anchor_kernel.batch_norm_f32");
  const auto fused_norm_body =
      materialize_call_body("anchor_kernel.batch_norm_relu_f32");
  const auto fused_norm_ops =
      fused_norm_body ? fused_norm_body->ops() : std::vector<joggle::Op>{};
  const auto nested_norm = std::find_if(
      fused_norm_ops.begin(), fused_norm_ops.end(),
      [](const joggle::Op& op) {
        return op.callee().symbol().qualified_name() ==
               "anchor.batch_norm_nchw";
      });
  const auto nested_norm_body =
      nested_norm != fused_norm_ops.end()
          ? compiler.materialize(*nested_norm)
          : std::optional<joggle::Function>{};
  const auto flatten_body =
      materialize_call_body("anchor_kernel.flatten_f32");
  const auto read = memory ? memory->interface("read") : std::nullopt;
  const auto write = memory ? memory->interface("write") : std::nullopt;
  if (!source || !map || !analyze || !fuse || !plan || !scratch || !cycles ||
      !emit || !reference || !ref || !linear || !tiled || !io || !local ||
      !config || !timeline || !load || !store || !relu || !add ||
      !linear_function || !linear_call || !linear_body || !padded_conv_body ||
      !strided_conv_body || !read || !biased_conv_body || !nested_conv_body ||
      !fused_conv_body || !nested_fused_conv_body || !write ||
      !padded_pool_body || !strided_pool_body ||
      !global_average_body || !batch_norm_body || !fused_norm_body ||
      !nested_norm_body || !flatten_body) {
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
  const auto fused_mapped =
      mapped ? compiler.run<joggle::Module>(*fuse, *mapped)
             : std::optional<joggle::Module>{};
  const auto fused_mapped_again =
      mapped ? compiler.run<joggle::Module>(*fuse, *mapped)
             : std::optional<joggle::Module>{};
  const auto fused_fixed_point =
      fused_mapped ? compiler.run<joggle::Module>(*fuse, *fused_mapped)
                   : std::optional<joggle::Module>{};
  const joggle::Function* body =
      mapped && !mapped->functions().empty()
          ? mapped->functions().front().body()
          : nullptr;
  const joggle::Function* fused_body =
      fused_mapped && !fused_mapped->functions().empty()
          ? fused_mapped->functions().front().body()
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
  const auto trace = planned && machine
                         ? compiler.run<joggle::Bytes>("anchor.trace", *planned,
                                                       *machine)
                         : std::optional<joggle::Bytes>{};
  const auto trace_again =
      planned && machine
          ? compiler.run<joggle::Bytes>("anchor.trace", *planned, *machine)
          : std::optional<joggle::Bytes>{};
  const joggle::Function* planned_body =
      planned && !planned->functions().empty()
          ? planned->functions().front().body()
          : nullptr;
  if (!mapped || !mapped_again || !fused_mapped || !fused_mapped_again ||
      !fused_fixed_point || body == nullptr || fused_body == nullptr ||
      !bytes || !planned || !planned_again || planned_body == nullptr ||
      !scratch_size ||
      !cycle_count || !emitted || !emitted_again || !trace || !trace_again) {
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
  ok &= expect(compiler.verify(*padded_conv_body) &&
                   compiler.verify(*strided_conv_body) &&
                   padded_conv_body->blocks().size() > 30U &&
                   padded_conv_body->blocks().size() ==
                       strided_conv_body->blocks().size() &&
                   padded_conv_body->ops().size() ==
                       strided_conv_body->ops().size() &&
                   calls_named(*padded_conv_body, "mem.alloc") == 1U &&
                   calls_named(*padded_conv_body, "anchor.load") == 2U &&
                   calls_named(*padded_conv_body, "anchor.store") == 1U &&
                   calls_named(*padded_conv_body, "anchor.conv2d_nchw") == 0U,
               "padded and strided Conv2D calls materialize the same "
               "trip-count-independent seven-loop CFG with explicit guarded "
               "NCHW indexing");
  ok &= expect(compiler.verify(*biased_conv_body) &&
                   biased_conv_body->blocks().size() == 17U &&
                   calls_named(*biased_conv_body, "mem.alloc") == 0U &&
                   calls_named(*biased_conv_body, "anchor.load") == 2U &&
                   calls_named(*biased_conv_body, "anchor.store") == 1U &&
                   calls_named(*biased_conv_body,
                               "anchor.conv2d_nchw") == 1U &&
                   compiler.verify(*nested_conv_body) &&
                   nested_conv_body->blocks().size() ==
                       strided_conv_body->blocks().size() &&
                   calls_named(*nested_conv_body, "mem.alloc") == 1U &&
                   calls_named(*nested_conv_body,
                               "anchor.conv2d_nchw") == 0U,
               "biased Conv2D composes the verified convolution body with a "
               "fixed-size in-place bias epilogue instead of duplicating it");
  ok &= expect(compiler.verify(*fused_conv_body) &&
                   fused_conv_body->blocks().size() == 5U &&
                   calls_named(*fused_conv_body, "mem.alloc") == 0U &&
                   calls_named(*fused_conv_body, "anchor.load") == 1U &&
                   calls_named(*fused_conv_body, "anchor.store") == 1U &&
                   calls_named(*fused_conv_body, "arith.max") == 1U &&
                   calls_named(*fused_conv_body,
                               "anchor.conv2d_nchw") == 1U &&
                   calls_named(*fused_conv_body,
                               "anchor.conv_relu_nchw") == 0U &&
                   compiler.verify(*nested_fused_conv_body) &&
                   nested_fused_conv_body->blocks().size() ==
                       biased_conv_body->blocks().size(),
               "fused Conv-ReLU composes biased convolution with one "
               "fixed-size in-place activation epilogue");
  ok &= expect(compiler.verify(*padded_pool_body) &&
                   compiler.verify(*strided_pool_body) &&
                   padded_pool_body->blocks().size() > 25U &&
                   padded_pool_body->blocks().size() ==
                       strided_pool_body->blocks().size() &&
                   padded_pool_body->ops().size() ==
                       strided_pool_body->ops().size() &&
                   calls_named(*padded_pool_body, "mem.alloc") == 1U &&
                   calls_named(*padded_pool_body, "anchor.load") == 1U &&
                   calls_named(*padded_pool_body, "anchor.store") == 1U &&
                   calls_named(*padded_pool_body,
                               "anchor.max_pool2d_nchw") == 0U,
               "MaxPool materializes fixed-size guarded NCHW loops and uses "
               "the first valid element instead of zero as its maximum seed");
  ok &= expect(compiler.verify(*global_average_body) &&
                   calls_named(*global_average_body, "mem.alloc") == 1U &&
                   calls_named(*global_average_body, "anchor.load") == 1U &&
                   calls_named(*global_average_body, "anchor.store") == 1U &&
                   calls_named(*global_average_body,
                               "anchor.global_average_pool_nchw") == 0U &&
                   compiler.verify(*flatten_body) &&
                   flatten_body->blocks().size() == 5U &&
                   calls_named(*flatten_body, "mem.alloc") == 1U &&
                   calls_named(*flatten_body, "anchor.load") == 1U &&
                   calls_named(*flatten_body, "anchor.store") == 1U,
               "global average pooling and flatten materialize deterministic "
               "logical-order reduction and copy bodies");
  ok &= expect(compiler.verify(*batch_norm_body) &&
                   batch_norm_body->blocks().size() == 17U &&
                   calls_named(*batch_norm_body, "mem.alloc") == 1U &&
                   calls_named(*batch_norm_body, "anchor.load") == 5U &&
                   calls_named(*batch_norm_body, "anchor.store") == 1U &&
                   calls_named(*batch_norm_body, "arith.sqrt") == 1U &&
                   calls_named(*batch_norm_body,
                               "anchor.batch_norm_nchw") == 0U,
               "BatchNorm materializes a fixed-size four-loop NCHW body "
               "with the standard floating-point inference equation");
  ok &= expect(compiler.verify(*fused_norm_body) &&
                   fused_norm_body->blocks().size() == 5U &&
                   calls_named(*fused_norm_body, "mem.alloc") == 0U &&
                   calls_named(*fused_norm_body, "anchor.load") == 1U &&
                   calls_named(*fused_norm_body, "anchor.store") == 1U &&
                   calls_named(*fused_norm_body, "arith.max") == 1U &&
                   calls_named(*fused_norm_body,
                               "anchor.batch_norm_nchw") == 1U &&
                   calls_named(*fused_norm_body,
                               "anchor.batch_norm_relu_nchw") == 0U &&
                   compiler.verify(*nested_norm_body) &&
                   nested_norm_body->blocks().size() ==
                       batch_norm_body->blocks().size(),
               "fused BatchNorm-ReLU composes the verified normalization "
               "body with one in-place residual epilogue");
  ok &= expect(calls(*source, "nn") == 7U && calls(*body, "nn") == 0U &&
                   calls(*body, "anchor") == 7U,
               "mapping replaces the complete basic-block NN vocabulary");
  ok &= expect(fused_body->ops().size() == 6U &&
                   calls_named(*fused_body,
                               "anchor.batch_norm_relu_nchw") == 1U &&
                   calls_named(*fused_body, "anchor.batch_norm_nchw") == 1U &&
                   calls_named(*fused_body, "anchor.relu") == 1U &&
                   body->ops().size() == 7U,
               "target fusion replaces only the single-use BatchNorm-ReLU "
               "pair and leaves unrelated normalization and activation calls");
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
  const std::string timeline_text = decode(*trace);
  ok &= expect(*cycle_count > 0 && *emitted == *emitted_again &&
                   manifest.starts_with("anchor 1\nmodule basic_block#") &&
                   manifest.find("\nscratch-bytes 1605632\n") !=
                       std::string::npos &&
                   manifest.find("\ncycles " +
                                 std::to_string(*cycle_count) + "\n") !=
                       std::string::npos &&
                   manifest.ends_with(joggle::format(*planned)),
               "cycle analysis and manifest emission consume the same plan");
  ok &= expect(*trace == *trace_again &&
                   timeline_text.starts_with(
                       "anchor timeline 1\nmodule basic_block#") &&
                   timeline_text.find("\nscratch-bytes 1605632\n") !=
                       std::string::npos &&
                   timeline_text.find("\ncycles " +
                                      std::to_string(*cycle_count) + "\n") !=
                       std::string::npos &&
                   timeline_text.find("\nevents 7\n") != std::string::npos,
               "simulation produces a deterministic seven-event timeline "
               "whose duration agrees with cycle analysis");
  ok &= expect(joggle::format(*mapped) == joggle::format(*mapped_again),
               "the target mapping is deterministic");
  ok &= expect(joggle::format(*fused_mapped) ==
                       joggle::format(*fused_mapped_again) &&
                   joggle::format(*fused_mapped) ==
                       joggle::format(*fused_fixed_point),
               "target epilogue fusion is deterministic and idempotent");
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
