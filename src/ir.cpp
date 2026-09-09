#include "detail.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>
#include <utility>

namespace joggle {

namespace {

template <class T>
const detail::Slot<T>* slot(const std::vector<detail::Slot<T>>& slots,
                            std::uint32_t id, std::uint32_t generation) {
  return detail::live(slots, id, generation) ? &slots[id] : nullptr;
}

std::string_view trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return text;
}

bool valid_atom(std::string_view text) {
  return !text.empty() &&
         text.find_first_of("<>[],()") == std::string_view::npos &&
         std::none_of(text.begin(), text.end(), [](char ch) {
           return std::isspace(static_cast<unsigned char>(ch));
         });
}

std::optional<std::vector<std::string_view>>
split_terms(std::string_view text) {
  std::vector<std::string_view> terms;
  std::size_t start = 0;
  std::vector<char> closes;
  for (std::size_t index = 0; index < text.size(); ++index) {
    switch (text[index]) {
    case '<':
      closes.push_back('>');
      break;
    case '[':
      closes.push_back(']');
      break;
    case '(':
      closes.push_back(')');
      break;
    case '>':
    case ']':
    case ')':
      if (closes.empty() || closes.back() != text[index])
        return std::nullopt;
      closes.pop_back();
      break;
    case ',':
      if (closes.empty()) {
        const std::string_view term = trim(text.substr(start, index - start));
        if (term.empty())
          return std::nullopt;
        terms.push_back(term);
        start = index + 1;
      }
      break;
    default:
      break;
    }
  }
  if (!closes.empty())
    return std::nullopt;
  const std::string_view tail = trim(text.substr(start));
  if (!tail.empty())
    terms.push_back(tail);
  else if (start != 0)
    return std::nullopt;
  return terms;
}

void touch(detail::Store& store) {
  if (store.revision != std::numeric_limits<std::uint64_t>::max())
    ++store.revision;
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

Ty::Ty(std::string text) {
  const std::string_view source = trim(text);
  text_ = std::string(source);
  if (source.empty())
    return;
  std::size_t open = std::string_view::npos;
  char close = '\0';
  if (source.front() == '[' && source.back() == ']') {
    name_ = "[]";
    open = 0;
    close = ']';
  } else {
    open = source.find('<');
    if (open != std::string_view::npos && source.back() == '>') {
      name_ = std::string(trim(source.substr(0, open)));
      close = '>';
    } else if (open == std::string_view::npos)
      open = std::string_view::npos;
    else
      return;
  }
  if (open == std::string_view::npos) {
    if (!valid_atom(source)) {
      name_.clear();
      return;
    }
    name_ = std::string(source);
    text_ = name_;
    valid_ = true;
    return;
  }
  if (name_ != "[]" && !valid_atom(name_))
    return;
  const std::size_t first = open + 1;
  const std::size_t count = source.size() - first - 1;
  const auto terms = split_terms(source.substr(first, count));
  if (!terms || (close == '>' && terms->empty())) {
    name_.clear();
    return;
  }
  for (std::string_view term : *terms) {
    Ty arg{std::string(term)};
    if (!arg.valid()) {
      name_.clear();
      args_.clear();
      return;
    }
    args_.push_back(std::move(arg));
  }
  text_ = name_ == "[]" ? "[" : name_ + '<';
  for (std::size_t index = 0; index < args_.size(); ++index) {
    if (index)
      text_ += ", ";
    text_ += args_[index].text();
  }
  text_ += close;
  valid_ = true;
}
bool Ty::empty() const noexcept { return text_.empty(); }
bool Ty::valid() const noexcept { return valid_; }
std::string_view Ty::text() const noexcept { return text_; }
std::string_view Ty::name() const noexcept { return name_; }
const std::vector<Ty>& Ty::args() const noexcept { return args_; }

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
std::vector<Op> Fn::ops() const {
  std::vector<Op> out;
  if (!valid() || !body())
    return out;
  const auto visit = [&](const auto& self, Blk block) -> void {
    for (Op op : block.ops()) {
      out.push_back(op);
      for (Blk child : op.blocks())
        self(self, child);
    }
  };
  visit(visit, body());
  return out;
}
Loc Fn::loc() const { return valid() ? store_->fns[id_].data.loc : Loc{}; }

Mod::Mod() : impl_(std::make_unique<Impl>()) {}
Mod::~Mod() = default;
Mod::Mod(Mod&&) noexcept = default;
Mod& Mod::operator=(Mod&&) noexcept = default;

std::string_view Mod::name() const noexcept { return impl_->store.name; }
void Mod::name(std::string name) {
  if (impl_->store.name == name)
    return;
  impl_->store.name = std::move(name);
  touch(impl_->store);
}
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

std::vector<Op> Mod::ops() const {
  std::vector<Op> out;
  for (Fn fn : fns()) {
    std::vector<Op> nested = fn.ops();
    out.insert(out.end(), nested.begin(), nested.end());
  }
  return out;
}

std::vector<Fn> Mod::find_fns(std::string_view name) const {
  std::vector<Fn> out;
  const auto found = impl_->store.symbols.find(std::string(name));
  if (found == impl_->store.symbols.end())
    return out;
  out.reserve(found->second.size());
  for (const std::uint32_t id : found->second)
    if (id < impl_->store.fns.size() && impl_->store.fns[id].live)
      out.push_back(Fn(&impl_->store, id, impl_->store.fns[id].generation));
  return out;
}

Fn Mod::find_fn(std::string_view name) const {
  const std::vector<Fn> matches = find_fns(name);
  return matches.size() == 1 ? matches.front() : Fn{};
}

std::uint64_t Mod::revision() const noexcept {
  return impl_->store.revision;
}

Op Mod::call(Op before, std::string callee, std::span<const Val> args,
             std::span<const Ty> types) {
  auto& store = impl_->store;
  if (!before.valid() || before.store_ != &store || callee.empty() ||
      std::any_of(types.begin(), types.end(),
                  [](const Ty& type) { return !type.valid(); })) {
    detail::add_diag(store.diags,
                     "call requires a live insertion point, callee, and valid "
                     "result types");
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
  detail::OpData op;
  op.kind = Op::Kind::call;
  op.block = block;
  op.callee = std::move(callee);
  op.form = types.empty() ? detail::Form::expr : detail::Form::hidden;
  op.loc = before.loc();
  op.args.reserve(args.size());
  for (Val arg : args)
    op.args.push_back(arg.id_);
  op.outs.reserve(types.size());
  for (std::size_t index = 0; index < types.size(); ++index) {
    const auto value_id = static_cast<std::uint32_t>(store.vals.size());
    detail::ValData value;
    value.type = types[index];
    value.def = op_id;
    value.index = index;
    value.type_annotation = types.size() > 1;
    store.vals.push_back({std::move(value), 1, true});
    op.outs.push_back(value_id);
  }
  store.ops.push_back({std::move(op), 1, true});
  order.insert(position, op_id);
  detail::rebuild_uses(store);
  touch(store);
  return Op(&store, op_id, store.ops[op_id].generation);
}

Val Mod::call(Op before, std::string callee, std::span<const Val> args,
              Ty type) {
  const Op op =
      call(before, std::move(callee), args, std::span<const Ty>(&type, 1));
  const std::vector<Val> outs = op.outs();
  return outs.size() == 1 ? outs.front() : Val{};
}

bool Mod::fuse(std::span<const Op> ops, std::string callee) {
  auto& store = impl_->store;
  auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return false;
  };
  if (ops.size() < 2 || callee.empty())
    return reject("fuse requires at least two calls and a callee");

  const Blk block = ops.front().block();
  if (!block || block.store_ != &store)
    return reject("fuse requires live calls in this module");
  const std::vector<Op> order = block.ops();
  std::unordered_set<std::uint32_t> selected;
  std::size_t previous = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const Op op = ops[index];
    if (!op.valid() || op.store_ != &store || op.block() != block ||
        op.kind() != Op::Kind::call || !selected.insert(op.id_).second)
      return reject("fuse requires distinct calls in one block", op.loc());
    const auto position = std::find(order.begin(), order.end(), op);
    if (position == order.end())
      return reject("fuse call is not in its block", op.loc());
    const auto at = static_cast<std::size_t>(position - order.begin());
    if (index && at <= previous)
      return reject("fuse calls must be in program order", op.loc());
    previous = at;
  }
  bool within = false;
  for (Op op : order) {
    if (op == ops.front())
      within = true;
    if (within && !selected.contains(op.id_) && op.kind() != Op::Kind::constant)
      return reject("fuse cannot cross an unselected operation", op.loc());
    if (op == ops.back())
      break;
  }

  std::vector<Val> inputs;
  std::unordered_set<std::uint32_t> input_ids;
  std::vector<Val> outputs;
  std::size_t external_uses = 0;
  for (Op op : ops) {
    for (Val arg : op.args()) {
      const Op def = arg.def();
      if ((!def || !selected.contains(def.id_)) &&
          input_ids.insert(arg.id_).second)
        inputs.push_back(arg);
    }
    for (Val out : op.outs()) {
      bool escapes = false;
      for (Op user : out.users()) {
        if (!selected.contains(user.id_)) {
          escapes = true;
          ++external_uses;
        }
      }
      if (escapes)
        outputs.push_back(out);
    }
  }
  if (outputs.size() != 1)
    return reject("fuse region must have exactly one live-out",
                  ops.front().loc());
  const Val output = outputs.front();
  for (Op user : output.users()) {
    if (selected.contains(user.id_))
      return reject("fuse live-out must leave the region only",
                    output.def().loc());
  }
  const auto old_form = store.ops[output.def().id_].data.form;
  if (old_form != detail::Form::hidden && old_form != detail::Form::let &&
      old_form != detail::Form::var)
    return reject("fuse live-out must be an expression value",
                  output.def().loc());
  if (external_uses > 1 && old_form == detail::Form::hidden)
    return reject("an unnamed fuse live-out cannot have multiple users",
                  output.def().loc());

  detail::Store backup = store;
  const auto rollback = [&]() {
    std::vector<Diag> diags = store.diags;
    store = backup;
    store.diags = std::move(diags);
    return false;
  };
  Val fused = call(ops.back(), std::move(callee), inputs, output.type());
  if (!fused)
    return rollback();
  store.vals[fused.id_].data.name = std::string(output.name());
  store.ops[fused.def().id_].data.form = old_form;
  store.ops[fused.def().id_].data.meta =
      store.ops[output.def().id_].data.meta;
  if (!replace(output, fused))
    return rollback();
  for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
    if (!erase(*it))
      return rollback();
  }
  store.revision = backup.revision;
  touch(store);
  return true;
}

bool Mod::replace(Val old_value, Val new_value) {
  auto& store = impl_->store;
  if (!old_value.valid() || !new_value.valid() || old_value.store_ != &store ||
      new_value.store_ != &store) {
    detail::add_diag(store.diags,
                     "replace requires two live values in this module");
    return false;
  }
  if (old_value == new_value)
    return true;
  const Ty old_type = old_value.type();
  const Ty new_type = new_value.type();
  if (old_type.text() != "_" && new_type.text() != "_" &&
      old_type != new_type) {
    detail::add_diag(store.diags, "replacement values have different types");
    return false;
  }
  const std::vector<Op> users = old_value.users();
  for (Op user : users) {
    if (!detail::dominates(store, new_value.id_, user.id_)) {
      detail::add_diag(store.diags,
                       "replacement value must dominate every selected use",
                       user.loc());
      return false;
    }
  }
  bool changed = false;
  for (auto& entry : store.ops) {
    if (!entry.live)
      continue;
    for (std::uint32_t& arg : entry.data.args) {
      if (arg == old_value.id_) {
        arg = new_value.id_;
        changed = true;
      }
    }
  }
  detail::rebuild_uses(store);
  if (changed)
    touch(store);
  return true;
}

bool Mod::replace(Val old_value, Val new_value, Op user) {
  auto& store = impl_->store;
  if (!old_value.valid() || !new_value.valid() || !user.valid() ||
      old_value.store_ != &store || new_value.store_ != &store ||
      user.store_ != &store) {
    detail::add_diag(
        store.diags,
        "selective replace requires live values and a live operation in this "
        "module");
    return false;
  }
  if (old_value == new_value)
    return true;
  const Ty old_type = old_value.type();
  const Ty new_type = new_value.type();
  if (old_type.text() != "_" && new_type.text() != "_" &&
      old_type != new_type) {
    detail::add_diag(store.diags, "replacement values have different types",
                     user.loc());
    return false;
  }
  if (!detail::dominates(store, new_value.id_, user.id_)) {
    detail::add_diag(store.diags,
                     "replacement value must dominate every selected use",
                     user.loc());
    return false;
  }
  bool changed = false;
  for (std::uint32_t& arg : store.ops[user.id_].data.args) {
    if (arg == old_value.id_) {
      arg = new_value.id_;
      changed = true;
    }
  }
  if (changed) {
    detail::rebuild_uses(store);
    touch(store);
  }
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
  touch(store);
  return true;
}

bool Mod::rename(Val value, std::string name) {
  auto& store = impl_->store;
  if (!value.valid() || value.store_ != &store || name.empty()) {
    detail::add_diag(store.diags,
                     "rename requires a live value and non-empty name");
    return false;
  }
  detail::ValData& data = store.vals[value.id_].data;
  const bool changed = data.name != name;
  data.name = std::move(name);
  if (data.def != detail::none) {
    detail::OpData& op = store.ops[data.def].data;
    if ((op.kind == Op::Kind::call || op.kind == Op::Kind::constant) &&
        op.form == detail::Form::hidden)
      op.form = detail::Form::let;
  }
  if (changed)
    touch(store);
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
  if (store.ops[call.id_].data.callee != callee) {
    store.ops[call.id_].data.callee = std::move(callee);
    touch(store);
  }
  return true;
}

bool Mod::set(Fn fn, std::string key, Attr value) {
  auto& store = impl_->store;
  if (!fn.valid() || fn.store_ != &store || key.empty()) {
    detail::add_diag(store.diags,
                     "set requires a live function and non-empty key");
    return false;
  }
  Attr::Dict& meta = store.fns[fn.id_].data.meta;
  const auto found = meta.find(key);
  if (found == meta.end() || found->second != value) {
    meta[std::move(key)] = std::move(value);
    touch(store);
  }
  return true;
}

bool Mod::set(Op op, std::string key, Attr value) {
  auto& store = impl_->store;
  if (!op.valid() || op.store_ != &store || key.empty()) {
    detail::add_diag(store.diags,
                     "set requires a live operation and non-empty key");
    return false;
  }
  const detail::OpData& data = store.ops[op.id_].data;
  if ((data.kind == Op::Kind::call || data.kind == Op::Kind::constant) &&
      data.form == detail::Form::hidden) {
    detail::add_diag(store.diags,
                     "cannot annotate an operation nested in an expression",
                     data.loc);
    return false;
  }
  Attr::Dict& meta = store.ops[op.id_].data.meta;
  const auto found = meta.find(key);
  if (found == meta.end() || found->second != value) {
    meta[std::move(key)] = std::move(value);
    touch(store);
  }
  return true;
}

bool Mod::unset(Fn fn, std::string_view key) {
  auto& store = impl_->store;
  if (!fn.valid() || fn.store_ != &store || key.empty()) {
    detail::add_diag(store.diags,
                     "unset requires a live function and non-empty key");
    return false;
  }
  if (!store.fns[fn.id_].data.meta.erase(std::string(key)))
    return false;
  touch(store);
  return true;
}

bool Mod::unset(Op op, std::string_view key) {
  auto& store = impl_->store;
  if (!op.valid() || op.store_ != &store || key.empty()) {
    detail::add_diag(store.diags,
                     "unset requires a live operation and non-empty key");
    return false;
  }
  if (!store.ops[op.id_].data.meta.erase(std::string(key)))
    return false;
  touch(store);
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
