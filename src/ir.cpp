#include "detail.h"

#include <algorithm>
#include <utility>

namespace joggle {

namespace {

template <class T>
const detail::Slot<T>* slot(const std::vector<detail::Slot<T>>& slots,
                            std::uint32_t id, std::uint32_t generation) {
  return detail::live(slots, id, generation) ? &slots[id] : nullptr;
}

}  // namespace

Attr::Attr(bool value) : data_(value) {}
Attr::Attr(std::int64_t value) : data_(value) {}
Attr::Attr(double value) : data_(value) {}
Attr::Attr(std::string value) : data_(std::move(value)) {}
Attr::Attr(const char* value) : data_(std::string(value)) {}
Attr::Attr(Bytes value) : data_(std::move(value)) {}
Attr::Attr(List value) : data_(std::move(value)) {}
Attr::Attr(Dict value) : data_(std::move(value)) {}

const Attr::Data& Attr::data() const noexcept { return data_; }
bool Attr::empty() const noexcept {
  return std::holds_alternative<std::monostate>(data_);
}
std::optional<bool> Attr::boolean() const noexcept {
  const auto* value = std::get_if<bool>(&data_);
  return value ? std::optional<bool>(*value) : std::nullopt;
}
std::optional<std::int64_t> Attr::integer() const noexcept {
  const auto* value = std::get_if<std::int64_t>(&data_);
  return value ? std::optional<std::int64_t>(*value) : std::nullopt;
}
std::optional<double> Attr::real() const noexcept {
  const auto* value = std::get_if<double>(&data_);
  return value ? std::optional<double>(*value) : std::nullopt;
}
std::optional<std::string_view> Attr::string() const& noexcept {
  const auto* value = std::get_if<std::string>(&data_);
  return value ? std::optional<std::string_view>(*value) : std::nullopt;
}
const Attr::Bytes* Attr::bytes() const& noexcept {
  return std::get_if<Bytes>(&data_);
}
const Attr::List* Attr::list() const& noexcept {
  return std::get_if<List>(&data_);
}
const Attr::Dict* Attr::dict() const& noexcept {
  return std::get_if<Dict>(&data_);
}

Ty::Ty(std::string text) : text_(std::move(text)) {}
bool Ty::empty() const noexcept { return text_.empty(); }
std::string_view Ty::text() const noexcept { return text_; }

Val::Val(detail::Store* store, std::uint32_t id,
         std::uint32_t generation) noexcept
    : store_(store), id_(id), generation_(generation) {}

bool Val::valid() const noexcept {
  return store_ && detail::live(store_->vals, id_, generation_);
}
Val::operator bool() const noexcept { return valid(); }
std::string_view Val::name() const noexcept {
  const auto* entry = valid() ? &store_->vals[id_] : nullptr;
  return entry ? std::string_view(entry->data.name) : std::string_view{};
}
Ty Val::type() const { return valid() ? store_->vals[id_].data.type : Ty{}; }
Op Val::def() const noexcept {
  if (!valid() || store_->vals[id_].data.def == detail::none)
    return {};
  const std::uint32_t id = store_->vals[id_].data.def;
  return Op(store_, id, store_->ops[id].generation);
}
std::vector<Op> Val::users() const {
  std::vector<Op> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->vals[id_].data.users) {
    if (id < store_->ops.size() && store_->ops[id].live)
      out.push_back(Op(store_, id, store_->ops[id].generation));
  }
  return out;
}
bool Val::is_const() const noexcept {
  const Op op = def();
  return op && op.kind() == Op::Kind::constant;
}
Attr Val::constant() const {
  const Op op = def();
  return op && op.kind() == Op::Kind::constant
             ? store_->ops[op.id_].data.literal
             : Attr{};
}

Op::Op(detail::Store* store, std::uint32_t id,
       std::uint32_t generation) noexcept
    : store_(store), id_(id), generation_(generation) {}

bool Op::valid() const noexcept {
  return store_ && detail::live(store_->ops, id_, generation_);
}
Op::operator bool() const noexcept { return valid(); }
Op::Kind Op::kind() const noexcept {
  return valid() ? store_->ops[id_].data.kind : Kind::call;
}
std::string_view Op::callee() const noexcept {
  return valid() ? std::string_view(store_->ops[id_].data.callee)
                 : std::string_view{};
}
std::vector<Val> Op::args() const {
  std::vector<Val> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->ops[id_].data.args)
    out.push_back(Val(store_, id, store_->vals[id].generation));
  return out;
}
std::vector<Val> Op::outs() const {
  std::vector<Val> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->ops[id_].data.outs)
    out.push_back(Val(store_, id, store_->vals[id].generation));
  return out;
}
std::vector<Blk> Op::blocks() const {
  std::vector<Blk> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->ops[id_].data.blocks)
    out.push_back(Blk(store_, id, store_->blocks[id].generation));
  return out;
}
Blk Op::block() const noexcept {
  if (!valid() || store_->ops[id_].data.block == detail::none)
    return {};
  const std::uint32_t id = store_->ops[id_].data.block;
  return Blk(store_, id, store_->blocks[id].generation);
}
Loc Op::loc() const { return valid() ? store_->ops[id_].data.loc : Loc{}; }

Blk::Blk(detail::Store* store, std::uint32_t id,
         std::uint32_t generation) noexcept
    : store_(store), id_(id), generation_(generation) {}

bool Blk::valid() const noexcept {
  return store_ && detail::live(store_->blocks, id_, generation_);
}
Blk::operator bool() const noexcept { return valid(); }
std::vector<Val> Blk::args() const {
  std::vector<Val> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->blocks[id_].data.args)
    out.push_back(Val(store_, id, store_->vals[id].generation));
  return out;
}
std::vector<Op> Blk::ops() const {
  std::vector<Op> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->blocks[id_].data.ops) {
    if (store_->ops[id].live)
      out.push_back(Op(store_, id, store_->ops[id].generation));
  }
  return out;
}
Fn Blk::fn() const noexcept {
  if (!valid() || store_->blocks[id_].data.fn == detail::none)
    return {};
  const std::uint32_t id = store_->blocks[id_].data.fn;
  return Fn(store_, id, store_->fns[id].generation);
}

Fn::Fn(detail::Store* store, std::uint32_t id,
       std::uint32_t generation) noexcept
    : store_(store), id_(id), generation_(generation) {}

bool Fn::valid() const noexcept {
  return store_ && detail::live(store_->fns, id_, generation_);
}
Fn::operator bool() const noexcept { return valid(); }
std::string_view Fn::name() const noexcept {
  return valid() ? std::string_view(store_->fns[id_].data.name)
                 : std::string_view{};
}
std::vector<std::string> Fn::generics() const {
  return valid() ? store_->fns[id_].data.generics : std::vector<std::string>{};
}
std::vector<Val> Fn::params() const {
  std::vector<Val> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->fns[id_].data.params)
    out.push_back(Val(store_, id, store_->vals[id].generation));
  return out;
}
std::vector<Ty> Fn::returns() const {
  return valid() ? store_->fns[id_].data.returns : std::vector<Ty>{};
}
bool Fn::external() const noexcept {
  return valid() && store_->fns[id_].data.external;
}
const Attr::Dict& Fn::meta() const noexcept {
  static const Attr::Dict empty;
  return valid() ? store_->fns[id_].data.meta : empty;
}
const Attr* Fn::meta(std::string_view key) const noexcept {
  if (!valid())
    return nullptr;
  const auto& values = store_->fns[id_].data.meta;
  const auto found = values.find(key);
  return found == values.end() ? nullptr : &found->second;
}
Blk Fn::body() const noexcept {
  if (!valid() || store_->fns[id_].data.blocks.empty())
    return {};
  const std::uint32_t id = store_->fns[id_].data.blocks.front();
  return Blk(store_, id, store_->blocks[id].generation);
}
std::vector<Blk> Fn::blocks() const {
  std::vector<Blk> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->fns[id_].data.blocks)
    out.push_back(Blk(store_, id, store_->blocks[id].generation));
  return out;
}
Loc Fn::loc() const { return valid() ? store_->fns[id_].data.loc : Loc{}; }

Mod::Mod() : impl_(std::make_unique<Impl>()) {}
Mod::~Mod() = default;
Mod::Mod(Mod&&) noexcept = default;
Mod& Mod::operator=(Mod&&) noexcept = default;

std::string_view Mod::name() const noexcept { return impl_->store.name; }
void Mod::name(std::string name) { impl_->store.name = std::move(name); }
std::vector<std::string> Mod::uses() const { return impl_->store.uses; }

std::vector<Fn> Mod::fns() const {
  std::vector<Fn> out;
  for (std::uint32_t id = 0; id < impl_->store.fns.size(); ++id) {
    const auto& entry = impl_->store.fns[id];
    if (entry.live)
      out.push_back(Fn(&impl_->store, id, entry.generation));
  }
  return out;
}

Fn Mod::find_fn(std::string_view name) const noexcept {
  const auto found = impl_->store.symbols.find(std::string(name));
  if (found == impl_->store.symbols.end())
    return {};
  const std::uint32_t id = found->second;
  return Fn(&impl_->store, id, impl_->store.fns[id].generation);
}

Val Mod::call(Op before, std::string callee, std::span<const Val> args,
              Ty type) {
  auto& store = impl_->store;
  if (!before.valid() || before.store_ != &store || callee.empty() ||
      type.empty()) {
    detail::add_diag(
        store.diags,
        "call requires a live insertion point, callee, and result type");
    return {};
  }
  for (Val arg : args) {
    if (!arg.valid() || arg.store_ != &store ||
        !detail::dominates(store, arg.id_, before.id_)) {
      detail::add_diag(store.diags,
                       "call argument must dominate the insertion point",
                       before.loc());
      return {};
    }
  }

  const std::uint32_t block = store.ops[before.id_].data.block;
  auto& order = store.blocks[block].data.ops;
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end()) {
    detail::add_diag(store.diags, "call insertion point is not in its block",
                     before.loc());
    return {};
  }

  const auto op_id = static_cast<std::uint32_t>(store.ops.size());
  const auto value_id = static_cast<std::uint32_t>(store.vals.size());
  detail::ValData value;
  value.type = std::move(type);
  value.def = op_id;
  detail::OpData op;
  op.kind = Op::Kind::call;
  op.block = block;
  op.callee = std::move(callee);
  op.outs.push_back(value_id);
  op.loc = before.loc();
  op.args.reserve(args.size());
  for (Val arg : args)
    op.args.push_back(arg.id_);
  store.vals.push_back({std::move(value), 1, true});
  store.ops.push_back({std::move(op), 1, true});
  order.insert(position, op_id);
  detail::rebuild_uses(store);
  return Val(&store, value_id, store.vals[value_id].generation);
}

bool Mod::replace(Val old_value, Val new_value) {
  auto& store = impl_->store;
  if (!old_value.valid() || !new_value.valid() || old_value.store_ != &store ||
      new_value.store_ != &store) {
    detail::add_diag(store.diags,
                     "replace requires two live values in this module");
    return false;
  }
  for (auto& entry : store.ops) {
    if (!entry.live)
      continue;
    for (std::uint32_t& arg : entry.data.args) {
      if (arg == old_value.id_)
        arg = new_value.id_;
    }
  }
  detail::rebuild_uses(store);
  return true;
}

bool Mod::erase(Op op) {
  auto& store = impl_->store;
  if (!op.valid() || op.store_ != &store) {
    detail::add_diag(store.diags,
                     "erase requires a live operation in this module");
    return false;
  }
  for (const std::uint32_t value : store.ops[op.id_].data.outs) {
    if (!store.vals[value].data.users.empty()) {
      detail::add_diag(store.diags, "cannot erase an operation with live users",
                       store.ops[op.id_].data.loc);
      return false;
    }
  }
  const std::uint32_t block = store.ops[op.id_].data.block;
  if (block != detail::none) {
    auto& order = store.blocks[block].data.ops;
    order.erase(std::remove(order.begin(), order.end(), op.id_), order.end());
  }
  for (const std::uint32_t value : store.ops[op.id_].data.outs) {
    store.vals[value].live = false;
    ++store.vals[value].generation;
  }
  store.ops[op.id_].live = false;
  ++store.ops[op.id_].generation;
  detail::rebuild_uses(store);
  return true;
}

bool Mod::rename(Op call, std::string callee) {
  auto& store = impl_->store;
  if (!call.valid() || call.store_ != &store || call.kind() != Op::Kind::call ||
      callee.empty()) {
    detail::add_diag(store.diags,
                     "rename requires a live call and non-empty callee");
    return false;
  }
  store.ops[call.id_].data.callee = std::move(callee);
  return true;
}

bool Mod::ok() const noexcept { return impl_->store.diags.empty(); }
const std::vector<Diag>& Mod::diags() const noexcept {
  return impl_->store.diags;
}
void Mod::clear_diags() noexcept { impl_->store.diags.clear(); }
int Mod::print_diags(std::FILE* file) const {
  return detail::print_diags(file, impl_->store.diags);
}

}  // namespace joggle

namespace joggle::detail {

void rebuild_uses(Store& store) {
  for (auto& value : store.vals)
    value.data.users.clear();
  for (std::uint32_t id = 0; id < store.ops.size(); ++id) {
    const auto& op = store.ops[id];
    if (!op.live)
      continue;
    for (const std::uint32_t arg : op.data.args) {
      if (arg < store.vals.size() && store.vals[arg].live)
        store.vals[arg].data.users.push_back(id);
    }
  }
}

}  // namespace joggle::detail
