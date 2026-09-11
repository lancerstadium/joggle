#include "joggle/joggle.h"

#include <bit>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
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

bool natural(std::string_view text, std::size_t& out) {
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), out);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
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

void store64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (unsigned shift = 0; shift != 64; shift += 8)
    out.push_back(static_cast<std::uint8_t>(value >> shift));
}

struct Value {
  std::uint64_t scalar = 0;
  std::shared_ptr<std::vector<std::uint64_t>> tensor;
};

Value scalar(std::uint64_t value) {
  return Value{value, {}};
}

struct State {
  std::unordered_map<int, Value> regs;
  Value result;
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

  const Value* read(int id) {
    const auto found = regs.find(id);
    if (found == regs.end()) {
      error = "read of undefined register " + std::to_string(id);
      return nullptr;
    }
    return &found->second;
  }

  bool read_scalar(int id, std::uint64_t& out) {
    const Value* value = read(id);
    if (!value)
      return false;
    if (value->tensor) {
      error = "register " + std::to_string(id) + " is not scalar";
      return false;
    }
    out = value->scalar;
    return true;
  }
};

bool execute(const std::vector<Line>& code, std::size_t first,
             std::size_t last, State& state);

bool branch_bounds(const std::vector<Line>& code, std::size_t at,
                   std::size_t last, std::size_t& otherwise,
                   std::size_t& end, std::string& error) {
  std::size_t depth = 0;
  otherwise = last;
  for (std::size_t index = at + 1; index < last; ++index) {
    const std::string_view op = code[index].front();
    if (op == "if" || op == "loop") {
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

bool loop_end(const std::vector<Line>& code, std::size_t at,
              std::size_t last, std::size_t& end, std::string& error) {
  std::size_t depth = 0;
  for (std::size_t index = at + 1; index < last; ++index) {
    const std::string_view op = code[index].front();
    if (op == "if" || op == "loop") {
      ++depth;
    } else if (op == "end") {
      if (depth == 0) {
        end = index;
        return true;
      }
      --depth;
    }
  }
  error = "unterminated loop";
  return false;
}

bool unary(std::string_view op, std::uint64_t input, std::uint64_t& output) {
  if (op == "neg")
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

bool tensor_access(const Line& line, bool store, State& state) {
  int out_id = -1;
  int tensor_id = -1;
  std::size_t rank = 0;
  if (line.size() < 4 || !reg(line[1], out_id) ||
      !reg(line[2], tensor_id) || !natural(line[3], rank) || rank == 0 ||
      rank > (std::numeric_limits<std::size_t>::max() - 5) / 2 ||
      line.size() != (store ? 5 : 4) + 2 * rank) {
    state.error = "invalid tensor access instruction";
    return false;
  }
  const Value* source = state.read(tensor_id);
  if (!source)
    return false;
  const Value tensor = *source;
  if (!tensor.tensor) {
    state.error = "tensor access on a scalar register";
    return false;
  }
  std::size_t offset = 0;
  std::size_t extent_product = 1;
  for (std::size_t axis = 0; axis < rank; ++axis) {
    std::size_t extent = 0;
    int index_id = -1;
    std::uint64_t index_bits = 0;
    if (!natural(line[4 + axis], extent) ||
        !reg(line[4 + rank + axis], index_id) ||
        !state.read_scalar(index_id, index_bits))
      return false;
    const std::int64_t index = signed_value(index_bits);
    if (index < 0 || static_cast<std::uint64_t>(index) >= extent) {
      state.error = "tensor index is out of bounds";
      return false;
    }
    if (extent != 0 &&
        (extent_product > std::numeric_limits<std::size_t>::max() / extent ||
         offset > std::numeric_limits<std::size_t>::max() / extent)) {
      state.error = "tensor extent overflow";
      return false;
    }
    extent_product *= extent;
    offset = offset * extent + static_cast<std::size_t>(index);
  }
  if (extent_product != tensor.tensor->size() ||
      offset >= tensor.tensor->size()) {
    state.error = "tensor access shape does not match storage";
    return false;
  }
  if (store) {
    int value_id = -1;
    std::uint64_t value = 0;
    if (!reg(line.back(), value_id) || !state.read_scalar(value_id, value))
      return false;
    (*tensor.tensor)[offset] = value;
    state.regs[out_id] = tensor;
  } else {
    state.regs[out_id] = scalar((*tensor.tensor)[offset]);
  }
  return state.tick();
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
          !state.read_scalar(condition_id, condition) ||
          !branch_bounds(code, at, last, otherwise, end, state.error) ||
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
    if (op == "loop") {
      int iterator_id = -1;
      int first_id = -1;
      int last_id = -1;
      std::uint64_t first_bits = 0;
      std::uint64_t last_bits = 0;
      std::size_t end = last;
      if (line.size() != 4 || !reg(line[1], iterator_id) ||
          !reg(line[2], first_id) || !reg(line[3], last_id) ||
          !state.read_scalar(first_id, first_bits) ||
          !state.read_scalar(last_id, last_bits) ||
          !loop_end(code, at, last, end, state.error))
        return false;
      std::int64_t value = signed_value(first_bits);
      const std::int64_t stop = signed_value(last_bits);
      while (!state.returned) {
        if (!state.tick())
          return false;
        if (value >= stop)
          break;
        state.regs[iterator_id] = scalar(bits(value));
        if (!execute(code, at + 1, end, state))
          return false;
        ++value;
      }
      at = end;
      continue;
    }
    if (op == "else" || op == "end") {
      state.error = "unexpected " + std::string(op);
      return false;
    }
    if (op == "ret") {
      int id = -1;
      std::size_t count = 1;
      if ((line.size() != 3 && line.size() != 4) ||
          !reg(line[1], id) ||
          (line[2] != "s" && line[2] != "t") ||
          (line[2] == "s" && line.size() != 3) ||
          (line[2] == "t" &&
           (line.size() != 4 || !natural(line[3], count)))) {
        state.error = "invalid return instruction";
        return false;
      }
      const Value* value = state.read(id);
      if (!value || (line[2] == "s" && value->tensor) ||
          (line[2] == "t" &&
           (!value->tensor || value->tensor->size() != count)) ||
          !state.tick())
        return false;
      state.result = *value;
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
      state.regs[out_id] = scalar(bits(value));
      continue;
    }
    if (op == "alloc") {
      std::size_t count = 0;
      int fill_id = -1;
      std::uint64_t fill = 0;
      if (line.size() != 4 || !reg(line[1], out_id) ||
          !natural(line[2], count) || !reg(line[3], fill_id) ||
          !state.read_scalar(fill_id, fill) || !state.tick()) {
        if (state.error.empty())
          state.error = "invalid alloc instruction";
        return false;
      }
      state.regs[out_id].tensor =
          std::make_shared<std::vector<std::uint64_t>>(count, fill);
      continue;
    }
    if (op == "load" || op == "store") {
      if (!tensor_access(line, op == "store", state))
        return false;
      continue;
    }
    if (op == "copy") {
      if (line.size() != 3 || !reg(line[1], out_id) ||
          !reg(line[2], left_id)) {
        state.error = "invalid copy instruction";
        return false;
      }
      const Value* input = state.read(left_id);
      if (!input)
        return false;
      const Value copied = *input;
      if (!state.tick())
        return false;
      state.regs[out_id] = copied;
      continue;
    }
    if (line.size() == 3) {
      std::uint64_t input = 0;
      std::uint64_t output = 0;
      if (!reg(line[1], out_id) || !reg(line[2], left_id) ||
          !state.read_scalar(left_id, input) || !unary(op, input, output) ||
          !state.tick()) {
        if (state.error.empty())
          state.error = "invalid unary instruction";
        return false;
      }
      state.regs[out_id] = scalar(output);
      continue;
    }
    if (line.size() == 4) {
      int right_id = -1;
      std::uint64_t left = 0;
      std::uint64_t right = 0;
      std::uint64_t output = 0;
      if (!reg(line[1], out_id) || !reg(line[2], left_id) ||
          !reg(line[3], right_id) || !state.read_scalar(left_id, left) ||
          !state.read_scalar(right_id, right) ||
          !binary(op, left, right, output, state.error) || !state.tick()) {
        if (state.error.empty())
          state.error = "invalid binary instruction";
        return false;
      }
      state.regs[out_id] = scalar(output);
      continue;
    }
    state.error = "unknown instruction " + std::string(op);
    return false;
  }
  return true;
}

bool run_image(jog_call* call) {
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
    std::size_t count = 1;
    if ((code[first].size() != 3 && code[first].size() != 4) ||
        !reg(code[first][1], id) ||
        (code[first][2] != "s" && code[first][2] != "t") ||
        (code[first][2] == "s" && code[first].size() != 3) ||
        (code[first][2] == "t" &&
         (code[first].size() != 4 || !natural(code[first][3], count))) ||
        count > (input_size - offset) / 8)
      return fail(call, "input does not match function parameters");
    if (code[first][2] == "s") {
      state.regs[id] = scalar(load64(bytes + offset));
    } else {
      auto values = std::make_shared<std::vector<std::uint64_t>>();
      values->reserve(count);
      for (std::size_t item = 0; item < count; ++item)
        values->push_back(load64(bytes + offset + item * 8));
      state.regs[id].tensor = std::move(values);
    }
    offset += count * 8;
    ++first;
  }
  if (offset != input_size)
    return fail(call, "input does not match function parameters");
  if (!execute(code, first, last, state))
    return fail(call, state.error.empty() ? "execution failed" : state.error);
  if (!state.returned)
    return fail(call, "function did not return");

  std::vector<std::uint8_t> output;
  if (state.result.tensor) {
    if (state.result.tensor->size() >
        std::numeric_limits<std::size_t>::max() / 8)
      return fail(call, "vm result is too large");
    output.reserve(state.result.tensor->size() * 8);
    for (const std::uint64_t value : *state.result.tensor)
      store64(output, value);
  } else {
    output.reserve(8);
    store64(output, state.result.scalar);
  }
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

bool run(jog_call* call, void*) {
  try {
    return run_image(call);
  } catch (const std::bad_alloc&) {
    return call->api->fail(call, "vm tensor allocation failed");
  } catch (const std::length_error&) {
    return call->api->fail(call, "vm image requests oversized storage");
  } catch (...) {
    return call->api->fail(call, "vm execution failed unexpectedly");
  }
}

}  // namespace

JOGGLE_MODULE_EXPORT bool joggle_module(const jog_api* api,
                                        jog_module* module) {
  return joggle::compatible(api) &&
         api->bind(module, "vm.run", run, nullptr);
}
