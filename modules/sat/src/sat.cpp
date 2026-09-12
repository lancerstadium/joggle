#include "joggle/joggle.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace {

bool integer(const joggle_call* call, std::size_t index, std::int64_t& out) {
  joggle_value value{};
  if (!call->api->arg(call, index, &value) || value.kind != JOGGLE_I64)
    return false;
  out = value.data.integer;
  return true;
}

bool result(joggle_call* call, std::int64_t value) {
  joggle_value out{};
  out.kind = JOGGLE_I64;
  out.data.integer = value;
  return call->api->ret(call, 0, &out);
}

bool result(joggle_call* call, std::string_view value) {
  joggle_value out{};
  out.kind = JOGGLE_STR;
  out.data.string = {value.data(), value.size()};
  return call->api->ret(call, 0, &out);
}

bool sim(joggle_call* call, void*) {
  std::int64_t bits = 0;
  std::int64_t left = 0;
  std::int64_t right = 0;
  if (call->api->arg_count(call) != 3 || !integer(call, 0, bits) ||
      !integer(call, 1, left) || !integer(call, 2, right) || bits < 2 ||
      bits > 63)
    return call->api->fail(call, "expected width 2..63 and two integers");
  const std::int64_t limit = std::int64_t{1} << (bits - 1);
  const std::int64_t low = -limit;
  const std::int64_t high = limit - 1;
  if (left < low || left > high || right < low || right > high)
    return call->api->fail(call, "operand is outside the selected format");
  if (right > 0 && left > high - right)
    return result(call, high);
  if (right < 0 && left < low - right)
    return result(call, low);
  return result(call, left + right);
}

bool emit(joggle_call* call, void*) {
  std::int64_t bits = 0;
  if (call->api->arg_count(call) != 1 || !integer(call, 0, bits) || bits < 2 ||
      bits > 63)
    return call->api->fail(call, "expected a width from 2 to 63");
  const std::uint64_t limit = std::uint64_t{1} << (bits - 1);
  std::ostringstream out;
  out << "module sat_add_" << bits << "(\n"
      << "  input logic signed [" << bits - 1 << ":0] a, b,\n"
      << "  output logic signed [" << bits - 1 << ":0] y\n"
      << ");\n"
      << "  logic signed [" << bits << ":0] sum;\n"
      << "  always_comb begin\n"
      << "    sum = {a[" << bits - 1 << "], a} + {b[" << bits - 1 << "], b};\n"
      << "    if (sum > " << bits + 1 << "'sd" << limit - 1 << ")\n"
      << "      y = " << bits + 1 << "'sd" << limit - 1 << ";\n"
      << "    else if (sum < -" << bits + 1 << "'sd" << limit << ")\n"
      << "      y = -" << bits + 1 << "'sd" << limit << ";\n"
      << "    else\n"
      << "      y = sum[" << bits - 1 << ":0];\n"
      << "  end\n"
      << "endmodule\n";
  return result(call, out.str());
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const joggle_api* api,
                                        joggle_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "sat.sim", sim, nullptr) &&
         api->bind(module, "sat.emit", emit, nullptr);
}
