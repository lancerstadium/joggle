#include "joggle/joggle.h"

#include <bit>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <memory>
#include <new>
#include <sstream>
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

template <class T>
bool floating(std::string_view text, T& out) {
  std::istringstream input{std::string(text)};
  input.imbue(std::locale::classic());
  input >> out;
  return input && input.peek() == std::char_traits<char>::eof();
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

int nibble(char value) {
  if (value >= '0' && value <= '9')
    return value - '0';
  if (value >= 'a' && value <= 'f')
    return value - 'a' + 10;
  if (value >= 'A' && value <= 'F')
    return value - 'A' + 10;
  return -1;
}

bool byte_literal(std::string_view text, std::vector<std::uint8_t>& out) {
  if (!text.starts_with("hex\"") || text.size() < 5 || text.back() != '"')
    return false;
  text.remove_prefix(4);
  text.remove_suffix(1);
  if (text.size() % 2 != 0)
    return false;
  out.clear();
  out.reserve(text.size() / 2);
  for (std::size_t at = 0; at < text.size(); at += 2) {
    const int high = nibble(text[at]);
    const int low = nibble(text[at + 1]);
    if (high < 0 || low < 0)
      return false;
    out.push_back(static_cast<std::uint8_t>((high << 4) | low));
  }
  return true;
}

enum class Kind { i64, f32, f64 };

bool kind(std::string_view text, Kind& out) {
  if (text == "i64")
    out = Kind::i64;
  else if (text == "f32")
    out = Kind::f32;
  else if (text == "f64")
    out = Kind::f64;
  else
    return false;
  return true;
}

std::size_t width(Kind kind) {
  return kind == Kind::f32 ? 4 : 8;
}

std::uint64_t bits(std::int64_t value) {
  return std::bit_cast<std::uint64_t>(value);
}

std::uint64_t bits(float value) {
  return std::bit_cast<std::uint32_t>(value);
}

std::uint64_t bits(double value) {
  return std::bit_cast<std::uint64_t>(value);
}

std::int64_t signed_value(std::uint64_t value) {
  return std::bit_cast<std::int64_t>(value);
}

float float_value(std::uint64_t value) {
  return std::bit_cast<float>(static_cast<std::uint32_t>(value));
}

double double_value(std::uint64_t value) {
  return std::bit_cast<double>(value);
}

std::uint32_t load32(const unsigned char* data) {
  std::uint32_t out = 0;
  for (unsigned shift = 0; shift != 32; shift += 8)
    out |= std::uint32_t{*data++} << shift;
  return out;
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

void store32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (unsigned shift = 0; shift != 32; shift += 8)
    out.push_back(static_cast<std::uint8_t>(value >> shift));
}

std::uint64_t load_value(const unsigned char* data, Kind kind) {
  return kind == Kind::f32 ? load32(data) : load64(data);
}

void store_value(std::vector<std::uint8_t>& out, Kind kind,
                 std::uint64_t value) {
  if (kind == Kind::f32)
    store32(out, static_cast<std::uint32_t>(value));
  else
    store64(out, value);
}

struct Tensor {
  Kind kind = Kind::i64;
  std::vector<std::uint64_t> items;
};

struct Value {
  Kind kind = Kind::i64;
  std::uint64_t bits = 0;
  std::shared_ptr<Tensor> tensor;
};

Value scalar(Kind kind, std::uint64_t value) {
  return Value{kind, value, {}};
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

  bool read_scalar(int id, Kind expected, std::uint64_t& out) {
    const Value* value = read(id);
    if (!value)
      return false;
    if (value->tensor) {
      error = "register " + std::to_string(id) + " is not scalar";
      return false;
    }
    if (value->kind != expected) {
      error = "register " + std::to_string(id) + " has the wrong format";
      return false;
    }
    out = value->bits;
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
    if (op == "if" || op == "loop" || op == "each") {
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
    if (op == "if" || op == "loop" || op == "each") {
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

template <class T>
bool floating_unary(std::string_view op, T input, Value& output, Kind kind) {
  T result{};
  if (op == "neg")
    result = -input;
  else if (op == "sqrt")
    result = std::sqrt(input);
  else if (op == "exp")
    result = std::exp(input);
  else if (op == "ceil")
    result = std::ceil(input);
  else if (op == "tanh")
    result = std::tanh(input);
  else if (op == "round_even")
    result = std::nearbyint(input);
  else
    return false;
  output = scalar(kind, bits(result));
  return true;
}

bool unary(std::string_view op, Kind kind, std::uint64_t input,
           Value& output) {
  if (kind == Kind::f32)
    return floating_unary(op, float_value(input), output, kind);
  if (kind == Kind::f64)
    return floating_unary(op, double_value(input), output, kind);
  if (op == "neg") {
    output = scalar(kind, std::uint64_t{0} - input);
  } else if (op == "lnot" && kind == Kind::i64) {
    output = scalar(Kind::i64, input == 0);
  } else if (op == "bnot" && kind == Kind::i64) {
    output = scalar(kind, ~input);
  } else {
    return false;
  }
  return true;
}

template <class T>
bool floating_binary(std::string_view op, T left, T right, Value& output,
                     Kind kind) {
  if (op == "add")
    output = scalar(kind, bits(left + right));
  else if (op == "sub")
    output = scalar(kind, bits(left - right));
  else if (op == "mul")
    output = scalar(kind, bits(left * right));
  else if (op == "div")
    output = scalar(kind, bits(left / right));
  else if (op == "pow")
    output = scalar(kind, bits(std::pow(left, right)));
  else if (op == "eq")
    output = scalar(Kind::i64, left == right);
  else if (op == "ne")
    output = scalar(Kind::i64, left != right);
  else if (op == "lt")
    output = scalar(Kind::i64, left < right);
  else if (op == "le")
    output = scalar(Kind::i64, left <= right);
  else if (op == "gt")
    output = scalar(Kind::i64, left > right);
  else if (op == "ge")
    output = scalar(Kind::i64, left >= right);
  else
    return false;
  return true;
}

bool binary(std::string_view op, Kind kind, std::uint64_t left,
            std::uint64_t right, Value& output, std::string& error) {
  if (kind == Kind::f32)
    return floating_binary(op, float_value(left), float_value(right), output,
                           kind);
  if (kind == Kind::f64)
    return floating_binary(op, double_value(left), double_value(right), output,
                           kind);
  const std::int64_t lhs = signed_value(left);
  const std::int64_t rhs = signed_value(right);
  if (op == "add")
    output = scalar(kind, left + right);
  else if (op == "sub")
    output = scalar(kind, left - right);
  else if (op == "mul")
    output = scalar(kind, left * right);
  else if (op == "div" || op == "rem") {
    if (rhs == 0) {
      error = "division by zero";
      return false;
    }
    if (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1)
      output = scalar(kind, op == "div" ? bits(lhs) : 0);
    else
      output = scalar(kind, bits(op == "div" ? lhs / rhs : lhs % rhs));
  } else if (op == "and")
    output = scalar(kind, left & right);
  else if (op == "or")
    output = scalar(kind, left | right);
  else if (op == "xor")
    output = scalar(kind, left ^ right);
  else if (op == "shl" || op == "shr") {
    if (rhs < 0 || rhs >= 64) {
      error = "invalid shift count";
      return false;
    }
    const unsigned shift = static_cast<unsigned>(rhs);
    if (op == "shl")
      output = scalar(kind, left << shift);
    else if ((left >> 63U) == 0 || shift == 0)
      output = scalar(kind, left >> shift);
    else
      output = scalar(kind, ~(~left >> shift));
  } else if (op == "eq")
    output = scalar(Kind::i64, left == right);
  else if (op == "ne")
    output = scalar(Kind::i64, left != right);
  else if (op == "lt")
    output = scalar(Kind::i64, lhs < rhs);
  else if (op == "le")
    output = scalar(Kind::i64, lhs <= rhs);
  else if (op == "gt")
    output = scalar(Kind::i64, lhs > rhs);
  else if (op == "ge")
    output = scalar(Kind::i64, lhs >= rhs);
  else if (op == "land")
    output = scalar(Kind::i64, left != 0 && right != 0);
  else if (op == "lor")
    output = scalar(Kind::i64, left != 0 || right != 0);
  else
    return false;
  return true;
}

bool constant(std::string_view text, Kind kind, Value& output) {
  if (kind == Kind::i64) {
    if (text == "true" || text == "false") {
      output = scalar(kind, text == "true");
      return true;
    }
    std::int64_t value = 0;
    if (!integer(text, value))
      return false;
    output = scalar(kind, bits(value));
    return true;
  }
  if (kind == Kind::f32) {
    float value = 0;
    if (!floating(text, value))
      return false;
    output = scalar(kind, bits(value));
    return true;
  }
  double value = 0;
  if (!floating(text, value))
    return false;
  output = scalar(kind, bits(value));
  return true;
}

bool cast(Kind target, const Value& input, Value& output) {
  if (input.tensor)
    return false;
  if (target == input.kind) {
    output = input;
    return true;
  }
  if (target == Kind::f32) {
    const float value = input.kind == Kind::i64
                            ? static_cast<float>(signed_value(input.bits))
                            : static_cast<float>(double_value(input.bits));
    output = scalar(target, bits(value));
    return true;
  }
  if (target == Kind::f64) {
    const double value = input.kind == Kind::i64
                             ? static_cast<double>(signed_value(input.bits))
                             : static_cast<double>(float_value(input.bits));
    output = scalar(target, bits(value));
    return true;
  }
  const long double value = input.kind == Kind::f32
                                ? float_value(input.bits)
                                : double_value(input.bits);
  if (!std::isfinite(value) || value < -0x1p63L || value >= 0x1p63L)
    return false;
  output = scalar(target, bits(static_cast<std::int64_t>(value)));
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
        !state.read_scalar(index_id, Kind::i64, index_bits))
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
  if (extent_product != tensor.tensor->items.size() ||
      offset >= tensor.tensor->items.size()) {
    state.error = "tensor access shape does not match storage";
    return false;
  }
  if (store) {
    int value_id = -1;
    if (!reg(line.back(), value_id))
      return false;
    const Value* value = state.read(value_id);
    if (!value || value->tensor || value->kind != tensor.tensor->kind) {
      state.error = "tensor store format does not match its element";
      return false;
    }
    tensor.tensor->items[offset] = value->bits;
    state.regs[out_id] = tensor;
  } else {
    state.regs[out_id] =
        scalar(tensor.tensor->kind, tensor.tensor->items[offset]);
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
          !state.read_scalar(condition_id, Kind::i64, condition) ||
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
          !state.read_scalar(first_id, Kind::i64, first_bits) ||
          !state.read_scalar(last_id, Kind::i64, last_bits) ||
          !loop_end(code, at, last, end, state.error))
        return false;
      std::int64_t value = signed_value(first_bits);
      const std::int64_t stop = signed_value(last_bits);
      while (!state.returned) {
        if (!state.tick())
          return false;
        if (value >= stop)
          break;
        state.regs[iterator_id] = scalar(Kind::i64, bits(value));
        if (!execute(code, at + 1, end, state))
          return false;
        ++value;
      }
      at = end;
      continue;
    }
    if (op == "each") {
      int iterator_id = -1;
      Kind format = Kind::i64;
      std::size_t count = 0;
      std::size_t end = last;
      if (line.size() < 4 || !reg(line[1], iterator_id) ||
          !kind(line[2], format) || !natural(line[3], count) ||
          count > std::numeric_limits<std::size_t>::max() - 4 ||
          line.size() != count + 4 ||
          !loop_end(code, at, last, end, state.error))
        return false;
      for (std::size_t item = 0; item < count && !state.returned; ++item) {
        if (!state.tick())
          return false;
        int item_id = -1;
        if (!reg(line[4 + item], item_id)) {
          state.error = "invalid list-loop item register";
          return false;
        }
        const Value* value = state.read(item_id);
        if (!value || value->tensor || value->kind != format) {
          state.error = "list-loop item format does not match its iterator";
          return false;
        }
        const Value copied = *value;
        state.regs[iterator_id] = copied;
        if (!execute(code, at + 1, end, state))
          return false;
      }
      if (!state.returned && !state.tick())
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
      std::size_t count = 1;
      Kind format = Kind::i64;
      if ((line.size() != 4 && line.size() != 5) ||
          !reg(line[1], id) ||
          (line[2] != "s" && line[2] != "t") ||
          !kind(line[3], format) ||
          (line[2] == "s" && line.size() != 4) ||
          (line[2] == "t" &&
           (line.size() != 5 || !natural(line[4], count)))) {
        state.error = "invalid return instruction";
        return false;
      }
      const Value* value = state.read(id);
      if (!value || value->kind != format ||
          (line[2] == "s" && value->tensor) ||
          (line[2] == "t" &&
           (!value->tensor || value->tensor->kind != format ||
            value->tensor->items.size() != count)) ||
          !state.tick())
        return false;
      state.result = *value;
      state.returned = true;
      continue;
    }
    int out_id = -1;
    int left_id = -1;
    if (op == "const") {
      Kind format = Kind::i64;
      Value value;
      if (line.size() != 4 || !kind(line[1], format) ||
          !reg(line[2], out_id) || !constant(line[3], format, value) ||
          !state.tick()) {
        state.error = "invalid const instruction";
        return false;
      }
      state.regs[out_id] = value;
      continue;
    }
    if (op == "alloc") {
      Kind format = Kind::i64;
      std::size_t count = 0;
      int fill_id = -1;
      std::uint64_t fill = 0;
      if (line.size() != 5 || !kind(line[1], format) ||
          !reg(line[2], out_id) || !natural(line[3], count) ||
          !reg(line[4], fill_id) ||
          !state.read_scalar(fill_id, format, fill) || !state.tick()) {
        if (state.error.empty())
          state.error = "invalid alloc instruction";
        return false;
      }
      auto tensor = std::make_shared<Tensor>();
      tensor->kind = format;
      tensor->items.assign(count, fill);
      state.regs[out_id] = Value{format, 0, std::move(tensor)};
      continue;
    }
    if (op == "literal") {
      Kind format = Kind::i64;
      std::size_t count = 0;
      std::vector<std::uint8_t> data;
      if (line.size() != 5 || !kind(line[1], format) ||
          !reg(line[2], out_id) || !natural(line[3], count) ||
          !byte_literal(line[4], data) ||
          count > std::numeric_limits<std::size_t>::max() / width(format) ||
          data.size() != count * width(format)) {
        state.error = "invalid tensor literal instruction";
        return false;
      }
      auto tensor = std::make_shared<Tensor>();
      tensor->kind = format;
      tensor->items.reserve(count);
      for (std::size_t item = 0; item < count; ++item)
        tensor->items.push_back(
            load_value(data.data() + item * width(format), format));
      if (!state.tick())
        return false;
      state.regs[out_id] = Value{format, 0, std::move(tensor)};
      continue;
    }
    if (op == "cast") {
      Kind target = Kind::i64;
      if (line.size() != 4 || !kind(line[1], target) ||
          !reg(line[2], out_id) || !reg(line[3], left_id)) {
        state.error = "invalid cast instruction";
        return false;
      }
      const Value* input = state.read(left_id);
      Value output;
      if (!input || !cast(target, *input, output) || !state.tick()) {
        if (state.error.empty())
          state.error = "invalid scalar cast";
        return false;
      }
      state.regs[out_id] = output;
      continue;
    }
    if (op == "pick") {
      Kind format = Kind::i64;
      int index_id = -1;
      std::size_t count = 0;
      std::uint64_t index_bits = 0;
      if (line.size() < 5 || !kind(line[1], format) ||
          !reg(line[2], out_id) || !reg(line[3], index_id) ||
          !natural(line[4], count) ||
          count > std::numeric_limits<std::size_t>::max() - 5 ||
          line.size() != count + 5 ||
          !state.read_scalar(index_id, Kind::i64, index_bits)) {
        if (state.error.empty())
          state.error = "invalid list selection instruction";
        return false;
      }
      const std::int64_t index = signed_value(index_bits);
      if (index < 0 || static_cast<std::uint64_t>(index) >= count) {
        state.error = "list index is out of bounds";
        return false;
      }
      int item_id = -1;
      if (!reg(line[5 + static_cast<std::size_t>(index)], item_id)) {
        state.error = "invalid list selection item register";
        return false;
      }
      const Value* item = state.read(item_id);
      if (!item || item->tensor || item->kind != format || !state.tick()) {
        if (state.error.empty())
          state.error = "list selection item has the wrong format";
        return false;
      }
      state.regs[out_id] = *item;
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
    if (line.size() == 4) {
      Kind format = Kind::i64;
      std::uint64_t input = 0;
      Value output;
      if (!kind(line[1], format) || !reg(line[2], out_id) ||
          !reg(line[3], left_id) ||
          !state.read_scalar(left_id, format, input) ||
          !unary(op, format, input, output) ||
          !state.tick()) {
        if (state.error.empty())
          state.error = "invalid unary instruction";
        return false;
      }
      state.regs[out_id] = output;
      continue;
    }
    if (line.size() == 5) {
      Kind format = Kind::i64;
      int right_id = -1;
      std::uint64_t left = 0;
      std::uint64_t right = 0;
      Value output;
      if (!kind(line[1], format) || !reg(line[2], out_id) ||
          !reg(line[3], left_id) || !reg(line[4], right_id) ||
          !state.read_scalar(left_id, format, left) ||
          !state.read_scalar(right_id, format, right) ||
          !binary(op, format, left, right, output, state.error) ||
          !state.tick()) {
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
      code.front()[0] != "joggle-vm" || code.front()[1] != "3")
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
    Kind format = Kind::i64;
    std::size_t count = 1;
    if ((code[first].size() != 4 && code[first].size() != 5) ||
        !reg(code[first][1], id) ||
        (code[first][2] != "s" && code[first][2] != "t") ||
        !kind(code[first][3], format) ||
        (code[first][2] == "s" && code[first].size() != 4) ||
        (code[first][2] == "t" &&
         (code[first].size() != 5 || !natural(code[first][4], count))) ||
        count > (input_size - offset) / width(format))
      return fail(call, "input does not match function parameters");
    if (code[first][2] == "s") {
      state.regs[id] = scalar(format, load_value(bytes + offset, format));
    } else {
      auto values = std::make_shared<Tensor>();
      values->kind = format;
      values->items.reserve(count);
      for (std::size_t item = 0; item < count; ++item)
        values->items.push_back(
            load_value(bytes + offset + item * width(format), format));
      state.regs[id] = Value{format, 0, std::move(values)};
    }
    offset += count * width(format);
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
    const std::size_t item_width = width(state.result.tensor->kind);
    if (state.result.tensor->items.size() >
        std::numeric_limits<std::size_t>::max() / item_width)
      return fail(call, "vm result is too large");
    output.reserve(state.result.tensor->items.size() * item_width);
    for (const std::uint64_t value : state.result.tensor->items)
      store_value(output, state.result.tensor->kind, value);
  } else {
    output.reserve(width(state.result.kind));
    store_value(output, state.result.kind, state.result.bits);
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
