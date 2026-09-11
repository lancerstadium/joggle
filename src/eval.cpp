#include "detail.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace joggle::detail {

namespace {

struct Item;
using Items = std::vector<Item>;

struct List {
  Items items;
  Ty element{"_"};
};

struct Item {
  using Data =
      std::variant<std::monostate, std::shared_ptr<Attr>, Ty, Mod*, Fn, Blk,
                   Op, Val, std::shared_ptr<List>>;
  Data data;

  Item() = default;
  Item(Attr value) : data(std::make_shared<Attr>(std::move(value))) {}
  Item(Ty value) : data(std::move(value)) {}
  Item(Mod* value) : data(value) {}
  Item(Fn value) : data(value) {}
  Item(Blk value) : data(value) {}
  Item(Op value) : data(value) {}
  Item(Val value) : data(value) {}
  Item(Items value);
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
  if constexpr (std::is_same_v<T, Attr>) {
    const auto* value = std::get_if<std::shared_ptr<Attr>>(&item.data);
    return value && *value ? value->get() : nullptr;
  } else {
    return std::get_if<T>(&item.data);
  }
}

template <class T> T* as(Item& item) {
  if constexpr (std::is_same_v<T, Attr>) {
    auto* value = std::get_if<std::shared_ptr<Attr>>(&item.data);
    return value && *value ? value->get() : nullptr;
  } else {
    return std::get_if<T>(&item.data);
  }
}

const Items* list(const Item& item) {
  const auto* value = as<std::shared_ptr<List>>(item);
  return value && *value ? &(*value)->items : nullptr;
}

const List* list_data(const Item& item) {
  const auto* value = as<std::shared_ptr<List>>(item);
  return value && *value ? value->get() : nullptr;
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

template <class T> std::optional<std::vector<T>> handles(const Item& item) {
  const Items* items = list(item);
  if (!items)
    return std::nullopt;
  std::vector<T> out;
  out.reserve(items->size());
  for (const Item& entry : *items) {
    const auto* value = as<T>(entry);
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
  if (const List* value = list_data(item)) {
    const std::array<Ty, 1> args{value->element};
    return Ty("list", args);
  }
  return Ty("_");
}

bool accepts_runtime(const Ty& expected, const Item& item) {
  if (expected.text() == "_" || expected.text() == "Attr" ||
      expected.text() == "meta")
    return expected.text() == "_" || as<Attr>(item);
  const Ty actual = runtime_type(item);
  if (expected.name() == "list" && expected.args().size() == 1 &&
      actual.name() == "list" && actual.args().size() == 1) {
    if (actual.args().front().text() == "_")
      return true;
    return expected.args().front() == actual.args().front();
  }
  return expected == actual;
}

Item::Item(Items value) {
  Ty element("_");
  if (!value.empty()) {
    element = runtime_type(value.front());
    for (std::size_t index = 1; index < value.size(); ++index) {
      if (runtime_type(value[index]) != element) {
        element = Ty("_");
        break;
      }
    }
  }
  data = std::make_shared<List>(List{std::move(value), std::move(element)});
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

  static bool sequence(Env& env,
                       std::span<const std::string_view> functions, Mod& mod,
                       Attr* report, std::span<const Attr> args,
                       std::vector<std::chrono::nanoseconds>* elapsed);

  Eval(Env& env, Error error, Attr::List* trace = nullptr)
      : env_(env), error_(std::move(error)), trace_(trace) {}

  std::optional<Items> run(Fn fn, Mod& mod,
                           std::span<const Attr> args = {}) {
    Items values{Item(&mod)};
    values.reserve(args.size() + 1);
    for (const Attr& value : args)
      values.push_back(materialize(value));
    return invoke(fn, std::move(values));
  }

  std::optional<Items> query(Fn fn, Mod& mod,
                             std::span<const Attr> args) {
    Items values{Item(&mod)};
    values.reserve(args.size() + 1);
    for (const Attr& value : args)
      values.push_back(materialize(value));
    return invoke(fn, std::move(values));
  }

private:
  struct ValueKey {
    const detail::Store* store = nullptr;
    std::uint32_t value = 0;

    friend bool operator==(const ValueKey&, const ValueKey&) = default;
  };

  struct ValueHash {
    std::size_t operator()(const ValueKey& key) const noexcept {
      const auto address = reinterpret_cast<std::uintptr_t>(key.store);
      return static_cast<std::size_t>(address ^ (address >> 17) ^ key.value);
    }
  };

  struct Frame {
    explicit Frame(const Frame* parent = nullptr) : parent(parent) {}

    const Frame* parent = nullptr;
    std::vector<std::pair<Val, Item>> values;
    std::unordered_map<ValueKey, std::size_t, ValueHash> index;
  };

  struct Site {
    const detail::Store* store = nullptr;
    std::uint32_t id = 0;

    friend bool operator==(const Site&, const Site&) = default;
  };

  struct SiteHash {
    std::size_t operator()(const Site& site) const noexcept {
      const auto address = reinterpret_cast<std::uintptr_t>(site.store);
      return static_cast<std::size_t>(address ^ (address >> 17) ^ site.id);
    }
  };

  struct Dispatch {
    std::vector<Ty> args;
    std::vector<Ty> explicit_args;
    Fn target;
    std::vector<Ty> generics;
  };

  struct Folded {
    Op op;
    Attr value;
  };

  void fail(std::string message, Loc loc = {}) {
    if (!failed_) {
      failed_ = true;
      error_(std::move(message), std::move(loc));
    }
  }

  const Item* get(const Frame& frame, Val value, Loc loc = {}) {
    for (const Frame* scope = &frame; scope; scope = scope->parent) {
      const auto found = scope->index.find({value.store_, value.id_});
      if (found != scope->index.end())
        return &scope->values[found->second].second;
    }
    fail("compile-time value is not available", std::move(loc));
    return nullptr;
  }

  Item* local(Frame& frame, Val value) {
    const auto found = frame.index.find({value.store_, value.id_});
    if (found != frame.index.end())
      return &frame.values[found->second].second;
    return nullptr;
  }

  Attr* write_attr(Item& item) {
    auto* value = std::get_if<std::shared_ptr<Attr>>(&item.data);
    if (!value || !*value)
      return nullptr;
    if (value->use_count() != 1)
      *value = std::make_shared<Attr>(**value);
    return value->get();
  }

  List* write_list(Item& item) {
    auto* value = std::get_if<std::shared_ptr<List>>(&item.data);
    if (!value || !*value)
      return nullptr;
    if (value->use_count() != 1)
      *value = std::make_shared<List>(**value);
    return value->get();
  }

  const std::vector<Val>& block_args(Blk block) {
    const Site site{block.store_, block.id_};
    const auto found = block_args_.find(site);
    if (found != block_args_.end())
      return found->second;
    return block_args_.emplace(site, block.args()).first->second;
  }

  const std::vector<Op>& block_ops(Blk block) {
    const Site site{block.store_, block.id_};
    const auto found = block_ops_.find(site);
    if (found != block_ops_.end())
      return found->second;
    return block_ops_.emplace(site, block.ops()).first->second;
  }

  const std::vector<Val>& op_args(Op op) {
    const Site site{op.store_, op.id_};
    const auto found = op_args_.find(site);
    if (found != op_args_.end())
      return found->second;
    return op_args_.emplace(site, op.args()).first->second;
  }

  const std::vector<Val>& op_outs(Op op) {
    const Site site{op.store_, op.id_};
    const auto found = op_outs_.find(site);
    if (found != op_outs_.end())
      return found->second;
    return op_outs_.emplace(site, op.outs()).first->second;
  }

  const std::vector<Blk>& op_blks(Op op) {
    const Site site{op.store_, op.id_};
    const auto found = op_blks_.find(site);
    if (found != op_blks_.end())
      return found->second;
    return op_blks_.emplace(site, op.blks()).first->second;
  }

  void put(Frame& frame, Val value, Item item) {
    const ValueKey key{value.store_, value.id_};
    const auto found = frame.index.find(key);
    if (found == frame.index.end()) {
      frame.index.emplace(key, frame.values.size());
      frame.values.emplace_back(value, std::move(item));
    } else {
      frame.values[found->second].second = std::move(item);
    }
  }

  void record(Fn fn, Mod& mod, std::uint64_t before,
              const Items& results) {
    if (!trace_)
      return;
    const std::uint64_t after = mod.revision();
    if (after == before)
      return;
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

  void record_expand(std::string source, Fn implementation,
                     std::uint64_t before, std::uint64_t after) {
    if (!trace_)
      return;
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

  std::optional<Items> values(Frame& frame, const std::vector<Val>& vals,
                              Op consumer, Loc loc = {}) {
    Items out;
    out.reserve(vals.size());
    for (Val value : vals) {
      Item* own = local(frame, value);
      const Item* item = own;
      if (!item && frame.parent)
        item = get(*frame.parent, value, loc);
      if (!item)
        return std::nullopt;
      const auto& users = value.store_->vals[value.id_].data.users;
      if (own && consumer && value.store_ == consumer.store_ &&
          users.size() == 1 && users.front() == consumer.id_)
        out.push_back(std::move(*own));
      else
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

  std::optional<Items> invoke(Fn fn, Items args,
                              Items generic_args = {}) {
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
      put(frame, generics[index], std::move(generic_args[index]));
    for (std::size_t index = 0; index < params.size(); ++index)
      put(frame, params[index], std::move(args[index]));
    Flow flow = blk(fn.body(), {}, frame);
    if (flow.kind == FlowKind::ret) {
      if (observed)
        record(fn, *observed, before, flow.values);
      return std::move(flow.values);
    }
    if (flow.kind != FlowKind::fail)
      fail("compile-time function reached the end without return", fn.loc());
    return std::nullopt;
  }

  Flow blk(Blk blk, Items args, Frame& frame) {
    const std::vector<Val>& params = block_args(blk);
    if (params.size() != args.size()) {
      fail("compile-time Blk argument count is inconsistent");
      return {FlowKind::fail, {}};
    }
    for (std::size_t index = 0; index < params.size(); ++index)
      put(frame, params[index], std::move(args[index]));

    for (Op op : block_ops(blk)) {
      const Loc loc = op.loc();
      if (op.kind() == Op::Kind::constant) {
        const std::vector<Val>& outs = op_outs(op);
        if (outs.size() != 1) {
          fail("constant result count is inconsistent", loc);
          return {FlowKind::fail, {}};
        }
        put(frame, outs.front(), materialize(outs.front().constant()));
        continue;
      }
      if (op.kind() == Op::Kind::call) {
        const std::vector<Val>& operands = op_args(op);
        auto call_args = values(frame, operands, op, loc);
        if (!call_args)
          return {FlowKind::fail, {}};
        const auto result = call(blk.fn(), op, op.callee(),
                                 std::move(*call_args), operands, loc);
        if (!result)
          return {FlowKind::fail, {}};
        const std::vector<Val>& outs = op_outs(op);
        if (result->size() != outs.size()) {
          fail("compile-time call result count is inconsistent", loc);
          return {FlowKind::fail, {}};
        }
        for (std::size_t index = 0; index < outs.size(); ++index)
          put(frame, outs[index], std::move((*result)[index]));
        continue;
      }
      if (op.kind() == Op::Kind::branch) {
        auto branch_args = values(frame, op_args(op), op, loc);
        if (!branch_args)
          return {FlowKind::fail, {}};
        Flow flow = branch(op, frame, std::move(*branch_args));
        if (flow.kind != FlowKind::next)
          return flow;
        continue;
      }
      if (op.kind() == Op::Kind::loop) {
        auto loop_args = values(frame, op_args(op), op, loc);
        if (!loop_args)
          return {FlowKind::fail, {}};
        Flow flow = loop(op, frame, std::move(*loop_args));
        if (flow.kind != FlowKind::next)
          return flow;
        continue;
      }
      if (op.kind() == Op::Kind::ret || op.kind() == Op::Kind::yield) {
        auto result = values(frame, op_args(op), op, loc);
        if (!result)
          return {FlowKind::fail, {}};
        return {op.kind() == Op::Kind::ret ? FlowKind::ret : FlowKind::yield,
                std::move(*result)};
      }
    }
    return {FlowKind::next, {}};
  }

  Flow branch(Op op, Frame& frame, Items args) {
    if (args.empty())
      return {FlowKind::fail, {}};
    const auto condition = boolean(args.front());
    if (!condition) {
      fail("if condition is not a compile-time bool", op.loc());
      return {FlowKind::fail, {}};
    }
    const std::vector<Blk>& blks = op_blks(op);
    const Blk arm = blks[*condition ? 0 : 1];
    Items carried(std::make_move_iterator(args.begin() + 1),
                  std::make_move_iterator(args.end()));
    Frame nested{&frame};
    Flow flow = blk(arm, std::move(carried), nested);
    if (flow.kind == FlowKind::ret || flow.kind == FlowKind::fail)
      return flow;
    if (flow.kind != FlowKind::yield) {
      fail("compile-time if did not yield", op.loc());
      return {FlowKind::fail, {}};
    }
    const std::vector<Val>& outs = op_outs(op);
    if (outs.size() != flow.values.size()) {
      fail("compile-time if result count is inconsistent", op.loc());
      return {FlowKind::fail, {}};
    }
    for (std::size_t index = 0; index < outs.size(); ++index)
      put(frame, outs[index], std::move(flow.values[index]));
    return {};
  }

  Flow loop(Op op, Frame& frame, Items args) {
    const std::vector<Blk>& blks = op_blks(op);
    const std::vector<Val>& outs = op_outs(op);
    const std::size_t iter_count =
        block_args(blks.front()).size() - outs.size();
    if (iter_count > args.size()) {
      fail("compile-time loop source count is inconsistent", op.loc());
      return {FlowKind::fail, {}};
    }
    std::vector<const Items*> sources;
    for (std::size_t index = 0; index < iter_count; ++index) {
      const Items* source = list(args[index]);
      if (!source) {
        fail("compile-time loop source is not iterable", op.loc());
        return {FlowKind::fail, {}};
      }
      sources.push_back(source);
    }
    Items carried(
        std::make_move_iterator(args.begin() +
                                static_cast<std::ptrdiff_t>(iter_count)),
        std::make_move_iterator(args.end()));
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
      blk_args.insert(blk_args.end(),
                      std::make_move_iterator(carried.begin()),
                      std::make_move_iterator(carried.end()));
      carried.clear();
      Frame nested{&frame};
      Flow flow = blk(blks.front(), std::move(blk_args), nested);
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
    if (outs.size() != carried.size()) {
      fail("compile-time loop result count is inconsistent", op.loc());
      return {FlowKind::fail, {}};
    }
    for (std::size_t index = 0; index < outs.size(); ++index)
      put(frame, outs[index], std::move(carried[index]));
    return {};
  }

  std::optional<Items> call(Fn current, Op site, std::string_view name,
                            Items args, std::span<const Val> operands,
                            Loc loc) {
    if (name == "base.copy" && args.size() == 1)
      return std::move(args);
    if (name == "base.list")
      return Items{Item(std::move(args))};
    if (name.starts_with("ir.")) {
      const Ty applied{std::string(name.substr(3))};
      const std::string_view intrinsic_name =
          applied.args().empty() ? name.substr(3) : applied.name();
      const std::vector<Ty> generics = site.generics();
      return intrinsic(intrinsic_name, args, std::move(loc), generics);
    }

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
    Fn target;
    auto& dispatches = dispatch_[Site{site.store_, site.id_}];
    const auto cached = std::find_if(
        dispatches.begin(), dispatches.end(), [&](const Dispatch& dispatch) {
          return dispatch.args == argument_types &&
                 dispatch.explicit_args == explicit_arguments;
        });
    if (cached != dispatches.end()) {
      target = cached->target;
      generic_values = cached->generics;
    } else {
      target = resolve_overload(candidates, argument_types, explicit_arguments,
                                nullptr, &ambiguous, context, &generic_values);
      if (target)
        dispatches.push_back({argument_types, explicit_arguments, target,
                              generic_values});
    }
    if (name.starts_with("operator ") &&
        (!target || target.external() || target.module() == "base"))
      return operation(name.substr(9), std::move(args), std::move(loc));
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
    return invoke(target, std::move(args), std::move(resolved));
  }

  std::optional<Items> operation(std::string_view name, Items args,
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
        List* out = write_list(args[0]);
        out->items[static_cast<std::size_t>(*index)] = std::move(args[2]);
        return Items{std::move(args[0])};
      }
      const auto key = string(args[1]);
      const auto item = attribute(args[2]);
      Attr* value = write_attr(args[0]);
      auto* out = value ? std::get_if<Attr::Dict>(&value->data_) : nullptr;
      if (out && key && item) {
        out->insert_or_assign(std::string(*key), std::move(*item));
        return Items{std::move(args[0])};
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
        if (left_text && right_text) {
          Attr* value = write_attr(args[0]);
          auto* out =
              value ? std::get_if<std::string>(&value->data_) : nullptr;
          if (!out) {
            fail("string operator has invalid storage", loc);
            return std::nullopt;
          }
          out->append(right_text->data(), right_text->size());
          return Items{std::move(args[0])};
        }
        const List* left = list_data(args[0]);
        const List* right = list_data(args[1]);
        if (left && right) {
          List* value = write_list(args[0]);
          value->items.insert(value->items.end(), right->items.begin(),
                              right->items.end());
          if (value->element.text() == "_")
            value->element = right->element;
          else if (right->element.text() != "_" &&
                   value->element != right->element)
            value->element = Ty("_");
          return Items{std::move(args[0])};
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
           name == "text" || name == "hex" || name == "replace" ||
           name == "ident" || name == "ty";
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
    } else if (name == "hex" && args.size() == 2) {
      const Attr* value = as<Attr>(args[0]);
      const Attr::Bytes* bytes = value ? value->bytes() : nullptr;
      const auto separator = string(args[1]);
      if (bytes && separator) {
        static constexpr char digits[] = "0123456789abcdef";
        const std::size_t unit = separator->size() + 2;
        if (unit < separator->size() ||
            (!bytes->empty() &&
             bytes->size() >
                 (std::numeric_limits<std::size_t>::max() - 2) / unit)) {
          fail("hex output is too large", loc);
          return std::nullopt;
        }
        std::string out;
        if (!bytes->empty())
          out.reserve(bytes->size() * unit - separator->size());
        for (std::size_t index = 0; index < bytes->size(); ++index) {
          if (index)
            out.append(separator->data(), separator->size());
          const std::uint8_t byte = (*bytes)[index];
          out.push_back(digits[byte >> 4]);
          out.push_back(digits[byte & 15]);
        }
        return Items{Item(Attr(std::move(out)))};
      }
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
    } else if (name == "ident" && args.size() == 1) {
      if (const auto input = string(args[0])) {
        static constexpr char digits[] = "0123456789abcdef";
        std::string out;
        if (input->size() > std::numeric_limits<std::size_t>::max() / 4) {
          fail("identifier output is too large", loc);
          return std::nullopt;
        }
        out.reserve(input->size() * 4);
        for (std::size_t index = 0; index < input->size(); ++index) {
          const unsigned char byte =
              static_cast<unsigned char>((*input)[index]);
          if ((byte >= 'a' && byte <= 'z') ||
              (byte >= 'A' && byte <= 'Y') ||
              (byte >= '0' && byte <= '9')) {
            out.push_back(static_cast<char>(byte));
          } else if (byte == 'Z') {
            out.append("ZZ");
          } else if (byte == '.') {
            out.append("ZD");
          } else if (byte == '_') {
            const bool reserved = index == 0 ||
                                  (index != 0 && (*input)[index - 1] == '_') ||
                                  (index + 1 < input->size() &&
                                   (*input)[index + 1] == '_');
            out.append(reserved ? "ZU" : "_");
          } else {
            out.append("ZX");
            out.push_back(digits[byte >> 4]);
            out.push_back(digits[byte & 15]);
          }
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
      const auto arguments = types(args[1]);
      if (constructor && arguments) {
        Ty type(std::string(*constructor), *arguments);
        if (type.valid())
          return Items{Item(std::move(type))};
      }
    }
    fail("invalid base." + std::string(name) + " compile-time call", loc);
    return std::nullopt;
  }

  std::optional<Item>
  static_value(Val value,
               std::unordered_set<ValueKey, ValueHash>& visiting) {
    if (!value)
      return std::nullopt;
    if (value.is_const())
      return materialize(value.constant());
    const ValueKey key{value.store_, value.id_};
    if (!visiting.insert(key).second)
      return std::nullopt;
    const Op def = value.def();
    if (!def || def.kind() != Op::Kind::call ||
        def.callee() != "base.list") {
      visiting.erase(key);
      return std::nullopt;
    }
    const std::vector<Val>& outputs = op_outs(def);
    if (outputs.size() != 1 || outputs.front() != value) {
      visiting.erase(key);
      return std::nullopt;
    }
    Items items;
    for (Val input : op_args(def)) {
      auto item = static_value(input, visiting);
      if (!item) {
        visiting.erase(key);
        return std::nullopt;
      }
      items.push_back(std::move(*item));
    }
    visiting.erase(key);
    return Item(std::move(items));
  }

  std::optional<Attr> fold_value(Mod& mod, Op op, Fn fn) {
    if (!op || op.store_ != &mod.impl_->store ||
        op.kind() != Op::Kind::call || !fn || op_outs(op).size() != 1 ||
        !env_.accepts(op, fn))
      return std::nullopt;

    Items args;
    std::unordered_set<ValueKey, ValueHash> visiting;
    for (Val value : op_args(op)) {
      auto item = static_value(value, visiting);
      if (!item)
        return std::nullopt;
      args.push_back(std::move(*item));
    }

    std::optional<Items> result;
    const std::string_view callee = op.callee();
    if (callee == "base.copy" && args.size() == 1) {
      result = std::move(args);
    } else if (callee.starts_with("operator ") && fn.external() &&
               fn.module() == "base") {
      result = operation(callee.substr(9), std::move(args), op.loc());
    } else if (fn.external() && fn.module() == "base" &&
               fundamental(fn.name())) {
      if (fn.name() == "assert")
        return std::nullopt;
      result = fundamental(fn.name(), args, op.loc());
    } else {
      Items generics;
      const std::vector<Val> params = fn.generics();
      const std::vector<Ty> values = env_.match(op, fn);
      if (params.size() != values.size())
        return std::nullopt;
      for (std::size_t index = 0; index < params.size(); ++index) {
        auto item = generic(values[index], params[index].type(), op.loc());
        if (!item)
          return std::nullopt;
        generics.push_back(std::move(*item));
      }
      result = invoke(fn, std::move(args), std::move(generics));
    }
    if (!result || result->size() != 1)
      return std::nullopt;
    auto value = attribute(result->front());
    if (!value || value->list() ||
        !detail::literal_matches(*value, op_outs(op).front().type()))
      return std::nullopt;
    return value;
  }

  bool replace_folded(Mod& mod, std::vector<Folded> folded) {
    if (folded.empty())
      return false;
    detail::Store& store = mod.impl_->store;
    std::unordered_map<std::uint32_t, std::uint32_t> op_replacements;
    std::unordered_map<std::uint32_t, std::uint32_t> value_replacements;
    op_replacements.reserve(folded.size());
    value_replacements.reserve(folded.size());
    store.ops.reserve(store.ops.size() + folded.size());
    store.vals.reserve(store.vals.size() + folded.size());

    for (Folded& item : folded) {
      if (!item.op.valid() || item.op.store_ != &store ||
          item.op.kind() != Op::Kind::call)
        continue;
      const std::uint32_t old_op = item.op.id_;
      const detail::OpData& old = store.ops[old_op].data;
      if (old.outs.size() != 1 ||
          op_replacements.contains(old_op))
        continue;
      const std::uint32_t old_value = old.outs.front();
      if (old_value >= store.vals.size() || !store.vals[old_value].live ||
          !detail::literal_matches(item.value,
                                   store.vals[old_value].data.type))
        continue;

      const std::uint32_t next_op =
          static_cast<std::uint32_t>(store.ops.size());
      const std::uint32_t next_value =
          static_cast<std::uint32_t>(store.vals.size());
      detail::ValData value = store.vals[old_value].data;
      value.kind = detail::ValKind::result;
      value.def = next_op;
      value.index = 0;
      value.users.clear();
      detail::OpData constant;
      constant.kind = Op::Kind::constant;
      constant.blk = old.blk;
      constant.outs.push_back(next_value);
      constant.literal = std::move(item.value);
      constant.meta = old.meta;
      constant.form = old.form;
      constant.loc = old.loc;
      store.vals.push_back({std::move(value), 1, true});
      store.ops.push_back({std::move(constant), 1, true});
      op_replacements.emplace(old_op, next_op);
      value_replacements.emplace(old_value, next_value);
    }
    if (op_replacements.empty())
      return false;

    for (auto& slot : store.ops) {
      if (!slot.live)
        continue;
      for (std::uint32_t& argument : slot.data.args) {
        const auto found = value_replacements.find(argument);
        if (found != value_replacements.end())
          argument = found->second;
      }
    }
    for (auto& slot : store.blks) {
      if (!slot.live)
        continue;
      std::vector<std::uint32_t> order;
      order.reserve(slot.data.ops.size());
      for (const std::uint32_t id : slot.data.ops) {
        const auto found = op_replacements.find(id);
        order.push_back(found == op_replacements.end() ? id : found->second);
      }
      slot.data.ops = std::move(order);
    }
    for (const auto& [old_op, next_op] : op_replacements) {
      (void)next_op;
      const std::uint32_t old_value = store.ops[old_op].data.outs.front();
      store.vals[old_value].live = false;
      ++store.vals[old_value].generation;
      store.ops[old_op].live = false;
      ++store.ops[old_op].generation;
    }
    detail::rebuild_uses(store);
    detail::touch(store);
    return true;
  }

  std::optional<Items> intrinsic(std::string_view name, const Items& args,
                                 Loc loc,
                                 std::span<const Ty> generics = {}) {
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
    } else if (name == "find" && args.size() == 1) {
      if (const auto symbol = string(args[0]))
        return Items{Item(env_.find_fn(*symbol))};
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
    } else if (name == "vals" && args.size() == 1) {
      std::vector<Val> vals;
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod)
        vals = (*mod)->vals();
      else if (const auto* fn = as<Fn>(args[0]))
        vals = fn->vals();
      else {
        fail("invalid ir.vals compile-time call", loc);
        return std::nullopt;
      }
      Items out;
      for (Val value : vals)
        out.emplace_back(value);
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
    } else if (name == "form" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]); op && *op) {
        std::string_view value;
        switch (op->form()) {
        case Op::Form::hidden:
          value = "hidden";
          break;
        case Op::Form::expr:
          value = "expr";
          break;
        case Op::Form::let:
          value = "let";
          break;
        case Op::Form::var:
          value = "var";
          break;
        case Op::Form::assign:
          value = "assign";
          break;
        case Op::Form::compound:
          value = "compound";
          break;
        case Op::Form::index_assign:
          value = "index_assign";
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
    } else if (name == "key" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]); value && *value)
        return Items{Item(Attr(static_cast<std::int64_t>(value->id_)))};
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
        if (generics.size() != 1) {
          fail("ir.invoke requires one explicit result type", loc);
          return std::nullopt;
        }
        const Ty& expected = generics.front();
        const std::vector<Val> params = fn->params();
        const std::vector<Ty> returns = fn->returns();
        if (!fn->generics().empty() || params.size() != 2 ||
            params[0].type() != Ty("Mod") || params[1].type() != Ty("Op") ||
            returns.size() != 1 || returns.front() != expected) {
          fail("ir.invoke callback must be fn(Mod, Op) -> " +
                   std::string(expected.text()),
               loc);
          return std::nullopt;
        }
        auto result = invoke(*fn, {Item(*mod), Item(*op)});
        if (!result)
          return std::nullopt;
        if (result->size() != 1 ||
            !accepts_runtime(expected, result->front())) {
          fail("ir.invoke callback returned a value other than " +
                   std::string(expected.text()),
               loc);
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
    } else if (name == "fold" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      if (!mod || !*mod) {
        fail("ir.fold requires a module", loc);
        return std::nullopt;
      }
      std::vector<Op> ops;
      std::vector<Fn> fns;
      if (const auto* op = as<Op>(args[1])) {
        const auto* fn = as<Fn>(args[2]);
        if (!fn) {
          fail("ir.fold requires a function for its call", loc);
          return std::nullopt;
        }
        ops.push_back(*op);
        fns.push_back(*fn);
      } else {
        auto selected_ops = handles<Op>(args[1]);
        auto selected_fns = handles<Fn>(args[2]);
        if (!selected_ops || !selected_fns ||
            selected_ops->size() != selected_fns->size()) {
          fail("ir.fold requires one function per call", loc);
          return std::nullopt;
        }
        ops = std::move(*selected_ops);
        fns = std::move(*selected_fns);
      }
      std::vector<Folded> folded;
      folded.reserve(ops.size());
      for (std::size_t index = 0; index < ops.size(); ++index) {
        auto value = fold_value(**mod, ops[index], fns[index]);
        if (value)
          folded.push_back({ops[index], std::move(*value)});
        if (failed_)
          return std::nullopt;
      }
      return Items{Item(Attr(replace_folded(**mod, std::move(folded))))};
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
          record_expand(std::move(source), *fn, before, (*mod)->revision());
        return Items{Item(Attr(expanded))};
      }
      auto ops = handles<Op>(args[1]);
      auto fns = handles<Fn>(args[2]);
      if (mod && *mod && ops && fns) {
        std::vector<std::string> sources;
        sources.reserve(ops->size());
        for (Op op : *ops) {
          std::string source(op.callee());
          if (const Fn resolved = env_.resolve(**mod, op))
            source = std::string(resolved.module()) + "." +
                     std::string(resolved.name());
          sources.push_back(std::move(source));
        }
        const std::uint64_t before = (*mod)->revision();
        const bool expanded = env_.expand(**mod, *ops, *fns);
        if (expanded)
          for (std::size_t index = 0; index < ops->size(); ++index)
            record_expand(std::move(sources[index]), (*fns)[index],
                          before + index, before + index + 1);
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
      if (mod && *mod && args.size() == 3) {
        auto old_values = value_handles(args[1]);
        auto new_values = value_handles(args[2]);
        if (old_values && new_values)
          return Items{
              Item(Attr((*mod)->replace(*old_values, *new_values)))};
      }
    } else if (name == "erase" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      if (mod && *mod) {
        if (const auto* op = as<Op>(args[1]))
          return Items{Item(Attr((*mod)->erase(*op)))};
        if (const auto* fn = as<Fn>(args[1]))
          return Items{Item(Attr((*mod)->erase(env_, *fn)))};
        if (auto ops = handles<Op>(args[1]))
          return Items{Item(Attr((*mod)->erase(*ops)))};
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
      auto vals = handles<Val>(args[1]);
      const Items* items = list(args[3]);
      if (mod && *mod && key && vals && items) {
        std::vector<Attr> values;
        values.reserve(items->size());
        for (const Item& item : *items) {
          auto value = attribute(item);
          if (!value) {
            values.clear();
            break;
          }
          values.push_back(std::move(*value));
        }
        if (values.size() == items->size())
          return Items{Item(Attr((*mod)->set(
              *vals, std::string(*key), values)))};
      }
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
  std::unordered_map<Site, std::vector<Dispatch>, SiteHash> dispatch_;
  std::unordered_map<Site, std::vector<Val>, SiteHash> block_args_;
  std::unordered_map<Site, std::vector<Op>, SiteHash> block_ops_;
  std::unordered_map<Site, std::vector<Val>, SiteHash> op_args_;
  std::unordered_map<Site, std::vector<Val>, SiteHash> op_outs_;
  std::unordered_map<Site, std::vector<Blk>, SiteHash> op_blks_;
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

bool detail::Eval::sequence(
    Env& env, std::span<const std::string_view> functions, Mod& mod,
    Attr* report, std::span<const Attr> args,
    std::vector<std::chrono::nanoseconds>* elapsed) {
  env.clear_diags();
  if (report)
    *report = Attr{};
  if (elapsed)
    elapsed->clear();
  detail::Store before = mod.impl_->store;
  const std::uint64_t before_revision = mod.revision();
  const auto rollback = [&]() {
    mod.impl_->store = std::move(before);
    if (elapsed)
      elapsed->clear();
    return false;
  };
  if (!mod.verify(env)) {
    env.error("cannot run compile-time functions on an invalid module");
    return false;
  }

  const auto one = [&](std::string_view function, Attr* step) {
    const std::uint64_t step_revision = mod.revision();
    const Ty applied{std::string(function)};
    const std::string symbol(applied.args().empty() ? function
                                                    : applied.name());
    const std::vector<Ty> explicit_args =
        applied.args().empty() ? std::vector<Ty>{} : applied.args();
    const std::vector<Fn> candidates = env.find_fns(symbol);
    std::vector<Ty> argument_types{Ty("Mod")};
    argument_types.reserve(args.size() + 1);
    for (const Attr& value : args)
      argument_types.push_back(detail::runtime_type(detail::materialize(value)));
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
        env.error("compile-time entry has no matching overload: " +
                  std::string(function));
      }
      return false;
    }
    const std::vector<Val> params = fn.params();
    if (params.size() != args.size() + 1 ||
        params.front().type().text() != "Mod") {
      env.error("compile-time entry must accept Mod followed by its arguments: " +
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
    }, step ? &trace : nullptr);
    const auto result = eval.run(fn, mod, args);
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
    if (!step)
      return true;
    if (!trace.empty())
      trace.pop_back();
    const std::uint64_t after_revision = mod.revision();
    Attr::Dict summary;
    summary["ok"] = Attr(true);
    summary["fn"] = Attr(std::string(function));
    if (!args.empty())
      summary["args"] = Attr(Attr::List(args.begin(), args.end()));
    summary["reported"] = *returned;
    summary["before"] = Attr(static_cast<std::int64_t>(step_revision));
    summary["after"] = Attr(static_cast<std::int64_t>(after_revision));
    summary["edits"] =
        Attr(static_cast<std::int64_t>(after_revision - step_revision));
    summary["changed"] = Attr(after_revision != step_revision);
    summary["steps"] = Attr(std::move(trace));
    *step = Attr(std::move(summary));
    return true;
  };

  Attr::List steps;
  if (report)
    steps.reserve(functions.size());
  if (elapsed)
    elapsed->reserve(functions.size());
  bool reported = false;
  for (const std::string_view function : functions) {
    Attr step;
    std::chrono::steady_clock::time_point started;
    if (elapsed)
      started = std::chrono::steady_clock::now();
    if (!one(function, report ? &step : nullptr))
      return rollback();
    if (elapsed)
      elapsed->push_back(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now() - started));
    if (report) {
      if (const Attr::Dict* values = step.dict()) {
        const auto found = values->find("reported");
        reported = reported ||
                   (found != values->end() && found->second.boolean() == true);
      }
      steps.push_back(std::move(step));
    }
  }
  if (!report)
    return true;
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
  *report = Attr(std::move(summary));
  return true;
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         Attr& report) {
  return detail::Eval::sequence(env, functions, mod, &report, {}, nullptr);
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         Attr& report, std::vector<std::chrono::nanoseconds>& elapsed) {
  return detail::Eval::sequence(env, functions, mod, &report, {}, &elapsed);
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report,
         std::chrono::nanoseconds& elapsed) {
  return run(env, function, mod, report, {}, elapsed);
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report,
         std::span<const Attr> args, std::chrono::nanoseconds& elapsed) {
  elapsed = std::chrono::nanoseconds::zero();
  const std::span<const std::string_view> functions(&function, 1);
  std::vector<std::chrono::nanoseconds> times;
  Attr sequence;
  if (!detail::Eval::sequence(env, functions, mod, &sequence, args, &times))
    return false;
  report = sequence.dict()->at("steps").list()->front();
  elapsed = times.front();
  return true;
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report) {
  return run(env, function, mod, report, {});
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report,
         std::span<const Attr> args) {
  const std::span<const std::string_view> functions(&function, 1);
  Attr sequence;
  if (!detail::Eval::sequence(env, functions, mod, &sequence, args, nullptr))
    return false;
  report = sequence.dict()->at("steps").list()->front();
  return true;
}

bool run(Env& env, std::string_view function, Mod& mod) {
  const std::span<const std::string_view> functions(&function, 1);
  return detail::Eval::sequence(env, functions, mod, nullptr, {}, nullptr);
}

bool run(Env& env, std::string_view function, Mod& mod,
         std::span<const Attr> args) {
  const std::span<const std::string_view> functions(&function, 1);
  return detail::Eval::sequence(env, functions, mod, nullptr, args, nullptr);
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod) {
  return detail::Eval::sequence(env, functions, mod, nullptr, {}, nullptr);
}

}  // namespace joggle
