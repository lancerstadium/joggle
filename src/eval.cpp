#include "detail.h"

#include <algorithm>
#include <charconv>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace joggle::detail {

namespace {

struct Item;
using Items = std::vector<Item>;

struct List {
  Items items;
};

struct Item {
  using Data = std::variant<std::monostate, Attr, Ty, Mod*, Fn, Blk, Op, Val,
                            std::shared_ptr<List>>;
  Data data;

  Item() = default;
  Item(Attr value) : data(std::move(value)) {}
  Item(Ty value) : data(std::move(value)) {}
  Item(Mod* value) : data(value) {}
  Item(Fn value) : data(value) {}
  Item(Blk value) : data(value) {}
  Item(Op value) : data(value) {}
  Item(Val value) : data(value) {}
  Item(Items value) : data(std::make_shared<List>(List{std::move(value)})) {}
};

Item materialize(Attr value) {
  if (const Attr::List* list = value.list()) {
    Items items;
    items.reserve(list->size());
    for (const Attr& item : *list)
      items.push_back(materialize(item));
    return Item(std::move(items));
  }
  return Item(std::move(value));
}

enum class FlowKind : std::uint8_t { next, ret, yield, fail };

struct Flow {
  FlowKind kind = FlowKind::next;
  Items values;
};

template <class T> const T* as(const Item& item) {
  return std::get_if<T>(&item.data);
}

const Items* list(const Item& item) {
  const auto* value = as<std::shared_ptr<List>>(item);
  return value && *value ? &(*value)->items : nullptr;
}

std::optional<Attr> attribute(const Item& item) {
  if (const auto* value = as<Attr>(item))
    return *value;
  const Items* values = list(item);
  if (!values)
    return std::nullopt;
  Attr::List out;
  out.reserve(values->size());
  for (const Item& value : *values) {
    auto converted = attribute(value);
    if (!converted)
      return std::nullopt;
    out.push_back(std::move(*converted));
  }
  return Attr(std::move(out));
}

std::optional<std::vector<Val>> value_handles(const Item& item) {
  const Items* items = list(item);
  if (!items)
    return std::nullopt;
  std::vector<Val> out;
  out.reserve(items->size());
  for (const Item& entry : *items) {
    const auto* value = as<Val>(entry);
    if (!value)
      return std::nullopt;
    out.push_back(*value);
  }
  return out;
}

std::optional<std::vector<std::string>> strings(const Item& item) {
  const Items* items = list(item);
  if (!items)
    return std::nullopt;
  std::vector<std::string> out;
  out.reserve(items->size());
  for (const Item& entry : *items) {
    const auto* attr = as<Attr>(entry);
    const auto value = attr ? attr->string() : std::nullopt;
    if (!value)
      return std::nullopt;
    out.emplace_back(*value);
  }
  return out;
}

std::optional<std::int64_t> integer(const Item& item) {
  const Attr* value = as<Attr>(item);
  return value ? value->integer() : std::nullopt;
}

std::optional<std::int64_t> integer(const Ty& value) {
  std::string_view text = value.text();
  if (text.starts_with('+'))
    text.remove_prefix(1);
  std::int64_t out = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), out);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
             ? std::optional<std::int64_t>(out)
             : std::nullopt;
}

std::optional<bool> boolean(const Item& item) {
  const Attr* value = as<Attr>(item);
  return value ? value->boolean() : std::nullopt;
}

std::optional<std::string_view> string(const Item& item) {
  const Attr* value = as<Attr>(item);
  return value ? value->string() : std::nullopt;
}

Ty runtime_type(const Item& item) {
  if (const auto* value = as<Attr>(item)) {
    if (value->boolean())
      return Ty("bool");
    if (value->integer())
      return Ty("int");
    if (value->real())
      return Ty("f64");
    if (value->string())
      return Ty("str");
    if (value->bytes())
      return Ty("bytes");
    if (value->dict())
      return Ty("dict");
    return Ty("Attr");
  }
  if (as<Mod*>(item))
    return Ty("Mod");
  if (as<Ty>(item))
    return Ty("Ty");
  if (as<Fn>(item))
    return Ty("Fn");
  if (as<Blk>(item))
    return Ty("Blk");
  if (as<Op>(item))
    return Ty("Op");
  if (as<Val>(item))
    return Ty("Val");
  if (const Items* items = list(item)) {
    Ty element("_");
    if (!items->empty()) {
      element = runtime_type(items->front());
      for (std::size_t index = 1; index < items->size(); ++index)
        if (runtime_type((*items)[index]) != element)
          element = Ty("_");
    }
    return Ty("list<" + std::string(element.text()) + ">");
  }
  return Ty("_");
}

bool same(const Item& left, const Item& right) {
  if (left.data.index() != right.data.index())
    return false;
  if (const auto* value = as<Attr>(left))
    return *value == *as<Attr>(right);
  if (const auto* value = as<Ty>(left))
    return *value == *as<Ty>(right);
  if (const auto* value = as<Mod*>(left))
    return *value == *as<Mod*>(right);
  if (const auto* value = as<Fn>(left))
    return *value == *as<Fn>(right);
  if (const auto* value = as<Blk>(left))
    return *value == *as<Blk>(right);
  if (const auto* value = as<Op>(left))
    return *value == *as<Op>(right);
  if (const auto* value = as<Val>(left))
    return *value == *as<Val>(right);
  if (const Items* values = list(left)) {
    const Items* other = list(right);
    return other && values->size() == other->size() &&
           std::equal(values->begin(), values->end(), other->begin(), same);
  }
  return true;
}

bool add(std::int64_t left, std::int64_t right, std::int64_t& out) {
  if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
      (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
    return false;
  out = left + right;
  return true;
}

bool subtract(std::int64_t left, std::int64_t right, std::int64_t& out) {
  if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
      (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right))
    return false;
  out = left - right;
  return true;
}

bool multiply(std::int64_t left, std::int64_t right, std::int64_t& out) {
  if (left == 0 || right == 0) {
    out = 0;
    return true;
  }
  const auto min = std::numeric_limits<std::int64_t>::min();
  const auto max = std::numeric_limits<std::int64_t>::max();
  if ((left > 0 && right > 0 && left > max / right) ||
      (left > 0 && right < 0 && right < min / left) ||
      (left < 0 && right > 0 && left < min / right) ||
      (left < 0 && right < 0 && left < max / right))
    return false;
  out = left * right;
  return true;
}

}  // namespace

class Eval {
public:
  using Error = std::function<void(std::string, Loc)>;

  Eval(Env& env, Error error, Attr::List* trace = nullptr)
      : env_(env), error_(std::move(error)), trace_(trace) {}

  std::optional<Items> run(Fn fn, Mod& mod) {
    return invoke(fn, {Item(&mod)});
  }

  std::optional<Items> query(Fn fn, Mod& mod,
                             std::span<const Attr> args) {
    Items values{Item(&mod)};
    values.reserve(args.size() + 1);
    for (const Attr& value : args)
      values.push_back(materialize(value));
    return invoke(fn, values);
  }

private:
  using Frame = std::vector<std::pair<Val, Item>>;

  void fail(std::string message, Loc loc = {}) {
    if (!failed_) {
      failed_ = true;
      error_(std::move(message), std::move(loc));
    }
  }

  const Item* get(const Frame& frame, Val value, Loc loc = {}) {
    const auto found =
        std::find_if(frame.rbegin(), frame.rend(),
                     [&](const auto& entry) { return entry.first == value; });
    if (found != frame.rend())
      return &found->second;
    fail("compile-time value is not available", std::move(loc));
    return nullptr;
  }

  void put(Frame& frame, Val value, Item item) {
    const auto found =
        std::find_if(frame.begin(), frame.end(),
                     [&](const auto& entry) { return entry.first == value; });
    if (found == frame.end())
      frame.emplace_back(value, std::move(item));
    else
      found->second = std::move(item);
  }

  void record(Fn fn, Mod& mod, std::uint64_t before,
              const Items& results) {
    if (!trace_)
      return;
    const std::uint64_t after = mod.revision();
    Attr::Dict event;
    event["fn"] =
        Attr(std::string(fn.module()) + "." + std::string(fn.name()));
    event["before"] = Attr(static_cast<std::int64_t>(before));
    event["after"] = Attr(static_cast<std::int64_t>(after));
    event["edits"] = Attr(static_cast<std::int64_t>(after - before));
    event["changed"] = Attr(after != before);
    if (results.size() == 1)
      if (const auto* value = as<Attr>(results.front()); value && value->boolean())
        event["reported"] = *value;
    trace_->emplace_back(std::move(event));
  }

  std::optional<Items> values(const Frame& frame, const std::vector<Val>& vals,
                              Loc loc = {}) {
    Items out;
    out.reserve(vals.size());
    for (Val value : vals) {
      const Item* item = get(frame, value, loc);
      if (!item)
        return std::nullopt;
      out.push_back(*item);
    }
    return out;
  }

  std::optional<Item> generic(Ty value, Ty type, Loc loc) {
    if (value.text() == "_") {
      fail("compile-time generic argument could not be inferred", loc);
      return std::nullopt;
    }
    if (type.text() == "_") {
      if (value.name() == "[]")
        type = Ty("list<_>");
      else {
        if (integer(value))
          type = Ty("int");
        else if (value.text() == "true" || value.text() == "false")
          type = Ty("bool");
        else
          type = Ty("Ty");
      }
    }
    if (type.text() == "int" || type.text() == "index") {
      if (const auto integer_value = integer(value))
        return Item(Attr(*integer_value));
    } else if (type.text() == "bool") {
      if (value.text() == "true" || value.text() == "false")
        return Item(Attr(value.text() == "true"));
    } else if (type.text() == "Ty") {
      return Item(std::move(value));
    } else if (type.name() == "list" && type.args().size() == 1 &&
               value.name() == "[]") {
      Items items;
      for (const Ty& element : value.args()) {
        auto item = generic(element, type.args().front(), loc);
        if (!item)
          return std::nullopt;
        items.push_back(std::move(*item));
      }
      return Item(std::move(items));
    } else if (type.text() == "Attr" || type.text() == "meta") {
      return Item(Attr(std::string(value.text())));
    }
    fail("compile-time generic argument '" + std::string(value.text()) +
             "' does not have type '" + std::string(type.text()) + "'",
         loc);
    return std::nullopt;
  }

  std::optional<Items> invoke(Fn fn, const Items& args,
                              const Items& generic_args = {}) {
    if (!fn) {
      fail("compile-time function is invalid");
      return std::nullopt;
    }
    const std::vector<Val> params = fn.params();
    const std::vector<Val> generics = fn.generics();
    if (generics.size() != generic_args.size()) {
      fail("generic argument count does not match compile-time function '" +
               std::string(fn.name()) + "'",
           fn.loc());
      return std::nullopt;
    }
    if (params.size() != args.size()) {
      fail("argument count does not match compile-time function '" +
               std::string(fn.name()) + "'",
           fn.loc());
      return std::nullopt;
    }
    if (fn.external()) {
      const std::string symbol = fn.store_->name + "." + std::string(fn.name());
      if (!env_.bound(symbol)) {
        fail("compile-time function has no implementation: " + fn.store_->name +
                 "." + std::string(fn.name()),
             fn.loc());
        return std::nullopt;
      }
      std::vector<Attr> scalar_args;
      scalar_args.reserve(args.size());
      for (const Item& item : args) {
        const Attr* value = as<Attr>(item);
        if (!value) {
          fail("native functions accept scalar compile-time values only",
               fn.loc());
          return std::nullopt;
        }
        scalar_args.push_back(*value);
      }
      std::vector<Attr> scalar_returns;
      if (!env_.call(symbol, scalar_args, scalar_returns)) {
        failed_ = true;
        return std::nullopt;
      }
      Items out;
      for (Attr& value : scalar_returns)
        out.emplace_back(std::move(value));
      return out;
    }

    Mod* observed = nullptr;
    for (const Item& item : args)
      if (const auto* value = as<Mod*>(item); value && *value) {
        observed = *value;
        break;
      }
    const std::uint64_t before = observed ? observed->revision() : 0;
    Frame frame;
    for (std::size_t index = 0; index < generics.size(); ++index)
      put(frame, generics[index], generic_args[index]);
    for (std::size_t index = 0; index < params.size(); ++index)
      put(frame, params[index], args[index]);
    Flow flow = block(fn.body(), {}, frame);
    if (flow.kind == FlowKind::ret) {
      if (observed)
        record(fn, *observed, before, flow.values);
      return flow.values;
    }
    if (flow.kind != FlowKind::fail)
      fail("compile-time function reached the end without return", fn.loc());
    return std::nullopt;
  }

  Flow block(Blk block, const Items& args, Frame& frame) {
    const std::vector<Val> params = block.args();
    if (params.size() != args.size()) {
      fail("compile-time block argument count is inconsistent");
      return {FlowKind::fail, {}};
    }
    for (std::size_t index = 0; index < params.size(); ++index)
      put(frame, params[index], args[index]);

    for (Op op : block.ops()) {
      const Loc loc = op.loc();
      if (op.kind() == Op::Kind::constant) {
        const std::vector<Val> outs = op.outs();
        if (outs.size() != 1) {
          fail("constant result count is inconsistent", loc);
          return {FlowKind::fail, {}};
        }
        put(frame, outs.front(), materialize(outs.front().constant()));
        continue;
      }
      if (op.kind() == Op::Kind::call) {
        const auto call_args = values(frame, op.args(), loc);
        if (!call_args)
          return {FlowKind::fail, {}};
        const auto result = call(block.fn(), op.callee(), *call_args, loc);
        if (!result)
          return {FlowKind::fail, {}};
        const std::vector<Val> outs = op.outs();
        if (result->size() != outs.size()) {
          fail("compile-time call result count is inconsistent", loc);
          return {FlowKind::fail, {}};
        }
        for (std::size_t index = 0; index < outs.size(); ++index)
          put(frame, outs[index], (*result)[index]);
        continue;
      }
      if (op.kind() == Op::Kind::branch) {
        Flow flow = branch(op, frame);
        if (flow.kind != FlowKind::next)
          return flow;
        continue;
      }
      if (op.kind() == Op::Kind::loop) {
        Flow flow = loop(op, frame);
        if (flow.kind != FlowKind::next)
          return flow;
        continue;
      }
      if (op.kind() == Op::Kind::ret || op.kind() == Op::Kind::yield) {
        const auto result = values(frame, op.args(), loc);
        if (!result)
          return {FlowKind::fail, {}};
        return {op.kind() == Op::Kind::ret ? FlowKind::ret : FlowKind::yield,
                *result};
      }
    }
    return {FlowKind::next, {}};
  }

  Flow branch(Op op, Frame& frame) {
    const auto args = values(frame, op.args(), op.loc());
    if (!args || args->empty())
      return {FlowKind::fail, {}};
    const auto condition = boolean(args->front());
    if (!condition) {
      fail("if condition is not a compile-time bool", op.loc());
      return {FlowKind::fail, {}};
    }
    const std::vector<Blk> blks = op.blks();
    const Blk arm = blks[*condition ? 0 : 1];
    Items carried(args->begin() + 1, args->end());
    Frame nested = frame;
    Flow flow = block(arm, carried, nested);
    if (flow.kind == FlowKind::ret || flow.kind == FlowKind::fail)
      return flow;
    if (flow.kind != FlowKind::yield) {
      fail("compile-time if did not yield", op.loc());
      return {FlowKind::fail, {}};
    }
    const std::vector<Val> outs = op.outs();
    if (outs.size() != flow.values.size()) {
      fail("compile-time if result count is inconsistent", op.loc());
      return {FlowKind::fail, {}};
    }
    for (std::size_t index = 0; index < outs.size(); ++index)
      put(frame, outs[index], flow.values[index]);
    return {};
  }

  Flow loop(Op op, Frame& frame) {
    const auto args = values(frame, op.args(), op.loc());
    if (!args)
      return {FlowKind::fail, {}};
    const std::size_t iter_count =
        op.blks().front().args().size() - op.outs().size();
    if (iter_count > args->size()) {
      fail("compile-time loop source count is inconsistent", op.loc());
      return {FlowKind::fail, {}};
    }
    std::vector<const Items*> sources;
    for (std::size_t index = 0; index < iter_count; ++index) {
      const Items* source = list((*args)[index]);
      if (!source) {
        fail("compile-time loop source is not iterable", op.loc());
        return {FlowKind::fail, {}};
      }
      sources.push_back(source);
    }
    Items carried(args->begin() + static_cast<std::ptrdiff_t>(iter_count),
                  args->end());
    Items indices;
    Flow escaped;
    std::function<void(std::size_t)> visit = [&](std::size_t depth) {
      if (escaped.kind != FlowKind::next)
        return;
      if (depth != sources.size()) {
        for (const Item& item : *sources[depth]) {
          indices.push_back(item);
          visit(depth + 1);
          indices.pop_back();
          if (escaped.kind != FlowKind::next)
            return;
        }
        return;
      }
      Items block_args = indices;
      block_args.insert(block_args.end(), carried.begin(), carried.end());
      Frame nested = frame;
      Flow flow = block(op.blks().front(), block_args, nested);
      if (flow.kind == FlowKind::yield)
        carried = std::move(flow.values);
      else if (flow.kind != FlowKind::next)
        escaped = std::move(flow);
      else {
        fail("compile-time loop body did not yield", op.loc());
        escaped = {FlowKind::fail, {}};
      }
    };
    visit(0);
    if (escaped.kind != FlowKind::next)
      return escaped;
    const std::vector<Val> outs = op.outs();
    if (outs.size() != carried.size()) {
      fail("compile-time loop result count is inconsistent", op.loc());
      return {FlowKind::fail, {}};
    }
    for (std::size_t index = 0; index < outs.size(); ++index)
      put(frame, outs[index], carried[index]);
    return {};
  }

  std::optional<Items> call(Fn current, std::string_view name,
                            const Items& args, Loc loc) {
    if (name == "base.copy" && args.size() == 1)
      return args;
    if (name == "base.list")
      return Items{Item(args)};
    if (name.starts_with("ir."))
      return intrinsic(name.substr(3), args, std::move(loc));

    const Ty applied{std::string(name)};
    const std::string_view symbol =
        applied.args().empty() ? name : applied.name();
    const std::vector<Ty> explicit_arguments =
        applied.args().empty() ? std::vector<Ty>{} : applied.args();
    const std::vector<Fn> candidates = env_.resolve_fns(current, symbol);
    std::vector<Ty> argument_types;
    argument_types.reserve(args.size());
    for (const Item& item : args)
      argument_types.push_back(runtime_type(item));
    bool ambiguous = false;
    const std::vector<Val> context = current.generics();
    std::vector<Ty> generic_values;
    const Fn target =
        resolve_overload(candidates, argument_types, explicit_arguments,
                         nullptr, &ambiguous, context, &generic_values);
    if (name.starts_with("operator ") &&
        (!target || target.external() || target.module() == "base"))
      return operation(name.substr(9), args, std::move(loc));
    if (!target) {
      if (ambiguous) {
        fail("ambiguous compile-time function: " + std::string(name), loc);
        return std::nullopt;
      }
      fail("unknown compile-time function: " + std::string(name), loc);
      return std::nullopt;
    }
    Items resolved;
    const std::vector<Val> generics = target.generics();
    resolved.reserve(generics.size());
    for (std::size_t index = 0; index < generics.size(); ++index) {
      auto value = generic(generic_values[index], generics[index].type(), loc);
      if (!value)
        return std::nullopt;
      resolved.push_back(std::move(*value));
    }
    return invoke(target, args, resolved);
  }

  std::optional<Items> operation(std::string_view name, const Items& args,
                                 Loc loc) {
    if (name == "[]" && args.size() == 2) {
      const Items* items = list(args[0]);
      const auto index = integer(args[1]);
      if (!items || !index || *index < 0 ||
          static_cast<std::size_t>(*index) >= items->size()) {
        fail("compile-time index is out of bounds", loc);
        return std::nullopt;
      }
      return Items{(*items)[static_cast<std::size_t>(*index)]};
    }
    if (name == ".." && args.size() == 2) {
      const auto first = integer(args[0]);
      const auto last = integer(args[1]);
      const std::uint64_t size = first && last && *last >= *first
                                     ? static_cast<std::uint64_t>(*last) -
                                           static_cast<std::uint64_t>(*first)
                                     : 1000001;
      if (!first || !last || size > 1000000) {
        fail("compile-time range is invalid or too large", loc);
        return std::nullopt;
      }
      Items out;
      out.reserve(static_cast<std::size_t>(size));
      for (std::int64_t value = *first; value < *last; ++value)
        out.emplace_back(Attr(value));
      return Items{Item(std::move(out))};
    }
    if ((name == "==" || name == "!=") && args.size() == 2) {
      const bool equal = same(args[0], args[1]);
      return Items{Item(Attr(name == "==" ? equal : !equal))};
    }
    if ((name == "&&" || name == "||") && args.size() == 2) {
      const auto left = boolean(args[0]);
      const auto right = boolean(args[1]);
      if (!left || !right) {
        fail("logical operator requires bool operands", loc);
        return std::nullopt;
      }
      return Items{
          Item(Attr(name == "&&" ? *left && *right : *left || *right))};
    }
    if ((name == "<" || name == "<=" || name == ">" || name == ">=") &&
        args.size() == 2) {
      const auto left = integer(args[0]);
      const auto right = integer(args[1]);
      if (!left || !right) {
        fail("comparison requires integer operands", loc);
        return std::nullopt;
      }
      const bool value = name == "<"    ? *left < *right
                         : name == "<=" ? *left <= *right
                         : name == ">"  ? *left > *right
                                        : *left >= *right;
      return Items{Item(Attr(value))};
    }
    if ((name == "+" || name == "-" || name == "!" || name == "~") &&
        args.size() == 1) {
      if (name == "!") {
        const auto value = boolean(args[0]);
        if (value)
          return Items{Item(Attr(!*value))};
      } else {
        const auto value = integer(args[0]);
        if (value && name == "+")
          return Items{Item(Attr(*value))};
        if (value && name == "~")
          return Items{Item(Attr(~*value))};
        if (value && *value != std::numeric_limits<std::int64_t>::min())
          return Items{Item(Attr(-*value))};
      }
      fail("unary operator has invalid operand", loc);
      return std::nullopt;
    }
    if ((name == "+" || name == "-" || name == "*" || name == "/" ||
         name == "%") &&
        args.size() == 2) {
      if (name == "+") {
        const Items* left = list(args[0]);
        const Items* right = list(args[1]);
        if (left && right) {
          Items value = *left;
          value.insert(value.end(), right->begin(), right->end());
          return Items{Item(std::move(value))};
        }
      }
      const auto left = integer(args[0]);
      const auto right = integer(args[1]);
      const auto min = std::numeric_limits<std::int64_t>::min();
      if (!left || !right || ((name == "/" || name == "%") && *right == 0) ||
          ((name == "/" || name == "%") && *left == min && *right == -1)) {
        fail("integer operator has invalid operands", loc);
        return std::nullopt;
      }
      std::int64_t value = 0;
      bool valid = true;
      if (name == "+")
        valid = add(*left, *right, value);
      else if (name == "-")
        valid = subtract(*left, *right, value);
      else if (name == "*")
        valid = multiply(*left, *right, value);
      else if (name == "/")
        value = *left / *right;
      else
        value = *left % *right;
      if (!valid) {
        fail("compile-time integer overflow", loc);
        return std::nullopt;
      }
      return Items{Item(Attr(value))};
    }
    fail("unsupported compile-time operator: " + std::string(name), loc);
    return std::nullopt;
  }

  std::optional<Items> intrinsic(std::string_view name, const Items& args,
                                 Loc loc) {
    if (name == "fns" && args.size() == 1) {
      const auto* mod = as<Mod*>(args[0]);
      if (mod && *mod) {
        Items out;
        for (Fn fn : (*mod)->fns())
          out.emplace_back(fn);
        return Items{Item(std::move(out))};
      }
    } else if (name == "params" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0])) {
        Items out;
        for (Val value : fn->params())
          out.emplace_back(value);
        return Items{Item(std::move(out))};
      }
    } else if (name == "blks" && args.size() == 1) {
      std::vector<Blk> blks;
      if (const auto* fn = as<Fn>(args[0]))
        blks = fn->blks();
      else if (const auto* op = as<Op>(args[0]))
        blks = op->blks();
      else {
        fail("invalid ir.blks compile-time call", loc);
        return std::nullopt;
      }
      Items out;
      for (Blk block : blks)
        out.emplace_back(block);
      return Items{Item(std::move(out))};
    } else if (name == "block" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]); op && op->block())
        return Items{Item(op->block())};
    } else if (name == "ops" && args.size() == 1) {
      std::vector<Op> ops;
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod)
        ops = (*mod)->ops();
      else if (const auto* fn = as<Fn>(args[0]))
        ops = fn->ops();
      else if (const auto* block = as<Blk>(args[0]))
        ops = block->ops();
      else {
        fail("invalid ir.ops compile-time call", loc);
        return std::nullopt;
      }
      Items out;
      for (Op op : ops)
        out.emplace_back(op);
      return Items{Item(std::move(out))};
    } else if ((name == "args" || name == "outs") && args.size() == 1) {
      std::vector<Val> vals;
      bool node = false;
      if (const auto* op = as<Op>(args[0])) {
        vals = name == "args" ? op->args() : op->outs();
        node = true;
      } else if (name == "args") {
        if (const auto* block = as<Blk>(args[0])) {
          vals = block->args();
          node = true;
        }
      }
      if (node) {
        Items out;
        for (Val value : vals)
          out.emplace_back(value);
        return Items{Item(std::move(out))};
      }
    } else if (name == "users" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0])) {
        Items out;
        for (Op user : value->users())
          out.emplace_back(user);
        return Items{Item(std::move(out))};
      }
    } else if (name == "live" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]))
        return Items{Item(Attr(fn->valid()))};
      if (const auto* block = as<Blk>(args[0]))
        return Items{Item(Attr(block->valid()))};
      if (const auto* op = as<Op>(args[0]))
        return Items{Item(Attr(op->valid()))};
      if (const auto* value = as<Val>(args[0]))
        return Items{Item(Attr(value->valid()))};
    } else if (name == "kind" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0])) {
        std::string_view value;
        switch (op->kind()) {
        case Op::Kind::call:
          value = "call";
          break;
        case Op::Kind::constant:
          value = "constant";
          break;
        case Op::Kind::loop:
          value = "loop";
          break;
        case Op::Kind::branch:
          value = "branch";
          break;
        case Op::Kind::ret:
          value = "return";
          break;
        case Op::Kind::yield:
          value = "yield";
          break;
        }
        return Items{Item(Attr(std::string(value)))};
      }
    } else if (name == "callee" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]))
        return Items{Item(Attr(std::string(op->callee())))};
    } else if (name == "type" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return Items{Item(Attr(std::string(value->type().text())))};
    } else if (name == "meta" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]); fn && *fn)
        return Items{Item(Attr(fn->meta()))};
      if (const auto* op = as<Op>(args[0]); op && *op)
        return Items{Item(Attr(op->meta()))};
    } else if ((name == "has" || name == "meta") && args.size() == 2) {
      const auto key = string(args[1]);
      if (key) {
        const Attr* value = nullptr;
        bool node = false;
        if (const auto* fn = as<Fn>(args[0])) {
          value = fn->meta(*key);
          node = true;
        } else if (const auto* op = as<Op>(args[0])) {
          value = op->meta(*key);
          node = true;
        }
        if (node) {
          if (name == "has")
            return Items{Item(Attr(value != nullptr))};
          return Items{Item(value ? *value : Attr{})};
        }
      }
    } else if (name == "is_const" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return Items{Item(Attr(value->is_const()))};
    } else if (name == "constant") {
      if (args.size() == 1) {
        if (const auto* value = as<Val>(args[0]); value && value->is_const())
          return Items{Item(value->constant())};
      } else if (args.size() == 4) {
        const auto* mod = as<Mod*>(args[0]);
        const auto* before = as<Op>(args[1]);
        auto value = attribute(args[2]);
        const auto type = string(args[3]);
        if (mod && *mod && before && value && type) {
          Val result = (*mod)->constant(*before, std::move(*value),
                                        Ty(std::string(*type)));
          if (result)
            return Items{Item(result)};
        }
      }
    } else if (name == "len" && args.size() == 1) {
      if (const Items* values = list(args[0]))
        return Items{Item(Attr(static_cast<std::int64_t>(values->size())))};
    } else if (name == "call" && args.size() == 5) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* before = as<Op>(args[1]);
      const auto callee = string(args[2]);
      const Items* values = list(args[3]);
      if (mod && *mod && before && callee && values) {
        std::vector<Val> inputs;
        inputs.reserve(values->size());
        for (const Item& item : *values) {
          const auto* value = as<Val>(item);
          if (!value) {
            fail("ir.call arguments must be values", loc);
            return std::nullopt;
          }
          inputs.push_back(*value);
        }
        if (const auto type = string(args[4])) {
          Val result = (*mod)->call(*before, std::string(*callee), inputs,
                                    Ty(std::string(*type)));
          if (result)
            return Items{Item(result)};
        } else if (const Items* type_items = list(args[4])) {
          std::vector<Ty> types;
          types.reserve(type_items->size());
          for (const Item& item : *type_items) {
            const auto type = string(item);
            if (!type) {
              fail("ir.call result types must be strings", loc);
              return std::nullopt;
            }
            types.emplace_back(std::string(*type));
          }
          Op result =
              (*mod)->call(*before, std::string(*callee), inputs, types);
          if (result)
            return Items{Item(result)};
        }
      }
    } else if (name == "loop" && args.size() == 5) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* before = as<Op>(args[1]);
      auto names = strings(args[2]);
      auto sources = value_handles(args[3]);
      auto carried = value_handles(args[4]);
      if (mod && *mod && before && names && sources && carried) {
        Op result = (*mod)->loop(*before, *names, *sources, *carried);
        if (result)
          return Items{Item(result)};
      }
    } else if (name == "branch" && args.size() == 4) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* before = as<Op>(args[1]);
      const auto* condition = as<Val>(args[2]);
      auto carried = value_handles(args[3]);
      if (mod && *mod && before && condition && carried) {
        Op result = (*mod)->branch(*before, *condition, *carried);
        if (result)
          return Items{Item(result)};
      }
    } else if (name == "clone" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      const auto* before = as<Op>(args[2]);
      if (mod && *mod && op && before) {
        Op result = (*mod)->clone(*op, *before);
        if (result)
          return Items{Item(result)};
      }
    } else if (name == "move" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      const auto* before = as<Op>(args[2]);
      if (mod && *mod && op && before)
        return Items{Item(Attr((*mod)->move(*op, *before)))};
    } else if (name == "args" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      auto values = value_handles(args[2]);
      if (mod && *mod && op && values)
        return Items{Item(Attr((*mod)->args(*op, *values)))};
    } else if (name == "fuse" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const Items* values = list(args[1]);
      const auto callee = string(args[2]);
      if (mod && *mod && values && callee) {
        std::vector<Op> ops;
        ops.reserve(values->size());
        for (const Item& item : *values) {
          const auto* op = as<Op>(item);
          if (!op) {
            fail("ir.fuse arguments must be operations", loc);
            return std::nullopt;
          }
          ops.push_back(*op);
        }
        return Items{Item(Attr((*mod)->fuse(ops, std::string(*callee))))};
      }
    } else if (name == "replace" &&
               (args.size() == 3 || args.size() == 4)) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* old_value = as<Val>(args[1]);
      const auto* new_value = as<Val>(args[2]);
      if (mod && *mod && old_value && new_value) {
        if (args.size() == 3)
          return Items{Item(Attr((*mod)->replace(*old_value, *new_value)))};
        if (const auto* user = as<Op>(args[3]))
          return Items{
              Item(Attr((*mod)->replace(*old_value, *new_value, *user)))};
      }
    } else if (name == "erase" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      if (mod && *mod && op)
        return Items{Item(Attr((*mod)->erase(*op)))};
    } else if (name == "rename" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto value = string(args[2]);
      if (mod && *mod && value) {
        if (const auto* op = as<Op>(args[1]))
          return Items{Item(Attr((*mod)->rename(*op, std::string(*value))))};
        if (const auto* val = as<Val>(args[1]))
          return Items{Item(Attr((*mod)->rename(*val, std::string(*value))))};
      }
    } else if (name == "set" && args.size() == 4) {
      const auto* mod = as<Mod*>(args[0]);
      const auto key = string(args[2]);
      auto value = attribute(args[3]);
      if (mod && *mod && key && value) {
        if (const auto* fn = as<Fn>(args[1]))
          return Items{Item(Attr((*mod)->set(*fn, std::string(*key),
                                             std::move(*value))))};
        if (const auto* op = as<Op>(args[1]))
          return Items{Item(Attr((*mod)->set(*op, std::string(*key),
                                             std::move(*value))))};
      }
    } else if (name == "unset" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto key = string(args[2]);
      if (mod && *mod && key) {
        if (const auto* fn = as<Fn>(args[1]))
          return Items{Item(Attr((*mod)->unset(*fn, *key)))};
        if (const auto* op = as<Op>(args[1]))
          return Items{Item(Attr((*mod)->unset(*op, *key)))};
      }
    }
    fail("invalid ir." + std::string(name) + " compile-time call", loc);
    return std::nullopt;
  }

  Env& env_;
  Error error_;
  Attr::List* trace_ = nullptr;
  bool failed_ = false;
};

}  // namespace joggle::detail

namespace joggle {

bool query(Env& env, std::string_view function, const Mod& mod, Attr& result,
           std::span<const Attr> args, bool* cached) {
  env.clear_diags();
  result = Attr{};
  if (cached)
    *cached = false;

  auto& entries = mod.impl_->store.queries;
  const auto hit = std::find_if(entries.begin(), entries.end(),
                                [&](const detail::QueryData& entry) {
                                  return entry.env == env.cache_id() &&
                                         entry.epoch == env.cache_epoch() &&
                                         entry.revision == mod.revision() &&
                                         entry.function == function &&
                                         entry.args.size() == args.size() &&
                                         std::equal(entry.args.begin(),
                                                    entry.args.end(),
                                                    args.begin());
                                });
  if (hit != entries.end()) {
    result = hit->result;
    if (cached)
      *cached = true;
    return true;
  }

  const Ty applied{std::string(function)};
  const std::string symbol(applied.args().empty() ? function : applied.name());
  const std::vector<Ty> explicit_args =
      applied.args().empty() ? std::vector<Ty>{} : applied.args();
  const std::vector<Fn> candidates = env.find_fns(symbol);
  std::vector<Ty> argument_types{Ty("Mod")};
  argument_types.reserve(args.size() + 1);
  for (const Attr& value : args)
    argument_types.push_back(detail::runtime_type(detail::materialize(value)));
  std::vector<Ty> result_types;
  bool ambiguous = false;
  const Fn fn = detail::resolve_overload(
      candidates, argument_types, explicit_args, &result_types, &ambiguous);
  if (!fn) {
    env.error((ambiguous ? "ambiguous query function: "
                         : "query function not found: ") +
              std::string(function));
    return false;
  }
  if (fn.external()) {
    env.error("query entry must have a textual body: " +
                  std::string(function),
              fn.loc());
    return false;
  }
  if (result_types.size() != 1) {
    env.error("query entry must return exactly one value: " +
                  std::string(function),
              fn.loc());
    return false;
  }

  Mod scratch;
  scratch.impl_->store = mod.impl_->store;
  scratch.impl_->store.queries.clear();
  if (!scratch.verify(env)) {
    for (const Diag& diag : scratch.diags())
      env.error(diag.message, diag.loc);
    env.error("cannot query an invalid module");
    return false;
  }
  const std::uint64_t before = scratch.revision();
  const std::string structure = print(scratch);
  detail::Eval eval(env, [&](std::string message, Loc loc) {
    env.error(std::move(message), std::move(loc));
  });
  const auto values = eval.query(fn, scratch, args);
  if (!values)
    return false;
  if (scratch.revision() != before || print(scratch) != structure) {
    env.error("query function mutated its module snapshot: " +
                  std::string(function),
              fn.loc());
    return false;
  }
  if (values->size() != 1) {
    env.error("query function returned an invalid result: " +
              std::string(function));
    return false;
  }
  auto converted = detail::attribute(values->front());
  if (!converted) {
    env.error("query result is not representable as Attr: " +
                  std::string(function),
              fn.loc());
    return false;
  }
  result = std::move(*converted);
  entries.push_back({env.cache_id(), env.cache_epoch(), mod.revision(),
                     std::string(function),
                     std::vector<Attr>(args.begin(), args.end()), result});
  return true;
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report) {
  env.clear_diags();
  report = Attr{};
  if (!mod.verify(env)) {
    env.error("cannot run a compile-time function on an invalid module");
    return false;
  }
  detail::Store before = mod.impl_->store;
  Fn fn = env.find_fn(function);
  if (!fn) {
    env.error("compile-time function not found: " + std::string(function));
    return false;
  }
  const std::vector<Val> params = fn.params();
  if (params.size() != 1 || params.front().type().text() != "Mod") {
    env.error("compile-time entry must accept exactly one Mod: " +
                  std::string(function),
              fn.loc());
    return false;
  }
  const std::vector<Ty> returns = fn.returns();
  if (returns.size() != 1 || returns.front().text() != "bool") {
    env.error("compile-time entry must return exactly one bool: " +
                  std::string(function),
              fn.loc());
    return false;
  }
  const std::uint64_t before_revision = mod.revision();
  Attr::List trace;
  detail::Eval eval(env, [&](std::string message, Loc loc) {
    env.error(std::move(message), std::move(loc));
  }, &trace);
  const auto result = eval.run(fn, mod);
  if (!result) {
    mod.impl_->store = std::move(before);
    return false;
  }
  if (result->size() != 1) {
    mod.impl_->store = std::move(before);
    env.error("compile-time entry returned an invalid result: " +
              std::string(function));
    return false;
  }
  const Attr* returned = detail::as<Attr>(result->front());
  if (!returned || !returned->boolean()) {
    mod.impl_->store = std::move(before);
    env.error("compile-time entry did not return bool: " +
              std::string(function));
    return false;
  }
  if (!mod.verify(env)) {
    mod.impl_->store = std::move(before);
    env.error("compile-time function produced an invalid module: " +
              std::string(function));
    return false;
  }
  if (!trace.empty())
    trace.pop_back();
  const std::uint64_t after_revision = mod.revision();
  Attr::Dict summary;
  summary["ok"] = Attr(true);
  summary["fn"] = Attr(std::string(function));
  summary["reported"] = *returned;
  summary["before"] = Attr(static_cast<std::int64_t>(before_revision));
  summary["after"] = Attr(static_cast<std::int64_t>(after_revision));
  summary["edits"] =
      Attr(static_cast<std::int64_t>(after_revision - before_revision));
  summary["changed"] = Attr(after_revision != before_revision);
  summary["steps"] = Attr(std::move(trace));
  report = Attr(std::move(summary));
  return true;
}

bool run(Env& env, std::string_view function, Mod& mod) {
  Attr ignored;
  return run(env, function, mod, ignored);
}

}  // namespace joggle
