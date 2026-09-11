#include "joggle/joggle.h"

#include <bit>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

using Line = std::vector<std::string_view>;

bool fail(jog_call* call, std::string message) {
  return call->api->fail(call, message.c_str());
}

bool argument(const jog_call* call, std::size_t index, jog_value_kind kind,
              jog_value& out) {
  return call->api->arg(call, index, &out) && out.kind == kind;
}

std::vector<Line> lines(std::string_view source) {
  std::vector<Line> out;
  while (!source.empty()) {
    const std::size_t newline = source.find('\n');
    std::string_view line = source.substr(0, newline);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    Line tokens;
    while (!line.empty()) {
      while (!line.empty() &&
             std::isspace(static_cast<unsigned char>(line.front())))
        line.remove_prefix(1);
      if (line.empty())
        break;
      std::size_t end = 0;
      while (end < line.size() &&
             !std::isspace(static_cast<unsigned char>(line[end])))
        ++end;
      tokens.push_back(line.substr(0, end));
      line.remove_prefix(end);
    }
    if (!tokens.empty())
      out.push_back(std::move(tokens));
    if (newline == std::string_view::npos)
      break;
    source.remove_prefix(newline + 1);
  }
  return out;
}

bool integer(std::string_view text, std::int64_t& out) {
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), out);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool reg(std::string_view text, int& out) {
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), out);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
         out >= 0;
}

std::uint64_t bits(std::int64_t value) {
  return std::bit_cast<std::uint64_t>(value);
}

std::int64_t signed_value(std::uint64_t value) {
  return std::bit_cast<std::int64_t>(value);
}

std::uint64_t load64(const unsigned char* data) {
  std::uint64_t out = 0;
  for (unsigned shift = 0; shift != 64; shift += 8)
    out |= std::uint64_t{*data++} << shift;
  return out;
}

std::vector<std::uint8_t> store64(std::uint64_t value) {
  std::vector<std::uint8_t> out(8);
  for (unsigned shift = 0; shift != 64; shift += 8)
    out[shift / 8] = static_cast<std::uint8_t>(value >> shift);
  return out;
}

struct State {
  std::unordered_map<int, std::uint64_t> regs;
  std::uint64_t result = 0;
  std::int64_t steps = 0;
  bool returned = false;
  std::string error;

  bool tick() {
    if (steps == std::numeric_limits<std::int64_t>::max()) {
      error = "step count overflow";
      return false;
    }
    ++steps;
    return true;
  }

  bool read(int id, std::uint64_t& out) {
    const auto found = regs.find(id);
    if (found == regs.end()) {
      error = "read of undefined register " + std::to_string(id);
      return false;
    }
    out = found->second;
    return true;
  }
};

bool execute(const std::vector<Line>& code, std::size_t first,
             std::size_t last, State& state);

bool bounds(const std::vector<Line>& code, std::size_t at, std::size_t last,
            std::size_t& otherwise, std::size_t& end, std::string& error) {
  std::size_t depth = 0;
  otherwise = last;
  for (std::size_t index = at + 1; index < last; ++index) {
    const std::string_view op = code[index].front();
    if (op == "if") {
      ++depth;
    } else if (op == "end") {
      if (depth == 0) {
        end = index;
        return true;
      }
      --depth;
    } else if (op == "else" && depth == 0) {
      if (otherwise != last) {
        error = "duplicate else";
        return false;
      }
      otherwise = index;
    }
  }
  error = "unterminated if";
  return false;
}

bool unary(std::string_view op, std::uint64_t input, std::uint64_t& output) {
  if (op == "copy")
    output = input;
  else if (op == "neg")
    output = std::uint64_t{0} - input;
  else if (op == "lnot")
    output = input == 0;
  else if (op == "bnot")
    output = ~input;
  else
    return false;
  return true;
}

bool binary(std::string_view op, std::uint64_t left, std::uint64_t right,
            std::uint64_t& output, std::string& error) {
  const std::int64_t lhs = signed_value(left);
  const std::int64_t rhs = signed_value(right);
  if (op == "add")
    output = left + right;
  else if (op == "sub")
    output = left - right;
  else if (op == "mul")
    output = left * right;
  else if (op == "div" || op == "rem") {
    if (rhs == 0) {
      error = "division by zero";
      return false;
    }
    if (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1) {
      output = op == "div" ? bits(lhs) : 0;
    } else {
      output = bits(op == "div" ? lhs / rhs : lhs % rhs);
    }
  } else if (op == "and")
    output = left & right;
  else if (op == "or")
    output = left | right;
  else if (op == "xor")
    output = left ^ right;
  else if (op == "shl" || op == "shr") {
    if (rhs < 0 || rhs >= 64) {
      error = "invalid shift count";
      return false;
    }
    const unsigned shift = static_cast<unsigned>(rhs);
    if (op == "shl") {
      output = left << shift;
    } else if ((left >> 63U) == 0 || shift == 0) {
      output = left >> shift;
    } else {
      output = ~(~left >> shift);
    }
  } else if (op == "eq")
    output = left == right;
  else if (op == "ne")
    output = left != right;
  else if (op == "lt")
    output = lhs < rhs;
  else if (op == "le")
    output = lhs <= rhs;
  else if (op == "gt")
    output = lhs > rhs;
  else if (op == "ge")
    output = lhs >= rhs;
  else if (op == "land")
    output = left != 0 && right != 0;
  else if (op == "lor")
    output = left != 0 || right != 0;
  else
    return false;
  return true;
}

bool execute(const std::vector<Line>& code, std::size_t first,
             std::size_t last, State& state) {
  for (std::size_t at = first; at < last && !state.returned; ++at) {
    const Line& line = code[at];
    const std::string_view op = line.front();
    if (op == "if") {
      int condition_id = -1;
      std::uint64_t condition = 0;
      std::size_t otherwise = last;
      std::size_t end = last;
      if (line.size() != 2 || !reg(line[1], condition_id) ||
          !state.read(condition_id, condition) ||
          !bounds(code, at, last, otherwise, end, state.error) ||
          !state.tick())
        return false;
      const std::size_t then_end = otherwise == last ? end : otherwise;
      const std::size_t arm_first = condition ? at + 1 : otherwise + 1;
      const std::size_t arm_last = condition ? then_end : end;
      if (!condition && otherwise == last) {
        at = end;
        continue;
      }
      if (!execute(code, arm_first, arm_last, state))
        return false;
      at = end;
      continue;
    }
    if (op == "else" || op == "end") {
      state.error = "unexpected " + std::string(op);
      return false;
    }
    if (op == "ret") {
      int id = -1;
      if (line.size() != 2 || !reg(line[1], id) ||
          !state.read(id, state.result) || !state.tick())
        return false;
      state.returned = true;
      continue;
    }
    int out_id = -1;
    int left_id = -1;
    if (op == "const") {
      std::int64_t value = 0;
      if (line.size() != 3 || !reg(line[1], out_id) ||
          !integer(line[2], value) || !state.tick()) {
        state.error = "invalid const instruction";
        return false;
      }
      state.regs[out_id] = bits(value);
      continue;
    }
    if (line.size() == 3) {
      std::uint64_t input = 0;
      std::uint64_t output = 0;
      if (!reg(line[1], out_id) || !reg(line[2], left_id) ||
          !state.read(left_id, input) || !unary(op, input, output) ||
          !state.tick()) {
        if (state.error.empty())
          state.error = "invalid unary instruction";
        return false;
      }
      state.regs[out_id] = output;
      continue;
    }
    if (line.size() == 4) {
      int right_id = -1;
      std::uint64_t left = 0;
      std::uint64_t right = 0;
      std::uint64_t output = 0;
      if (!reg(line[1], out_id) || !reg(line[2], left_id) ||
          !reg(line[3], right_id) || !state.read(left_id, left) ||
          !state.read(right_id, right) ||
          !binary(op, left, right, output, state.error) || !state.tick()) {
        if (state.error.empty())
          state.error = "invalid binary instruction";
        return false;
      }
      state.regs[out_id] = output;
      continue;
    }
    state.error = "unknown instruction " + std::string(op);
    return false;
  }
  return true;
}

bool run(jog_call* call, void*) {
  jog_value image{};
  jog_value entry{};
  jog_value input{};
  if (call->api->arg_count(call) != 3 ||
      !argument(call, 0, JOG_STR, image) ||
      !argument(call, 1, JOG_STR, entry) ||
      !argument(call, 2, JOG_BYTES, input))
    return fail(call, "expected image, entry name, and input bytes");
  const std::string_view source(image.data.string.data,
                                image.data.string.size);
  const std::string_view entry_name(entry.data.string.data,
                                    entry.data.string.size);
  const std::vector<Line> code = lines(source);
  if (code.empty() || code.front().size() != 2 ||
      code.front()[0] != "joggle-vm" || code.front()[1] != "1")
    return fail(call, "invalid image header");

  std::size_t first = code.size();
  std::size_t last = code.size();
  for (std::size_t at = 1; at < code.size(); ++at) {
    if (code[at].size() == 2 && code[at][0] == "fn" &&
        code[at][1] == entry_name) {
      if (first != code.size())
        return fail(call, "duplicate entry function");
      first = at + 1;
      std::size_t depth = 0;
      for (std::size_t end = first; end < code.size(); ++end) {
        if (code[end][0] == "if")
          ++depth;
        else if (code[end][0] == "end" && depth)
          --depth;
        else if (code[end][0] == "endfn" && depth == 0) {
          last = end;
          break;
        }
      }
    }
  }
  if (first == code.size() || last == code.size())
    return fail(call, "entry function not found or unterminated");

  State state;
  const auto* bytes =
      reinterpret_cast<const unsigned char*>(input.data.bytes.data);
  const std::size_t input_size = input.data.bytes.size;
  std::size_t offset = 0;
  while (first < last && code[first][0] == "param") {
    int id = -1;
    if (code[first].size() != 2 || !reg(code[first][1], id) ||
        offset + 8 > input_size)
      return fail(call, "input does not match function parameters");
    state.regs[id] = load64(bytes + offset);
    offset += 8;
    ++first;
  }
  if (offset != input_size)
    return fail(call, "input does not match function parameters");
  if (!execute(code, first, last, state))
    return fail(call, state.error.empty() ? "execution failed" : state.error);
  if (!state.returned)
    return fail(call, "function did not return");

  const std::vector<std::uint8_t> output = store64(state.result);
  jog_value result{};
  result.kind = JOG_BYTES;
  result.data.bytes = {reinterpret_cast<const char*>(output.data()),
                       output.size()};
  if (!call->api->ret(call, 0, &result))
    return false;
  jog_value steps{};
  steps.kind = JOG_I64;
  steps.data.integer = state.steps;
  return call->api->ret(call, 1, &steps);
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const jog_api* api,
                                        jog_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "vm.run", run, nullptr);
}
