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

using Slot = std::size_t;
constexpr Slot no_slot = std::numeric_limits<Slot>::max();

enum class Tag {
  constant,
  alloc,
  literal,
  cast,
  pick,
  load,
  store,
  copy,
  unary,
  binary,
  branch,
  loop,
  each,
  otherwise,
  end,
  ret,
};

enum class Unary {
  neg,
  abs,
  ceil,
  erf,
  exp,
  floor,
  log,
  sqrt,
  tanh,
  round_even,
  lnot,
  bnot,
};

enum class Binary {
  add,
  sub,
  mul,
  div,
  rem,
  bit_and,
  bit_or,
  bit_xor,
  shl,
  shr,
  eq,
  ne,
  lt,
  le,
  gt,
  ge,
  logical_and,
  logical_or,
  pow,
};

struct Inst {
  Tag tag = Tag::end;
  Kind kind = Kind::i64;
  Unary unary = Unary::neg;
  Binary binary = Binary::add;
  Slot out = no_slot;
  Slot left = no_slot;
  Slot right = no_slot;
  std::size_t count = 0;
  std::size_t end = no_slot;
  std::size_t alternative = no_slot;
  std::uint64_t value = 0;
  bool tensor = false;
  std::vector<std::size_t> extents;
  std::vector<Slot> items;
  std::vector<std::uint64_t> literal;
};

struct Param {
  Slot slot = no_slot;
  Kind kind = Kind::i64;
  std::size_t count = 1;
  bool tensor = false;
};

struct Program {
  std::vector<Param> params;
  std::vector<Inst> code;
  std::vector<int> ids;
  std::unordered_map<int, Slot> slots;

  Slot intern(int id) {
    const auto found = slots.find(id);
    if (found != slots.end())
      return found->second;
    const Slot slot = ids.size();
    ids.push_back(id);
    slots.emplace(id, slot);
    return slot;
  }
};

struct State {
  explicit State(const Program& program)
      : program(program), regs(program.ids.size()), defined(program.ids.size()) {}

  const Program& program;
  std::vector<Value> regs;
  std::vector<unsigned char> defined;
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

  void write(Slot slot, Value value) {
    regs[slot] = std::move(value);
    defined[slot] = 1;
  }

  const Value* read(Slot slot) {
    if (slot >= regs.size() || !defined[slot]) {
      const std::string id = slot < program.ids.size()
                                 ? std::to_string(program.ids[slot])
                                 : std::string("?");
      error = "read of undefined register " + id;
      return nullptr;
    }
    return &regs[slot];
  }

  bool read_scalar(Slot slot, Kind expected, std::uint64_t& out) {
    const Value* value = read(slot);
    if (!value)
      return false;
    if (value->tensor) {
      error = "register " + std::to_string(program.ids[slot]) +
              " is not scalar";
      return false;
    }
    if (value->kind != expected) {
      error = "register " + std::to_string(program.ids[slot]) +
              " has the wrong format";
      return false;
    }
    out = value->bits;
    return true;
  }
};

template <class T>
T round_even(T input) {
  if (!std::isfinite(input) || input == T{0})
    return input;
  const T lower = std::floor(input);
  const T delta = input - lower;
  if (delta < T{0.5})
    return lower;
  if (delta > T{0.5})
    return lower + T{1};
  return std::fmod(lower, T{2}) == T{0} ? lower : lower + T{1};
}

template <class T>
bool floating_unary(Unary op, T input, Value& output, Kind kind) {
  T result{};
  if (op == Unary::neg)
    result = -input;
  else if (op == Unary::abs)
    result = std::abs(input);
  else if (op == Unary::ceil)
    result = std::ceil(input);
  else if (op == Unary::erf)
    result = std::erf(input);
  else if (op == Unary::exp)
    result = std::exp(input);
  else if (op == Unary::floor)
    result = std::floor(input);
  else if (op == Unary::log)
    result = std::log(input);
  else if (op == Unary::sqrt)
    result = std::sqrt(input);
  else if (op == Unary::tanh)
    result = std::tanh(input);
  else if (op == Unary::round_even)
    result = round_even(input);
  else
    return false;
  output = scalar(kind, bits(result));
  return true;
}

bool unary(Unary op, Kind kind, std::uint64_t input, Value& output) {
  if (kind == Kind::f32)
    return floating_unary(op, float_value(input), output, kind);
  if (kind == Kind::f64)
    return floating_unary(op, double_value(input), output, kind);
  if (op == Unary::neg) {
    output = scalar(kind, std::uint64_t{0} - input);
  } else if (op == Unary::lnot && kind == Kind::i64) {
    output = scalar(Kind::i64, input == 0);
  } else if (op == Unary::bnot && kind == Kind::i64) {
    output = scalar(kind, ~input);
  } else {
    return false;
  }
  return true;
}

template <class T>
bool floating_binary(Binary op, T left, T right, Value& output,
                     Kind kind) {
  if (op == Binary::add)
    output = scalar(kind, bits(left + right));
  else if (op == Binary::sub)
    output = scalar(kind, bits(left - right));
  else if (op == Binary::mul)
    output = scalar(kind, bits(left * right));
  else if (op == Binary::div)
    output = scalar(kind, bits(left / right));
  else if (op == Binary::pow)
    output = scalar(kind, bits(std::pow(left, right)));
  else if (op == Binary::eq)
    output = scalar(Kind::i64, left == right);
  else if (op == Binary::ne)
    output = scalar(Kind::i64, left != right);
  else if (op == Binary::lt)
    output = scalar(Kind::i64, left < right);
  else if (op == Binary::le)
    output = scalar(Kind::i64, left <= right);
  else if (op == Binary::gt)
    output = scalar(Kind::i64, left > right);
  else if (op == Binary::ge)
    output = scalar(Kind::i64, left >= right);
  else
    return false;
  return true;
}

bool binary(Binary op, Kind kind, std::uint64_t left,
            std::uint64_t right, Value& output, std::string& error) {
  if (kind == Kind::f32)
    return floating_binary(op, float_value(left), float_value(right), output,
                           kind);
  if (kind == Kind::f64)
    return floating_binary(op, double_value(left), double_value(right), output,
                           kind);
  const std::int64_t lhs = signed_value(left);
  const std::int64_t rhs = signed_value(right);
  if (op == Binary::add)
    output = scalar(kind, left + right);
  else if (op == Binary::sub)
    output = scalar(kind, left - right);
  else if (op == Binary::mul)
    output = scalar(kind, left * right);
  else if (op == Binary::div || op == Binary::rem) {
    if (rhs == 0) {
      error = "division by zero";
      return false;
    }
    if (lhs == std::numeric_limits<std::int64_t>::min() && rhs == -1)
      output = scalar(kind, op == Binary::div ? bits(lhs) : 0);
    else
      output = scalar(kind,
                      bits(op == Binary::div ? lhs / rhs : lhs % rhs));
  } else if (op == Binary::bit_and)
    output = scalar(kind, left & right);
  else if (op == Binary::bit_or)
    output = scalar(kind, left | right);
  else if (op == Binary::bit_xor)
    output = scalar(kind, left ^ right);
  else if (op == Binary::shl || op == Binary::shr) {
    if (rhs < 0 || rhs >= 64) {
      error = "invalid shift count";
      return false;
    }
    const unsigned shift = static_cast<unsigned>(rhs);
    if (op == Binary::shl)
      output = scalar(kind, left << shift);
    else if ((left >> 63U) == 0 || shift == 0)
      output = scalar(kind, left >> shift);
    else
      output = scalar(kind, ~(~left >> shift));
  } else if (op == Binary::eq)
    output = scalar(Kind::i64, left == right);
  else if (op == Binary::ne)
    output = scalar(Kind::i64, left != right);
  else if (op == Binary::lt)
    output = scalar(Kind::i64, lhs < rhs);
  else if (op == Binary::le)
    output = scalar(Kind::i64, lhs <= rhs);
  else if (op == Binary::gt)
    output = scalar(Kind::i64, lhs > rhs);
  else if (op == Binary::ge)
    output = scalar(Kind::i64, lhs >= rhs);
  else if (op == Binary::logical_and)
    output = scalar(Kind::i64, left != 0 && right != 0);
  else if (op == Binary::logical_or)
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

bool unary_code(std::string_view text, Unary& out) {
  if (text == "neg") out = Unary::neg;
  else if (text == "abs") out = Unary::abs;
  else if (text == "ceil") out = Unary::ceil;
  else if (text == "erf") out = Unary::erf;
  else if (text == "exp") out = Unary::exp;
  else if (text == "floor") out = Unary::floor;
  else if (text == "log") out = Unary::log;
  else if (text == "sqrt") out = Unary::sqrt;
  else if (text == "tanh") out = Unary::tanh;
  else if (text == "round_even") out = Unary::round_even;
  else if (text == "lnot") out = Unary::lnot;
  else if (text == "bnot") out = Unary::bnot;
  else return false;
  return true;
}

bool binary_code(std::string_view text, Binary& out) {
  if (text == "add") out = Binary::add;
  else if (text == "sub") out = Binary::sub;
  else if (text == "mul") out = Binary::mul;
  else if (text == "div") out = Binary::div;
  else if (text == "rem") out = Binary::rem;
  else if (text == "and") out = Binary::bit_and;
  else if (text == "or") out = Binary::bit_or;
  else if (text == "xor") out = Binary::bit_xor;
  else if (text == "shl") out = Binary::shl;
  else if (text == "shr") out = Binary::shr;
  else if (text == "eq") out = Binary::eq;
  else if (text == "ne") out = Binary::ne;
  else if (text == "lt") out = Binary::lt;
  else if (text == "le") out = Binary::le;
  else if (text == "gt") out = Binary::gt;
  else if (text == "ge") out = Binary::ge;
  else if (text == "land") out = Binary::logical_and;
  else if (text == "lor") out = Binary::logical_or;
  else if (text == "pow") out = Binary::pow;
  else return false;
  return true;
}

bool slot(Program& program, std::string_view text, Slot& out) {
  int id = -1;
  if (!reg(text, id))
    return false;
  out = program.intern(id);
  return true;
}

struct Open {
  Tag tag;
  std::size_t at;
  std::size_t otherwise = no_slot;
};

bool decode(const std::vector<Line>& lines, std::size_t first,
            std::size_t last, Program& program, std::string& error) {
  while (first < last && lines[first][0] == "param") {
    const Line& line = lines[first++];
    Param param;
    if ((line.size() != 4 && line.size() != 5) ||
        !slot(program, line[1], param.slot) ||
        (line[2] != "s" && line[2] != "t") ||
        !kind(line[3], param.kind) ||
        (line[2] == "s" && line.size() != 4) ||
        (line[2] == "t" &&
         (line.size() != 5 || !natural(line[4], param.count)))) {
      error = "invalid parameter instruction";
      return false;
    }
    param.tensor = line[2] == "t";
    program.params.push_back(param);
  }

  std::vector<Open> open;
  for (; first < last; ++first) {
    const Line& line = lines[first];
    const std::string_view op = line.front();
    Inst inst;
    if (op == "if") {
      if (line.size() != 2 || !slot(program, line[1], inst.left)) {
        error = "invalid if instruction";
        return false;
      }
      inst.tag = Tag::branch;
      open.push_back({inst.tag, program.code.size()});
    } else if (op == "loop") {
      if (line.size() != 4 || !slot(program, line[1], inst.out) ||
          !slot(program, line[2], inst.left) ||
          !slot(program, line[3], inst.right)) {
        error = "invalid loop instruction";
        return false;
      }
      inst.tag = Tag::loop;
      open.push_back({inst.tag, program.code.size()});
    } else if (op == "each") {
      if (line.size() < 4 || !slot(program, line[1], inst.out) ||
          !kind(line[2], inst.kind) || !natural(line[3], inst.count) ||
          inst.count > std::numeric_limits<std::size_t>::max() - 4 ||
          line.size() != inst.count + 4) {
        error = "invalid each instruction";
        return false;
      }
      inst.tag = Tag::each;
      inst.items.reserve(inst.count);
      for (std::size_t item = 0; item < inst.count; ++item) {
        Slot value = no_slot;
        if (!slot(program, line[4 + item], value)) {
          error = "invalid list-loop item register";
          return false;
        }
        inst.items.push_back(value);
      }
      open.push_back({inst.tag, program.code.size()});
    } else if (op == "else") {
      if (line.size() != 1 || open.empty() ||
          open.back().tag != Tag::branch ||
          open.back().otherwise != no_slot) {
        error = open.empty() || open.back().tag != Tag::branch
                    ? "unexpected else"
                    : "duplicate else";
        return false;
      }
      inst.tag = Tag::otherwise;
      open.back().otherwise = program.code.size();
    } else if (op == "end") {
      if (line.size() != 1 || open.empty()) {
        error = "unexpected end";
        return false;
      }
      inst.tag = Tag::end;
      const Open closed = open.back();
      open.pop_back();
      program.code[closed.at].end = program.code.size();
      if (closed.tag == Tag::branch && closed.otherwise != no_slot) {
        program.code[closed.at].alternative = closed.otherwise + 1;
        program.code[closed.otherwise].end = program.code.size();
      }
    } else if (op == "ret") {
      if ((line.size() != 4 && line.size() != 5) ||
          !slot(program, line[1], inst.left) ||
          (line[2] != "s" && line[2] != "t") ||
          !kind(line[3], inst.kind) ||
          (line[2] == "s" && line.size() != 4) ||
          (line[2] == "t" &&
           (line.size() != 5 || !natural(line[4], inst.count)))) {
        error = "invalid return instruction";
        return false;
      }
      inst.tag = Tag::ret;
      inst.tensor = line[2] == "t";
    } else if (op == "const") {
      Value value;
      if (line.size() != 4 || !kind(line[1], inst.kind) ||
          !slot(program, line[2], inst.out) ||
          !constant(line[3], inst.kind, value)) {
        error = "invalid const instruction";
        return false;
      }
      inst.tag = Tag::constant;
      inst.value = value.bits;
    } else if (op == "alloc") {
      if (line.size() != 5 || !kind(line[1], inst.kind) ||
          !slot(program, line[2], inst.out) ||
          !natural(line[3], inst.count) ||
          !slot(program, line[4], inst.left)) {
        error = "invalid alloc instruction";
        return false;
      }
      inst.tag = Tag::alloc;
    } else if (op == "literal") {
      std::vector<std::uint8_t> data;
      if (line.size() != 5 || !kind(line[1], inst.kind) ||
          !slot(program, line[2], inst.out) ||
          !natural(line[3], inst.count) || !byte_literal(line[4], data) ||
          inst.count > std::numeric_limits<std::size_t>::max() /
                           width(inst.kind) ||
          data.size() != inst.count * width(inst.kind)) {
        error = "invalid tensor literal instruction";
        return false;
      }
      inst.tag = Tag::literal;
      inst.literal.reserve(inst.count);
      for (std::size_t item = 0; item < inst.count; ++item)
        inst.literal.push_back(
            load_value(data.data() + item * width(inst.kind), inst.kind));
    } else if (op == "cast") {
      if (line.size() != 4 || !kind(line[1], inst.kind) ||
          !slot(program, line[2], inst.out) ||
          !slot(program, line[3], inst.left)) {
        error = "invalid cast instruction";
        return false;
      }
      inst.tag = Tag::cast;
    } else if (op == "pick") {
      if (line.size() < 5 || !kind(line[1], inst.kind) ||
          !slot(program, line[2], inst.out) ||
          !slot(program, line[3], inst.left) ||
          !natural(line[4], inst.count) ||
          inst.count > std::numeric_limits<std::size_t>::max() - 5 ||
          line.size() != inst.count + 5) {
        error = "invalid list selection instruction";
        return false;
      }
      inst.tag = Tag::pick;
      inst.items.reserve(inst.count);
      for (std::size_t item = 0; item < inst.count; ++item) {
        Slot value = no_slot;
        if (!slot(program, line[5 + item], value)) {
          error = "invalid list selection item register";
          return false;
        }
        inst.items.push_back(value);
      }
    } else if (op == "load" || op == "store") {
      std::size_t rank = 0;
      const bool store = op == "store";
      if (line.size() < 4 || !slot(program, line[1], inst.out) ||
          !slot(program, line[2], inst.left) || !natural(line[3], rank) ||
          rank == 0 ||
          rank > (std::numeric_limits<std::size_t>::max() - 5) / 2 ||
          line.size() != (store ? 5 : 4) + 2 * rank) {
        error = "invalid tensor access instruction";
        return false;
      }
      inst.tag = store ? Tag::store : Tag::load;
      inst.extents.reserve(rank);
      inst.items.reserve(rank);
      inst.count = 1;
      for (std::size_t axis = 0; axis < rank; ++axis) {
        std::size_t extent = 0;
        Slot index = no_slot;
        if (!natural(line[4 + axis], extent) ||
            !slot(program, line[4 + rank + axis], index) ||
            (extent != 0 &&
             inst.count > std::numeric_limits<std::size_t>::max() / extent)) {
          error = "invalid tensor access instruction";
          return false;
        }
        inst.count *= extent;
        inst.extents.push_back(extent);
        inst.items.push_back(index);
      }
      if (store && !slot(program, line.back(), inst.right)) {
        error = "invalid tensor access instruction";
        return false;
      }
    } else if (op == "copy") {
      if (line.size() != 3 || !slot(program, line[1], inst.out) ||
          !slot(program, line[2], inst.left)) {
        error = "invalid copy instruction";
        return false;
      }
      inst.tag = Tag::copy;
    } else if (line.size() == 4) {
      if (!kind(line[1], inst.kind) || !slot(program, line[2], inst.out) ||
          !slot(program, line[3], inst.left) ||
          !unary_code(op, inst.unary)) {
        error = "invalid unary instruction";
        return false;
      }
      inst.tag = Tag::unary;
    } else if (line.size() == 5) {
      if (!kind(line[1], inst.kind) || !slot(program, line[2], inst.out) ||
          !slot(program, line[3], inst.left) ||
          !slot(program, line[4], inst.right) ||
          !binary_code(op, inst.binary)) {
        error = "invalid binary instruction";
        return false;
      }
      inst.tag = Tag::binary;
    } else {
      error = "unknown instruction " + std::string(op);
      return false;
    }
    program.code.push_back(std::move(inst));
  }
  if (!open.empty()) {
    error = open.back().tag == Tag::branch ? "unterminated if"
                                           : "unterminated loop";
    return false;
  }
  return true;
}

bool tensor_access(const Inst& inst, State& state) {
  const Value* source = state.read(inst.left);
  if (!source)
    return false;
  const Value& tensor = *source;
  if (!tensor.tensor) {
    state.error = "tensor access on a scalar register";
    return false;
  }
  std::size_t offset = 0;
  for (std::size_t axis = 0; axis < inst.extents.size(); ++axis) {
    std::uint64_t index_bits = 0;
    if (!state.read_scalar(inst.items[axis], Kind::i64, index_bits))
      return false;
    const std::int64_t index = signed_value(index_bits);
    if (index < 0 ||
        static_cast<std::uint64_t>(index) >= inst.extents[axis]) {
      state.error = "tensor index is out of bounds";
      return false;
    }
    offset = offset * inst.extents[axis] + static_cast<std::size_t>(index);
  }
  if (inst.count != tensor.tensor->items.size() ||
      offset >= tensor.tensor->items.size()) {
    state.error = "tensor access shape does not match storage";
    return false;
  }
  if (inst.tag == Tag::store) {
    const Value* value = state.read(inst.right);
    if (!value || value->tensor || value->kind != tensor.tensor->kind) {
      state.error = "tensor store format does not match its element";
      return false;
    }
    tensor.tensor->items[offset] = value->bits;
    state.write(inst.out, *source);
  } else {
    state.write(inst.out,
                scalar(tensor.tensor->kind, tensor.tensor->items[offset]));
  }
  return state.tick();
}

bool execute(const Program& program, std::size_t first, std::size_t last,
             State& state) {
  for (std::size_t at = first; at < last && !state.returned; ++at) {
    const Inst& inst = program.code[at];
    if (inst.tag == Tag::branch) {
      std::uint64_t condition = 0;
      if (!state.read_scalar(inst.left, Kind::i64, condition) ||
          !state.tick())
        return false;
      const bool has_else = inst.alternative != no_slot;
      const std::size_t then_end = has_else ? inst.alternative - 1 : inst.end;
      if (condition) {
        if (!execute(program, at + 1, then_end, state))
          return false;
      } else if (has_else &&
                 !execute(program, inst.alternative, inst.end, state)) {
        return false;
      }
      at = inst.end;
      continue;
    }
    if (inst.tag == Tag::loop) {
      std::uint64_t first_bits = 0;
      std::uint64_t last_bits = 0;
      if (!state.read_scalar(inst.left, Kind::i64, first_bits) ||
          !state.read_scalar(inst.right, Kind::i64, last_bits))
        return false;
      std::int64_t value = signed_value(first_bits);
      const std::int64_t stop = signed_value(last_bits);
      while (!state.returned) {
        if (!state.tick())
          return false;
        if (value >= stop)
          break;
        state.write(inst.out, scalar(Kind::i64, bits(value)));
        if (!execute(program, at + 1, inst.end, state))
          return false;
        ++value;
      }
      at = inst.end;
      continue;
    }
    if (inst.tag == Tag::each) {
      for (const Slot item : inst.items) {
        if (state.returned)
          break;
        if (!state.tick())
          return false;
        const Value* value = state.read(item);
        if (!value || value->tensor || value->kind != inst.kind) {
          state.error = "list-loop item format does not match its iterator";
          return false;
        }
        state.write(inst.out, *value);
        if (!execute(program, at + 1, inst.end, state))
          return false;
      }
      if (!state.returned && !state.tick())
        return false;
      at = inst.end;
      continue;
    }
    if (inst.tag == Tag::otherwise || inst.tag == Tag::end) {
      state.error = inst.tag == Tag::otherwise ? "unexpected else"
                                               : "unexpected end";
      return false;
    }
    if (inst.tag == Tag::ret) {
      const Value* value = state.read(inst.left);
      if (!value || value->kind != inst.kind ||
          (!inst.tensor && value->tensor) ||
          (inst.tensor &&
           (!value->tensor || value->tensor->kind != inst.kind ||
            value->tensor->items.size() != inst.count)) ||
          !state.tick())
        return false;
      state.result = *value;
      state.returned = true;
      continue;
    }
    if (inst.tag == Tag::constant) {
      if (!state.tick())
        return false;
      state.write(inst.out, scalar(inst.kind, inst.value));
      continue;
    }
    if (inst.tag == Tag::alloc) {
      std::uint64_t fill = 0;
      if (!state.read_scalar(inst.left, inst.kind, fill) || !state.tick())
        return false;
      auto tensor = std::make_shared<Tensor>();
      tensor->kind = inst.kind;
      tensor->items.assign(inst.count, fill);
      state.write(inst.out, Value{inst.kind, 0, std::move(tensor)});
      continue;
    }
    if (inst.tag == Tag::literal) {
      if (!state.tick())
        return false;
      auto tensor = std::make_shared<Tensor>();
      tensor->kind = inst.kind;
      tensor->items = inst.literal;
      state.write(inst.out, Value{inst.kind, 0, std::move(tensor)});
      continue;
    }
    if (inst.tag == Tag::cast) {
      const Value* input = state.read(inst.left);
      Value output;
      if (!input || !cast(inst.kind, *input, output) || !state.tick()) {
        if (state.error.empty())
          state.error = "invalid scalar cast";
        return false;
      }
      state.write(inst.out, output);
      continue;
    }
    if (inst.tag == Tag::pick) {
      std::uint64_t index_bits = 0;
      if (!state.read_scalar(inst.left, Kind::i64, index_bits))
        return false;
      const std::int64_t index = signed_value(index_bits);
      if (index < 0 || static_cast<std::uint64_t>(index) >= inst.count) {
        state.error = "list index is out of bounds";
        return false;
      }
      const Value* item = state.read(inst.items[static_cast<std::size_t>(index)]);
      if (!item || item->tensor || item->kind != inst.kind || !state.tick()) {
        if (state.error.empty())
          state.error = "list selection item has the wrong format";
        return false;
      }
      state.write(inst.out, *item);
      continue;
    }
    if (inst.tag == Tag::load || inst.tag == Tag::store) {
      if (!tensor_access(inst, state))
        return false;
      continue;
    }
    if (inst.tag == Tag::copy) {
      const Value* input = state.read(inst.left);
      if (!input || !state.tick())
        return false;
      state.write(inst.out, *input);
      continue;
    }
    if (inst.tag == Tag::unary) {
      std::uint64_t input = 0;
      Value output;
      if (!state.read_scalar(inst.left, inst.kind, input) ||
          !unary(inst.unary, inst.kind, input, output) || !state.tick()) {
        if (state.error.empty())
          state.error = "invalid unary instruction";
        return false;
      }
      state.write(inst.out, output);
      continue;
    }
    if (inst.tag == Tag::binary) {
      std::uint64_t left = 0;
      std::uint64_t right = 0;
      Value output;
      if (!state.read_scalar(inst.left, inst.kind, left) ||
          !state.read_scalar(inst.right, inst.kind, right) ||
          !binary(inst.binary, inst.kind, left, right, output, state.error) ||
          !state.tick()) {
        if (state.error.empty())
          state.error = "invalid binary instruction";
        return false;
      }
      state.write(inst.out, output);
      continue;
    }
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

  Program program;
  std::string decode_error;
  if (!decode(code, first, last, program, decode_error))
    return fail(call, decode_error);

  State state(program);
  const auto* bytes =
      reinterpret_cast<const unsigned char*>(input.data.bytes.data);
  const std::size_t input_size = input.data.bytes.size;
  std::size_t offset = 0;
  for (const Param& param : program.params) {
    if (offset > input_size ||
        param.count > (input_size - offset) / width(param.kind))
      return fail(call, "input does not match function parameters");
    if (!param.tensor) {
      state.write(param.slot,
                  scalar(param.kind, load_value(bytes + offset, param.kind)));
    } else {
      auto values = std::make_shared<Tensor>();
      values->kind = param.kind;
      values->items.reserve(param.count);
      for (std::size_t item = 0; item < param.count; ++item)
        values->items.push_back(
            load_value(bytes + offset + item * width(param.kind), param.kind));
      state.write(param.slot, Value{param.kind, 0, std::move(values)});
    }
    offset += param.count * width(param.kind);
  }
  if (offset != input_size)
    return fail(call, "input does not match function parameters");
  if (!execute(program, 0, program.code.size(), state))
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
