#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <joggle/joggle.h>

namespace {

constexpr std::string_view source = R"(
joggle 1;

module bitpack_fixture@1.0.0 {
  import bitpack@1 as bp;
  import tensor@1 as t;

  fn model(
    lhs: t.tensor<bp.integer<4>, [1, 8]>,
    rhs: t.tensor<bp.integer<4>, [1, 8]>
  ) -> t.tensor<bp.integer<4>, [1, 16]> {
    left: t.tensor<bp.integer<4>, [1, 8]> = t.relu(lhs);
    right: t.tensor<bp.integer<4>, [1, 8]> = t.relu(rhs);
    return t.concat(left, right, 1);
  }

  fn pack(input: function) -> function {
    return @bp.run(input, u32, 1, "lsb");
  }
}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  joggle::Compiler compiler;
  compiler.load(JOGGLE_TENSOR_MODULE);
  compiler.load(JOGGLE_BITPACK_MODULE);
  compiler.add(source, "bitpack-fixture.joggle");
  if (!compiler.link() ||
      !compiler.load_native("bitpack", JOGGLE_BITPACK_NATIVE)) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }
  const auto model = compiler.materialize("bitpack_fixture.model");
  const auto transformed = model ? compiler.run<joggle::Function>(
                                       "bitpack_fixture.pack", *model)
                                 : std::nullopt;
  if (!model || !transformed) {
    compiler.diagnostics().print(std::cerr);
    return EXIT_FAILURE;
  }

  bool ok = true;
  const auto arguments = transformed->arguments();
  const auto result = transformed->result_types();
  const auto input_storage = arguments.empty()
                                 ? std::nullopt
                                 : arguments.front().type().get<joggle::Type>(
                                       "storage");
  const auto output_storage = result.empty()
                                  ? std::nullopt
                                  : result.front().get<joggle::Type>(
                                        "storage");
  ok &= expect(arguments.size() == 2U && result.size() == 1U &&
                   arguments.front().type().schema().symbol().module_name() ==
                       "bitpack" &&
                   input_storage && output_storage &&
                   input_storage->get<std::vector<std::int64_t>>("shape") ==
                       std::vector<std::int64_t>({1, 1}) &&
                   output_storage->get<std::vector<std::int64_t>>("shape") ==
                       std::vector<std::int64_t>({1, 2}),
               "eight logical i4 lanes map to each physical u32 word");
  const auto ops = transformed->ops();
  ok &= expect(ops.size() == 3U &&
                   std::all_of(ops.begin(), ops.end(), [](const auto& op) {
                     return op.callee().symbol().module_name() == "bitpack";
                   }) &&
                   compiler.verify(*transformed),
               "format-aware source functions replace the tensor vocabulary");
  const joggle::TypeProjection logical = [](const joggle::Type& type) {
    if (type.schema().symbol().module_name() == "bitpack" &&
        type.schema().symbol().local_name() == "packed") {
      return type.get<joggle::Type>("logical");
    }
    return std::optional<joggle::Type>{type};
  };
  joggle::Diagnostics equivalence;
  ok &= expect(joggle::equivalent(compiler, *model, *transformed, logical,
                                  equivalence) &&
                   equivalence.ok(),
               "logical projection proves the physical representation change");
  const auto bitpack = compiler.module("bitpack");
  const auto tensor = compiler.module("tensor");
  const auto packed = bitpack ? bitpack->type("packed") : std::nullopt;
  const auto tensor_type = tensor ? tensor->type("tensor") : std::nullopt;
  const auto u16 = compiler.make("u16");
  const auto bad_storage = tensor_type && u16
                               ? compiler.make(*tensor_type, *u16,
                                               std::vector<std::int64_t>{1, 1})
                               : std::nullopt;
  const auto invalid = packed && bad_storage
                           ? compiler.make(*packed,
                                           model->arguments().front().type(),
                                           *bad_storage, std::int64_t{1},
                                           std::int64_t{8}, std::string{"lsb"})
                           : std::nullopt;
  ok &= expect(!invalid && !compiler.ok(),
               "the format verifier rejects a lane/storage width mismatch");
  if (!ok) {
    equivalence.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
