#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <optional>
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

  fn roundtrip(values: list<int>) -> list<int> {
    encoded = @bp.encode(values, bp.integer<4>, u32, "lsb");
    return @bp.decode(encoded, bp.integer<4>, u32, "lsb");
  }

  fn roundtrip_signed(values: list<int>) -> list<int> {
    encoded = @bp.encode(values, bp.integer<4, true>, u32, "msb");
    return @bp.decode(encoded, bp.integer<4, true>, u32, "msb");
  }
}
)";

bool expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "test failure: " << message << '\n';
  }
  return condition;
}

bool bytes_equal(const std::optional<joggle::Bytes>& actual,
                 std::initializer_list<unsigned int> expected) {
  return actual && actual->size() == expected.size() &&
         std::equal(actual->begin(), actual->end(), expected.begin(),
                    [](std::byte lhs, unsigned int rhs) {
                      return std::to_integer<unsigned int>(lhs) == rhs;
                    });
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

  const std::vector<std::int64_t> values{0, 1, 2,  3,  4,  5,  6,  7,
                                         8, 9, 10, 11, 12, 13, 14, 15};
  const auto decoded = compiler.run<std::vector<std::int64_t>>(
      "bitpack_fixture.roundtrip", values);
  const auto bitpack_module = compiler.module("bitpack");
  const auto integer = bitpack_module ? bitpack_module->type("integer")
                                      : std::nullopt;
  const auto i4 = integer ? compiler.make(*integer, std::int64_t{4}, false)
                          : std::nullopt;
  const auto signed_i4 =
      integer ? compiler.make(*integer, std::int64_t{4}, true) : std::nullopt;
  const auto u32 = compiler.make("u32");
  const auto encoded = i4 && u32
                           ? compiler.run<joggle::Bytes>(
                                 "bitpack.encode", values, *i4, *u32,
                                 std::string{"lsb"})
                           : std::nullopt;
  const auto msb_encoded = i4 && u32
                               ? compiler.run<joggle::Bytes>(
                                     "bitpack.encode", values, *i4, *u32,
                                     std::string{"msb"})
                               : std::nullopt;
  const std::vector<std::int64_t> signed_values{-8, -7, -6, -5, -4, -3,
                                                 -2, -1, 0,  1,  2,  3,
                                                 4,  5,  6,  7};
  const auto signed_decoded = compiler.run<std::vector<std::int64_t>>(
      "bitpack_fixture.roundtrip_signed", signed_values);
  const auto signed_encoded = signed_i4 && u32
                                  ? compiler.run<joggle::Bytes>(
                                        "bitpack.encode", signed_values,
                                        *signed_i4, *u32, std::string{"msb"})
                                  : std::nullopt;

  bool ok = true;
  ok &= expect(decoded == values &&
                   bytes_equal(encoded, {0x10U, 0x32U, 0x54U, 0x76U, 0x98U,
                                         0xbaU, 0xdcU, 0xfeU}),
               "source staging round-trips every unsigned i4 value");
  ok &= expect(bytes_equal(msb_encoded,
                           {0x67U, 0x45U, 0x23U, 0x01U, 0xefU, 0xcdU, 0xabU,
                            0x89U}),
               "msb lane order is distinct from little-endian word order");
  ok &= expect(
      signed_decoded == signed_values && signed_encoded &&
          bytes_equal(signed_encoded,
                      {0xefU, 0xcdU, 0xabU, 0x89U, 0x67U, 0x45U, 0x23U,
                       0x01U}),
      "every signed i4 value uses two's complement and staged decoding");
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
  const std::vector<std::int64_t> out_of_range{0, 1, 2, 3, 4, 5, 6, 16};
  const auto invalid_encoding =
      i4 && u32 ? compiler.run<joggle::Bytes>(
                      "bitpack.encode", out_of_range, *i4, *u32,
                      std::string{"lsb"})
                : std::nullopt;
  ok &= expect(!invalid && !invalid_encoding && !compiler.ok(),
               "format and value-domain violations both fail closed");
  if (!ok) {
    equivalence.print(std::cerr);
    compiler.diagnostics().print(std::cerr);
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
