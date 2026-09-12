#include "joggle/joggle.h"

#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
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

void append(joggle::Attr::Bytes& out, std::int64_t value) {
  const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
  for (unsigned shift = 0; shift != 64; shift += 8)
    out.push_back(static_cast<std::uint8_t>(bits >> shift));
}

void append(joggle::Attr::Bytes& out, float value) {
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  for (unsigned shift = 0; shift != 32; shift += 8)
    out.push_back(static_cast<std::uint8_t>(bits >> shift));
}

void append(joggle::Attr::Bytes& out, double value) {
  const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
  for (unsigned shift = 0; shift != 64; shift += 8)
    out.push_back(static_cast<std::uint8_t>(bits >> shift));
}

std::vector<std::int64_t> integers(const joggle::Attr::Bytes& bytes) {
  std::vector<std::int64_t> out;
  if (bytes.size() % 8 != 0)
    return out;
  out.reserve(bytes.size() / 8);
  for (std::size_t offset = 0; offset < bytes.size(); offset += 8) {
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift != 64; shift += 8)
      value |= std::uint64_t{bytes[offset + shift / 8]} << shift;
    out.push_back(std::bit_cast<std::int64_t>(value));
  }
  return out;
}

std::vector<float> floats(const joggle::Attr::Bytes& bytes) {
  std::vector<float> out;
  if (bytes.size() % 4 != 0)
    return out;
  out.reserve(bytes.size() / 4);
  for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift != 32; shift += 8)
      value |= std::uint32_t{bytes[offset + shift / 8]} << shift;
    out.push_back(std::bit_cast<float>(value));
  }
  return out;
}

double real(const joggle::Attr::Bytes& bytes) {
  if (bytes.size() != 8)
    return 0;
  std::uint64_t value = 0;
  for (unsigned shift = 0; shift != 64; shift += 8)
    value |= std::uint64_t{bytes[shift / 8]} << shift;
  return std::bit_cast<double>(value);
}

bool execute_bytes(joggle::Env& env, std::string image, std::string entry,
                   joggle::Attr::Bytes input, joggle::Attr::Bytes& result,
                   std::int64_t& steps) {
  const std::vector<joggle::Attr> args{joggle::Attr(std::move(image)),
                                       joggle::Attr(std::move(entry)),
                                       joggle::Attr(std::move(input))};
  std::vector<joggle::Attr> returns;
  if (!env.call("vm.run", args, returns) || returns.size() != 2 ||
      !returns[0].bytes() || !returns[1].integer())
    return false;
  result = *returns[0].bytes();
  steps = *returns[1].integer();
  return true;
}

bool execute(joggle::Env& env, std::string image, std::string entry,
             std::vector<std::int64_t> inputs, joggle::Attr::Bytes& result,
             std::int64_t& steps) {
  joggle::Attr::Bytes bytes;
  for (const std::int64_t input : inputs)
    append(bytes, input);
  return execute_bytes(env, std::move(image), std::move(entry),
                       std::move(bytes), result, steps);
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(argc == 5);
  std::ifstream input(argv[1]);
  CHECK(input);
  std::ostringstream source;
  source << input.rdbuf();

  joggle::Env env;
  env.path(argv[4]);
  CHECK(env.load("vm"));
  CHECK(env.load("opt"));
  CHECK(env.load("c"));
  CHECK(env.load("math"));
  CHECK(env.load("nn"));
  joggle::Mod model;
  CHECK(joggle::parse(env, source.str(), model, argv[1]));
  CHECK(model.verify(env));
  CHECK(joggle::run(env, "vm.prepare", model));
  CHECK(model.verify(env));

  joggle::Attr image;
  CHECK(joggle::query(env, "vm.image", model, image));
  CHECK(image.string());
  CHECK(image.string()->starts_with("joggle-vm 3\nfn main\n"));
  joggle::Attr repeated_image;
  CHECK(joggle::query(env, "vm.image", model, repeated_image));
  CHECK(repeated_image == image);

  joggle::Attr::Bytes result;
  std::int64_t then_steps = 0;
  CHECK(!execute_bytes(env, "joggle-vm 2\n", "main", {}, result,
                       then_steps));
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 1}, result,
                then_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{30} && then_steps > 0);
  std::int64_t again_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 1}, result,
                again_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{30} &&
        again_steps == then_steps);

  std::int64_t else_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "main", {10, 5, 0}, result,
                else_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{14} &&
        else_steps == then_steps);

  std::int64_t pick_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "pick", {1}, result,
                pick_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{5} && pick_steps > 0);
  CHECK(!execute(env, std::string(*image.string()), "pick", {-1}, result,
                 pick_steps));
  CHECK(!execute(env, std::string(*image.string()), "pick", {3}, result,
                 pick_steps));

  joggle::Attr::Bytes root_input;
  append(root_input, 9.0F);
  std::int64_t root_steps = 0;
  CHECK(execute_bytes(env, std::string(*image.string()), "root",
                      std::move(root_input), result, root_steps));
  CHECK(floats(result) == std::vector<float>{3.0F} && root_steps > 0);

  joggle::Attr::Bytes unary_math_input;
  append(unary_math_input, 1.5);
  std::int64_t unary_math_steps = 0;
  CHECK(execute_bytes(env, std::string(*image.string()), "unary_math",
                      std::move(unary_math_input), result,
                      unary_math_steps));
  const double unary_math_expected =
      std::abs(1.5) + std::floor(1.5) + std::log(1.5) + std::erf(1.5) +
      std::exp(1.5) + std::ceil(1.5) + std::tanh(1.5) +
      std::nearbyint(1.5);
  CHECK(std::abs(real(result) - unary_math_expected) < 1.0e-12 &&
        unary_math_steps > root_steps);

  joggle::Attr::Bytes power_input;
  append(power_input, 2.0);
  append(power_input, 5.0);
  std::int64_t power_steps = 0;
  CHECK(execute_bytes(env, std::string(*image.string()), "power",
                      std::move(power_input), result, power_steps));
  CHECK(real(result) == 32.0 && power_steps > 0);

  CHECK(std::fesetround(FE_UPWARD) == 0);
  joggle::Attr::Bytes nearest_input;
  append(nearest_input, 2.5);
  std::int64_t nearest_steps = 0;
  CHECK(execute_bytes(env, std::string(*image.string()), "nearest",
                      std::move(nearest_input), result, nearest_steps));
  const bool fixed_rounding = real(result) == 2.0;
  CHECK(std::fesetround(FE_TONEAREST) == 0);
  CHECK(fixed_rounding && nearest_steps > 0);

  std::int64_t shift_steps = 0;
  CHECK(execute(env, std::string(*image.string()), "shift", {-8, 2}, result,
                shift_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{-2} && shift_steps > 0);

  CHECK(!execute(env, std::string(*image.string()), "shift", {-8, 64},
                 result, shift_steps));
  CHECK(!execute(env, std::string(*image.string()), "divide", {1, 0}, result,
                 shift_steps));
  CHECK(!execute(env, "not-a-vm", "main", {10, 5, 1}, result, else_steps));
  CHECK(!execute_bytes(env,
                       "joggle-vm 3\nfn bad\nconst i64 0 1\nif 0\n"
                       "else\nelse\nend\nret 0 s i64\nendfn\n",
                       "bad", {}, result, else_steps));
  CHECK(!execute_bytes(env,
                       "joggle-vm 3\nfn bad\nconst i64 0 0\n"
                       "loop 1 0 0\nret 0 s i64\nendfn\n",
                       "bad", {}, result, else_steps));
  CHECK(!execute_bytes(env,
                       "joggle-vm 3\nfn bad\nconst i64 0 0\nif 0\n"
                       "unknown\nelse\nret 0 s i64\nend\nendfn\n",
                       "bad", {}, result, else_steps));
  CHECK(!env.diags().empty());

  joggle::Attr::Bytes convert_input;
  append(convert_input, 1.5F);
  std::int64_t convert_steps = 0;
  CHECK(execute_bytes(env, std::string(*image.string()), "convert",
                      std::move(convert_input), result, convert_steps));
  CHECK(real(result) == 1.75 && convert_steps > 0);
  joggle::Attr::Bytes truncate_input;
  append(truncate_input, 1.75);
  std::int64_t truncate_steps = 0;
  CHECK(execute_bytes(env, std::string(*image.string()), "truncate",
                      std::move(truncate_input), result, truncate_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{1});
  joggle::Attr::Bytes overflow_input;
  append(overflow_input, 0x1p63);
  CHECK(!execute_bytes(env, std::string(*image.string()), "truncate",
                       std::move(overflow_input), result, truncate_steps));
  std::int64_t truth_steps = 0;
  CHECK(execute_bytes(env, std::string(*image.string()), "truth", {}, result,
                      truth_steps));
  CHECK(integers(result) == std::vector<std::int64_t>{1});

  std::ifstream tensor_input(argv[2]);
  CHECK(tensor_input);
  std::ostringstream tensor_source;
  tensor_source << tensor_input.rdbuf();
  joggle::Mod tensor_model;
  CHECK(joggle::parse(env, tensor_source.str(), tensor_model, argv[2]));
  CHECK(tensor_model.verify(env));
  const std::vector<joggle::Attr> selection{joggle::Attr("int_add")};
  joggle::Attr tensor_image;
  if (!joggle::query(env, "vm.image", tensor_model, tensor_image, selection)) {
    env.print_diags(stderr);
    return 1;
  }
  CHECK(tensor_image.string());
  CHECK(tensor_image.string()->starts_with("joggle-vm 3\nfn int_add\n"));
  std::int64_t tensor_steps = 0;
  CHECK(execute(env, std::string(*tensor_image.string()), "int_add",
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, result,
                tensor_steps));
  CHECK(integers(result) ==
        (std::vector<std::int64_t>{8, 10, 12, 14, 16, 18}));
  CHECK(tensor_steps > then_steps);

  const std::vector<joggle::Attr> matmul_selection{
      joggle::Attr("int_matmul")};
  joggle::Attr matmul_image;
  CHECK(joggle::query(env, "vm.image", tensor_model, matmul_image,
                      matmul_selection));
  CHECK(matmul_image.string());
  std::int64_t matmul_steps = 0;
  CHECK(execute(env, std::string(*matmul_image.string()), "int_matmul",
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, result,
                matmul_steps));
  CHECK(integers(result) ==
        (std::vector<std::int64_t>{58, 64, 139, 154}));
  CHECK(matmul_steps > tensor_steps);
  std::int64_t repeated_matmul_steps = 0;
  CHECK(execute(env, std::string(*matmul_image.string()), "int_matmul",
                {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, result,
                repeated_matmul_steps));
  CHECK(integers(result) ==
        (std::vector<std::int64_t>{58, 64, 139, 154}));
  CHECK(repeated_matmul_steps == matmul_steps);

  const std::vector<joggle::Attr> float_selection{joggle::Attr("matmul")};
  joggle::Attr float_image;
  CHECK(joggle::query(env, "vm.image", tensor_model, float_image,
                      float_selection));
  CHECK(float_image.string());
  joggle::Attr::Bytes float_input;
  for (const float value :
       {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F,
        7.0F, 8.0F, 9.0F, 10.0F, 11.0F, 12.0F})
    append(float_input, value);
  std::int64_t float_steps = 0;
  CHECK(execute_bytes(env, std::string(*float_image.string()), "matmul",
                      std::move(float_input), result, float_steps));
  CHECK(floats(result) == (std::vector<float>{58.0F, 64.0F, 139.0F, 154.0F}));
  CHECK(float_steps == matmul_steps);
  CHECK(!execute(env, std::string(*float_image.string()), "matmul",
                 {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, result,
                 float_steps));

  const std::vector<joggle::Attr> weight_selection{joggle::Attr("weights")};
  joggle::Attr weight_image;
  CHECK(joggle::query(env, "vm.image", tensor_model, weight_image,
                      weight_selection));
  CHECK(weight_image.string());
  std::int64_t weight_steps = 0;
  CHECK(execute_bytes(env, std::string(*weight_image.string()), "weights", {},
                      result, weight_steps));
  CHECK(floats(result) == (std::vector<float>{1.0F, 2.0F}));
  CHECK(weight_steps > 0);

  constexpr std::string_view bad_literal_source =
      "module bad.literal\n"
      "use tensor\n"
      "fn main() -> tensor<f32, [2]> {\n"
      "  let x: tensor<f32, [2]> = tensor.literal(hex\"00\")\n"
      "  return x\n"
      "}\n";
  joggle::Mod bad_literal;
  CHECK(joggle::parse(env, bad_literal_source, bad_literal,
                      "bad-literal.jog"));
  CHECK(bad_literal.verify(env));
  joggle::Attr rejected_literal;
  CHECK(!joggle::query(env, "vm.image", bad_literal, rejected_literal));
  CHECK(!joggle::query(env, "c.source", bad_literal, rejected_literal));

  std::ifstream open_input(argv[3]);
  CHECK(open_input);
  std::ostringstream open_source;
  open_source << open_input.rdbuf();
  joggle::Mod open_model;
  CHECK(joggle::parse(env, open_source.str(), open_model, argv[3]));
  CHECK(open_model.verify(env));
  const std::string open_before = joggle::print(open_model);
  const std::uint64_t open_revision = open_model.revision();
  const std::vector<joggle::Attr> bad_expand_args{
      joggle::Attr(std::int64_t{1})};
  joggle::Attr expand_report;
  CHECK(!joggle::run(env, "opt.expand", open_model, expand_report,
                     bad_expand_args));
  CHECK(joggle::print(open_model) == open_before &&
        open_model.revision() == open_revision);
  const std::vector<joggle::Attr> expand_args{
      joggle::Attr(joggle::Attr::List{joggle::Attr("operator +")})};
  CHECK(joggle::run(env, "opt.expand", open_model, expand_report,
                    expand_args));
  CHECK(expand_report.dict() && expand_report.dict()->contains("args") &&
        expand_report.dict()->at("changed").boolean() == true);
  const std::vector<joggle::Attr> numel_args{
      joggle::Attr(joggle::Attr::List{joggle::Attr("tensor.numel")})};
  CHECK(joggle::run(env, "opt.expand", open_model, expand_report,
                    numel_args));
  CHECK(expand_report.dict() &&
        expand_report.dict()->at("changed").boolean() == true);
  std::chrono::nanoseconds expand_time;
  CHECK(joggle::run(env, "opt.expand", open_model, expand_report, numel_args,
                    expand_time));
  CHECK(expand_report.dict() && expand_report.dict()->contains("args") &&
        expand_report.dict()->at("changed").boolean() == false &&
        expand_time >= std::chrono::nanoseconds::zero());
  const std::vector<joggle::Attr> add_selection{joggle::Attr("add")};
  joggle::Attr add_image;
  if (!joggle::query(env, "vm.image", open_model, add_image, add_selection)) {
    env.print_diags(stderr);
    return 1;
  }
  CHECK(add_image.string());
  joggle::Attr::Bytes add_input;
  for (const float value : {1.0F, 2.0F, 3.0F, 4.0F,
                            5.0F, 6.0F, 7.0F, 8.0F})
    append(add_input, value);
  std::int64_t add_steps = 0;
  CHECK(execute_bytes(env, std::string(*add_image.string()), "add",
                      std::move(add_input), result, add_steps));
  CHECK(floats(result) == (std::vector<float>{6.0F, 8.0F, 10.0F, 12.0F}));
  CHECK(add_steps > 0);

  joggle::Mod prepared_open_model;
  CHECK(joggle::parse(env, open_source.str(), prepared_open_model, argv[3]));
  CHECK(prepared_open_model.verify(env));
  joggle::Attr prepare_report;
  CHECK(joggle::run(env, "vm.prepare", prepared_open_model,
                    prepare_report));
  CHECK(prepare_report.dict() &&
        prepare_report.dict()->at("changed").boolean() == true);
  const joggle::Attr::List* prepare_steps =
      prepare_report.dict()->at("steps").list();
  CHECK(prepare_steps && !prepare_steps->empty());
  for (const joggle::Attr& step : *prepare_steps) {
    const joggle::Attr::Dict* event = step.dict();
    CHECK(event);
    const auto kind = event->find("kind");
    const auto changed = event->find("changed");
    if (kind != event->end() && kind->second.string() == "fn")
      CHECK(changed != event->end() && changed->second.boolean() == true);
    const auto function = event->find("fn");
    CHECK(function == event->end() ||
          function->second.string() != "vm.accepts");
  }
  CHECK(prepared_open_model.verify(env));
  CHECK(joggle::run(env, "vm.prepare", prepared_open_model,
                    prepare_report));
  CHECK(prepare_report.dict() &&
        prepare_report.dict()->at("changed").boolean() == false);
  joggle::Attr prepared_add_image;
  CHECK(joggle::query(env, "vm.image", prepared_open_model,
                      prepared_add_image, add_selection));
  CHECK(prepared_add_image.string());
  joggle::Attr::Bytes prepared_add_input;
  for (const float value : {1.0F, 2.0F, 3.0F, 4.0F,
                            5.0F, 6.0F, 7.0F, 8.0F})
    append(prepared_add_input, value);
  std::int64_t prepared_add_steps = 0;
  CHECK(execute_bytes(env, std::string(*prepared_add_image.string()), "add",
                      std::move(prepared_add_input), result,
                      prepared_add_steps));
  CHECK(floats(result) ==
        (std::vector<float>{6.0F, 8.0F, 10.0F, 12.0F}));
  CHECK(prepared_add_steps == add_steps);
  const std::vector<joggle::Attr> sigmoid_selection{
      joggle::Attr("sigmoid")};
  joggle::Attr sigmoid_image;
  CHECK(joggle::query(env, "vm.image", prepared_open_model, sigmoid_image,
                      sigmoid_selection));
  CHECK(sigmoid_image.string());
  joggle::Attr::Bytes sigmoid_input;
  for (const float value : {-2.0F, -0.5F, 0.5F, 2.0F})
    append(sigmoid_input, value);
  std::int64_t sigmoid_steps = 0;
  CHECK(execute_bytes(env, std::string(*sigmoid_image.string()), "sigmoid",
                      std::move(sigmoid_input), result, sigmoid_steps));
  const std::vector<float> sigmoid_result = floats(result);
  CHECK(sigmoid_result.size() == 4 && sigmoid_steps > 0);
  const std::array<float, 4> sigmoid_values{-2.0F, -0.5F, 0.5F, 2.0F};
  for (std::size_t index = 0; index < sigmoid_values.size(); ++index) {
    const float expected = 1.0F / (1.0F + std::exp(-sigmoid_values[index]));
    CHECK(std::abs(sigmoid_result[index] - expected) < 1.0e-6F);
  }
  return 0;
}
