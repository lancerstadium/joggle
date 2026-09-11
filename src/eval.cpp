#include "detail.h"

#include <algorithm>
#include <bit>
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

std::optional<std::vector<Ty>> types(const Item& item) {
  const Items* items = list(item);
  if (!items)
    return std::nullopt;
  std::vector<Ty> out;
  out.reserve(items->size());
  for (const Item& entry : *items) {
    const auto* type = as<Ty>(entry);
    if (!type)
      return std::nullopt;
    out.push_back(*type);
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
    event["kind"] = Attr("fn");
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

  void record_expand(Mod& mod, std::string source, Fn implementation,
                     std::uint64_t before) {
    if (!trace_)
      return;
    const std::uint64_t after = mod.revision();
    Attr::List params;
    for (Val param : implementation.params())
      params.emplace_back(std::string(param.type().text()));
    Attr::List returns;
    for (const Ty& type : implementation.returns())
      returns.emplace_back(std::string(type.text()));
    Attr::Dict event;
    event["kind"] = Attr("expand");
    event["source"] = Attr(std::move(source));
    event["impl"] = Attr(std::string(implementation.module()) + "." +
                          std::string(implementation.name()));
    event["params"] = Attr(std::move(params));
    event["returns"] = Attr(std::move(returns));
    event["before"] = Attr(static_cast<std::int64_t>(before));
    event["after"] = Attr(static_cast<std::int64_t>(after));
    event["edits"] = Attr(static_cast<std::int64_t>(after - before));
    trace_->emplace_back(std::move(event));
  }

  void record_clone(Mod& mod, Fn source, Fn copy, std::uint64_t before,
                    std::span<const Ty> generics = {}) {
    if (!trace_)
      return;
    Attr::List params;
    for (Val param : copy.params())
      params.emplace_back(std::string(param.type().text()));
    Attr::List returns;
    for (const Ty& type : copy.returns())
      returns.emplace_back(std::string(type.text()));
    Attr::List bindings;
    for (const Ty& type : generics)
      bindings.emplace_back(std::string(type.text()));
    const std::uint64_t after = mod.revision();
    Attr::Dict event;
    event["kind"] = Attr("clone");
    event["source"] =
        Attr(std::string(source.module()) + "." + std::string(source.name()));
    event["copy"] =
        Attr(std::string(copy.module()) + "." + std::string(copy.name()));
    event["params"] = Attr(std::move(params));
    event["returns"] = Attr(std::move(returns));
    event["generics"] = Attr(std::move(bindings));
    event["before"] = Attr(static_cast<std::int64_t>(before));
    event["after"] = Attr(static_cast<std::int64_t>(after));
    event["edits"] = Attr(static_cast<std::int64_t>(after - before));
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
    Flow flow = blk(fn.body(), {}, frame);
    if (flow.kind == FlowKind::ret) {
      if (observed)
        record(fn, *observed, before, flow.values);
      return flow.values;
    }
    if (flow.kind != FlowKind::fail)
      fail("compile-time function reached the end without return", fn.loc());
    return std::nullopt;
  }

  Flow blk(Blk blk, const Items& args, Frame& frame) {
    const std::vector<Val> params = blk.args();
    if (params.size() != args.size()) {
      fail("compile-time Blk argument count is inconsistent");
      return {FlowKind::fail, {}};
    }
    for (std::size_t index = 0; index < params.size(); ++index)
      put(frame, params[index], args[index]);

    for (Op op : blk.ops()) {
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
        const std::vector<Val> operands = op.args();
        const auto call_args = values(frame, operands, loc);
        if (!call_args)
          return {FlowKind::fail, {}};
        const auto result =
            call(blk.fn(), op.callee(), *call_args, operands, loc);
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
    Flow flow = blk(arm, carried, nested);
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
      Items blk_args = indices;
      blk_args.insert(blk_args.end(), carried.begin(), carried.end());
      Frame nested = frame;
      Flow flow = blk(op.blks().front(), blk_args, nested);
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
                            const Items& args,
                            std::span<const Val> operands, Loc loc) {
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
    for (std::size_t index = 0; index < args.size(); ++index) {
      Ty type = runtime_type(args[index]);
      if (index < operands.size()) {
        const Ty declared = operands[index].type();
        const bool unknown = type.name() == "_";
        const bool empty_list =
            type.name() == "list" && type.args().size() == 1 &&
            type.args().front().name() == "_" &&
            declared.name() == "list";
        if (unknown || empty_list)
          type = declared;
      }
      argument_types.push_back(std::move(type));
    }
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
    if (target.external() && target.module() == "base" &&
        fundamental(target.name()))
      return fundamental(target.name(), args, std::move(loc));
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
      if (const Items* items = list(args[0])) {
        const auto index = integer(args[1]);
        if (!index || *index < 0 ||
            static_cast<std::size_t>(*index) >= items->size()) {
          fail("compile-time index is out of bounds", loc);
          return std::nullopt;
        }
        return Items{(*items)[static_cast<std::size_t>(*index)]};
      }
      const auto* value = as<Attr>(args[0]);
      const auto* values = value ? value->dict() : nullptr;
      const auto key = string(args[1]);
      if (!values || !key) {
        fail("compile-time index has invalid operands", loc);
        return std::nullopt;
      }
      const auto found = values->find(*key);
      if (found == values->end()) {
        fail("compile-time dictionary key not found: " + std::string(*key),
             loc);
        return std::nullopt;
      }
      return Items{materialize(found->second)};
    }
    if (name == "[]=" && args.size() == 3) {
      if (const Items* items = list(args[0])) {
        const auto index = integer(args[1]);
        if (!index || *index < 0 ||
            static_cast<std::size_t>(*index) >= items->size()) {
          fail("compile-time index is out of bounds", loc);
          return std::nullopt;
        }
        Items out = *items;
        out[static_cast<std::size_t>(*index)] = args[2];
        return Items{Item(std::move(out))};
      }
      const auto* value = as<Attr>(args[0]);
      const auto key = string(args[1]);
      const auto item = attribute(args[2]);
      if (value && value->dict() && key && item) {
        Attr::Dict out = *value->dict();
        out.insert_or_assign(std::string(*key), *item);
        return Items{Item(Attr(std::move(out)))};
      }
      fail("compile-time indexed assignment has invalid operands", loc);
      return std::nullopt;
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
        const auto left_text = string(args[0]);
        const auto right_text = string(args[1]);
        if (left_text && right_text)
          return Items{Item(Attr(std::string(*left_text) +
                                      std::string(*right_text)))};
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
    if ((name == "|" || name == "^" || name == "&" || name == "<<" ||
         name == ">>") &&
        args.size() == 2) {
      const auto left = integer(args[0]);
      const auto right = integer(args[1]);
      if (!left || !right ||
          ((name == "<<" || name == ">>") &&
           (*right < 0 || *right >= 64))) {
        fail("bit operator has invalid operands", loc);
        return std::nullopt;
      }
      const std::uint64_t left_bits = std::bit_cast<std::uint64_t>(*left);
      std::uint64_t bits = 0;
      if (name == "|")
        bits = left_bits | std::bit_cast<std::uint64_t>(*right);
      else if (name == "^")
        bits = left_bits ^ std::bit_cast<std::uint64_t>(*right);
      else if (name == "&")
        bits = left_bits & std::bit_cast<std::uint64_t>(*right);
      else if (name == "<<")
        bits = left_bits << static_cast<unsigned>(*right);
      else {
        bits = left_bits >> static_cast<unsigned>(*right);
        if (*left < 0 && *right)
          bits |= ~std::uint64_t{0} << (64 - static_cast<unsigned>(*right));
      }
      return Items{Item(Attr(std::bit_cast<std::int64_t>(bits)))};
    }
    fail("unsupported compile-time operator: " + std::string(name), loc);
    return std::nullopt;
  }

  bool fundamental(std::string_view name) const noexcept {
    return name == "len" || name == "keys" || name == "has" ||
           name == "get" || name == "size" || name == "byte" ||
           name == "kind" || name == "assert" || name == "name" ||
           name == "args" || name == "int" || name == "str" ||
           name == "text" || name == "replace" ||
           name == "ty";
  }

  std::optional<Items> fundamental(std::string_view name, const Items& args,
                                   Loc loc) {
    if (name == "len" && args.size() == 1) {
      if (const Items* values = list(args[0]))
        return Items{Item(Attr(static_cast<std::int64_t>(values->size())))};
      if (const auto* value = as<Attr>(args[0]); value && value->dict())
        return Items{
            Item(Attr(static_cast<std::int64_t>(value->dict()->size())))};
    } else if (name == "size" && args.size() == 1) {
      if (const auto* value = as<Attr>(args[0]); value && value->bytes())
        return Items{
            Item(Attr(static_cast<std::int64_t>(value->bytes()->size())))};
    } else if (name == "byte" && args.size() == 2) {
      const auto* value = as<Attr>(args[0]);
      const auto index = integer(args[1]);
      if (value && value->bytes() && index && *index >= 0 &&
          static_cast<std::size_t>(*index) < value->bytes()->size())
        return Items{Item(Attr(static_cast<std::int64_t>(
            (*value->bytes())[static_cast<std::size_t>(*index)])))};
    } else if (name == "keys" && args.size() == 1) {
      if (const auto* value = as<Attr>(args[0]); value && value->dict()) {
        Items out;
        out.reserve(value->dict()->size());
        for (const auto& [key, ignored] : *value->dict()) {
          (void)ignored;
          out.emplace_back(Attr(key));
        }
        return Items{Item(std::move(out))};
      }
    } else if (name == "has" && args.size() == 2) {
      const auto* value = as<Attr>(args[0]);
      const auto key = string(args[1]);
      if (value && value->dict() && key)
        return Items{Item(Attr(value->dict()->contains(*key)))};
    } else if (name == "get" && (args.size() == 2 || args.size() == 3)) {
      const auto* value = as<Attr>(args[0]);
      const auto key = string(args[1]);
      if (value && value->dict() && key) {
        const auto found = value->dict()->find(*key);
        if (found != value->dict()->end())
          return Items{materialize(found->second)};
        if (args.size() == 3)
          return Items{args[2]};
        return Items{Item(Attr{})};
      }
    } else if (name == "kind" && args.size() == 1) {
      if (list(args[0]))
        return Items{Item(Attr("list"))};
      if (const auto* value = as<Ty>(args[0])) {
        std::string_view type = "Ty";
        if (integer(*value))
          type = "int";
        else if (value->text() == "true" || value->text() == "false")
          type = "bool";
        else if (value->name() == "[]")
          type = "list";
        return Items{Item(Attr(std::string(type)))};
      }
      if (const auto* value = as<Attr>(args[0])) {
        const std::string_view type = value->boolean() ? "bool"
                                      : value->integer() ? "int"
                                      : value->real()    ? "f64"
                                      : value->string()  ? "str"
                                      : value->bytes()   ? "bytes"
                                      : value->list()    ? "list"
                                      : value->dict()    ? "dict"
                                                         : "nil";
        return Items{Item(Attr(std::string(type)))};
      }
    } else if (name == "assert" && args.size() == 2) {
      const auto condition = boolean(args[0]);
      const auto message = string(args[1]);
      if (condition && message) {
        if (!*condition) {
          fail(std::string(*message), loc);
          return std::nullopt;
        }
        return Items{Item(Attr(true))};
      }
    } else if (name == "name" && args.size() == 1) {
      if (const auto* type = as<Ty>(args[0]); type && type->valid())
        return Items{Item(Attr(std::string(type->name())))};
    } else if (name == "args" && args.size() == 1) {
      if (const auto* type = as<Ty>(args[0]); type && type->valid()) {
        Items out;
        for (const Ty& argument : type->args())
          out.emplace_back(argument);
        return Items{Item(std::move(out))};
      }
    } else if (name == "int" && args.size() == 1) {
      if (const auto* type = as<Ty>(args[0]); type && type->valid()) {
        if (const auto value = integer(*type))
          return Items{Item(Attr(*value))};
      } else if (const auto value = integer(args[0])) {
        return Items{Item(Attr(*value))};
      }
    } else if (name == "str" && args.size() == 1) {
      if (const auto* type = as<Ty>(args[0]); type && type->valid()) {
        return Items{Item(Attr(std::string(type->text())))};
      } else if (const auto value = string(args[0])) {
        return Items{Item(Attr(std::string(*value)))};
      }
    } else if (name == "text" && args.size() == 1) {
      if (const auto* type = as<Ty>(args[0]); type && type->valid())
        return Items{Item(Attr(std::string(type->text())))};
      if (const auto value = attribute(args[0]))
        return Items{Item(Attr(joggle::print(*value)))};
    } else if (name == "replace" && args.size() == 3) {
      const auto input = string(args[0]);
      const auto from = string(args[1]);
      const auto to = string(args[2]);
      if (input && from && to && !from->empty()) {
        std::string out(*input);
        std::size_t offset = 0;
        while ((offset = out.find(*from, offset)) != std::string::npos) {
          out.replace(offset, from->size(), *to);
          offset += to->size();
        }
        return Items{Item(Attr(std::move(out)))};
      }
    } else if (name == "ty" && args.size() == 1) {
      Ty type;
      if (const auto value = integer(args[0]))
        type = Ty(std::to_string(*value));
      else if (const auto value = string(args[0]))
        type = Ty(std::string(*value));
      if (type.valid())
        return Items{Item(std::move(type))};
    } else if (name == "ty" && args.size() == 2) {
      const auto constructor = string(args[0]);
      const Items* arguments = list(args[1]);
      if (constructor && arguments) {
        std::string text = *constructor == "[]"
                               ? "["
                               : std::string(*constructor) + '<';
        for (std::size_t index = 0; index < arguments->size(); ++index) {
          const auto* argument = as<Ty>((*arguments)[index]);
          if (!argument || !argument->valid()) {
            text.clear();
            break;
          }
          if (index)
            text += ", ";
          text += argument->text();
        }
        if (!text.empty()) {
          text += *constructor == "[]" ? ']' : '>';
          Ty type(std::move(text));
          if (type.valid())
            return Items{Item(std::move(type))};
        }
      }
    }
    fail("invalid base." + std::string(name) + " compile-time call", loc);
    return std::nullopt;
  }

  std::optional<Items> intrinsic(std::string_view name, const Items& args,
                                 Loc loc) {
    if (name == "fns" && args.size() == 1) {
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod) {
        Items out;
        for (Fn fn : (*mod)->fns())
          out.emplace_back(fn);
        return Items{Item(std::move(out))};
      } else if (const auto module = string(args[0])) {
        Items out;
        for (Fn fn : env_.fns(*module))
          out.emplace_back(fn);
        return Items{Item(std::move(out))};
      }
    } else if (name == "find" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto symbol = string(args[1]);
      if (mod && *mod && symbol)
        return Items{Item((*mod)->find_fn(*symbol))};
    } else if (name == "uses" && args.size() == 1) {
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod) {
        Items out;
        for (const std::string& module : (*mod)->uses())
          out.emplace_back(Attr(module));
        return Items{Item(std::move(out))};
      }
    } else if (name == "revision" && args.size() == 1) {
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod)
        return Items{Item(Attr(static_cast<std::int64_t>((*mod)->revision())))};
    } else if (name == "params" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0])) {
        Items out;
        for (Val value : fn->params())
          out.emplace_back(value);
        return Items{Item(std::move(out))};
      }
    } else if (name == "returns" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0])) {
        Items out;
        for (const Ty& type : fn->returns())
          out.emplace_back(type);
        return Items{Item(std::move(out))};
      }
    } else if (name == "generics" && args.size() == 1) {
      Items out;
      if (const auto* fn = as<Fn>(args[0])) {
        for (Val value : fn->generics())
          out.emplace_back(value);
      } else if (const auto* op = as<Op>(args[0])) {
        for (const Ty& type : op->generics())
          out.emplace_back(type);
      } else {
        fail("invalid ir.generics compile-time call", loc);
        return std::nullopt;
      }
      return Items{Item(std::move(out))};
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
      for (Blk blk : blks)
        out.emplace_back(blk);
      return Items{Item(std::move(out))};
    } else if (name == "blk" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]); op && op->blk())
        return Items{Item(op->blk())};
    } else if (name == "ops" && args.size() == 1) {
      std::vector<Op> ops;
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod)
        ops = (*mod)->ops();
      else if (const auto* fn = as<Fn>(args[0]))
        ops = fn->ops();
      else if (const auto* blk = as<Blk>(args[0]))
        ops = blk->ops();
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
        if (const auto* blk = as<Blk>(args[0])) {
          vals = blk->args();
          node = true;
        }
      }
      if (node) {
        Items out;
        for (Val value : vals)
          out.emplace_back(value);
        return Items{Item(std::move(out))};
      }
    } else if (name == "def" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return Items{Item(value->def())};
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
      if (const auto* blk = as<Blk>(args[0]))
        return Items{Item(Attr(blk->valid()))};
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
    } else if (name == "name" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]); fn && *fn)
        return Items{Item(Attr(std::string(fn->name())))};
      if (const auto* value = as<Val>(args[0]); value && *value)
        return Items{Item(Attr(std::string(value->name())))};
    } else if (name == "type" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return Items{Item(value->type())};
    } else if (name == "type" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* value = as<Val>(args[1]);
      const auto* type = as<Ty>(args[2]);
      if (mod && *mod && value && type)
        return Items{Item(Attr((*mod)->type(*value, *type)))};
    } else if (name == "resolve" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      if (mod && *mod && op) {
        const Fn target = env_.resolve(**mod, *op);
        return Items{Item(target)};
      }
    } else if (name == "accepts" && args.size() == 2) {
      const auto* op = as<Op>(args[0]);
      const auto* fn = as<Fn>(args[1]);
      if (op && fn)
        return Items{Item(Attr(env_.accepts(*op, *fn)))};
    } else if (name == "match" && args.size() == 2) {
      const auto* op = as<Op>(args[0]);
      const auto* fn = as<Fn>(args[1]);
      if (op && fn) {
        Items out;
        for (const Ty& type : env_.match(*op, *fn))
          out.emplace_back(type);
        return Items{Item(std::move(out))};
      }
      const Items* items = list(args[1]);
      if (op && items) {
        std::vector<Fn> candidates;
        candidates.reserve(items->size());
        for (const Item& item : *items) {
          const auto* fn = as<Fn>(item);
          if (!fn) {
            fail("ir.match candidates must be functions", loc);
            return std::nullopt;
          }
          candidates.push_back(*fn);
        }
        bool ambiguous = false;
        const Fn selected = env_.match(*op, candidates, &ambiguous);
        if (ambiguous) {
          fail("ir.match has equally specific candidates", loc);
          return std::nullopt;
        }
        return Items{Item(selected)};
      }
    } else if (name == "where" && args.size() == 3) {
      const Items* items = list(args[0]);
      const auto key = string(args[1]);
      auto expected = attribute(args[2]);
      if (items && key && expected) {
        Items out;
        for (const Item& item : *items) {
          const auto* fn = as<Fn>(item);
          if (!fn) {
            fail("ir.where candidates must be functions", loc);
            return std::nullopt;
          }
          const Attr* actual = fn->meta(*key);
          bool selected = actual && *actual == *expected;
          if (actual && actual->list()) {
            for (const Attr& value : *actual->list())
              selected = value == *expected || selected;
          }
          if (selected)
            out.emplace_back(*fn);
        }
        return Items{Item(std::move(out))};
      }
    } else if (name == "invoke" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      const auto* fn = as<Fn>(args[2]);
      if (mod && *mod && op && *op && fn && *fn) {
        const std::vector<Val> params = fn->params();
        const std::vector<Ty> returns = fn->returns();
        if (!fn->generics().empty() || params.size() != 2 ||
            params[0].type() != Ty("Mod") || params[1].type() != Ty("Op") ||
            returns.size() != 1 || returns.front() != Ty("bool")) {
          fail("ir.invoke requires fn(Mod, Op) -> bool", loc);
          return std::nullopt;
        }
        auto result = invoke(*fn, {Item(*mod), Item(*op)});
        if (!result)
          return std::nullopt;
        if (result->size() != 1 || !boolean(result->front())) {
          fail("ir.invoke function returned an invalid result", loc);
          return std::nullopt;
        }
        return result;
      }
    } else if (name == "symbol" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]); fn && *fn)
        return Items{Item(Attr(std::string(fn->module()) + "." +
                               std::string(fn->name())))};
    } else if (name == "meta" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]); fn && *fn)
        return Items{Item(Attr(fn->meta()))};
      if (const auto* value = as<Val>(args[0]); value && *value)
        return Items{Item(Attr(value->meta()))};
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
        } else if (const auto* val = as<Val>(args[0])) {
          value = val->meta(*key);
          node = true;
        } else if (const auto* op = as<Op>(args[0])) {
          value = op->meta(*key);
          node = true;
        }
        if (node) {
          if (name == "has")
            return Items{Item(Attr(value != nullptr))};
          return Items{value ? materialize(*value) : Item(Attr{})};
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
        const auto* type = as<Ty>(args[3]);
        if (mod && *mod && before && value && type) {
          Val result = (*mod)->constant(*before, std::move(*value), *type);
          if (result)
            return Items{Item(result)};
        }
      }
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
        if (const auto* type = as<Ty>(args[4])) {
          Val result = (*mod)->call(*before, std::string(*callee), inputs,
                                    *type);
          if (result)
            return Items{Item(result)};
        } else if (auto result_types = types(args[4])) {
          Op result =
              (*mod)->call(*before, std::string(*callee), inputs,
                           *result_types);
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
    } else if (name == "clone" &&
               (args.size() == 3 || args.size() == 4)) {
      const auto* mod = as<Mod*>(args[0]);
      if (mod && *mod) {
        if (args.size() == 3) {
          if (const auto* op = as<Op>(args[1])) {
            if (const auto* before = as<Op>(args[2])) {
              Op result = (*mod)->clone(*op, *before);
              if (result)
                return Items{Item(result)};
            }
          }
        }
        if (const auto* fn = as<Fn>(args[1])) {
          const auto target = string(args[2]);
          std::vector<Ty> generics;
          bool valid = args.size() == 3;
          if (args.size() == 4) {
            if (const Items* items = list(args[3])) {
              valid = true;
              generics.reserve(items->size());
              for (const Item& item : *items) {
                const auto* type = as<Ty>(item);
                if (!type) {
                  valid = false;
                  break;
                }
                generics.push_back(*type);
              }
            }
          }
          if (target && valid) {
            const std::uint64_t before = (*mod)->revision();
            Fn result =
                (*mod)->clone(env_, *fn, std::string(*target), generics);
            if (result) {
              record_clone(**mod, *fn, result, before, generics);
              return Items{Item(result)};
            }
          }
        }
      }
    } else if (name == "expand" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      const auto* fn = as<Fn>(args[2]);
      if (mod && *mod && op && fn) {
        std::string source(op->callee());
        if (const Fn resolved = env_.resolve(**mod, *op))
          source = std::string(resolved.module()) + "." +
                   std::string(resolved.name());
        const std::uint64_t before = (*mod)->revision();
        const bool expanded = env_.expand(**mod, *op, *fn);
        if (expanded)
          record_expand(**mod, std::move(source), *fn, before);
        return Items{Item(Attr(expanded))};
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
        return Items{Item(Attr((*mod)->args(env_, *op, *values)))};
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
        return Items{
            Item(Attr((*mod)->fuse(env_, ops, std::string(*callee))))};
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
      if (mod && *mod) {
        if (const auto* op = as<Op>(args[1]))
          return Items{Item(Attr((*mod)->erase(*op)))};
        if (const auto* fn = as<Fn>(args[1]))
          return Items{Item(Attr((*mod)->erase(env_, *fn)))};
      }
    } else if (name == "returns" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* fn = as<Fn>(args[1]);
      auto result_types = types(args[2]);
      if (mod && *mod && fn && result_types)
        return Items{Item(Attr((*mod)->returns(*fn, *result_types)))};
    } else if (name == "generics" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      auto applied_types = types(args[2]);
      if (mod && *mod && op && applied_types)
        return Items{
            Item(Attr((*mod)->generics(env_, *op, *applied_types)))};
    } else if (name == "retarget" && args.size() == 4) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      const auto callee = string(args[2]);
      const auto values = value_handles(args[3]);
      if (mod && *mod && op && callee && values)
        return Items{Item(Attr((*mod)->retarget(
            env_, *op, std::string(*callee), *values)))};
    } else if (name == "rename" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto value = string(args[2]);
      if (mod && *mod && value) {
        if (const auto* fn = as<Fn>(args[1]))
          return Items{Item(
              Attr((*mod)->rename(env_, *fn, std::string(*value))))};
        if (const auto* op = as<Op>(args[1]))
          return Items{
              Item(Attr((*mod)->rename(env_, *op, std::string(*value))))};
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
        if (const auto* val = as<Val>(args[1]))
          return Items{Item(Attr((*mod)->set(*val, std::string(*key),
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
        if (const auto* val = as<Val>(args[1]))
          return Items{Item(Attr((*mod)->unset(*val, *key)))};
        if (const auto* op = as<Op>(args[1]))
          return Items{Item(Attr((*mod)->unset(*op, *key)))};
      }
    } else if (name == "use" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto module = string(args[1]);
      if (mod && *mod && module)
        return Items{Item(Attr((*mod)->use(env_, std::string(*module))))};
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

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         Attr& report) {
  env.clear_diags();
  report = Attr{};
  detail::Store before = mod.impl_->store;
  const std::uint64_t before_revision = mod.revision();
  const auto rollback = [&]() {
    mod.impl_->store = std::move(before);
    return false;
  };
  if (!mod.verify(env)) {
    env.error("cannot run compile-time functions on an invalid module");
    return false;
  }

  const auto one = [&](std::string_view function, Attr& step) {
    const std::uint64_t step_revision = mod.revision();
    const Ty applied{std::string(function)};
    const std::string symbol(applied.args().empty() ? function
                                                    : applied.name());
    const std::vector<Ty> explicit_args =
        applied.args().empty() ? std::vector<Ty>{} : applied.args();
    const std::vector<Fn> candidates = env.find_fns(symbol);
    const std::vector<Ty> argument_types{Ty("Mod")};
    bool ambiguous = false;
    const Fn fn = detail::resolve_overload(
        candidates, argument_types, explicit_args, nullptr, &ambiguous);
    if (!fn) {
      if (ambiguous) {
        env.error("ambiguous compile-time entry: " + std::string(function));
      } else if (candidates.empty()) {
        env.error("compile-time function not found: " +
                  std::string(function));
      } else {
        env.error("compile-time entry has no fn(Mod) overload: " +
                  std::string(function));
      }
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
    Attr::List trace;
    detail::Eval eval(env, [&](std::string message, Loc loc) {
      env.error(std::move(message), std::move(loc));
    }, &trace);
    const auto result = eval.run(fn, mod);
    if (!result)
      return false;
    if (result->size() != 1) {
      env.error("compile-time entry returned an invalid result: " +
                std::string(function));
      return false;
    }
    const Attr* returned = detail::as<Attr>(result->front());
    if (!returned || !returned->boolean()) {
      env.error("compile-time entry did not return bool: " +
                std::string(function));
      return false;
    }
    if (!mod.verify(env)) {
      for (const Diag& diagnostic : mod.diags())
        env.error(diagnostic.message, diagnostic.loc);
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
    summary["before"] = Attr(static_cast<std::int64_t>(step_revision));
    summary["after"] = Attr(static_cast<std::int64_t>(after_revision));
    summary["edits"] =
        Attr(static_cast<std::int64_t>(after_revision - step_revision));
    summary["changed"] = Attr(after_revision != step_revision);
    summary["steps"] = Attr(std::move(trace));
    step = Attr(std::move(summary));
    return true;
  };

  Attr::List steps;
  steps.reserve(functions.size());
  bool reported = false;
  for (const std::string_view function : functions) {
    Attr step;
    if (!one(function, step))
      return rollback();
    if (const Attr::Dict* values = step.dict()) {
      const auto found = values->find("reported");
      reported = reported ||
                 (found != values->end() && found->second.boolean() == true);
    }
    steps.push_back(std::move(step));
  }
  const std::uint64_t after_revision = mod.revision();
  Attr::Dict summary;
  summary["ok"] = Attr(true);
  summary["reported"] = Attr(reported);
  summary["before"] = Attr(static_cast<std::int64_t>(before_revision));
  summary["after"] = Attr(static_cast<std::int64_t>(after_revision));
  summary["edits"] =
      Attr(static_cast<std::int64_t>(after_revision - before_revision));
  summary["changed"] = Attr(after_revision != before_revision);
  summary["steps"] = Attr(std::move(steps));
  report = Attr(std::move(summary));
  return true;
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report) {
  const std::span<const std::string_view> functions(&function, 1);
  Attr sequence;
  if (!run(env, functions, mod, sequence))
    return false;
  report = sequence.dict()->at("steps").list()->front();
  return true;
}

bool run(Env& env, std::string_view function, Mod& mod) {
  Attr ignored;
  return run(env, function, mod, ignored);
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod) {
  Attr ignored;
  return run(env, functions, mod, ignored);
}

}  // namespace joggle
