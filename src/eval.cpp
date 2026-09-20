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

struct List;
struct Item;
using Items = std::vector<Item>;

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

struct List {
  Items items;
  Ty element{"_"};
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

struct SingleResult {
  Item value;
  bool written = false;
};

class CallArgs {
public:
  explicit CallArgs(Items& values) : values_(values), owner_(&values) {}
  explicit CallArgs(std::span<Item> values, Items* owner = nullptr,
                    bool* materialized = nullptr)
      : values_(values), owner_(owner), materialized_(materialized) {}

  bool empty() const noexcept { return values_.empty(); }
  std::size_t size() const noexcept { return values_.size(); }
  Item& front() noexcept { return values_.front(); }
  Item& operator[](std::size_t index) noexcept { return values_[index]; }
  std::span<Item> mutable_view() noexcept { return values_; }
  std::span<const Item> view() const noexcept { return values_; }

  Items take() {
    if (owner_)
      return std::move(*owner_);
    if (materialized_ && !values_.empty())
      *materialized_ = true;
    return Items(std::make_move_iterator(values_.begin()),
                 std::make_move_iterator(values_.end()));
  }

private:
  std::span<Item> values_;
  Items* owner_ = nullptr;
  bool* materialized_ = nullptr;
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

const Attr* meta(const Item& item, std::string_view key) {
  if (const auto* fn = as<Fn>(item))
    return fn->meta(key);
  if (const auto* op = as<Op>(item))
    return op->meta(key);
  if (const auto* val = as<Val>(item))
    return val->meta(key);
  return nullptr;
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

Fn exact_fn(std::span<const Fn> candidates, std::span<const Ty> params) {
  Fn found;
  for (Fn candidate : candidates) {
    std::vector<Ty> declared;
    for (Val param : candidate.params())
      declared.push_back(param.type());
    if (candidate.generics().empty() && declared.size() == params.size() &&
        std::equal(declared.begin(), declared.end(), params.begin())) {
      if (found)
        return {};
      found = candidate;
    }
  }
  return found;
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

std::optional<double> real(const Item& item) {
  const Attr* value = as<Attr>(item);
  if (!value)
    return std::nullopt;
  if (const auto number = value->real())
    return number;
  if (const auto whole = value->integer())
    return static_cast<double>(*whole);
  return std::nullopt;
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

bool runtime_type_is(const Item& item, const Ty& expected,
                     const Ty& declared) {
  if (const auto* value = as<Attr>(item)) {
    if (value->boolean())
      return expected.text() == "bool";
    if (value->integer())
      return expected.text() == "int";
    if (value->real())
      return expected.text() == "f64";
    if (value->string())
      return expected.text() == "str";
    if (value->bytes())
      return expected.text() == "bytes";
    if (value->dict())
      return expected.text() == "dict";
    return expected.text() == "Attr";
  }
  if (as<Mod*>(item))
    return expected.text() == "Mod";
  if (as<Ty>(item))
    return expected.text() == "Ty";
  if (as<Fn>(item))
    return expected.text() == "Fn";
  if (as<Blk>(item))
    return expected.text() == "Blk";
  if (as<Op>(item))
    return expected.text() == "Op";
  if (as<Val>(item))
    return expected.text() == "Val";
  if (const List* value = list_data(item)) {
    if (value->element.text() == "_" && declared.name() == "list")
      return expected == declared;
    return expected.name() == "list" && expected.args().size() == 1 &&
           expected.args().front() == value->element;
  }
  return expected == declared;
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

bool valid_runtime_handles(const Item& item) {
  if (const auto* mod = as<Mod*>(item))
    return *mod != nullptr;
  if (const auto* fn = as<Fn>(item))
    return bool(*fn);
  if (const auto* blk = as<Blk>(item))
    return bool(*blk);
  if (const auto* op = as<Op>(item))
    return bool(*op);
  if (const auto* val = as<Val>(item))
    return bool(*val);
  if (const Items* items = list(item)) {
    for (const Item& value : *items)
      if (!valid_runtime_handles(value))
        return false;
  }
  return true;
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

void hash_combine(std::size_t& seed, std::size_t value) {
  constexpr std::size_t salt =
      static_cast<std::size_t>(0x9e3779b97f4a7c15ULL);
  seed ^= value + salt + (seed << 6) + (seed >> 2);
}

std::size_t hash_attr(const Attr& value) {
  std::size_t seed = value.data().index();
  switch (value.data().index()) {
  case 0:
    break;
  case 1:
    hash_combine(seed, std::hash<bool>{}(std::get<bool>(value.data())));
    break;
  case 2:
    hash_combine(seed,
                 std::hash<std::int64_t>{}(
                     std::get<std::int64_t>(value.data())));
    break;
  case 3:
    hash_combine(seed, std::hash<double>{}(std::get<double>(value.data())));
    break;
  case 4:
    hash_combine(seed,
                 std::hash<std::string>{}(
                     std::get<std::string>(value.data())));
    break;
  case 5:
    for (const std::uint8_t byte : std::get<Attr::Bytes>(value.data()))
      hash_combine(seed, byte);
    break;
  case 6:
    for (const Attr& item : std::get<Attr::List>(value.data()))
      hash_combine(seed, hash_attr(item));
    break;
  case 7:
    for (const auto& [key, item] : std::get<Attr::Dict>(value.data())) {
      hash_combine(seed, std::hash<std::string>{}(key));
      hash_combine(seed, hash_attr(item));
    }
    break;
  default:
    break;
  }
  return seed;
}

std::vector<std::pair<std::uint32_t, std::uint32_t>> collection_members(
    const Store& store, std::uint32_t function,
    QueryCollectionKind kind) {
  std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
  if (function >= store.fns.size() || !store.fns[function].live)
    return out;
  const FnData& fn = store.fns[function].data;
  if (kind == QueryCollectionKind::values) {
    for (const std::uint32_t value : fn.params)
      if (value < store.vals.size() && store.vals[value].live)
        out.emplace_back(value, store.vals[value].generation);
    for (const std::uint32_t block : fn.blks) {
      if (block >= store.blks.size() || !store.blks[block].live)
        continue;
      for (const std::uint32_t value : store.blks[block].data.args)
        if (value < store.vals.size() && store.vals[value].live)
          out.emplace_back(value, store.vals[value].generation);
    }
  }
  const auto visit = [&](const auto& self, std::uint32_t block) -> void {
    if (block >= store.blks.size() || !store.blks[block].live)
      return;
    for (const std::uint32_t operation : store.blks[block].data.ops) {
      if (operation >= store.ops.size() || !store.ops[operation].live)
        continue;
      if (kind == QueryCollectionKind::operations)
        out.emplace_back(operation, store.ops[operation].generation);
      else
        for (const std::uint32_t value : store.ops[operation].data.outs)
          if (value < store.vals.size() && store.vals[value].live)
            out.emplace_back(value, store.vals[value].generation);
      for (const std::uint32_t child : store.ops[operation].data.blks)
        self(self, child);
    }
  };
  if (!fn.blks.empty())
    visit(visit, fn.blks.front());
  return out;
}

std::size_t query_key(std::string_view function,
                      std::span<const Attr> args) {
  std::size_t seed = std::hash<std::string_view>{}(function);
  hash_combine(seed, args.size());
  for (const Attr& arg : args)
    hash_combine(seed, hash_attr(arg));
  return seed;
}

bool same(const Items& left, const Items& right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(),
                    [](const Item& a, const Item& b) { return same(a, b); });
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

// Central effect boundary for the editing overloads declared by
// modules/ir/module.jog. Read-only overloads that share a name are separated
// by arity and, for ir.type, by the runtime subject kind.
bool mutates_ir(std::string_view name, std::span<const Item> args) {
  const std::size_t arity = args.size();
  if (name == "trim" || name == "call" || name == "assign" ||
      name == "loop" || name == "branch" || name == "bind" ||
      name == "clone" || name == "fold" || name == "expand" ||
      name == "expand_partial" || name == "move" || name == "fuse" ||
      name == "replace" || name == "erase" || name == "retarget" ||
      name == "rename" || name == "set" || name == "unset" ||
      name == "use")
    return true;
  return (name == "type" &&
          (arity == 3 ||
           (arity == 1 && as<Mod*>(args.front()) != nullptr))) ||
         (name == "constant" && arity == 4) ||
         ((name == "args" || name == "returns" || name == "generics") &&
          arity == 3);
}

struct MetadataUndo {
  MetadataTarget target;
  std::uint32_t id;
  Attr::Dict meta;
};

class MutationTransaction {
public:
  MutationTransaction(Store& store, RunTiming* timing)
      : store_(store), timing_(timing), revision_(store.revision),
        structure_revision_(store.structure_revision),
        verified_revision_(store.verified_revision),
        verified_env_(store.verified_env),
        verified_epoch_(store.verified_epoch) {
    const auto begin = std::chrono::steady_clock::now();
    diags_ = store.diags;
    if (timing_)
      timing_->snapshot += std::chrono::steady_clock::now() - begin;
    active_transactions.push_back(this);
  }

  ~MutationTransaction() {
    const auto found = std::find(active_transactions.begin(),
                                 active_transactions.end(), this);
    if (found != active_transactions.end())
      active_transactions.erase(found);
  }

  bool owns(const Store& store) const noexcept { return &store == &store_; }

  static MutationTransaction* active(Store& store) {
    for (auto found = active_transactions.rbegin();
         found != active_transactions.rend(); ++found)
      if ((*found)->owns(store))
        return *found;
    return nullptr;
  }

  void journal(MetadataTarget target, std::uint32_t id) {
    if (snapshot_)
      return;
    const std::uint64_t key =
        (static_cast<std::uint64_t>(target) << 32) | id;
    if (!journaled_.insert(key).second)
      return;
    Attr::Dict meta;
    std::uint32_t owner = none;
    switch (target) {
      case MetadataTarget::fn:
        if (id >= store_.fns.size() || !store_.fns[id].live)
          return;
        meta = store_.fns[id].data.meta;
        owner = id;
        break;
      case MetadataTarget::op:
        if (id >= store_.ops.size() || !store_.ops[id].live)
          return;
        meta = store_.ops[id].data.meta;
        if (store_.ops[id].data.blk < store_.blks.size())
          owner = store_.blks[store_.ops[id].data.blk].data.fn;
        break;
      case MetadataTarget::val:
        if (id >= store_.vals.size() || !store_.vals[id].live)
          return;
        meta = store_.vals[id].data.meta;
        owner = store_.vals[id].data.fn;
        break;
    }
    undo_.push_back({target, id, std::move(meta)});
    if (owner < store_.fns.size() && store_.fns[owner].live)
      fn_revisions_.try_emplace(owner, store_.fns[owner].data.revision);
  }

  void prepare_structure() {
    if (snapshot_)
      return;
    const auto begin = std::chrono::steady_clock::now();
    snapshot_.emplace(store_);
    restore_lightweight(*snapshot_);
    if (timing_) {
      timing_->structural_snapshot = true;
      timing_->snapshot += std::chrono::steady_clock::now() - begin;
    }
  }

  void rollback() {
    if (snapshot_)
      store_ = std::move(*snapshot_);
    else
      restore_lightweight(store_);
  }

private:
  void restore_lightweight(Store& target) const {
    for (auto found = undo_.rbegin(); found != undo_.rend(); ++found) {
      switch (found->target) {
        case MetadataTarget::fn:
          target.fns[found->id].data.meta = found->meta;
          break;
        case MetadataTarget::op:
          target.ops[found->id].data.meta = found->meta;
          break;
        case MetadataTarget::val:
          target.vals[found->id].data.meta = found->meta;
          break;
      }
    }
    for (const auto& [id, revision] : fn_revisions_)
      target.fns[id].data.revision = revision;
    target.revision = revision_;
    target.structure_revision = structure_revision_;
    target.verified_revision = verified_revision_;
    target.verified_env = verified_env_;
    target.verified_epoch = verified_epoch_;
    target.diags = diags_;
  }

  Store& store_;
  RunTiming* timing_ = nullptr;
  std::uint64_t revision_ = 0;
  std::uint64_t structure_revision_ = 0;
  std::uint64_t verified_revision_ = 0;
  std::uint64_t verified_env_ = 0;
  std::uint64_t verified_epoch_ = 0;
  std::vector<Diag> diags_;
  std::vector<MetadataUndo> undo_;
  std::unordered_set<std::uint64_t> journaled_;
  std::unordered_map<std::uint32_t, std::uint64_t> fn_revisions_;
  std::optional<Store> snapshot_;

  static thread_local std::vector<MutationTransaction*> active_transactions;
};

thread_local std::vector<MutationTransaction*>
    MutationTransaction::active_transactions;

MutationTransaction* active_transaction(Store& store) {
  return MutationTransaction::active(store);
}

}  // namespace

void journal_metadata(Store& store, MetadataTarget target, std::uint32_t id) {
  if (MutationTransaction* transaction = active_transaction(store))
    transaction->journal(target, id);
}

void prepare_structural_mutation(Store& store) {
  if (MutationTransaction* transaction = active_transaction(store))
    transaction->prepare_structure();
}

class Eval {
  struct SharedCache;

public:
  using Error = std::function<void(std::string, Loc)>;

  static bool sequence(Env& env,
                       std::span<const std::string_view> functions, Mod& mod,
                       Attr* report, std::span<const Attr> args,
                       RunTiming* timing,
                       Fn direct = {},
                       std::vector<StageDependencyData>* dependencies = nullptr);
  static bool read(Env& env, Fn fn, const Mod& mod, Attr& result,
                   std::span<const Attr> args,
                   QueryData* dependencies = nullptr,
                   QueryTiming* timing = nullptr);

  Eval(Env& env, Error error, Attr::List* trace = nullptr,
       const Store* observed = nullptr, bool read_only = false
#if defined(JOGGLE_EVAL_COUNTERS)
       , RunStepTiming* counters = nullptr
#endif
       )
      : env_(env), error_(std::move(error)), trace_(trace),
        observed_(observed), read_only_(read_only)
#if defined(JOGGLE_EVAL_COUNTERS)
        , counters_(counters)
#endif
  {
    initialize_cache();
  }

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

  void dependencies(QueryData& out) const {
    out.whole_revision = observed_whole_;
    out.structure = observed_structure_;
    out.packages = observed_packages_;
    out.package_dependencies = observed_packages_
                                   ? observed_->uses
                                   : std::vector<std::string>{};
    out.intrinsics.assign(observed_intrinsics_.begin(),
                          observed_intrinsics_.end());
    std::sort(out.intrinsics.begin(), out.intrinsics.end());
    out.dependencies.clear();
    out.collections.clear();
    out.operations.clear();
    out.values.clear();
    if (!observed_)
      return;
    std::vector<std::uint32_t> functions(observed_fns_.begin(),
                                         observed_fns_.end());
    std::sort(functions.begin(), functions.end());
    for (const std::uint32_t id : functions) {
      if (id >= observed_->fns.size() || !observed_->fns[id].live)
        continue;
      const auto& slot = observed_->fns[id];
      out.dependencies.push_back(
          {id, slot.generation, slot.data.revision, true});
    }
    std::vector<std::uint32_t> handles(observed_fn_generations_.begin(),
                                       observed_fn_generations_.end());
    std::sort(handles.begin(), handles.end());
    for (const std::uint32_t id : handles) {
      if (observed_fns_.contains(id) || id >= observed_->fns.size() ||
          !observed_->fns[id].live)
        continue;
      const auto& slot = observed_->fns[id];
      out.dependencies.push_back(
          {id, slot.generation, slot.data.revision, false});
    }
    const auto append_collections = [&](const auto& observed,
                                        QueryCollectionKind kind) {
      std::vector<std::uint32_t> ids(observed.begin(), observed.end());
      std::sort(ids.begin(), ids.end());
      for (const std::uint32_t id : ids) {
        if (observed_fns_.contains(id) || id >= observed_->fns.size() ||
            !observed_->fns[id].live)
          continue;
        out.collections.push_back(
            {kind, id, observed_->fns[id].generation,
             collection_members(*observed_, id, kind)});
      }
    };
    append_collections(observed_fn_ops_, QueryCollectionKind::operations);
    append_collections(observed_fn_vals_, QueryCollectionKind::values);
    std::vector<std::uint32_t> operations(observed_ops_.begin(),
                                          observed_ops_.end());
    std::sort(operations.begin(), operations.end());
    for (const std::uint32_t id : operations) {
      if (id >= observed_->ops.size() || !observed_->ops[id].live)
        continue;
      const auto& slot = observed_->ops[id];
      const std::uint32_t block = slot.data.blk;
      if (block < observed_->blks.size() &&
          observed_fns_.contains(observed_->blks[block].data.fn))
        continue;
      out.operations.push_back({id, slot.generation, slot.data});
    }
    std::vector<std::uint32_t> values(observed_vals_.begin(),
                                      observed_vals_.end());
    std::sort(values.begin(), values.end());
    for (const std::uint32_t id : values) {
      if (id >= observed_->vals.size() || !observed_->vals[id].live)
        continue;
      const auto& slot = observed_->vals[id];
      if (observed_fns_.contains(slot.data.fn))
        continue;
      out.values.push_back({id, slot.generation, slot.data});
    }
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
  };

  struct PlanKey {
    const detail::Store* store = nullptr;
    std::uint32_t function = 0;
    std::uint32_t generation = 0;
    std::uint64_t revision = 0;

    friend bool operator==(const PlanKey&, const PlanKey&) = default;
  };

  struct PlanKeyHash {
    std::size_t operator()(const PlanKey& key) const noexcept {
      const auto address = reinterpret_cast<std::uintptr_t>(key.store);
      std::size_t seed =
          static_cast<std::size_t>(address ^ (address >> 17) ^ key.function);
      hash_combine(seed, key.generation);
      hash_combine(seed, key.revision);
      return seed;
    }
  };

  struct Dispatch {
    std::vector<Ty> args;
    std::vector<Ty> explicit_args;
    Fn target;
    std::vector<Ty> generics;
    std::uint64_t environment_epoch = 0;
    std::uint64_t site_revision = 0;
    std::uint64_t target_revision = 0;
  };

  struct CallName {
    std::string symbol;
    std::vector<Ty> explicit_args;
  };

  struct IntrinsicName {
    std::string symbol;
    std::vector<Ty> generics;
  };

  enum class OperatorCode : std::uint8_t {
    none,
    index,
    assign_index,
    range,
    equal,
    not_equal,
    logical_and,
    logical_or,
    less,
    less_equal,
    greater,
    greater_equal,
    add,
    subtract,
    logical_not,
    bit_not,
    multiply,
    divide,
    modulo,
    bit_or,
    bit_xor,
    bit_and,
    shift_left,
    shift_right,
  };

  struct PlanOp {
    Op op;
    std::vector<Val> operands;
    std::vector<Ty> operand_types;
    CallName call;
    IntrinsicName intrinsic;
    OperatorCode direct_operator = OperatorCode::none;
    std::vector<std::size_t> args;
    std::vector<bool> moves;
    std::vector<std::size_t> outs;
    std::vector<std::size_t> blks;
    Attr literal;
    std::vector<Dispatch> dispatches;
  };

  static bool depends_on_generics(const Ty& type,
                                  std::span<const Val> generics) {
    if (std::any_of(generics.begin(), generics.end(), [&](Val generic) {
          return type.name() == generic.name();
        }))
      return true;
    for (const Ty& argument : type.args())
      if (depends_on_generics(argument, generics))
        return true;
    return false;
  }

  static OperatorCode operator_code(std::string_view name) noexcept {
    if (name == "[]") return OperatorCode::index;
    if (name == "[]=") return OperatorCode::assign_index;
    if (name == "..") return OperatorCode::range;
    if (name == "==") return OperatorCode::equal;
    if (name == "!=") return OperatorCode::not_equal;
    if (name == "&&") return OperatorCode::logical_and;
    if (name == "||") return OperatorCode::logical_or;
    if (name == "<") return OperatorCode::less;
    if (name == "<=") return OperatorCode::less_equal;
    if (name == ">") return OperatorCode::greater;
    if (name == ">=") return OperatorCode::greater_equal;
    if (name == "+") return OperatorCode::add;
    if (name == "-") return OperatorCode::subtract;
    if (name == "!") return OperatorCode::logical_not;
    if (name == "~") return OperatorCode::bit_not;
    if (name == "*") return OperatorCode::multiply;
    if (name == "/") return OperatorCode::divide;
    if (name == "%") return OperatorCode::modulo;
    if (name == "|") return OperatorCode::bit_or;
    if (name == "^") return OperatorCode::bit_xor;
    if (name == "&") return OperatorCode::bit_and;
    if (name == "<<") return OperatorCode::shift_left;
    if (name == ">>") return OperatorCode::shift_right;
    return OperatorCode::none;
  }

  static std::string_view operator_name(OperatorCode code) noexcept {
    switch (code) {
      case OperatorCode::index: return "[]";
      case OperatorCode::assign_index: return "[]=";
      case OperatorCode::range: return "..";
      case OperatorCode::equal: return "==";
      case OperatorCode::not_equal: return "!=";
      case OperatorCode::logical_and: return "&&";
      case OperatorCode::logical_or: return "||";
      case OperatorCode::less: return "<";
      case OperatorCode::less_equal: return "<=";
      case OperatorCode::greater: return ">";
      case OperatorCode::greater_equal: return ">=";
      case OperatorCode::add: return "+";
      case OperatorCode::subtract: return "-";
      case OperatorCode::logical_not: return "!";
      case OperatorCode::bit_not: return "~";
      case OperatorCode::multiply: return "*";
      case OperatorCode::divide: return "/";
      case OperatorCode::modulo: return "%";
      case OperatorCode::bit_or: return "|";
      case OperatorCode::bit_xor: return "^";
      case OperatorCode::bit_and: return "&";
      case OperatorCode::shift_left: return "<<";
      case OperatorCode::shift_right: return ">>";
      case OperatorCode::none: return {};
    }
    return {};
  }

  struct PlanBlock {
    std::vector<std::size_t> args;
    std::vector<PlanOp> ops;
  };

  struct Plan {
    std::size_t generic_count = 0;
    std::size_t slot_count = 0;
    std::size_t body = 0;
    std::vector<PlanBlock> blks;
  };

  struct YieldTarget {
    enum class Kind : std::uint8_t { flow, items, slots };

    Kind kind = Kind::flow;
    Items* items = nullptr;
    std::span<const std::size_t> slots;
  };

  struct SharedCache {
    std::unordered_map<PlanKey, std::unique_ptr<Plan>, PlanKeyHash> plans;
    std::vector<std::vector<Item>> register_windows;
  };

  void initialize_cache() {
    std::uint64_t epoch = 0;
    std::shared_ptr<void> stored = env_.evaluator_cache(epoch);
    if (stored && epoch == env_.cache_epoch()) {
      shared_cache_ = std::static_pointer_cast<SharedCache>(std::move(stored));
      return;
    }
    shared_cache_ = std::make_shared<SharedCache>();
    env_.evaluator_cache(shared_cache_, env_.cache_epoch());
#if defined(JOGGLE_EVAL_COUNTERS)
    if (counters_) [[unlikely]]
      ++counters_->plan_cache_resets;
#endif
  }

  struct Site {
    Site(const detail::Store* store, std::uint32_t id)
        : store(store), id(id), revision(store ? store->revision : 0) {}

    const detail::Store* store = nullptr;
    std::uint32_t id = 0;
    std::uint64_t revision = 0;

    friend bool operator==(const Site&, const Site&) = default;
  };

  struct SiteHash {
    std::size_t operator()(const Site& site) const noexcept {
      const auto address = reinterpret_cast<std::uintptr_t>(site.store);
      std::size_t seed =
          static_cast<std::size_t>(address ^ (address >> 17) ^ site.id);
      hash_combine(seed, site.revision);
      return seed;
    }
  };

  struct Version {
    const detail::Store* store = nullptr;
    std::uint64_t revision = 0;

    friend bool operator==(const Version&, const Version&) = default;
  };

  struct Memo {
    Items generics;
    Items args;
    Items result;
    std::vector<Version> dependencies;
  };

  struct MemoProbe {
    std::size_t hash = 0;
    std::vector<Version> versions;
  };

  using MemoEntries = std::unordered_multimap<std::size_t, Memo>;

  void observe(const Item& item) {
    if (const auto* fn = as<Fn>(item)) {
      if (fn->store_ == observed_ && fn->valid())
        observed_fns_.insert(fn->id_);
    } else if (const auto* blk = as<Blk>(item)) {
      if (blk->store_ == observed_ && blk->valid())
        observed_fns_.insert(observed_->blks[blk->id_].data.fn);
    } else if (const auto* op = as<Op>(item)) {
      if (op->store_ == observed_ && op->valid())
        observed_ops_.insert(op->id_);
    } else if (const auto* val = as<Val>(item)) {
      if (val->store_ == observed_ && val->valid())
        observed_vals_.insert(val->id_);
    }
    if (const Items* items = list(item))
      for (const Item& nested : *items)
        observe(nested);
  }

  void observe_intrinsic(std::string_view name,
                         std::span<const Item> args) {
    if (!observed_)
      return;
    observed_intrinsics_.insert(std::string(name));
    if (name == "live" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args.front());
          fn && fn->store_ == observed_ && fn->valid()) {
        observed_fn_generations_.insert(fn->id_);
        return;
      }
    }
    if ((name == "ops" || name == "vals") && args.size() == 1) {
      if (const auto* fn = as<Fn>(args.front());
          fn && fn->store_ == observed_ && fn->valid()) {
        if (name == "ops")
          observed_fn_ops_.insert(fn->id_);
        else
          observed_fn_vals_.insert(fn->id_);
        return;
      }
    }
    if ((name == "constant" || name == "is_const") && args.size() == 1) {
      if (const auto* value = as<Val>(args.front());
          value && value->store_ == observed_ && value->valid()) {
        observed_vals_.insert(value->id_);
        const std::uint32_t definition =
            observed_->vals[value->id_].data.def;
        if (definition != detail::none)
          observed_ops_.insert(definition);
        return;
      }
    }
    for (const Item& arg : args)
      observe(arg);
    if (name == "affected") {
      const auto definitions = [&](const auto& self, const Item& item) -> void {
        if (const auto* value = as<Val>(item)) {
          if (value->store_ == observed_ && value->valid()) {
            const std::uint32_t definition =
                observed_->vals[value->id_].data.def;
            if (definition != detail::none)
              observed_ops_.insert(definition);
          }
        }
        if (const Items* items = list(item))
          for (const Item& nested : *items)
            self(self, nested);
      };
      for (const Item& arg : args)
        definitions(definitions, arg);
    }
    const bool subject_mod = std::any_of(args.begin(), args.end(),
                                         [&](const Item& item) {
      const auto* mod = as<Mod*>(item);
      return mod && *mod && &(*mod)->impl_->store == observed_;
    });
    if (!subject_mod)
      return;
    // Mutation arguments already identify the functions read or written by
    // these calls. Treating the Mod argument of metadata edits and callbacks
    // as a whole-module read would make every reactive stage depend on every
    // unrelated edit.
    if (name == "set" || name == "unset" || name == "invoke" ||
        name == "use")
      return;
    if (name == "uses") {
      observed_packages_ = true;
    } else if (name == "fns" || name == "find" || name == "owned" ||
        name == "resolve" || name == "target" || name == "calls" ||
        name == "path" || name == "at") {
      observed_structure_ = true;
    } else if (name == "ops" || name == "vals") {
      observed_structure_ = true;
      for (std::uint32_t id = 0; id < observed_->fns.size(); ++id)
        if (observed_->fns[id].live)
          observed_fns_.insert(id);
    } else {
      observed_whole_ = true;
    }
  }

  static void memo_version(std::vector<Version>& versions,
                           const detail::Store* store) {
    if (store &&
        std::none_of(versions.begin(), versions.end(),
                     [store](const Version& version) {
                       return version.store == store;
                     }))
      versions.push_back({store, store->revision});
  }

  static bool memo_item(const Item& item, std::size_t& seed,
                        std::vector<Version>& versions) {
    if (const Attr* value = as<Attr>(item)) {
      hash_combine(seed, 1);
      hash_combine(seed, hash_attr(*value));
      return true;
    }
    if (const Ty* value = as<Ty>(item)) {
      hash_combine(seed, 2);
      hash_combine(seed, std::hash<std::string_view>{}(value->text()));
      return true;
    }
    if (const auto* value = as<Mod*>(item)) {
      if (!*value)
        return false;
      const detail::Store* store = &(*value)->impl_->store;
      hash_combine(seed, 3);
      hash_combine(seed, reinterpret_cast<std::uintptr_t>(store));
      hash_combine(seed, store->revision);
      memo_version(versions, store);
      return true;
    }
    const auto handle = [&](std::size_t tag, const detail::Store* store,
                            std::uint32_t id,
                            std::uint32_t generation) -> bool {
      if (!store)
        return false;
      hash_combine(seed, tag);
      hash_combine(seed, reinterpret_cast<std::uintptr_t>(store));
      hash_combine(seed, id);
      hash_combine(seed, generation);
      hash_combine(seed, store->revision);
      memo_version(versions, store);
      return true;
    };
    if (const auto* value = as<Fn>(item))
      return handle(4, value->store_, value->id_, value->generation_);
    if (const auto* value = as<Blk>(item))
      return handle(5, value->store_, value->id_, value->generation_);
    if (const auto* value = as<Op>(item))
      return handle(6, value->store_, value->id_, value->generation_);
    if (const auto* value = as<Val>(item))
      return handle(7, value->store_, value->id_, value->generation_);
    if (const Items* values = list(item)) {
      hash_combine(seed, 8);
      hash_combine(seed, values->size());
      for (const Item& value : *values)
        if (!memo_item(value, seed, versions))
          return false;
      return true;
    }
    return false;
  }

  static std::optional<MemoProbe> memo_probe(const Items& generics,
                                             const Items& args) {
    MemoProbe out;
    hash_combine(out.hash, generics.size());
    for (const Item& item : generics)
      if (!memo_item(item, out.hash, out.versions))
        return std::nullopt;
    hash_combine(out.hash, args.size());
    for (const Item& item : args)
      if (!memo_item(item, out.hash, out.versions))
        return std::nullopt;
    return out;
  }

  static bool memo_current(std::span<const Version> versions) {
    return std::all_of(versions.begin(), versions.end(),
                       [](const Version& version) {
                         return version.store &&
                                version.store->revision == version.revision;
                       });
  }

  void memo_store(const Site& site, std::size_t key, Items generics,
                  Items args, const Items& result,
                  const MemoProbe& input) {
    if (!memo_current(input.versions))
      return;
    const auto output = memo_probe({}, result);
    if (!output)
      return;
    std::vector<Version> dependencies = input.versions;
    for (const Version& version : output->versions)
      memo_version(dependencies, version.store);
    memo_[site].emplace(
        key, Memo{std::move(generics), std::move(args), result,
                  std::move(dependencies)});
  }

  struct Folded {
    Op op;
    Attr value;
  };

  struct FoldedStructure {
    Op op;
    std::vector<Attr> values;
  };

  void fail(std::string message, Loc loc = {}) {
    if (!failed_) {
      failed_ = true;
      error_(std::move(message), std::move(loc));
    }
  }

  const Item* get(const Frame& frame, Val value, const Loc& loc = {}) {
#if !defined(JOGGLE_EVAL_COUNTERS)
    for (const Frame* scope = &frame; scope; scope = scope->parent)
      for (auto found = scope->values.rbegin(); found != scope->values.rend();
           ++found)
        if (found->first.store_ == value.store_ &&
            found->first.id_ == value.id_)
          return &found->second;
    fail("compile-time value is not available", loc);
    return nullptr;
#else
    if (!counters_) {
      for (const Frame* scope = &frame; scope; scope = scope->parent)
        for (auto found = scope->values.rbegin();
             found != scope->values.rend(); ++found)
          if (found->first.store_ == value.store_ &&
              found->first.id_ == value.id_)
            return &found->second;
      fail("compile-time value is not available", loc);
      return nullptr;
    }
    ++counters_->frame_lookups;
    for (const Frame* scope = &frame; scope; scope = scope->parent) {
      for (auto found = scope->values.rbegin(); found != scope->values.rend();
           ++found) {
        ++counters_->frame_probes;
        if (found->first.store_ == value.store_ &&
            found->first.id_ == value.id_)
          return &found->second;
      }
    }
    fail("compile-time value is not available", loc);
    return nullptr;
#endif
  }

  Item* local(Frame& frame, Val value) {
#if !defined(JOGGLE_EVAL_COUNTERS)
    for (auto found = frame.values.rbegin(); found != frame.values.rend();
         ++found)
      if (found->first.store_ == value.store_ && found->first.id_ == value.id_)
        return &found->second;
    return nullptr;
#else
    if (!counters_) {
      for (auto found = frame.values.rbegin(); found != frame.values.rend();
           ++found)
        if (found->first.store_ == value.store_ &&
            found->first.id_ == value.id_)
          return &found->second;
      return nullptr;
    }
    ++counters_->frame_lookups;
    for (auto found = frame.values.rbegin(); found != frame.values.rend();
         ++found) {
      ++counters_->frame_probes;
      if (found->first.store_ == value.store_ && found->first.id_ == value.id_)
        return &found->second;
    }
    return nullptr;
#endif
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
#if defined(JOGGLE_EVAL_COUNTERS)
    const std::size_t old_capacity = frame.values.capacity();
    if (counters_) [[unlikely]]
      ++counters_->frame_writes;
#endif
    // Evaluator values are SSA bindings. Appending preserves the existing
    // newest-binding-wins lookup semantics even for duplicate operands, while
    // avoiding a reverse scan before every write.
    frame.values.emplace_back(value, std::move(item));
#if defined(JOGGLE_EVAL_COUNTERS)
    if (counters_) [[unlikely]] {
      if (frame.values.capacity() != old_capacity)
        ++counters_->frame_growths;
      counters_->frame_peak_capacity =
          std::max(counters_->frame_peak_capacity,
                   static_cast<std::uint64_t>(frame.values.capacity()));
    }
#endif
  }

  Frame acquire_frame(const Frame* parent = nullptr) {
    Frame frame{parent};
    if (!frame_values_.empty()) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]]
        ++counters_->frame_pool_hits;
#endif
      frame.values = std::move(frame_values_.back());
      frame_values_.pop_back();
#if defined(JOGGLE_EVAL_COUNTERS)
    } else if (counters_) [[unlikely]] {
      ++counters_->frame_pool_misses;
#endif
    }
    return frame;
  }

  void release_frame(Frame& frame) {
    frame.values.clear();
    frame_values_.push_back(std::move(frame.values));
  }

  std::unique_ptr<Plan> compile_plan(Fn fn) {
    auto plan = std::make_unique<Plan>();
    const std::vector<Val> generics = fn.generics();
    const std::vector<Val> params = fn.params();
    plan->generic_count = generics.size();

    const std::vector<Blk> blocks = fn.blks();
    std::unordered_map<std::uint32_t, std::size_t> block_indices;
    plan->blks.resize(blocks.size());
    for (std::size_t index = 0; index < blocks.size(); ++index)
      block_indices.emplace(blocks[index].id_, index);
    const auto body = block_indices.find(fn.body().id_);
    if (body == block_indices.end())
      return nullptr;
    plan->body = body->second;

    std::unordered_map<ValueKey, std::size_t, ValueHash> slots;
    std::unordered_map<ValueKey, std::size_t, ValueHash> scopes;
    const auto define = [&](Val value, std::size_t scope) {
      const ValueKey key{value.store_, value.id_};
      const auto found = slots.find(key);
      if (found != slots.end())
        return found->second;
      const std::size_t index = slots.size();
      slots.emplace(key, index);
      scopes.emplace(key, scope);
      return index;
    };
    for (Val value : generics)
      define(value, plan->body);
    for (Val value : params)
      define(value, plan->body);
    for (std::size_t block_index = 0; block_index < blocks.size();
         ++block_index) {
      for (Val value : block_args(blocks[block_index]))
        define(value, block_index);
      for (Op op : block_ops(blocks[block_index]))
        for (Val value : op_outs(op))
          define(value, block_index);
    }

    for (std::size_t block_index = 0; block_index < blocks.size();
         ++block_index) {
      PlanBlock& target = plan->blks[block_index];
      for (Val value : block_args(blocks[block_index]))
        target.args.push_back(define(value, block_index));
      for (Op op : block_ops(blocks[block_index])) {
        PlanOp instruction;
        instruction.op = op;
        instruction.operands = op_args(op);
        instruction.operand_types.reserve(instruction.operands.size());
        for (Val operand : instruction.operands)
          instruction.operand_types.push_back(operand.type());
        if (op.kind() == Op::Kind::call) {
          if (op.callee().starts_with("ir.")) {
            const Ty applied{std::string(op.callee().substr(3))};
            instruction.intrinsic.symbol =
                applied.args().empty()
                    ? std::string(op.callee().substr(3))
                    : std::string(applied.name());
            instruction.intrinsic.generics = op.generics();
          } else if (op.callee() != "base.copy" &&
                     op.callee() != "base.list" && op.callee() != "i64") {
            const Ty applied{std::string(op.callee())};
            instruction.call.symbol =
                applied.args().empty() ? std::string(op.callee())
                                       : std::string(applied.name());
            if (!applied.args().empty())
              instruction.call.explicit_args = applied.args();
          }
        }
        instruction.args.reserve(instruction.operands.size());
        instruction.moves.reserve(instruction.operands.size());
        for (Val value : instruction.operands) {
          const auto found = slots.find(ValueKey{value.store_, value.id_});
          if (found == slots.end())
            return nullptr;
          instruction.args.push_back(found->second);
          const auto& users = value.store_->vals[value.id_].data.users;
          const auto scope = scopes.find(ValueKey{value.store_, value.id_});
          instruction.moves.push_back(
              scope != scopes.end() && scope->second == block_index &&
              users.size() == 1 && users.front() == op.id_);
        }
        if (op.kind() == Op::Kind::call &&
            instruction.call.symbol.starts_with("operator ") &&
            std::none_of(instruction.operand_types.begin(),
                         instruction.operand_types.end(), [&](const Ty& type) {
                           return depends_on_generics(type, generics);
                         }) &&
            std::none_of(instruction.call.explicit_args.begin(),
                         instruction.call.explicit_args.end(),
                         [&](const Ty& type) {
                           return depends_on_generics(type, generics);
                         })) {
          const std::vector<Fn> candidates =
              env_.resolve_fns(fn, instruction.call.symbol);
          bool ambiguous = false;
          const Fn target = resolve_overload(
              candidates, instruction.operand_types,
              instruction.call.explicit_args, nullptr, &ambiguous, generics);
          if (target && !ambiguous && target.external() &&
              target.module() == "base")
            instruction.direct_operator = operator_code(
                std::string_view(instruction.call.symbol)
                    .substr(std::string_view("operator ").size()));
        }
        for (Val value : op_outs(op))
          instruction.outs.push_back(define(value, block_index));
        for (Blk block : op_blks(op)) {
          const auto found = block_indices.find(block.id_);
          if (found == block_indices.end())
            return nullptr;
          instruction.blks.push_back(found->second);
        }
        if (op.kind() == Op::Kind::constant) {
          if (instruction.outs.size() != 1)
            return nullptr;
          instruction.literal = op_outs(op).front().constant();
        }
        target.ops.push_back(std::move(instruction));
      }
    }
    plan->slot_count = slots.size();
    return plan;
  }

  Plan* execution_plan(Fn fn) {
    const PlanKey key{fn.store_, fn.id_, fn.generation_, fn.revision()};
    const auto known_owner = owned_stores_.find(fn.store_);
    const bool persistent = known_owner != owned_stores_.end()
                                ? known_owner->second
                                : owned_stores_.emplace(fn.store_, env_.owns(fn))
                                      .first->second;
    auto& plans = persistent ? shared_cache_->plans : local_plans_;
    const auto found = plans.find(key);
    if (found != plans.end()) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]] {
        ++counters_->plan_hits;
        if (persistent && !compiled_plans_.contains(key))
          ++counters_->plan_persistent_hits;
      }
#endif
      return found->second.get();
    }
    auto plan = compile_plan(fn);
    if (!plan) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]]
        ++counters_->plan_fallbacks;
#endif
      return nullptr;
    }
#if defined(JOGGLE_EVAL_COUNTERS)
    if (counters_) [[unlikely]]
      ++counters_->plan_compiles;
#endif
    Plan* result = plan.get();
    plans.emplace(key, std::move(plan));
    if (persistent)
      compiled_plans_.insert(key);
    return result;
  }

  std::vector<Item> acquire_window(std::size_t slots) {
    std::vector<Item> window;
    if (shared_cache_->register_windows.empty()) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]]
        ++counters_->plan_window_misses;
#endif
    } else {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]]
        ++counters_->plan_window_hits;
#endif
      window = std::move(shared_cache_->register_windows.back());
      shared_cache_->register_windows.pop_back();
    }
    if (window.size() < slots)
      window.resize(slots);
    return window;
  }

  void release_window(std::vector<Item>& window, std::size_t slots) {
    for (std::size_t index = 0; index < slots; ++index)
      window[index] = Item{};
    shared_cache_->register_windows.push_back(std::move(window));
  }

  Item plan_value(std::vector<Item>& window, const PlanOp& instruction,
                  std::size_t index) {
    Item& value = window[instruction.args[index]];
    return instruction.moves[index] ? std::move(value) : value;
  }

  Items plan_values(std::vector<Item>& window, const PlanOp& instruction) {
    Items out;
    out.reserve(instruction.args.size());
    for (std::size_t index = 0; index < instruction.args.size(); ++index)
      out.push_back(plan_value(window, instruction, index));
    return out;
  }

  Flow execute_plan_block(Fn fn, Plan& plan, std::size_t block_index,
                          std::span<Item> args, std::vector<Item>& window,
                          YieldTarget yield_target,
                          bool arguments_bound,
                          RunFunctionTiming* function_counters) {
    PlanBlock& block = plan.blks[block_index];
    if ((!arguments_bound && block.args.size() != args.size()) ||
        (arguments_bound && !args.empty())) {
      fail("compile-time Blk argument count is inconsistent");
      return {FlowKind::fail, {}};
    }
    if (!arguments_bound)
      for (std::size_t index = 0; index < args.size(); ++index)
        window[block.args[index]] = std::move(args[index]);

    for (PlanOp& instruction : block.ops) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]]
        ++counters_->evaluated_ops;
      if (function_counters) [[unlikely]]
        ++function_counters->plan_evaluated_ops;
#endif
      const Op::Kind kind = instruction.op.kind();
      if (kind == Op::Kind::constant) {
        window[instruction.outs.front()] = materialize(instruction.literal);
        continue;
      }
      if (kind == Op::Kind::call) {
        std::array<Item, 2> inline_inputs;
        Items owned_inputs;
        std::span<Item> input_values;
        const bool inline_arguments =
            instruction.args.size() <= inline_inputs.size();
        bool argument_materialized = !inline_arguments;
        if (inline_arguments) {
          for (std::size_t index = 0; index < instruction.args.size();
               ++index)
            inline_inputs[index] = plan_value(window, instruction, index);
          input_values = std::span<Item>(inline_inputs).first(
              instruction.args.size());
        } else {
          owned_inputs = plan_values(window, instruction);
          input_values = owned_inputs;
        }
#if defined(JOGGLE_EVAL_COUNTERS)
        if (counters_) [[unlikely]] {
          ++counters_->plan_call_argument_vectors;
          counters_->plan_call_argument_items += input_values.size();
          if (input_values.empty())
            ++counters_->plan_call_argument_arity_0;
          else if (input_values.size() == 1)
            ++counters_->plan_call_argument_arity_1;
          else if (input_values.size() == 2)
            ++counters_->plan_call_argument_arity_2;
          else
            ++counters_->plan_call_argument_arity_many;
        }
#endif
        SingleResult single;
        SingleResult* sink = instruction.outs.size() == 1 ? &single : nullptr;
        const auto result = instruction.direct_operator == OperatorCode::none
                                ? call(fn, instruction.op,
                                       instruction.op.callee(),
                                       CallArgs(input_values,
                                                inline_arguments
                                                    ? nullptr
                                                    : &owned_inputs,
                                                &argument_materialized),
                                       instruction.operands,
                                       instruction.op.loc(),
                                       &instruction.dispatches,
                                       instruction.operand_types,
                                       &instruction.call,
                                       instruction.intrinsic.symbol.empty()
                                           ? nullptr
                                           : &instruction.intrinsic,
                                       sink)
                                : operation(instruction.direct_operator,
                                            input_values,
                                            instruction.op.loc(), sink);
#if defined(JOGGLE_EVAL_COUNTERS)
        if (instruction.direct_operator != OperatorCode::none && counters_)
            [[unlikely]] {
          ++counters_->plan_call_operators;
          ++counters_->plan_direct_operator_links;
        }
#endif
        if (!result)
          return {FlowKind::fail, {}};
#if defined(JOGGLE_EVAL_COUNTERS)
        if (counters_) [[unlikely]] {
          counters_->plan_call_argument_materializations +=
              argument_materialized;
          ++counters_->plan_call_result_vectors;
          counters_->plan_call_result_items +=
              single.written ? 1 : result->size();
          counters_->plan_call_direct_results += single.written;
        }
#endif
        if (single.written) {
          window[instruction.outs.front()] = std::move(single.value);
          continue;
        }
        if (result->size() != instruction.outs.size()) {
          fail("compile-time call result count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        for (std::size_t index = 0; index < result->size(); ++index)
          window[instruction.outs[index]] = std::move((*result)[index]);
        continue;
      }
      if (kind == Op::Kind::ret || kind == Op::Kind::yield) {
#if defined(JOGGLE_EVAL_COUNTERS)
        if (counters_) [[unlikely]] {
          if (kind == Op::Kind::ret)
            ++counters_->plan_returns;
          else {
            ++counters_->plan_yields;
            if (yield_target.kind != YieldTarget::Kind::flow)
              ++counters_->plan_direct_yields;
          }
        }
#endif
        if (kind == Op::Kind::ret ||
            yield_target.kind == YieldTarget::Kind::flow) {
          Items inputs = plan_values(window, instruction);
          return {kind == Op::Kind::ret ? FlowKind::ret : FlowKind::yield,
                  std::move(inputs)};
        }
        if (yield_target.kind == YieldTarget::Kind::slots &&
            instruction.args.size() != yield_target.slots.size()) {
          fail("compile-time yield result count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        if (yield_target.kind == YieldTarget::Kind::items) {
          if (!yield_target.items) {
            fail("compile-time yield target is inconsistent",
                 instruction.op.loc());
            return {FlowKind::fail, {}};
          }
          yield_target.items->clear();
          yield_target.items->reserve(instruction.args.size());
          for (std::size_t index = 0; index < instruction.args.size(); ++index)
            yield_target.items->push_back(
                plan_value(window, instruction, index));
        } else {
          for (std::size_t index = 0; index < instruction.args.size(); ++index)
            window[yield_target.slots[index]] =
                plan_value(window, instruction, index);
        }
        return {FlowKind::yield, {}};
      }
      if (kind == Op::Kind::branch) {
#if defined(JOGGLE_EVAL_COUNTERS)
        if (counters_) [[unlikely]]
          ++counters_->plan_branches;
#endif
        if (instruction.args.empty())
          return {FlowKind::fail, {}};
        const auto condition = boolean(window[instruction.args.front()]);
        if (!condition) {
          fail("if condition is not a compile-time bool", instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        const std::size_t arm = *condition ? 0 : 1;
        if (instruction.blks.size() <= arm) {
          fail("compile-time if block count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        PlanBlock& target = plan.blks[instruction.blks[arm]];
        if (target.args.size() + 1 != instruction.args.size()) {
          fail("compile-time if argument count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        for (std::size_t index = 0; index < target.args.size(); ++index)
          window[target.args[index]] =
              plan_value(window, instruction, index + 1);
#if defined(JOGGLE_EVAL_COUNTERS)
        if (counters_) [[unlikely]]
          ++counters_->plan_direct_block_entries;
#endif
        Flow nested = execute_plan_block(fn, plan, instruction.blks[arm],
                                         std::span<Item>{}, window,
                                         {YieldTarget::Kind::slots, nullptr,
                                          instruction.outs},
                                         true, function_counters);
        if (nested.kind == FlowKind::ret || nested.kind == FlowKind::fail)
          return nested;
        if (nested.kind != FlowKind::yield) {
          fail("compile-time if result count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        continue;
      }
      if (kind == Op::Kind::loop) {
#if defined(JOGGLE_EVAL_COUNTERS)
        if (counters_) [[unlikely]]
          ++counters_->plan_loops;
#endif
        if (instruction.blks.empty()) {
          fail("compile-time loop block count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        const PlanBlock& body = plan.blks[instruction.blks.front()];
        if (body.args.size() < instruction.outs.size()) {
          fail("compile-time loop source count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        const std::size_t iter_count =
            body.args.size() - instruction.outs.size();
        if (iter_count > instruction.args.size()) {
          fail("compile-time loop source count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        std::vector<const Items*> sources;
        sources.reserve(iter_count);
        for (std::size_t index = 0; index < iter_count; ++index) {
          const Items* source = list(window[instruction.args[index]]);
          if (!source) {
            fail("compile-time loop source is not iterable",
                 instruction.op.loc());
            return {FlowKind::fail, {}};
          }
          sources.push_back(source);
        }
        Items carried;
        carried.reserve(instruction.args.size() - iter_count);
        for (std::size_t index = iter_count; index < instruction.args.size();
             ++index)
          carried.push_back(plan_value(window, instruction, index));
        Items indices;
        indices.reserve(iter_count);
        Flow escaped;
        const auto visit = [&](const auto& self, std::size_t depth) -> void {
          if (escaped.kind != FlowKind::next)
            return;
          if (depth != sources.size()) {
            for (const Item& item : *sources[depth]) {
              indices.push_back(item);
              self(self, depth + 1);
              indices.pop_back();
              if (escaped.kind != FlowKind::next)
                return;
            }
            return;
          }
#if defined(JOGGLE_EVAL_COUNTERS)
          if (counters_) [[unlikely]]
            ++counters_->plan_loop_iterations;
          if (function_counters) [[unlikely]]
            ++function_counters->plan_loop_iterations;
#endif
          for (std::size_t index = 0; index < indices.size(); ++index)
            window[body.args[index]] = indices[index];
          for (std::size_t index = 0; index < carried.size(); ++index)
            window[body.args[iter_count + index]] =
                std::move(carried[index]);
          carried.clear();
#if defined(JOGGLE_EVAL_COUNTERS)
          if (counters_) [[unlikely]]
            ++counters_->plan_direct_block_entries;
#endif
          Flow nested = execute_plan_block(fn, plan,
                                           instruction.blks.front(),
                                           std::span<Item>{}, window,
                                           {YieldTarget::Kind::items,
                                            &carried, {}},
                                           true, function_counters);
          if (nested.kind != FlowKind::yield &&
              nested.kind != FlowKind::next)
            escaped = std::move(nested);
          else if (nested.kind == FlowKind::next) {
            fail("compile-time loop body did not yield", instruction.op.loc());
            escaped = {FlowKind::fail, {}};
          }
        };
        visit(visit, 0);
        if (escaped.kind != FlowKind::next)
          return escaped;
        if (carried.size() != instruction.outs.size()) {
          fail("compile-time loop result count is inconsistent",
               instruction.op.loc());
          return {FlowKind::fail, {}};
        }
        for (std::size_t index = 0; index < carried.size(); ++index)
          window[instruction.outs[index]] = std::move(carried[index]);
        continue;
      }
    }
    return {FlowKind::next, {}};
  }

  Flow execute_plan(Fn fn, Plan& plan, Items generic_args, Items args) {
    std::vector<Item> window = acquire_window(plan.slot_count);
    for (std::size_t index = 0; index < generic_args.size(); ++index)
      window[index] = std::move(generic_args[index]);
    for (std::size_t index = 0; index < args.size(); ++index)
      window[plan.generic_count + index] = std::move(args[index]);
    RunFunctionTiming* function_counters = nullptr;
#if defined(JOGGLE_EVAL_COUNTERS)
    if (counters_) [[unlikely]] {
      const std::string name =
          std::string(fn.module()) + "." + std::string(fn.name());
      function_counters = &counters_->functions[name];
    }
#endif
    Flow flow = execute_plan_block(fn, plan, plan.body,
                                   std::span<Item>{}, window, YieldTarget{},
                                   false, function_counters);
    release_window(window, plan.slot_count);
    return flow;
  }

  Attr calls() const {
    Attr::Dict out;
    for (const auto& [fn, count] : calls_)
      out.emplace(fn, Attr(static_cast<std::int64_t>(count)));
    return Attr(std::move(out));
  }

  Attr cached() const {
    Attr::Dict out;
    for (const auto& [fn, count] : cached_)
      out.emplace(fn, Attr(static_cast<std::int64_t>(count)));
    return Attr(std::move(out));
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

  void record_retarget(std::string source, Fn target,
                       std::uint64_t before, std::uint64_t after) {
    if (!trace_)
      return;
    Attr::List params;
    for (Val param : target.params())
      params.emplace_back(std::string(param.type().text()));
    Attr::List returns;
    for (const Ty& type : target.returns())
      returns.emplace_back(std::string(type.text()));
    Attr::Dict event;
    event["kind"] = Attr("retarget");
    event["source"] = Attr(std::move(source));
    event["target"] =
        Attr(std::string(target.module()) + "." + std::string(target.name()));
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
                              Op consumer, const Loc& loc = {}) {
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

  std::optional<Item> generic(Ty value, Ty type, const Loc& loc) {
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
    if (trace_)
      ++calls_[std::string(fn.module()) + "." + std::string(fn.name())];
#if defined(JOGGLE_EVAL_COUNTERS)
    RunFunctionTiming* function_counters = nullptr;
    if (counters_) [[unlikely]] {
      const std::string name =
          std::string(fn.module()) + "." + std::string(fn.name());
      function_counters = &counters_->functions[name];
      ++function_counters->invocations;
    }
#endif
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
    const Attr* memo = fn.meta("memo");
    const bool memoized = memo && memo->boolean() == true;
    const auto probe = memoized ? memo_probe(generic_args, args)
                                : std::optional<MemoProbe>{};
    const Site site{fn.store_, fn.id_};
    Items memo_generics;
    Items memo_args;
    if (probe) {
      auto function = memo_.find(site);
      if (function != memo_.end()) {
        const auto [begin, end] = function->second.equal_range(probe->hash);
        for (auto entry = begin; entry != end; ++entry) {
          if (same(entry->second.generics, generic_args) &&
              same(entry->second.args, args) &&
              memo_current(entry->second.dependencies)) {
            if (trace_)
              ++cached_[std::string(fn.module()) + "." +
                        std::string(fn.name())];
#if defined(JOGGLE_EVAL_COUNTERS)
            if (function_counters) [[unlikely]]
              ++function_counters->memo_hits;
#endif
            return entry->second.result;
          }
        }
      }
      memo_generics = generic_args;
      memo_args = args;
    }
    if (fn.external()) {
      const std::string symbol = fn.store_->name + "." + std::string(fn.name());
      // f32 and f64 are implemented here rather than bound as natives, like int.
      if (fn.module() == "base" && fn.name() == "real")
        return fundamental(fn.name(), args, fn.loc());
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
      if (probe)
        memo_store(site, probe->hash, std::move(memo_generics),
                   std::move(memo_args), out, *probe);
      return out;
    }

    Mod* observed = nullptr;
    for (const Item& item : args)
      if (const auto* value = as<Mod*>(item); value && *value) {
        observed = *value;
        break;
      }
    const std::uint64_t before = observed ? observed->revision() : 0;
    Flow flow;
    if (Plan* plan = execution_plan(fn)) {
      flow = execute_plan(fn, *plan, std::move(generic_args), std::move(args));
    } else
    {
      Frame frame = acquire_frame();
      for (std::size_t index = 0; index < generics.size(); ++index)
        put(frame, generics[index], std::move(generic_args[index]));
      for (std::size_t index = 0; index < params.size(); ++index)
        put(frame, params[index], std::move(args[index]));
      flow = blk(fn.body(), {}, frame);
      release_frame(frame);
    }
    if (flow.kind == FlowKind::ret) {
      if (observed)
        record(fn, *observed, before, flow.values);
      if (probe)
        memo_store(site, probe->hash, std::move(memo_generics),
                   std::move(memo_args), flow.values, *probe);
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
    const std::vector<Op>& ops = block_ops(blk);
    std::size_t value_capacity = params.size();
    for (const Op op : ops)
      value_capacity += op_outs(op).size();
    frame.values.reserve(frame.values.size() + value_capacity);
    for (std::size_t index = 0; index < params.size(); ++index)
      put(frame, params[index], std::move(args[index]));

    for (Op op : ops) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]]
        ++counters_->evaluated_ops;
#endif
      const Loc& loc = op.loc();
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
                                 CallArgs(*call_args), operands, loc);
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
    Frame nested = acquire_frame(&frame);
    Flow flow = blk(arm, std::move(carried), nested);
    release_frame(nested);
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
    Frame nested = acquire_frame(&frame);
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
      nested.values.clear();
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
    release_frame(nested);
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
                            CallArgs args, std::span<const Val> operands,
                            const Loc& loc,
                            std::vector<Dispatch>* planned = nullptr,
                            std::span<const Ty> declared_types = {},
                            const CallName* planned_name = nullptr,
                            const IntrinsicName* planned_intrinsic = nullptr,
                            SingleResult* single_result = nullptr) {
    if (name == "base.copy" && args.size() == 1) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (planned && counters_) [[unlikely]]
        ++counters_->plan_call_passthroughs;
#endif
      return single(std::move(args.front()), single_result);
    }
    if (name == "base.list") {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (planned && counters_) [[unlikely]]
        ++counters_->plan_call_lists;
#endif
      return single(Item(args.take()), single_result);
    }
    if (name == "i64" && args.size() == 1 && integer(args.front())) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (planned && counters_) [[unlikely]]
        ++counters_->plan_call_passthroughs;
#endif
      return single(std::move(args.front()), single_result);
    }
    if (name.starts_with("ir.")) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (planned && counters_) [[unlikely]]
        ++counters_->plan_call_intrinsics;
#endif
      if (planned_intrinsic)
        return intrinsic(current, planned_intrinsic->symbol, args.view(), loc,
                         planned_intrinsic->generics, single_result);
      const Ty applied{std::string(name.substr(3))};
      const std::string_view intrinsic_name =
          applied.args().empty() ? name.substr(3) : applied.name();
      const std::vector<Ty> generics = site.generics();
      return intrinsic(current, intrinsic_name, args.view(), loc, generics,
                       single_result);
    }

    Ty applied;
    std::vector<Ty> parsed_explicit_arguments;
    std::string_view symbol;
    std::span<const Ty> explicit_arguments;
    if (planned_name) {
      symbol = planned_name->symbol;
      explicit_arguments = planned_name->explicit_args;
    } else {
      applied = Ty{std::string(name)};
      symbol = applied.args().empty() ? name : applied.name();
      if (!applied.args().empty())
        parsed_explicit_arguments = applied.args();
      explicit_arguments = parsed_explicit_arguments;
    }
    auto& dispatches = planned ? *planned
                               : dispatch_[Site{site.store_, site.id_}];
    const std::uint64_t environment_epoch = env_.cache_epoch();
    const std::uint64_t site_revision = site.store_->revision;
    const auto same_explicit_arguments = [&](const Dispatch& dispatch) {
      return dispatch.explicit_args.size() == explicit_arguments.size() &&
             std::equal(dispatch.explicit_args.begin(),
                        dispatch.explicit_args.end(),
                        explicit_arguments.begin());
    };
    const auto current_dispatch = [&](const Dispatch& dispatch) {
      return same_explicit_arguments(dispatch) &&
             dispatch.environment_epoch == environment_epoch &&
             dispatch.site_revision == site_revision && dispatch.target &&
             dispatch.target.revision() == dispatch.target_revision;
    };
    auto cached = dispatches.end();
    const bool fast_signature = planned && declared_types.size() == args.size();
    if (fast_signature) {
      cached = std::find_if(
          dispatches.begin(), dispatches.end(), [&](const Dispatch& dispatch) {
            if (!current_dispatch(dispatch) ||
                dispatch.args.size() != args.size())
              return false;
            for (std::size_t index = 0; index < args.size(); ++index)
              if (!runtime_type_is(args[index], dispatch.args[index],
                                   declared_types[index]))
                return false;
            return true;
          });
    }
    std::vector<Ty> argument_types;
    if (cached == dispatches.end()) {
      argument_types.reserve(args.size());
      for (std::size_t index = 0; index < args.size(); ++index) {
        Ty type = runtime_type(args[index]);
        if (index < operands.size()) {
          const Ty declared = index < declared_types.size()
                                  ? declared_types[index]
                                  : operands[index].type();
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
    }
    bool ambiguous = false;
    std::vector<Ty> resolved_generic_values;
    std::span<const Ty> generic_values;
    Fn target;
    const auto same_signature = [&](const Dispatch& dispatch) {
      return dispatch.args == argument_types &&
             same_explicit_arguments(dispatch);
    };
    if (cached == dispatches.end() &&
        (!fast_signature || !argument_types.empty()))
      cached = std::find_if(dispatches.begin(), dispatches.end(),
                            [&](const Dispatch& dispatch) {
                              return same_signature(dispatch) &&
                                     current_dispatch(dispatch);
                            });
    if (cached != dispatches.end()) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]] {
        ++counters_->dispatch_hits;
        if (planned)
          ++counters_->plan_dispatch_hits;
      }
#endif
      target = cached->target;
      generic_values = cached->generics;
    } else {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (counters_) [[unlikely]] {
        ++counters_->dispatch_misses;
        if (planned)
          ++counters_->plan_dispatch_misses;
      }
#endif
      const std::vector<Fn> candidates = env_.resolve_fns(current, symbol);
      const std::vector<Val> context = current.generics();
      target = resolve_overload(candidates, argument_types, explicit_arguments,
                                nullptr, &ambiguous, context,
                                &resolved_generic_values);
      generic_values = resolved_generic_values;
      if (target) {
        Dispatch resolved{argument_types,
                          std::vector<Ty>(explicit_arguments.begin(),
                                          explicit_arguments.end()), target,
                          resolved_generic_values, environment_epoch,
                          site_revision,
                          target.revision()};
        const auto stale = std::find_if(dispatches.begin(), dispatches.end(),
                                        same_signature);
        if (stale == dispatches.end())
          dispatches.push_back(std::move(resolved));
        else
          *stale = std::move(resolved);
      }
    }
    if (name.starts_with("operator ") &&
        (!target || target.external() || target.module() == "base")) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (planned && counters_) [[unlikely]]
        ++counters_->plan_call_operators;
#endif
      return operation(name.substr(9), args.mutable_view(), loc,
                       single_result);
    }
    if (!target) {
      if (ambiguous) {
        fail("ambiguous compile-time function: " + std::string(name), loc);
        return std::nullopt;
      }
      fail("unknown compile-time function: " + std::string(name), loc);
      return std::nullopt;
    }
    if (target.external() && target.module() == "base" &&
        fundamental(target.name())) {
#if defined(JOGGLE_EVAL_COUNTERS)
      if (planned && counters_) [[unlikely]]
        ++counters_->plan_call_fundamentals;
#endif
      return fundamental(target.name(), args.view(), loc);
    }
#if defined(JOGGLE_EVAL_COUNTERS)
    if (planned && counters_) [[unlikely]]
      ++counters_->plan_call_invocations;
#endif
    if (generic_values.empty())
      return invoke(target, args.take());
    Items resolved;
    const std::vector<Val> generics = target.generics();
    resolved.reserve(generics.size());
    for (std::size_t index = 0; index < generics.size(); ++index) {
      auto value = generic(generic_values[index], generics[index].type(), loc);
      if (!value)
        return std::nullopt;
      resolved.push_back(std::move(*value));
    }
    return invoke(target, args.take(), std::move(resolved));
  }

  static std::optional<Items> single(Item value, SingleResult* result) {
    if (result) {
      result->value = std::move(value);
      result->written = true;
      return Items{};
    }
    return Items{std::move(value)};
  }

  std::optional<Items> operation(std::string_view name,
                                 std::span<Item> args,
                                 const Loc& loc,
                                 SingleResult* single_result = nullptr) {
    return operation(operator_code(name), args, loc, single_result);
  }

  std::optional<Items> operation(OperatorCode code, std::span<Item> args,
                                 const Loc& loc,
                                 SingleResult* single_result = nullptr) {
    if (code == OperatorCode::index && args.size() == 2) {
      if (const Items* items = list(args[0])) {
        const auto index = integer(args[1]);
        if (!index || *index < 0 ||
            static_cast<std::size_t>(*index) >= items->size()) {
          fail("compile-time index is out of bounds", loc);
          return std::nullopt;
        }
        return single((*items)[static_cast<std::size_t>(*index)],
                      single_result);
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
      return single(materialize(found->second), single_result);
    }
    if (code == OperatorCode::assign_index && args.size() == 3) {
      if (const Items* items = list(args[0])) {
        const auto index = integer(args[1]);
        if (!index || *index < 0 ||
            static_cast<std::size_t>(*index) >= items->size()) {
          fail("compile-time index is out of bounds", loc);
          return std::nullopt;
        }
        List* out = write_list(args[0]);
        out->items[static_cast<std::size_t>(*index)] = std::move(args[2]);
        return single(std::move(args[0]), single_result);
      }
      const auto key = string(args[1]);
      const auto item = attribute(args[2]);
      Attr* value = write_attr(args[0]);
      auto* out = value ? std::get_if<Attr::Dict>(&value->data_) : nullptr;
      if (out && key && item) {
        out->insert_or_assign(std::string(*key), std::move(*item));
        return single(std::move(args[0]), single_result);
      }
      fail("compile-time indexed assignment has invalid operands", loc);
      return std::nullopt;
    }
    if (code == OperatorCode::range && args.size() == 2) {
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
      return single(Item(std::move(out)), single_result);
    }
    if ((code == OperatorCode::equal || code == OperatorCode::not_equal) &&
        args.size() == 2) {
      const bool equal = same(args[0], args[1]);
      return single(Item(Attr(code == OperatorCode::equal ? equal : !equal)),
                    single_result);
    }
    if ((code == OperatorCode::logical_and ||
         code == OperatorCode::logical_or) &&
        args.size() == 2) {
      const auto left = boolean(args[0]);
      const auto right = boolean(args[1]);
      if (!left || !right) {
        fail("logical operator requires bool operands", loc);
        return std::nullopt;
      }
      return single(
          Item(Attr(code == OperatorCode::logical_and ? *left && *right
                                                      : *left || *right)),
          single_result);
    }
    if ((code == OperatorCode::less || code == OperatorCode::less_equal ||
         code == OperatorCode::greater ||
         code == OperatorCode::greater_equal) &&
        args.size() == 2) {
      const auto left = integer(args[0]);
      const auto right = integer(args[1]);
      if (!left || !right) {
        fail("comparison requires integer operands", loc);
        return std::nullopt;
      }
      const bool value = code == OperatorCode::less         ? *left < *right
                         : code == OperatorCode::less_equal ? *left <= *right
                         : code == OperatorCode::greater    ? *left > *right
                                                            : *left >= *right;
      return single(Item(Attr(value)), single_result);
    }
    if ((code == OperatorCode::add || code == OperatorCode::subtract ||
         code == OperatorCode::logical_not ||
         code == OperatorCode::bit_not) &&
        args.size() == 1) {
      if (code == OperatorCode::logical_not) {
        const auto value = boolean(args[0]);
        if (value)
          return single(Item(Attr(!*value)), single_result);
      } else {
        const auto value = integer(args[0]);
        if (value && code == OperatorCode::add)
          return single(Item(Attr(*value)), single_result);
        if (value && code == OperatorCode::bit_not)
          return single(Item(Attr(~*value)), single_result);
        if (value && *value != std::numeric_limits<std::int64_t>::min())
          return single(Item(Attr(-*value)), single_result);
      }
      fail("unary operator has invalid operand", loc);
      return std::nullopt;
    }
    if ((code == OperatorCode::add || code == OperatorCode::subtract ||
         code == OperatorCode::multiply || code == OperatorCode::divide ||
         code == OperatorCode::modulo) &&
        args.size() == 2) {
      if (code == OperatorCode::add) {
        const Attr* left_attr = as<Attr>(args[0]);
        const Attr* right_attr = as<Attr>(args[1]);
        const Attr::Bytes* left_bytes =
            left_attr ? left_attr->bytes() : nullptr;
        const Attr::Bytes* right_bytes =
            right_attr ? right_attr->bytes() : nullptr;
        if (left_bytes && right_bytes) {
          Attr* value = write_attr(args[0]);
          auto* out =
              value ? std::get_if<Attr::Bytes>(&value->data_) : nullptr;
          if (!out) {
            fail("bytes operator has invalid storage", loc);
            return std::nullopt;
          }
          out->insert(out->end(), right_bytes->begin(), right_bytes->end());
          return single(std::move(args[0]), single_result);
        }
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
          return single(std::move(args[0]), single_result);
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
          return single(std::move(args[0]), single_result);
        }
      }
      const auto left = integer(args[0]);
      const auto right = integer(args[1]);
      const auto min = std::numeric_limits<std::int64_t>::min();
      if (!left || !right ||
          ((code == OperatorCode::divide || code == OperatorCode::modulo) &&
           *right == 0) ||
          ((code == OperatorCode::divide || code == OperatorCode::modulo) &&
           *left == min && *right == -1)) {
        fail("integer operator has invalid operands", loc);
        return std::nullopt;
      }
      std::int64_t value = 0;
      bool valid = true;
      if (code == OperatorCode::add)
        valid = add(*left, *right, value);
      else if (code == OperatorCode::subtract)
        valid = subtract(*left, *right, value);
      else if (code == OperatorCode::multiply)
        valid = multiply(*left, *right, value);
      else if (code == OperatorCode::divide)
        value = *left / *right;
      else
        value = *left % *right;
      if (!valid) {
        fail("compile-time integer overflow", loc);
        return std::nullopt;
      }
      return single(Item(Attr(value)), single_result);
    }
    if ((code == OperatorCode::bit_or || code == OperatorCode::bit_xor ||
         code == OperatorCode::bit_and || code == OperatorCode::shift_left ||
         code == OperatorCode::shift_right) &&
        args.size() == 2) {
      const auto left = integer(args[0]);
      const auto right = integer(args[1]);
      if (!left || !right ||
          ((code == OperatorCode::shift_left ||
            code == OperatorCode::shift_right) &&
           (*right < 0 || *right >= 64))) {
        fail("bit operator has invalid operands", loc);
        return std::nullopt;
      }
      const std::uint64_t left_bits = std::bit_cast<std::uint64_t>(*left);
      std::uint64_t bits = 0;
      if (code == OperatorCode::bit_or)
        bits = left_bits | std::bit_cast<std::uint64_t>(*right);
      else if (code == OperatorCode::bit_xor)
        bits = left_bits ^ std::bit_cast<std::uint64_t>(*right);
      else if (code == OperatorCode::bit_and)
        bits = left_bits & std::bit_cast<std::uint64_t>(*right);
      else if (code == OperatorCode::shift_left)
        bits = left_bits << static_cast<unsigned>(*right);
      else {
        bits = left_bits >> static_cast<unsigned>(*right);
        if (*left < 0 && *right)
          bits |= ~std::uint64_t{0} << (64 - static_cast<unsigned>(*right));
      }
      return single(Item(Attr(std::bit_cast<std::int64_t>(bits))),
                    single_result);
    }
    fail("unsupported compile-time operator: " +
             std::string(operator_name(code)),
         loc);
    return std::nullopt;
  }

  bool fundamental(std::string_view name) const noexcept {
    return name == "len" || name == "keys" || name == "has" ||
           name == "get" || name == "size" || name == "byte" ||
           name == "kind" || name == "assert" || name == "name" ||
           name == "args" || name == "int" || name == "str" ||
           name == "text" || name == "hex" || name == "replace" ||
           name == "ident" || name == "ty" || name == "real";
  }

  std::optional<Items> fundamental(std::string_view name,
                                   std::span<const Item> args,
                                   const Loc& loc) {
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
    } else if (name == "real" && args.size() == 1) {
      // The evaluator could build an integer constant but not a real one, so a
      // module could not write a floating-point constant; opt.bind needs one to
      // replace a shape-carrying input with a value a caller knows.
      if (const auto value = real(args[0]))
        return Items{Item(Attr(*value))};
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

  bool foldable_operator(Op op) {
    const std::string_view name = op.callee().substr(9);
    const std::vector<Val>& args = op_args(op);
    const auto integer_type = [](Ty type) {
      return detail::integer_type(type.name());
    };
    const auto both = [&](auto predicate) {
      return args.size() == 2 && predicate(args[0].type()) &&
             predicate(args[1].type());
    };
    if (name == "==" || name == "!=")
      return args.size() == 2;
    if (name == ".." || name == "<" || name == "<=" || name == ">" ||
        name == ">=" || name == "-" || name == "*" || name == "/" ||
        name == "%" || name == "|" || name == "^" || name == "&" ||
        name == "<<" || name == ">>")
      return both(integer_type) ||
             ((name == "-" || name == "+" || name == "~") &&
              args.size() == 1 && integer_type(args[0].type()));
    if (name == "+") {
      if (args.size() == 1)
        return integer_type(args[0].type());
      if (both(integer_type))
        return true;
      return args.size() == 2 && args[0].type() == args[1].type() &&
             (args[0].type().name() == "str" ||
              args[0].type().name() == "bytes" ||
              args[0].type().name() == "list");
    }
    if (name == "!" || name == "~")
      return args.size() == 1 &&
             (name == "!" ? args[0].type().name() == "bool"
                          : integer_type(args[0].type()));
    if (name == "&&" || name == "||")
      return both([](Ty type) { return type.name() == "bool"; });
    if (name == "[]")
      return args.size() == 2 &&
             (args[0].type().name() == "list" ||
              args[0].type().name() == "dict" ||
              args[0].type().name() == "Attr");
    if (name == "[]=")
      return args.size() == 3 &&
             (args[0].type().name() == "list" ||
              args[0].type().name() == "dict");
    return false;
  }

  bool foldable_call(Mod& mod, Op op, std::span<const Fn> allowed) {
    if (!op || op.kind() != Op::Kind::call)
      return false;
    const std::string_view name = op.callee();
    if (name == "base.copy" || name == "base.list" || name == "i64")
      return true;
    const Fn target = env_.resolve(mod, op);
    if (name.starts_with("operator "))
      return foldable_operator(op) &&
             (!target || (target.external() && target.module() == "base"));
    if (!target)
      return false;
    if (target.external() && target.module() == "base")
      return target.name() != "assert" && fundamental(target.name());
    return std::find(allowed.begin(), allowed.end(), target) != allowed.end();
  }

  std::optional<Item> static_value(
      Mod& mod, Val value,
      std::unordered_set<ValueKey, ValueHash>& visiting,
      std::span<const Fn> allowed, bool allow_var = false) {
    if (!value)
      return std::nullopt;
    if (value.is_const())
      return materialize(value.constant());
    const ValueKey key{value.store_, value.id_};
    if (!visiting.insert(key).second)
      return std::nullopt;
    const Op def = value.def();
    if (!def || def.kind() != Op::Kind::call ||
        (def.form() != Op::Form::hidden && def.form() != Op::Form::let &&
         !(allow_var && def.form() == Op::Form::var)) ||
        !foldable_call(mod, def, allowed)) {
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
      auto item = static_value(mod, input, visiting, allowed);
      if (!item) {
        visiting.erase(key);
        return std::nullopt;
      }
      items.push_back(std::move(*item));
    }
    auto result = call(def.blk().fn(), def, def.callee(), CallArgs(items),
                       op_args(def), def.loc());
    visiting.erase(key);
    if (!result || result->size() != 1)
      return std::nullopt;
    return std::move(result->front());
  }

  std::optional<Attr> fold_value(Mod& mod, Op op, Fn fn) {
    if (!op || op.store_ != &mod.impl_->store ||
        op.kind() != Op::Kind::call || !fn || op_outs(op).size() != 1 ||
        !env_.accepts(op, fn))
      return std::nullopt;

    Items args;
    std::unordered_set<ValueKey, ValueHash> visiting;
    for (Val value : op_args(op)) {
      const std::span<const Fn> allowed(&fn, 1);
      auto item = static_value(mod, value, visiting, allowed);
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
      result = operation(callee.substr(9), std::span<Item>(args), op.loc());
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

  bool foldable_structure(Mod& mod, Op root,
                          std::span<const Fn> allowed) {
    if (!root || (root.kind() != Op::Kind::loop &&
                  root.kind() != Op::Kind::branch) ||
        !root.meta().empty())
      return false;
    for (Blk blk : op_blks(root)) {
      for (Val arg : block_args(blk))
        if (!arg.meta().empty())
          return false;
      for (Op op : block_ops(blk)) {
        if (!op.meta().empty())
          return false;
        for (Val out : op_outs(op))
          if (!out.meta().empty())
            return false;
        if (op.kind() == Op::Kind::call) {
          if (!foldable_call(mod, op, allowed))
            return false;
        } else if (op.kind() == Op::Kind::loop ||
                   op.kind() == Op::Kind::branch) {
          if (!foldable_structure(mod, op, allowed))
            return false;
        } else if (op.kind() == Op::Kind::ret) {
          return false;
        }
      }
    }
    return true;
  }

  std::optional<std::vector<Attr>> fold_structure(
      Mod& mod, Op op, std::span<const Fn> allowed) {
    if (!op || (op.kind() != Op::Kind::loop &&
                op.kind() != Op::Kind::branch) ||
        !op.meta().empty())
      return std::nullopt;
    Items args;
    std::unordered_set<ValueKey, ValueHash> visiting;
    const std::vector<Val>& inputs = op_args(op);
    const std::vector<Val>& outputs = op_outs(op);
    const auto scalar = [](Ty type) {
      const std::string_view name = type.name();
      return name == "bool" || detail::integer_type(name) || name == "str" ||
             name == "bytes";
    };
    if (outputs.empty() || inputs.size() < outputs.size() ||
        std::any_of(outputs.begin(), outputs.end(),
                    [&](Val value) { return !scalar(value.type()); }))
      return std::nullopt;
    const std::size_t carried = inputs.size() - outputs.size();
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      auto item = static_value(mod, inputs[index], visiting, allowed,
                               index >= carried);
      if (!item)
        return std::nullopt;
      args.push_back(std::move(*item));
    }
    if (!foldable_structure(mod, op, allowed))
      return std::nullopt;
    Frame frame;
    for (std::size_t index = 0; index < inputs.size(); ++index)
      put(frame, inputs[index], args[index]);

    std::unordered_set<ValueKey, ValueHash> locals;
    std::function<void(Op)> collect = [&](Op nested) {
      for (Blk blk : op_blks(nested)) {
        for (Val arg : block_args(blk))
          locals.insert({arg.store_, arg.id_});
        for (Op child : block_ops(blk)) {
          for (Val out : op_outs(child))
            locals.insert({out.store_, out.id_});
          if (child.kind() == Op::Kind::loop ||
              child.kind() == Op::Kind::branch)
            collect(child);
        }
      }
    };
    collect(op);
    bool captures = true;
    std::function<void(Op)> bind = [&](Op nested) {
      for (Blk blk : op_blks(nested)) {
        for (Op child : block_ops(blk)) {
          for (Val value : op_args(child)) {
            const ValueKey key{value.store_, value.id_};
            if (!captures || locals.contains(key) || local(frame, value))
              continue;
            auto item = static_value(mod, value, visiting, allowed);
            if (!item) {
              captures = false;
              return;
            }
            put(frame, value, std::move(*item));
          }
          if (child.kind() == Op::Kind::loop ||
              child.kind() == Op::Kind::branch)
            bind(child);
        }
      }
    };
    bind(op);
    if (!captures)
      return std::nullopt;
    Flow flow = op.kind() == Op::Kind::loop
                    ? loop(op, frame, std::move(args))
                    : branch(op, frame, std::move(args));
    if (flow.kind != FlowKind::next)
      return std::nullopt;
    std::vector<Attr> values;
    values.reserve(outputs.size());
    for (Val output : outputs) {
      const Item* item = get(frame, output, op.loc());
      if (!item)
        return std::nullopt;
      auto value = attribute(*item);
      if (!value || value->list() ||
          !detail::literal_matches(*value, output.type()))
        return std::nullopt;
      values.push_back(std::move(*value));
    }
    return values;
  }

  bool replace_structure_in_place(Mod& mod, FoldedStructure folded) {
    Op op = folded.op;
    const std::vector<Val> old = op.outs();
    const std::vector<Val> args = op.args();
    if (!op || (op.kind() != Op::Kind::loop &&
                op.kind() != Op::Kind::branch) ||
        old.size() != folded.values.size() || args.size() < old.size())
      return false;
    detail::Store& store = mod.impl_->store;
    std::vector<Val> next;
    next.reserve(old.size());
    const std::size_t carried = args.size() - old.size();
    for (std::size_t index = 0; index < old.size(); ++index) {
      const Val prior = old[index];
      const Val seed = args[carried + index];
      const Op def = seed ? seed.def() : Op{};
      const std::vector<Op> users = seed ? seed.users() : std::vector<Op>{};
      if (seed && seed.type() == prior.type() && seed.name() == prior.name() &&
          seed.meta() == prior.meta() && def &&
          def.form() == Op::Form::var && def.blks().empty() &&
          def.outs().size() == 1 && users.size() == 1 && users[0] == op) {
        detail::OpData& data = store.ops[def.id_].data;
        data.kind = Op::Kind::constant;
        data.callee.clear();
        data.args.clear();
        data.blks.clear();
        data.iter_names.clear();
        data.carried_count = 0;
        data.logic = detail::Logic::none;
        data.literal = std::move(folded.values[index]);
        next.push_back(seed);
        continue;
      }
      Val value = mod.constant(op, std::move(folded.values[index]),
                               prior.type());
      if (!value) {
        fail("could not materialize a folded control result", op.loc());
        return false;
      }
      detail::ValData& data = store.vals[value.id_].data;
      const detail::ValData& prior_data = store.vals[prior.id_].data;
      data.name = prior_data.name;
      data.meta = prior_data.meta;
      data.type_annotation = prior_data.type_annotation;
      next.push_back(value);
    }
    if ((!old.empty() && !mod.replace(old, next)) || !mod.erase(op)) {
      const Loc& loc = op.loc();
      fail("could not replace folded control", loc);
      return false;
    }
    return true;
  }

  bool replace_structure(Mod& mod, FoldedStructure folded) {
    detail::Store& store = mod.impl_->store;
    detail::Store backup = store;
    if (replace_structure_in_place(mod, std::move(folded)))
      return true;
    std::vector<Diag> diagnostics = std::move(store.diags);
    store = std::move(backup);
    store.diags = std::move(diagnostics);
    return false;
  }

  std::optional<bool> fold_structures(Mod& mod, std::span<const Op> ops,
                                      std::span<const Fn> allowed) {
    detail::Store& store = mod.impl_->store;
    detail::Store backup = store;
    const auto rollback = [&]() -> std::optional<bool> {
      std::vector<Diag> diagnostics = std::move(store.diags);
      store = std::move(backup);
      store.diags = std::move(diagnostics);
      return std::nullopt;
    };
    bool changed = false;
    for (Op op : ops) {
      if (!op.valid())
        continue;
      auto values = fold_structure(mod, op, allowed);
      if (failed_)
        return rollback();
      if (!values)
        continue;
      if (!replace_structure_in_place(
              mod, FoldedStructure{op, std::move(*values)}))
        return rollback();
      changed = true;
    }
    return changed;
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

  std::optional<Items> intrinsic(Fn current, std::string_view name,
                                 std::span<const Item> args, const Loc& loc,
                                 std::span<const Ty> generics = {},
                                 SingleResult* single_result = nullptr) {
    observe_intrinsic(name, args);
    if (read_only_ && mutates_ir(name, args)) {
      fail("query function attempted IR mutation through ir." +
               std::string(name),
           loc);
      return std::nullopt;
    }
    if (!read_only_ && name != "set" && name != "unset" &&
        mutates_ir(name, args)) {
      if (const auto* mod = as<Mod*>(args.front()); mod && *mod)
        prepare_structural_mutation((*mod)->impl_->store);
    }
    if (name == "fns" && args.size() == 1) {
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod) {
        Items out;
        for (Fn fn : (*mod)->fns())
          out.emplace_back(fn);
        return single(Item(std::move(out)), single_result);
      } else if (const auto module = string(args[0])) {
        Items out;
        if (current && current.module() == *module) {
          for (std::uint32_t id = 0; id < current.store_->fns.size(); ++id) {
            const auto& slot = current.store_->fns[id];
            if (slot.live)
              out.emplace_back(Fn(current.store_, id, slot.generation));
          }
        } else {
          for (Fn fn : env_.fns(*module))
            out.emplace_back(fn);
        }
        return single(Item(std::move(out)), single_result);
      }
    } else if (name == "find" && args.size() == 1) {
      if (const auto symbol = string(args[0]))
        return single(Item(env_.find_fn(*symbol)), single_result);
    } else if (name == "find" && args.size() == 2) {
      if (const auto symbol = string(args[0])) {
        if (const auto params = types(args[1]))
          return single(Item(exact_fn(env_.find_fns(*symbol), *params)),
                        single_result);
      }
      const auto* mod = as<Mod*>(args[0]);
      const auto symbol = string(args[1]);
      if (mod && *mod && symbol)
        return single(Item((*mod)->find_fn(*symbol)), single_result);
    } else if (name == "find" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto symbol = string(args[1]);
      const auto params = types(args[2]);
      if (mod && *mod && symbol && params)
        return single(Item(exact_fn((*mod)->find_fns(*symbol), *params)),
                      single_result);
    } else if (name == "owned" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* fn = as<Fn>(args[1]);
      if (mod && *mod && fn)
        return single(Item(Attr(fn->valid() &&
                                fn->store_ == &(*mod)->impl_->store)),
                      single_result);
    } else if (name == "uses" && args.size() == 1) {
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod) {
        Items out;
        for (const std::string& module : (*mod)->uses())
          out.emplace_back(Attr(module));
        return single(Item(std::move(out)), single_result);
      }
    } else if (name == "trim" && args.size() == 1) {
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod)
        return single(Item(Attr((*mod)->trim(env_))), single_result);
    } else if (name == "revision" && args.size() == 1) {
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod)
        return single(
            Item(Attr(static_cast<std::int64_t>((*mod)->revision()))),
            single_result);
      if (const auto* fn = as<Fn>(args[0]); fn && *fn)
        return single(Item(Attr(static_cast<std::int64_t>(fn->revision()))),
                      single_result);
    } else if (name == "params" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0])) {
        Items out;
        for (Val value : fn->params())
          out.emplace_back(value);
        return single(Item(std::move(out)), single_result);
      }
    } else if (name == "returns" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0])) {
        Items out;
        for (const Ty& type : fn->returns())
          out.emplace_back(type);
        return single(Item(std::move(out)), single_result);
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
      return single(Item(std::move(out)), single_result);
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
      return single(Item(std::move(out)), single_result);
    } else if (name == "blk" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]); op && op->blk())
        return single(Item(op->blk()), single_result);
    } else if (name == "owner" && args.size() == 1) {
      if (const auto* blk = as<Blk>(args[0]); blk && blk->fn())
        return single(Item(blk->fn()), single_result);
    } else if (name == "op" && args.size() == 1) {
      if (const auto* blk = as<Blk>(args[0]))
        return single(Item(blk->op()), single_result);
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
      return single(Item(std::move(out)), single_result);
    } else if (name == "ops" && args.size() == 2) {
      std::vector<Op> ops;
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod)
        ops = (*mod)->ops();
      else if (const auto* fn = as<Fn>(args[0]))
        ops = fn->ops();
      else if (const auto* blk = as<Blk>(args[0]))
        ops = blk->ops();
      else {
        fail("invalid ir.ops compile-time subject", loc);
        return std::nullopt;
      }
      const Items* requested = list(args[1]);
      if (!requested) {
        fail("ir.ops kind filter must be a list of strings", loc);
        return std::nullopt;
      }
      const auto kind_name = [](Op::Kind kind) -> std::string_view {
        switch (kind) {
        case Op::Kind::call:
          return "call";
        case Op::Kind::constant:
          return "constant";
        case Op::Kind::loop:
          return "loop";
        case Op::Kind::branch:
          return "branch";
        case Op::Kind::ret:
          return "return";
        case Op::Kind::yield:
          return "yield";
        }
        return {};
      };
      std::vector<std::string_view> kinds;
      kinds.reserve(requested->size());
      for (const Item& item : *requested) {
        const auto value = string(item);
        if (!value || (*value != "call" && *value != "constant" &&
                       *value != "loop" && *value != "branch" &&
                       *value != "return" && *value != "yield")) {
          fail("ir.ops kind filter contains an invalid operation kind", loc);
          return std::nullopt;
        }
        kinds.push_back(*value);
      }
      Items out;
      out.reserve(ops.size());
      for (Op op : ops)
        if (std::find(kinds.begin(), kinds.end(), kind_name(op.kind())) !=
            kinds.end())
          out.emplace_back(op);
      return single(Item(std::move(out)), single_result);
    } else if (name == "ops" && args.size() == 3) {
      const auto* blk = as<Blk>(args[0]);
      const auto start = integer(args[1]);
      const auto count = integer(args[2]);
      if (blk && *blk && start && count && *start >= 0 && *count >= 0) {
        const std::vector<Op> operations = blk->ops();
        const auto first = static_cast<std::size_t>(*start);
        const auto length = static_cast<std::size_t>(*count);
        if (first <= operations.size() &&
            length <= operations.size() - first) {
          Items out;
          out.reserve(length);
          for (std::size_t index = first; index < first + length; ++index)
            out.emplace_back(operations[index]);
          return single(Item(std::move(out)), single_result);
        }
      }
    } else if (name == "calls" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      auto operations = handles<Op>(args[1]);
      const Items* requested = list(args[2]);
      if (!mod || !*mod || !operations || !requested) {
        fail("ir.calls requires a module, operations, and symbol strings", loc);
        return std::nullopt;
      }
      std::vector<std::string_view> symbols;
      symbols.reserve(requested->size());
      for (const Item& item : *requested) {
        const auto symbol = string(item);
        if (!symbol || symbol->empty()) {
          fail("ir.calls symbol filter must contain nonempty strings", loc);
          return std::nullopt;
        }
        symbols.push_back(*symbol);
      }
      Items out;
      out.reserve(operations->size());
      for (Op op : *operations) {
        if (!op || !op.valid() || op.store_ != &(*mod)->impl_->store) {
          fail("ir.calls operations must be live in its module", loc);
          return std::nullopt;
        }
        if (op.kind() != Op::Kind::call)
          continue;
        const std::string_view callee = op.callee();
        const Fn target = env_.resolve(**mod, op);
        if (target)
          observe(Item(target));
        const std::string resolved =
            target ? std::string(target.module()) + "." +
                         std::string(target.name())
                   : std::string(callee);
        const bool matched = std::any_of(
            symbols.begin(), symbols.end(), [&](std::string_view symbol) {
              if (callee == symbol)
                return true;
              if (resolved != callee)
                return resolved == symbol;
              constexpr std::string_view base = "base.";
              return symbol.starts_with(base) &&
                     callee == symbol.substr(base.size());
            });
        if (matched)
          out.emplace_back(op);
      }
      return single(Item(std::move(out)), single_result);
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
      return single(Item(std::move(out)), single_result);
    } else if (name == "path" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      if (mod && *mod && op) {
        const std::vector<std::size_t> path = (*mod)->path(*op);
        if (!path.empty()) {
          Items out;
          out.reserve(path.size());
          for (const std::size_t index : path)
            out.emplace_back(Attr(static_cast<std::int64_t>(index)));
          return single(Item(std::move(out)), single_result);
        }
      }
      fail("ir.path requires a live operation in its module", loc);
      return std::nullopt;
    } else if (name == "at" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* fn = as<Fn>(args[1]);
      const Items* items = list(args[2]);
      if (mod && *mod && fn && items) {
        std::vector<std::size_t> path;
        path.reserve(items->size());
        for (const Item& item : *items) {
          const auto index = integer(item);
          if (!index || *index < 0) {
            fail("ir.at path must contain nonnegative integers", loc);
            return std::nullopt;
          }
          path.push_back(static_cast<std::size_t>(*index));
        }
        const Op op = (*mod)->at(*fn, path);
        if (op)
          return single(Item(op), single_result);
      }
      fail("ir.at path does not identify a live operation", loc);
      return std::nullopt;
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
        return single(Item(std::move(out)), single_result);
      }
    } else if (name == "def" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return single(Item(value->def()), single_result);
    } else if (name == "users" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0])) {
        Items out;
        for (Op user : value->users())
          out.emplace_back(user);
        return single(Item(std::move(out)), single_result);
      }
    } else if (name == "unused" && args.size() == 2) {
      auto ordered = handles<Op>(args[0]);
      auto removable = handles<Op>(args[1]);
      if (!ordered || !removable) {
        fail("ir.unused requires ordered and removable operation lists", loc);
        return std::nullopt;
      }
      detail::Store* store = nullptr;
      Blk block;
      std::unordered_set<std::uint32_t> ordered_ids;
      ordered_ids.reserve(ordered->size());
      for (Op op : *ordered) {
        if (!op || !op.valid() ||
            (store && op.store_ != store) ||
            (block && op.blk() != block) ||
            !ordered_ids.insert(op.id_).second) {
          fail("ir.unused requires unique live operations from one block", loc);
          return std::nullopt;
        }
        store = op.store_;
        block = op.blk();
      }
      std::unordered_set<std::uint32_t> removable_ids;
      removable_ids.reserve(removable->size());
      for (Op op : *removable) {
        if (!op || !op.valid() || !store || op.store_ != store ||
            !ordered_ids.contains(op.id_) ||
            !removable_ids.insert(op.id_).second) {
          fail("ir.unused removable operations must be a unique subset", loc);
          return std::nullopt;
        }
      }
      std::unordered_set<std::uint32_t> dead;
      dead.reserve(removable_ids.size());
      Items out;
      out.reserve(removable_ids.size());
      for (auto current = ordered->rbegin(); current != ordered->rend();
           ++current) {
        const Op op = *current;
        if (!removable_ids.contains(op.id_))
          continue;
        bool unused = true;
        for (Val value : op.outs())
          for (Op user : value.users())
            if (user && user.valid() && !dead.contains(user.id_))
              unused = false;
        if (unused) {
          dead.insert(op.id_);
          out.emplace_back(op);
        }
      }
      return single(Item(std::move(out)), single_result);
    } else if (name == "affected" && args.size() == 1) {
      if (const Items* values = list(args[0])) {
        const detail::Store* store = nullptr;
        std::vector<std::uint32_t> roots;
        roots.reserve(values->size());
        for (const Item& item : *values) {
          const auto* value = as<Val>(item);
          if (!value || !value->valid() ||
              (store && value->store_ != store)) {
            fail("ir.affected requires live values from one mod", loc);
            return std::nullopt;
          }
          store = value->store_;
          roots.push_back(value->id_);
        }
        Items out;
        if (store)
          for (const std::uint32_t id : detail::affected(*store, roots))
            out.emplace_back(Op(const_cast<detail::Store*>(store), id,
                                store->ops[id].generation));
        return single(Item(std::move(out)), single_result);
      }
    } else if (name == "live" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]))
        return single(Item(Attr(fn->valid())), single_result);
      if (const auto* blk = as<Blk>(args[0]))
        return single(Item(Attr(blk->valid())), single_result);
      if (const auto* op = as<Op>(args[0]))
        return single(Item(Attr(op->valid())), single_result);
      if (const auto* value = as<Val>(args[0]))
        return single(Item(Attr(value->valid())), single_result);
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
        return single(Item(Attr(std::string(value))), single_result);
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
        return single(Item(Attr(std::string(value))), single_result);
      }
    } else if (name == "callee" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]))
        return single(Item(Attr(std::string(op->callee()))), single_result);
    } else if (name == "name" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]); fn && *fn)
        return single(Item(Attr(std::string(fn->name()))), single_result);
      if (const auto* value = as<Val>(args[0]); value && *value)
        return single(Item(Attr(std::string(value->name()))), single_result);
    } else if (name == "bound" && args.size() == 2) {
      const auto* fn = as<Fn>(args[0]);
      const auto candidate = string(args[1]);
      if (fn && *fn && candidate) {
        const bool found = std::any_of(
            fn->store_->vals.begin(), fn->store_->vals.end(),
            [&](const auto& slot) {
              return slot.live && slot.data.fn == fn->id_ &&
                     slot.data.name == *candidate;
            });
        return single(Item(Attr(found)), single_result);
      }
    } else if (name == "local" && args.size() == 1) {
      if (const auto* fn = as<Fn>(args[0]))
        return single(Item(Attr(fn->local())), single_result);
    } else if (name == "key" && args.size() == 1) {
      if (const auto* op = as<Op>(args[0]); op && *op)
        return single(Item(Attr(static_cast<std::int64_t>(op->id_))),
                      single_result);
      if (const auto* value = as<Val>(args[0]); value && *value)
        return single(Item(Attr(static_cast<std::int64_t>(value->id_))),
                      single_result);
    } else if (name == "type" && args.size() == 1) {
      if (const auto* value = as<Val>(args[0]))
        return single(Item(value->type()), single_result);
      if (const auto* mod = as<Mod*>(args[0]); mod && *mod) {
        const std::uint64_t before = (*mod)->revision();
        if (!(*mod)->verify(env_)) {
          fail("ir.type requires a valid module", loc);
          return std::nullopt;
        }
        return Items{Item(Attr((*mod)->revision() != before))};
      }
    } else if (name == "type" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      auto values = handles<Val>(args[1]);
      auto result_types = types(args[2]);
      if (mod && *mod && values && result_types)
        return Items{
            Item(Attr((*mod)->type(*values, *result_types)))};
      const auto* value = as<Val>(args[1]);
      const auto* type = as<Ty>(args[2]);
      if (mod && *mod && value && type)
        return Items{Item(Attr((*mod)->type(*value, *type)))};
    } else if (name == "resolve" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      if (mod && *mod && op) {
        const Fn target = env_.resolve(**mod, *op);
        observe(Item(target));
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
          if (!as<Fn>(item) && !as<Op>(item) && !as<Val>(item)) {
            fail("ir.where items must be functions, operations, or values",
                 loc);
            return std::nullopt;
          }
          const Attr* actual = meta(item, *key);
          bool selected = actual && *actual == *expected;
          if (actual && actual->list()) {
            for (const Attr& value : *actual->list())
              selected = value == *expected || selected;
          }
          if (selected)
            out.push_back(item);
        }
        return Items{Item(std::move(out))};
      }
    } else if (name == "invoke" && args.size() >= 2 && args.size() <= 5) {
      const auto* mod = as<Mod*>(args[0]);
      const std::size_t fn_index = args.size() == 2 ? 1 : 2;
      const auto* fn = as<Fn>(args[fn_index]);
      if (mod && *mod && fn && *fn) {
        if (generics.size() != 1) {
          fail("ir.invoke requires one explicit result type", loc);
          return std::nullopt;
        }
        const Ty& expected = generics.front();
        const std::vector<Val> params = fn->params();
        const std::vector<Ty> returns = fn->returns();
        const std::size_t callback_arity = args.size() - 1;
        if (!fn->generics().empty() ||
            params.size() != callback_arity ||
            params[0].type() != Ty("Mod") ||
            returns.size() != 1 || returns.front() != expected) {
          fail("ir.invoke callback must match the supplied arguments and return " +
                   std::string(expected.text()),
               loc);
          return std::nullopt;
        }
        Items callback_args{Item(*mod)};
        for (std::size_t i = 1; i < args.size(); ++i) {
          if (i == fn_index)
            continue;
          const std::size_t param_index = callback_args.size();
          if (!accepts_runtime(params[param_index].type(), args[i]) ||
              !valid_runtime_handles(args[i])) {
            fail("ir.invoke argument does not match callback parameter " +
                     std::to_string(param_index),
                 loc);
            return std::nullopt;
          }
          callback_args.push_back(args[i]);
        }
        const std::size_t diagnostics_before = (*mod)->diags().size();
        auto result = invoke(*fn, std::move(callback_args));
        if (!result)
          return std::nullopt;
        if ((*mod)->diags().size() != diagnostics_before) {
          fail("ir.invoke callback attempted a rejected IR edit", loc);
          return std::nullopt;
        }
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
    } else if (name == "target" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      if (mod && *mod && op && *op) {
        std::string symbol(op->callee());
        if (const Fn target = env_.resolve(**mod, *op)) {
          observe(Item(target));
          symbol = std::string(target.module()) + "." +
                   std::string(target.name());
        }
        return Items{Item(Attr(std::move(symbol)))};
      }
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
      const auto* target = as<Fn>(args[2]);
      const Items* values = list(args[3]);
      if (mod && *mod && before && (callee || target) && values) {
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
          Val result = target
                           ? (*mod)->call(env_, *before, *target, inputs, *type)
                           : (*mod)->call(*before, std::string(*callee),
                                          inputs, *type);
          if (result)
            return Items{Item(result)};
        } else if (auto result_types = types(args[4])) {
          Op result = target
                          ? (*mod)->call(env_, *before, *target, inputs,
                                         *result_types)
                          : (*mod)->call(*before, std::string(*callee), inputs,
                                         *result_types);
          if (result)
            return Items{Item(result)};
        }
      }
    } else if (name == "assign" && args.size() == 4) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* before = as<Op>(args[1]);
      const auto* target = as<Val>(args[2]);
      const auto* value = as<Val>(args[3]);
      if (mod && *mod && before && target && value) {
        Val result = (*mod)->assign(*before, *target, *value);
        if (result)
          return Items{Item(result)};
      }
    } else if (name == "loop" && args.size() == 5) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* before = as<Op>(args[1]);
      auto names = strings(args[2]);
      auto sources = value_handles(args[3]);
      auto carried = value_handles(args[4]);
      if (mod && *mod && before && names && sources && carried) {
        Op result = (*mod)->loop(*before, *names, *sources, *carried);
        if (result) {
          fresh_names_.clear();
          return Items{Item(result)};
        }
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
    } else if (name == "bind" && args.size() == 5) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      const auto* fn = as<Fn>(args[2]);
      const auto target = string(args[3]);
      const Items* selected = list(args[4]);
      if (mod && *mod && op && fn && target && selected) {
        std::vector<std::size_t> params;
        params.reserve(selected->size());
        bool valid = true;
        for (const Item& item : *selected) {
          const auto index = integer(item);
          if (!index || *index < 0) {
            valid = false;
            break;
          }
          params.push_back(static_cast<std::size_t>(*index));
        }
        if (valid) {
          const std::uint64_t before = (*mod)->revision();
          const std::vector<Ty> generics = env_.match(*op, *fn);
          Fn result = (*mod)->bind(env_, *op, *fn, std::string(*target),
                                   params);
          if (result) {
            record_clone(**mod, *fn, result, before, generics);
            return Items{Item(result)};
          }
        }
      }
    } else if (name == "clone" &&
               (args.size() == 3 || args.size() == 4 || args.size() == 5)) {
      const auto* mod = as<Mod*>(args[0]);
      if (mod && *mod) {
        if (args.size() == 5) {
          const auto* op = as<Op>(args[1]);
          const auto* before = as<Op>(args[2]);
          auto old_values = value_handles(args[3]);
          auto new_values = value_handles(args[4]);
          if (op && before && old_values && new_values) {
            Op result =
                (*mod)->clone(*op, *before, *old_values, *new_values);
            if (result)
              fresh_names_.clear();
            return Items{Item(result)};
          }
          auto ops = handles<Op>(args[1]);
          if (ops && before && old_values && new_values) {
            const std::vector<Op> copies =
                (*mod)->clone(*ops, *before, *old_values, *new_values);
            if (!copies.empty())
              fresh_names_.clear();
            Items result;
            result.reserve(copies.size());
            for (Op copy : copies)
              result.emplace_back(copy);
            return Items{Item(std::move(result))};
          }
        }
        if (args.size() == 3) {
          if (const auto* op = as<Op>(args[1])) {
            if (const auto* before = as<Op>(args[2])) {
              Op result = (*mod)->clone(*op, *before);
              if (result)
                fresh_names_.clear();
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
              fresh_names_.clear();
              record_clone(**mod, *fn, result, before, generics);
              return Items{Item(result)};
            }
          }
        }
      }
    } else if (name == "fold" && args.size() == 2) {
      const auto* mod = as<Mod*>(args[0]);
      auto allowed = handles<Fn>(args[1]);
      if (!mod || !*mod || !allowed) {
        fail("ir.fold requires a module and an allowed function list", loc);
        return std::nullopt;
      }
      std::vector<Folded> folded;
      for (Op op : (*mod)->ops()) {
        if (!op || op.kind() != Op::Kind::call)
          continue;
        const Fn target = env_.resolve(**mod, op);
        if (!target ||
            std::find(allowed->begin(), allowed->end(), target) ==
                allowed->end())
          continue;
        auto value = fold_value(**mod, op, target);
        if (value)
          folded.push_back({op, std::move(*value)});
        if (failed_)
          return std::nullopt;
      }
      return Items{Item(Attr(replace_folded(**mod, std::move(folded))))};
    } else if (name == "fold" && args.size() == 3) {
      const auto* mod = as<Mod*>(args[0]);
      if (!mod || !*mod) {
        fail("ir.fold requires a module", loc);
        return std::nullopt;
      }
      std::vector<Op> ops;
      std::vector<Fn> fns;
      if (const auto* op = as<Op>(args[1])) {
        if (const auto* fn = as<Fn>(args[2])) {
          ops.push_back(*op);
          fns.push_back(*fn);
        } else if (auto allowed = handles<Fn>(args[2])) {
          auto values = fold_structure(**mod, *op, *allowed);
          if (failed_)
            return std::nullopt;
          if (!values)
            return Items{Item(Attr(false))};
          return Items{Item(Attr(replace_structure(
              **mod, FoldedStructure{*op, std::move(*values)})))};
        } else {
          fail("ir.fold requires a function for a call or a function list "
               "for structured control",
               loc);
          return std::nullopt;
        }
      } else {
        auto selected_ops = handles<Op>(args[1]);
        auto selected_fns = handles<Fn>(args[2]);
        if (!selected_ops || !selected_fns) {
          fail("ir.fold requires operation and function lists", loc);
          return std::nullopt;
        }
        const bool calls = std::all_of(
            selected_ops->begin(), selected_ops->end(),
            [](Op item) { return item && item.kind() == Op::Kind::call; });
        const bool structures = !selected_ops->empty() && std::all_of(
            selected_ops->begin(), selected_ops->end(), [](Op item) {
              return item && (item.kind() == Op::Kind::loop ||
                              item.kind() == Op::Kind::branch);
            });
        if (structures) {
          auto changed = fold_structures(**mod, *selected_ops, *selected_fns);
          if (!changed)
            return std::nullopt;
          return Items{Item(Attr(*changed))};
        }
        if (!calls || selected_ops->size() != selected_fns->size()) {
          fail("ir.fold requires one function per call or a homogeneous "
               "control list with an allowed function set",
               loc);
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
    } else if (name == "expand_partial" && args.size() == 4) {
      // The best-effort variant of expand, for a caller that will retry the calls
      // it could not expand. See Env::expand for why it exists.
      const auto* mod = as<Mod*>(args[0]);
      auto ops = handles<Op>(args[1]);
      auto fns = handles<Fn>(args[2]);
      const Attr* flag = as<Attr>(args[3]);
      const bool best_effort = flag && flag->integer().value_or(0) != 0;
      if (mod && *mod && ops && fns && ops->size() == fns->size()) {
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
        const bool expanded = env_.expand(**mod, *ops, *fns, best_effort);
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
      if (mod && *mod && args.size() == 3) {
        const auto* op = as<Op>(args[1]);
        auto value = attribute(args[2]);
        if (op && value)
          return Items{Item(Attr((*mod)->replace(*op, std::move(*value))))};
      }
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
    } else if (name == "retarget" &&
               (args.size() == 3 || args.size() == 4)) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* op = as<Op>(args[1]);
      if (mod && *mod && op && args.size() == 3) {
        if (const auto* target = as<Fn>(args[2]); target && *target) {
          std::string source(op->callee());
          if (const Fn resolved = env_.resolve(**mod, *op))
            source = std::string(resolved.module()) + "." +
                     std::string(resolved.name());
          const std::uint64_t before = (*mod)->revision();
          const bool changed = (*mod)->retarget(env_, *op, *target);
          if (changed && (*mod)->revision() != before)
            record_retarget(std::move(source), *target, before,
                            (*mod)->revision());
          return Items{Item(Attr(changed))};
        }
      }
      if (mod && *mod && op && args.size() == 4) {
        const auto values = value_handles(args[3]);
        if (const auto* target = as<Fn>(args[2]); target && *target && values)
          return Items{
              Item(Attr((*mod)->retarget(env_, *op, *target, *values)))};
        const auto callee = string(args[2]);
        if (callee && values)
          return Items{Item(Attr((*mod)->retarget(
              env_, *op, std::string(*callee), *values)))};
      }
    } else if (name == "rename" && args.size() == 4) {
      const auto* mod = as<Mod*>(args[0]);
      const auto* value = as<Val>(args[1]);
      const auto stem = string(args[2]);
      const auto ordinal = integer(args[3]);
      if (mod && value && stem && ordinal) {
        if (!*mod || !*value || *ordinal < 0 ||
            value->store_ != &(*mod)->impl_->store)
          return Items{Item(Attr(false))};
        const Site owner{value->store_, value->store_->vals[value->id_].data.fn};
        auto [entry, inserted] = fresh_names_.try_emplace(owner);
        auto& names = entry->second;
        if (inserted) {
          names.reserve(value->store_->vals.size());
          for (const auto& slot : value->store_->vals)
            if (slot.live && slot.data.fn == owner.id &&
                !slot.data.name.empty())
              names.insert(slot.data.name);
        }
        for (std::size_t offset = 0; offset <= names.size(); ++offset) {
          const std::string candidate =
              std::string(*stem) +
              std::to_string(static_cast<std::uint64_t>(*ordinal) + offset);
          if (names.contains(candidate))
            continue;
          if (!(*mod)->rename(*value, candidate))
            return Items{Item(Attr(false))};
          names.insert(candidate);
          return Items{Item(Attr(true))};
        }
        return Items{Item(Attr(false))};
      }
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
        if (const auto* val = as<Val>(args[1])) {
          const bool changed = (*mod)->rename(*val, std::string(*value));
          if (changed && *val) {
            const Site owner{val->store_,
                             val->store_->vals[val->id_].data.fn};
            const auto found = fresh_names_.find(owner);
            if (found != fresh_names_.end())
              found->second.insert(std::string(*value));
          }
          return Items{Item(Attr(changed))};
        }
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
  const Store* observed_ = nullptr;
  std::unordered_set<std::uint32_t> observed_fns_;
  std::unordered_set<std::uint32_t> observed_fn_generations_;
  std::unordered_set<std::uint32_t> observed_fn_ops_;
  std::unordered_set<std::uint32_t> observed_fn_vals_;
  std::unordered_set<std::uint32_t> observed_ops_;
  std::unordered_set<std::uint32_t> observed_vals_;
  std::unordered_set<std::string> observed_intrinsics_;
  bool observed_packages_ = false;
  bool observed_structure_ = false;
  bool observed_whole_ = false;
  bool read_only_ = false;
#if defined(JOGGLE_EVAL_COUNTERS)
  RunStepTiming* counters_ = nullptr;
#endif
  // Executable bodies can be edited between invocations. Keep snapshots keyed
  // by their source revision; clearing caches could invalidate active frames'
  // references. All snapshots are released with this evaluation.
  std::unordered_map<Site, std::vector<Dispatch>, SiteHash> dispatch_;
  std::unordered_map<Site, std::vector<Val>, SiteHash> block_args_;
  std::unordered_map<Site, std::vector<Op>, SiteHash> block_ops_;
  std::unordered_map<Site, std::vector<Val>, SiteHash> op_args_;
  std::unordered_map<Site, std::vector<Val>, SiteHash> op_outs_;
  std::unordered_map<Site, std::vector<Blk>, SiteHash> op_blks_;
  std::unordered_map<Site, MemoEntries, SiteHash> memo_;
  std::unordered_map<Site, std::unordered_set<std::string>, SiteHash>
      fresh_names_;
  std::shared_ptr<SharedCache> shared_cache_;
  std::unordered_map<PlanKey, std::unique_ptr<Plan>, PlanKeyHash> local_plans_;
  std::unordered_set<PlanKey, PlanKeyHash> compiled_plans_;
  std::unordered_map<const detail::Store*, bool> owned_stores_;
  std::vector<std::vector<std::pair<Val, Item>>> frame_values_;
  std::map<std::string, std::uint64_t, std::less<>> calls_;
  std::map<std::string, std::uint64_t, std::less<>> cached_;
  bool failed_ = false;
};

}  // namespace joggle::detail

namespace joggle {

bool detail::Eval::read(Env& env, Fn fn, const Mod& mod, Attr& result,
                        std::span<const Attr> args,
                        QueryData* dependencies, QueryTiming* timing) {
  result = Attr{};
  if (!fn) {
    env.error("query function handle is invalid");
    return false;
  }
  const std::string function =
      std::string(fn.module()) + "." + std::string(fn.name());
  if (fn.external()) {
    env.error("query entry must have a textual body: " +
                  std::string(function),
              fn.loc());
    return false;
  }
  if (!fn.generics().empty() || fn.returns().size() != 1 ||
      fn.params().size() != args.size() + 1 ||
      fn.params().front().type() != Ty("Mod")) {
    env.error("query entry must be a non-generic fn(Mod, ...) with one result: " +
                  function,
              fn.loc());
    return false;
  }
  std::vector<Ty> argument_types{Ty("Mod")};
  for (const Attr& value : args)
    argument_types.push_back(detail::runtime_type(detail::materialize(value)));
  if (!detail::resolve_overload(std::span<const Fn>(&fn, 1), argument_types,
                                {}, nullptr, nullptr)) {
    env.error("query arguments do not match function: " + function, fn.loc());
    return false;
  }

  Mod scratch;
  const bool verified = mod.impl_->store.verified_env == env.cache_id() &&
                        mod.impl_->store.verified_epoch == env.cache_epoch() &&
                        mod.impl_->store.verified_revision == mod.revision() &&
                        mod.impl_->store.diags.empty();
  Mod* target = nullptr;
  if (verified) {
    target = &const_cast<Mod&>(mod);
    if (timing)
      timing->verification_cached = true;
  } else {
    const auto snapshot_begin = std::chrono::steady_clock::now();
    scratch.impl_->store = mod.impl_->store;
    scratch.impl_->store.queries.clear();
    if (timing)
      timing->snapshot = std::chrono::steady_clock::now() - snapshot_begin;
    const auto verification_begin = std::chrono::steady_clock::now();
    if (!scratch.verify(env)) {
      if (timing)
        timing->verification =
            std::chrono::steady_clock::now() - verification_begin;
      for (const Diag& diag : scratch.diags())
        env.error(diag.message, diag.loc);
      env.error("cannot query an invalid module");
      return false;
    }
    if (timing)
      timing->verification =
          std::chrono::steady_clock::now() - verification_begin;
    target = &scratch;
  }
  const std::uint64_t before = target->revision();
  detail::Eval eval(env, [&](std::string message, Loc loc) {
    env.error(std::move(message), std::move(loc));
  }, nullptr, &target->impl_->store, true);
  const auto evaluation_begin = std::chrono::steady_clock::now();
  const auto values = eval.query(fn, *target, args);
  if (timing)
    timing->evaluation = std::chrono::steady_clock::now() - evaluation_begin;
  if (!values)
    return false;
  const auto validation_begin = std::chrono::steady_clock::now();
  if (target->revision() != before) {
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
  if (dependencies) {
    eval.dependencies(*dependencies);
    // Verification may infer types in the private snapshot. Dependency IDs
    // still correspond to the source module, but cache versions must be taken
    // from that source rather than from the normalized snapshot.
    for (QueryFnData& dependency : dependencies->dependencies)
      if (detail::live(mod.impl_->store.fns, dependency.id,
                       dependency.generation))
        dependency.revision =
            mod.impl_->store.fns[dependency.id].data.revision;
    for (QueryCollectionData& dependency : dependencies->collections)
      if (detail::live(mod.impl_->store.fns, dependency.function,
                       dependency.generation))
        dependency.members = collection_members(
            mod.impl_->store, dependency.function, dependency.kind);
    for (QueryOpData& dependency : dependencies->operations)
      if (detail::live(mod.impl_->store.ops, dependency.id,
                       dependency.generation))
        dependency.data = mod.impl_->store.ops[dependency.id].data;
    for (QueryValData& dependency : dependencies->values)
      if (detail::live(mod.impl_->store.vals, dependency.id,
                       dependency.generation))
        dependency.data = mod.impl_->store.vals[dependency.id].data;
  }
  if (timing)
    timing->validation = std::chrono::steady_clock::now() - validation_begin;
  return true;
}

bool query(Env& env, Fn function, const Mod& mod, Attr& result,
           std::span<const Attr> args) {
  env.clear_diags();
  return detail::Eval::read(env, function, mod, result, args);
}

bool query(Env& env, std::string_view function, const Mod& mod, Attr& result,
           std::span<const Attr> args, QueryReport* report) {
  env.clear_diags();
  result = Attr{};
  if (report)
    *report = QueryReport{};

  auto& entries = mod.impl_->store.queries;
  const std::size_t key = detail::query_key(function, args);
  const auto invalidation = [&](const detail::QueryData& entry) {
    if (entry.whole_revision && entry.revision != mod.revision())
      return QueryMiss::whole_revision;
    if (entry.structure && entry.structure_revision !=
                               mod.impl_->store.structure_revision)
      return QueryMiss::structure_revision;
    if (entry.packages &&
        entry.package_dependencies != mod.impl_->store.uses)
      return QueryMiss::package_dependencies;
    for (const detail::QueryFnData& dependency : entry.dependencies) {
      if (!detail::live(mod.impl_->store.fns, dependency.id,
                        dependency.generation))
        return QueryMiss::function_generation;
      if (dependency.content &&
          mod.impl_->store.fns[dependency.id].data.revision !=
              dependency.revision)
        return QueryMiss::function_revision;
    }
    for (const detail::QueryCollectionData& dependency : entry.collections) {
      if (!detail::live(mod.impl_->store.fns, dependency.function,
                        dependency.generation))
        return QueryMiss::function_generation;
      if (detail::collection_members(mod.impl_->store, dependency.function,
                                     dependency.kind) != dependency.members)
        return QueryMiss::function_shape;
    }
    for (const detail::QueryOpData& dependency : entry.operations) {
      if (!detail::live(mod.impl_->store.ops, dependency.id,
                        dependency.generation))
        return QueryMiss::operation_generation;
      if (mod.impl_->store.ops[dependency.id].data != dependency.data)
        return QueryMiss::operation_revision;
    }
    for (const detail::QueryValData& dependency : entry.values) {
      if (!detail::live(mod.impl_->store.vals, dependency.id,
                        dependency.generation))
        return QueryMiss::value_generation;
      if (mod.impl_->store.vals[dependency.id].data != dependency.data)
        return QueryMiss::value_revision;
    }
    return QueryMiss::none;
  };
  const auto same_key = [&](const detail::QueryData& entry) {
    return entry.function == function && entry.args.size() == args.size() &&
           std::equal(entry.args.begin(), entry.args.end(), args.begin());
  };
  const auto lookup_begin = std::chrono::steady_clock::now();
  QueryMiss miss = QueryMiss::cold;
  const auto bucket = entries.find(key);
  if (bucket != entries.end()) {
    for (const detail::QueryData& entry : bucket->second) {
      if (!same_key(entry))
        continue;
      if (entry.env != env.cache_id() || entry.epoch != env.cache_epoch()) {
        if (miss == QueryMiss::cold)
          miss = QueryMiss::environment;
        continue;
      }
      miss = invalidation(entry);
      if (miss != QueryMiss::none)
        continue;
      result = entry.result;
      if (report) {
        report->cached = true;
        report->miss = QueryMiss::none;
        report->observed_functions = entry.dependencies.size();
        report->observed_collections = entry.collections.size();
        report->observed_operations = entry.operations.size();
        report->observed_values = entry.values.size();
        report->observed_packages = entry.packages ? 1 : 0;
        report->observed_intrinsics = entry.intrinsics.size();
        report->observed_structure = entry.structure;
        report->observed_whole_mod = entry.whole_revision;
        report->lookup = std::chrono::steady_clock::now() - lookup_begin;
      }
      return true;
    }
  }
  if (report) {
    report->miss = miss;
    report->lookup = std::chrono::steady_clock::now() - lookup_begin;
  }

  const auto execute_begin = std::chrono::steady_clock::now();
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
    if (report)
      report->execute = std::chrono::steady_clock::now() - execute_begin;
    env.error((ambiguous ? "ambiguous query function: "
                         : "query function not found: ") +
              std::string(function));
    return false;
  }
  if (result_types.size() != 1) {
    if (report)
      report->execute = std::chrono::steady_clock::now() - execute_begin;
    env.error("query entry must return exactly one value: " +
              std::string(function), fn.loc());
    return false;
  }
  detail::QueryData entry;
  detail::QueryTiming timing;
  if (!detail::Eval::read(env, fn, mod, result, args, &entry, &timing)) {
    if (report)
      report->execute = std::chrono::steady_clock::now() - execute_begin;
    return false;
  }
  auto& destination = entries[key];
  destination.erase(std::remove_if(destination.begin(), destination.end(),
                                   [&](const detail::QueryData& stale) {
    return stale.env == env.cache_id() &&
           stale.epoch == env.cache_epoch() && stale.function == function &&
           stale.args.size() == args.size() &&
           std::equal(stale.args.begin(), stale.args.end(), args.begin());
  }), destination.end());
  entry.env = env.cache_id();
  entry.epoch = env.cache_epoch();
  entry.revision = mod.revision();
  entry.structure_revision = mod.impl_->store.structure_revision;
  entry.function = std::string(function);
  entry.args.assign(args.begin(), args.end());
  entry.result = result;
  if (report) {
    report->observed_functions = entry.dependencies.size();
    report->observed_collections = entry.collections.size();
    report->observed_operations = entry.operations.size();
    report->observed_values = entry.values.size();
    report->observed_packages = entry.packages ? 1 : 0;
    report->observed_intrinsics = entry.intrinsics.size();
    report->observed_structure = entry.structure;
    report->observed_whole_mod = entry.whole_revision;
    report->verification_cached = timing.verification_cached;
    report->snapshot = timing.snapshot;
    report->verification = timing.verification;
    report->evaluation = timing.evaluation;
    report->validation = timing.validation;
    report->execute = std::chrono::steady_clock::now() - execute_begin;
  }
  destination.push_back(std::move(entry));
  return true;
}

bool detail::Eval::sequence(
    Env& env, std::span<const std::string_view> functions, Mod& mod,
    Attr* report, std::span<const Attr> args,
    RunTiming* timing, Fn direct,
    std::vector<StageDependencyData>* dependencies) {
  env.clear_diags();
  if (report)
    *report = Attr{};
  if (timing)
    *timing = RunTiming{};
  if (dependencies) {
    dependencies->clear();
    dependencies->reserve(functions.size());
  }
  MutationTransaction transaction(mod.impl_->store, timing);
  const std::uint64_t before_revision = mod.revision();
  const auto rollback = [&]() {
    transaction.rollback();
    return false;
  };
  if (timing)
    timing->initial_verification_cached =
        mod.impl_->store.verified_env == env.cache_id() &&
        mod.impl_->store.verified_epoch == env.cache_epoch() &&
        mod.impl_->store.verified_revision == mod.revision() &&
        mod.impl_->store.diags.empty();
  const auto initial_verification_begin = std::chrono::steady_clock::now();
  if (!mod.verify(env)) {
    if (timing)
      timing->initial_verification =
          std::chrono::steady_clock::now() - initial_verification_begin;
    env.error("cannot run compile-time functions on an invalid module");
    return false;
  }
  if (timing)
    timing->initial_verification =
        std::chrono::steady_clock::now() - initial_verification_begin;

  const auto one = [&](std::string_view function, Attr* step,
                       RunStepTiming* step_timing,
                       StageDependencyData* stage_dependencies) {
    const auto total_begin = std::chrono::steady_clock::now();
    if (step_timing)
      step_timing->function = std::string(function);
#if defined(JOGGLE_EVAL_COUNTERS)
    if (step_timing)
      step_timing->counters_enabled = true;
#endif
    const std::uint64_t step_revision = mod.revision();
    if (step_timing)
      step_timing->before = step_revision;
    struct FunctionVersion {
      std::uint32_t generation = 0;
      std::uint64_t revision = 0;
      bool live = false;
    };
    std::vector<FunctionVersion> function_versions;
    const std::uint64_t structure_revision =
        mod.impl_->store.structure_revision;
    std::vector<std::string> package_dependencies;
    if (stage_dependencies) {
      package_dependencies = mod.impl_->store.uses;
      function_versions.reserve(mod.impl_->store.fns.size());
      for (const auto& slot : mod.impl_->store.fns)
        function_versions.push_back(
            {slot.generation, slot.data.revision, slot.live});
    }
    const auto resolve_begin = std::chrono::steady_clock::now();
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
    if (direct && !detail::resolve_overload(
                      std::span<const Fn>(&direct, 1), argument_types,
                      {}, nullptr, nullptr)) {
      env.error("compile-time arguments do not match function: " +
                    std::string(function),
                direct.loc());
      return false;
    }
    const Fn fn = direct ? direct : detail::resolve_overload(
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
    if (step_timing)
      step_timing->resolve =
          std::chrono::steady_clock::now() - resolve_begin;
    Attr::List trace;
    detail::Eval eval(env, [&](std::string message, Loc loc) {
      env.error(std::move(message), std::move(loc));
    }, step ? &trace : nullptr,
       stage_dependencies ? &mod.impl_->store : nullptr, false
#if defined(JOGGLE_EVAL_COUNTERS)
      , step_timing
#endif
    );
    const auto evaluation_begin = std::chrono::steady_clock::now();
    const auto result = eval.run(fn, mod, args);
    if (step_timing)
      step_timing->evaluation =
          std::chrono::steady_clock::now() - evaluation_begin;
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
    if (step_timing)
      step_timing->verification_cached =
          mod.impl_->store.verified_env == env.cache_id() &&
          mod.impl_->store.verified_epoch == env.cache_epoch() &&
          mod.impl_->store.verified_revision == mod.revision() &&
          mod.impl_->store.diags.empty();
    const auto verification_begin = std::chrono::steady_clock::now();
    if (!mod.verify(env)) {
      if (step_timing)
        step_timing->verification =
            std::chrono::steady_clock::now() - verification_begin;
      for (const Diag& diagnostic : mod.diags())
        env.error(diagnostic.message, diagnostic.loc);
      env.error("compile-time function produced an invalid module: " +
                std::string(function));
      return false;
    }
    if (stage_dependencies) {
      eval.dependencies(stage_dependencies->inputs);
      stage_dependencies->outputs.clear();
      const auto& current = mod.impl_->store.fns;
      const std::size_t common =
          std::min(function_versions.size(), current.size());
      for (std::size_t id = 0; id < common; ++id)
        if (function_versions[id].generation != current[id].generation ||
            function_versions[id].revision != current[id].data.revision ||
            function_versions[id].live != current[id].live)
          stage_dependencies->outputs.push_back(
              static_cast<std::uint32_t>(id));
      for (std::size_t id = common; id < current.size(); ++id)
        if (current[id].live)
          stage_dependencies->outputs.push_back(
              static_cast<std::uint32_t>(id));
      stage_dependencies->output_structure =
          structure_revision != mod.impl_->store.structure_revision;
      stage_dependencies->output_packages =
          package_dependencies != mod.impl_->store.uses ||
          std::binary_search(stage_dependencies->inputs.intrinsics.begin(),
                             stage_dependencies->inputs.intrinsics.end(),
                             "use");
    }
    if (step_timing) {
      step_timing->verification =
          std::chrono::steady_clock::now() - verification_begin;
      step_timing->after = mod.revision();
      step_timing->succeeded = true;
      step_timing->total = std::chrono::steady_clock::now() - total_begin;
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
    summary["calls"] = eval.calls();
    summary["cached"] = eval.cached();
    summary["steps"] = Attr(std::move(trace));
    *step = Attr(std::move(summary));
    return true;
  };

  Attr::List steps;
  if (report)
    steps.reserve(functions.size());
  if (timing)
    timing->steps.reserve(functions.size());
  bool reported = false;
  for (const std::string_view function : functions) {
    Attr step;
    RunStepTiming step_timing;
    StageDependencyData stage_dependencies;
    const auto step_begin = std::chrono::steady_clock::now();
    if (!one(function, report ? &step : nullptr,
             timing ? &step_timing : nullptr,
             dependencies ? &stage_dependencies : nullptr)) {
      if (timing) {
        if (step_timing.total == std::chrono::nanoseconds::zero())
          step_timing.total = std::chrono::steady_clock::now() - step_begin;
        step_timing.after = mod.revision();
        timing->steps.push_back(std::move(step_timing));
      }
      return rollback();
    }
    if (dependencies)
      dependencies->push_back(std::move(stage_dependencies));
    if (timing)
      timing->steps.push_back(std::move(step_timing));
    if (report) {
      if (const Attr::Dict* values = step.dict()) {
        const auto found = values->find("reported");
        reported = reported ||
                   (found != values->end() && found->second.boolean() == true);
      }
      steps.push_back(std::move(step));
    }
  }
  if (timing)
    timing->succeeded = true;
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

bool run(Env& env, Fn function, Mod& mod, Attr& report,
         std::span<const Attr> args) {
  const std::string symbol =
      function ? std::string(function.module()) + "." +
                     std::string(function.name())
               : "<invalid function>";
  const std::string_view entry = symbol;
  const std::span<const std::string_view> functions(&entry, 1);
  Attr sequence;
  if (!detail::Eval::sequence(env, functions, mod, &sequence, args, nullptr,
                              function))
    return false;
  report = sequence.dict()->at("steps").list()->front();
  return true;
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         Attr& report, std::span<const Attr> args) {
  return detail::Eval::sequence(env, functions, mod, &report, args, nullptr);
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         std::span<const Attr> args) {
  return detail::Eval::sequence(env, functions, mod, nullptr, args, nullptr);
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         Attr& report, RunTiming& timing) {
  return run(env, functions, mod, report, {}, timing);
}

bool run(Env& env, std::span<const std::string_view> functions, Mod& mod,
         Attr& report, std::span<const Attr> args, RunTiming& timing) {
  return detail::Eval::sequence(env, functions, mod, &report, args, &timing);
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report,
         RunTiming& timing) {
  return run(env, function, mod, report, {}, timing);
}

bool run(Env& env, std::string_view function, Mod& mod, Attr& report,
         std::span<const Attr> args, RunTiming& timing) {
  const std::span<const std::string_view> functions(&function, 1);
  Attr sequence;
  if (!detail::Eval::sequence(env, functions, mod, &sequence, args, &timing))
    return false;
  report = sequence.dict()->at("steps").list()->front();
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

struct ReactiveSchedule::Impl {
  struct Stage {
    std::string function;
    detail::StageDependencyData dependencies;
  };

  std::vector<Stage> stages;
  const detail::Store* store = nullptr;
  std::uint64_t environment = 0;
  std::uint64_t epoch = 0;
  std::vector<Attr> args;
  bool bound = false;
};

ReactiveSchedule::ReactiveSchedule(std::vector<std::string> stages)
    : impl_(std::make_unique<Impl>()) {
  impl_->stages.reserve(stages.size());
  for (std::string& function : stages)
    impl_->stages.push_back({std::move(function), {}});
}

ReactiveSchedule::~ReactiveSchedule() = default;
ReactiveSchedule::ReactiveSchedule(ReactiveSchedule&&) noexcept = default;
ReactiveSchedule& ReactiveSchedule::operator=(ReactiveSchedule&&) noexcept =
    default;

void ReactiveSchedule::reset() noexcept {
  for (Impl::Stage& stage : impl_->stages)
    stage.dependencies = {};
  impl_->store = nullptr;
  impl_->environment = 0;
  impl_->epoch = 0;
  impl_->args.clear();
  impl_->bound = false;
}

std::vector<std::string> ReactiveSchedule::stages() const {
  std::vector<std::string> out;
  out.reserve(impl_->stages.size());
  for (const Impl::Stage& stage : impl_->stages)
    out.push_back(stage.function);
  return out;
}

bool ReactiveSchedule::run(Env& env, Mod& mod,
                           std::span<const Attr> args,
                           ReactiveRunReport* report) {
  env.clear_diags();
  if (report) {
    *report = ReactiveRunReport{};
    report->stages.reserve(impl_->stages.size());
    for (const Impl::Stage& stage : impl_->stages)
      report->stages.push_back({stage.function});
  }
  if (impl_->stages.empty()) {
    env.clear_diags();
    env.error("reactive schedule requires at least one stage");
    return false;
  }

  ReactiveMiss cold_miss = ReactiveMiss::none;
  if (!impl_->bound || impl_->store != &mod.impl_->store)
    cold_miss = ReactiveMiss::cold;
  else if (impl_->environment != env.cache_id() ||
           impl_->epoch != env.cache_epoch())
    cold_miss = ReactiveMiss::environment;
  else if (impl_->args.size() != args.size() ||
           !std::equal(impl_->args.begin(), impl_->args.end(), args.begin()))
    cold_miss = ReactiveMiss::arguments;

  const auto invalidation = [&](const Impl::Stage& stage) {
    const detail::QueryData& input = stage.dependencies.inputs;
    const detail::Store& store = mod.impl_->store;
    if (input.whole_revision && input.revision != store.revision)
      return ReactiveMiss::whole_revision;
    if (input.structure &&
        input.structure_revision != store.structure_revision)
      return ReactiveMiss::structure_revision;
    if (input.packages && input.package_dependencies != store.uses)
      return ReactiveMiss::package_dependencies;
    for (const detail::QueryFnData& dependency : input.dependencies) {
      if (dependency.id >= store.fns.size() ||
          !store.fns[dependency.id].live ||
          store.fns[dependency.id].generation != dependency.generation)
        return ReactiveMiss::function_generation;
      if (dependency.content &&
          store.fns[dependency.id].data.revision != dependency.revision)
        return ReactiveMiss::function_revision;
    }
    for (const detail::QueryCollectionData& dependency : input.collections) {
      if (!detail::live(store.fns, dependency.function,
                        dependency.generation))
        return ReactiveMiss::function_generation;
      if (detail::collection_members(store, dependency.function,
                                     dependency.kind) != dependency.members)
        return ReactiveMiss::function_shape;
    }
    for (const detail::QueryOpData& dependency : input.operations) {
      if (!detail::live(store.ops, dependency.id, dependency.generation))
        return ReactiveMiss::operation_generation;
      if (store.ops[dependency.id].data != dependency.data)
        return ReactiveMiss::operation_revision;
    }
    for (const detail::QueryValData& dependency : input.values) {
      if (!detail::live(store.vals, dependency.id, dependency.generation))
        return ReactiveMiss::value_generation;
      if (store.vals[dependency.id].data != dependency.data)
        return ReactiveMiss::value_revision;
    }
    return ReactiveMiss::none;
  };

  std::vector<bool> selected(impl_->stages.size(), false);
  std::vector<ReactiveMiss> misses(impl_->stages.size(), ReactiveMiss::none);
  std::unordered_set<std::uint32_t> dirty_functions;
  bool dirty_structure = false;
  bool dirty_packages = false;
  for (std::size_t index = 0; index < impl_->stages.size(); ++index) {
    const Impl::Stage& stage = impl_->stages[index];
    ReactiveMiss miss = cold_miss == ReactiveMiss::none
                            ? invalidation(stage)
                            : cold_miss;
    if (miss == ReactiveMiss::none) {
      const detail::QueryData& input = stage.dependencies.inputs;
      bool upstream = input.whole_revision &&
                      (!dirty_functions.empty() || dirty_structure ||
                       dirty_packages);
      upstream = upstream || (input.structure && dirty_structure);
      upstream = upstream || (input.packages && dirty_packages);
      for (const detail::QueryFnData& dependency : input.dependencies)
        upstream = upstream || dirty_functions.contains(dependency.id);
      for (const detail::QueryCollectionData& dependency : input.collections)
        upstream = upstream ||
                   dirty_functions.contains(dependency.function);
      for (const detail::QueryOpData& dependency : input.operations) {
        const std::uint32_t block = dependency.data.blk;
        if (block < mod.impl_->store.blks.size())
          upstream = upstream || dirty_functions.contains(
              mod.impl_->store.blks[block].data.fn);
      }
      for (const detail::QueryValData& dependency : input.values)
        upstream = upstream || dirty_functions.contains(dependency.data.fn);
      if (upstream)
        miss = ReactiveMiss::upstream;
    }
    misses[index] = miss;
    selected[index] = miss != ReactiveMiss::none;
    if (!selected[index])
      continue;
    for (const std::uint32_t output : stage.dependencies.outputs)
      dirty_functions.insert(output);
    dirty_structure =
        dirty_structure || stage.dependencies.output_structure;
    dirty_packages = dirty_packages || stage.dependencies.output_packages;
  }

  std::vector<std::size_t> selected_indices;
  std::vector<std::string_view> selected_functions;
  for (std::size_t index = 0; index < selected.size(); ++index) {
    if (!selected[index])
      continue;
    selected_indices.push_back(index);
    selected_functions.push_back(impl_->stages[index].function);
  }
  if (report) {
    report->cold = cold_miss != ReactiveMiss::none;
    report->executed_stages = selected_indices.size();
    report->reused_stages = impl_->stages.size() - selected_indices.size();
    for (std::size_t index = 0; index < selected.size(); ++index) {
      report->stages[index].executed = selected[index];
      report->stages[index].miss = misses[index];
    }
  }

  std::vector<detail::StageDependencyData> captured;
  RunTiming timing;
  if (selected_functions.empty())
    timing.succeeded = true;
  if (!selected_functions.empty() &&
      !detail::Eval::sequence(env, selected_functions, mod, nullptr, args,
                              &timing, {}, &captured)) {
    if (report)
      report->execution = std::move(timing);
    return false;
  }
  if (captured.size() != selected_indices.size()) {
    env.error("reactive schedule produced inconsistent dependency records");
    return false;
  }
  for (std::size_t position = 0; position < selected_indices.size();
       ++position)
    impl_->stages[selected_indices[position]].dependencies =
        std::move(captured[position]);

  const detail::Store& store = mod.impl_->store;
  for (Impl::Stage& stage : impl_->stages) {
    detail::QueryData& input = stage.dependencies.inputs;
    input.env = env.cache_id();
    input.epoch = env.cache_epoch();
    input.revision = store.revision;
    input.structure_revision = store.structure_revision;
    if (input.packages)
      input.package_dependencies = store.uses;
    for (detail::QueryFnData& dependency : input.dependencies) {
      if (dependency.id >= store.fns.size() ||
          !store.fns[dependency.id].live)
        continue;
      dependency.generation = store.fns[dependency.id].generation;
      dependency.revision = store.fns[dependency.id].data.revision;
    }
    std::erase_if(input.collections,
                  [&](detail::QueryCollectionData& dependency) {
      if (!detail::live(store.fns, dependency.function,
                        dependency.generation))
        return true;
      dependency.members = detail::collection_members(
          store, dependency.function, dependency.kind);
      return false;
    });
    std::erase_if(input.operations, [&](detail::QueryOpData& dependency) {
      if (!detail::live(store.ops, dependency.id, dependency.generation))
        return true;
      dependency.data = store.ops[dependency.id].data;
      return false;
    });
    std::erase_if(input.values, [&](detail::QueryValData& dependency) {
      if (!detail::live(store.vals, dependency.id, dependency.generation))
        return true;
      dependency.data = store.vals[dependency.id].data;
      return false;
    });
  }
  impl_->store = &store;
  impl_->environment = env.cache_id();
  impl_->epoch = env.cache_epoch();
  impl_->args.assign(args.begin(), args.end());
  impl_->bound = true;

  if (report) {
    report->succeeded = true;
    report->execution = std::move(timing);
    for (std::size_t index = 0; index < impl_->stages.size(); ++index) {
      const detail::StageDependencyData& dependencies =
          impl_->stages[index].dependencies;
      report->stages[index].observed_functions =
          dependencies.inputs.dependencies.size();
      report->stages[index].observed_collections =
          dependencies.inputs.collections.size();
      report->stages[index].observed_operations =
          dependencies.inputs.operations.size();
      report->stages[index].observed_values =
          dependencies.inputs.values.size();
      report->stages[index].observed_packages =
          dependencies.inputs.packages ? 1 : 0;
      report->stages[index].observed_intrinsics =
          dependencies.inputs.intrinsics.size();
      report->stages[index].observed_structure =
          dependencies.inputs.structure;
      report->stages[index].observed_whole_mod =
          dependencies.inputs.whole_revision;
      report->stages[index].changed_functions =
          selected[index] ? dependencies.outputs.size() : 0;
    }
  }
  return true;
}

}  // namespace joggle
