#include "detail.h"

#include <algorithm>
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
  using Data = std::variant<std::monostate, Attr, Mod*, Fn, Blk, Op, Val,
                            std::shared_ptr<List>>;
  Data data;

  Item() = default;
  Item(Attr value) : data(std::move(value)) {}
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

std::optional<std::int64_t> integer(const Item& item) {
  const Attr* value = as<Attr>(item);
  return value ? value->integer() : std::nullopt;
}

std::optional<bool> boolean(const Item& item) {
  const Attr* value = as<Attr>(item);
  return value ? value->boolean() : std::nullopt;
}

std::optional<std::string_view> string(const Item& item) {
  const Attr* value = as<Attr>(item);
  return value ? value->string() : std::nullopt;
}

bool same(const Item& left, const Item& right) {
  if (left.data.index() != right.data.index())
    return false;
  if (const auto* value = as<Attr>(left))
    return *value == *as<Attr>(right);
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

  Eval(Env& env, Error error) : env_(env), error_(std::move(error)) {}

  bool run(Fn fn, Mod& mod) {
    const auto result = invoke(fn, {Item(&mod)});
    return result.has_value();
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

  Fn local(Fn current, std::string_view name) {
    if (!current || !current.store_)
      return {};
    const auto found = current.store_->symbols.find(std::string(name));
    if (found == current.store_->symbols.end())
      return {};
    const std::uint32_t id = found->second;
    return Fn(current.store_, id, current.store_->fns[id].generation);
  }

  std::optional<Items> invoke(Fn fn, const Items& args) {
    if (!fn) {
      fail("compile-time function is invalid");
      return std::nullopt;
    }
    const std::vector<Val> params = fn.params();
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

    Frame frame;
    for (std::size_t index = 0; index < params.size(); ++index)
      put(frame, params[index], args[index]);
    Flow flow = block(fn.body(), {}, frame);
    if (flow.kind == FlowKind::ret)
      return flow.values;
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
        if (result->size() > 1 || outs.size() > 1) {
          fail("multiple call results are not supported yet", loc);
          return {FlowKind::fail, {}};
        }
        if (!outs.empty())
          put(frame, outs.front(), result->empty() ? Item{} : result->front());
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
    const std::vector<Blk> blocks = op.blocks();
    const Blk arm = blocks[*condition ? 0 : 1];
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
        op.blocks().front().args().size() - op.outs().size();
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
      Flow flow = block(op.blocks().front(), block_args, nested);
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
    if (name.starts_with("operator "))
      return operation(name.substr(9), args, std::move(loc));
    if (name == "base.copy" && args.size() == 1)
      return args;
    if (name.starts_with("ir."))
      return intrinsic(name.substr(3), args, std::move(loc));
    Fn target = name.find('.') == std::string_view::npos ? local(current, name)
                                                         : env_.find_fn(name);
    if (!target) {
      fail("unknown compile-time function: " + std::string(name), loc);
      return std::nullopt;
    }
    return invoke(target, args);
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
    } else if (name == "blocks" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0])) {
        Items out;
        for (Blk block : fn->blocks())
          out.emplace_back(block);
        return Items{Item(std::move(out))};
      }
    } else if (name == "ops" && args.size() == 1) {
      if (const auto* block = as<Blk>(args[0])) {
        Items out;
        for (Op op : block->ops())
          out.emplace_back(op);
        return Items{Item(std::move(out))};
      }
    } else if ((name == "args" || name == "outs") && args.size() == 1) {
      if (const auto* op = as<Op>(args[0])) {
        Items out;
        const std::vector<Val> vals = name == "args" ? op->args() : op->outs();
        for (Val value : vals)
          out.emplace_back(value);
        return Items{Item(std::move(out))};
      }
    } else if (name == "callee" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]))
        return Items{Item(Attr(std::string(op->callee())))};
    } else if (name == "type" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return Items{Item(Attr(std::string(value->type().text())))};
    } else if ((name == "has" || name == "meta") && args.size() == 2) {
      const auto* fn = as<Fn>(args[0]);
      const auto key = string(args[1]);
      if (fn && key) {
        const Attr* value = fn->meta(*key);
        if (name == "has")
          return Items{Item(Attr(value != nullptr))};
        return Items{Item(value ? *value : Attr{})};
      }
    } else if (name == "is_const" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return Items{Item(Attr(value->is_const()))};
    } else if (name == "constant" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]); value && value->is_const())
        return Items{Item(value->constant())};
    } else if (name == "len" && args.size() == 1) {
      if (const Items* values = list(args[0]))
        return Items{Item(Attr(static_cast<std::int64_t>(values->size())))};
    } else if (name == "replace" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* old_value = as<Val>(args[1]);
      const auto* new_value = as<Val>(args[2]);
      if (mod && *mod && old_value && new_value)
        return Items{Item(Attr((*mod)->replace(*old_value, *new_value)))};
    } else if (name == "erase" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      if (mod && *mod && op)
        return Items{Item(Attr((*mod)->erase(*op)))};
    } else if (name == "rename" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      const auto value = string(args[2]);
      if (mod && *mod && op && value)
        return Items{Item(Attr((*mod)->rename(*op, std::string(*value))))};
    }
    fail("invalid ir." + std::string(name) + " compile-time call", loc);
    return std::nullopt;
  }

  Env& env_;
  Error error_;
  bool failed_ = false;
};

}  // namespace joggle::detail

namespace joggle {

bool run(Env& env, std::string_view function, Mod& mod) {
  env.clear_diags();
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
  detail::Eval eval(env, [&](std::string message, Loc loc) {
    env.error(std::move(message), std::move(loc));
  });
  if (!eval.run(fn, mod)) {
    mod.impl_->store = std::move(before);
    return false;
  }
  if (!mod.verify(env)) {
    mod.impl_->store = std::move(before);
    env.error("compile-time function produced an invalid module: " +
              std::string(function));
    return false;
  }
  return true;
}

}  // namespace joggle
