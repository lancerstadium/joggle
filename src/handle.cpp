#include "detail.h"

namespace joggle {

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
const Attr::Dict& Val::meta() const noexcept {
  static const Attr::Dict empty;
  return valid() ? store_->vals[id_].data.meta : empty;
}
const Attr* Val::meta(std::string_view key) const noexcept {
  if (!valid())
    return nullptr;
  const auto& values = store_->vals[id_].data.meta;
  const auto found = values.find(key);
  return found == values.end() ? nullptr : &found->second;
}
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
Op::Form Op::form() const noexcept {
  return valid() ? store_->ops[id_].data.form : Form::hidden;
}
std::string_view Op::callee() const noexcept {
  return valid() ? std::string_view(store_->ops[id_].data.callee)
                 : std::string_view{};
}
std::vector<Ty> Op::generics() const {
  if (!valid() || kind() != Kind::call)
    return {};
  const Ty applied{std::string(callee())};
  return applied.valid() ? applied.args() : std::vector<Ty>{};
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
std::vector<Blk> Op::blks() const {
  std::vector<Blk> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->ops[id_].data.blks)
    if (id < store_->blks.size() && store_->blks[id].live)
      out.push_back(Blk(store_, id, store_->blks[id].generation));
  return out;
}
Blk Op::blk() const noexcept {
  if (!valid() || store_->ops[id_].data.blk == detail::none)
    return {};
  const std::uint32_t id = store_->ops[id_].data.blk;
  return Blk(store_, id, store_->blks[id].generation);
}
const Attr::Dict& Op::meta() const noexcept {
  static const Attr::Dict empty;
  return valid() ? store_->ops[id_].data.meta : empty;
}
const Attr* Op::meta(std::string_view key) const noexcept {
  if (!valid())
    return nullptr;
  const auto& values = store_->ops[id_].data.meta;
  const auto found = values.find(key);
  return found == values.end() ? nullptr : &found->second;
}
Loc Op::loc() const { return valid() ? store_->ops[id_].data.loc : Loc{}; }

Blk::Blk(detail::Store* store, std::uint32_t id,
         std::uint32_t generation) noexcept
    : store_(store), id_(id), generation_(generation) {}

bool Blk::valid() const noexcept {
  return store_ && detail::live(store_->blks, id_, generation_);
}
Blk::operator bool() const noexcept { return valid(); }
std::vector<Val> Blk::args() const {
  std::vector<Val> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->blks[id_].data.args)
    if (id < store_->vals.size() && store_->vals[id].live)
      out.push_back(Val(store_, id, store_->vals[id].generation));
  return out;
}
std::vector<Op> Blk::ops() const {
  std::vector<Op> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->blks[id_].data.ops) {
    if (store_->ops[id].live)
      out.push_back(Op(store_, id, store_->ops[id].generation));
  }
  return out;
}

Op Blk::op() const noexcept {
  if (!valid() || store_->blks[id_].data.parent_op == detail::none)
    return {};
  const std::uint32_t id = store_->blks[id_].data.parent_op;
  if (id >= store_->ops.size() || !store_->ops[id].live)
    return {};
  return Op(store_, id, store_->ops[id].generation);
}

Fn Blk::fn() const noexcept {
  if (!valid() || store_->blks[id_].data.fn == detail::none)
    return {};
  const std::uint32_t id = store_->blks[id_].data.fn;
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
std::string_view Fn::module() const noexcept {
  return valid() ? std::string_view(store_->name) : std::string_view{};
}
std::vector<Val> Fn::generics() const {
  std::vector<Val> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->fns[id_].data.generic_vals)
    out.push_back(Val(store_, id, store_->vals[id].generation));
  return out;
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
bool Fn::local() const noexcept {
  return valid() && store_->fns[id_].data.local;
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
  if (!valid() || store_->fns[id_].data.blks.empty())
    return {};
  const std::uint32_t id = store_->fns[id_].data.blks.front();
  return Blk(store_, id, store_->blks[id].generation);
}
std::vector<Blk> Fn::blks() const {
  std::vector<Blk> out;
  if (!valid())
    return out;
  for (const std::uint32_t id : store_->fns[id_].data.blks)
    if (id < store_->blks.size() && store_->blks[id].live)
      out.push_back(Blk(store_, id, store_->blks[id].generation));
  return out;
}
std::vector<Op> Fn::ops() const {
  std::vector<Op> out;
  if (!valid() || !body())
    return out;
  const auto visit = [&](const auto& self, Blk blk) -> void {
    for (Op op : blk.ops()) {
      out.push_back(op);
      for (Blk child : op.blks())
        self(self, child);
    }
  };
  visit(visit, body());
  return out;
}
std::vector<Val> Fn::vals() const {
  std::vector<Val> out = params();
  for (Blk blk : blks()) {
    const std::vector<Val> args = blk.args();
    out.insert(out.end(), args.begin(), args.end());
  }
  for (Op op : ops()) {
    const std::vector<Val> results = op.outs();
    out.insert(out.end(), results.begin(), results.end());
  }
  return out;
}
Loc Fn::loc() const { return valid() ? store_->fns[id_].data.loc : Loc{}; }

}  // namespace joggle
