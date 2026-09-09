#include "detail.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>
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

bool valid_binding(std::string_view text) {
  if (text.empty() ||
      (!std::isalpha(static_cast<unsigned char>(text.front())) &&
       text.front() != '_'))
    return false;
  if (std::any_of(text.begin() + 1, text.end(), [](char ch) {
        return !std::isalnum(static_cast<unsigned char>(ch)) && ch != '_';
      }))
    return false;
  static constexpr std::string_view reserved[] = {
      "module", "use", "fn", "let", "var", "for", "in",
      "if", "else", "return", "true", "false", "nil"};
  for (const std::string_view word : reserved)
    if (text == word)
      return false;
  return true;
}

bool carried_arg(const detail::Store& store, std::uint32_t value) {
  for (const auto& slot : store.blocks) {
    if (!slot.live || slot.data.parent_op == detail::none ||
        slot.data.parent_op >= store.ops.size() ||
        !store.ops[slot.data.parent_op].live)
      continue;
    const auto found =
        std::find(slot.data.args.begin(), slot.data.args.end(), value);
    if (found == slot.data.args.end())
      continue;
    const std::size_t index =
        static_cast<std::size_t>(found - slot.data.args.begin());
    const detail::OpData& parent = store.ops[slot.data.parent_op].data;
    if (parent.kind == Op::Kind::loop)
      return index >= parent.iter_names.size() &&
             index < parent.iter_names.size() + parent.carried_count;
    if (parent.kind == Op::Kind::branch)
      return index < parent.carried_count;
    return false;
  }
  return false;
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
    if (id < store_->blocks.size() && store_->blocks[id].live)
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
    if (id < store_->vals.size() && store_->vals[id].live)
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
    if (id < store_->blocks.size() && store_->blocks[id].live)
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

Val Mod::constant(Op before, Attr literal, Ty type) {
  auto& store = impl_->store;
  if (!before.valid() || before.store_ != &store || !type.valid()) {
    detail::add_diag(
        store.diags,
        "constant requires a live insertion point and a valid result type");
    return {};
  }
  const std::uint32_t block = store.ops[before.id_].data.block;
  auto& order = store.blocks[block].data.ops;
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end()) {
    detail::add_diag(store.diags,
                     "constant insertion point is not in its block",
                     before.loc());
    return {};
  }

  const auto op_id = static_cast<std::uint32_t>(store.ops.size());
  const auto value_id = static_cast<std::uint32_t>(store.vals.size());
  detail::ValData value;
  value.type = std::move(type);
  value.def = op_id;
  value.type_annotation = true;
  detail::OpData op;
  op.kind = Op::Kind::constant;
  op.block = block;
  op.literal = std::move(literal);
  op.outs.push_back(value_id);
  op.loc = before.loc();
  store.vals.push_back({std::move(value), 1, true});
  store.ops.push_back({std::move(op), 1, true});
  order.insert(position, op_id);
  touch(store);
  return Val(&store, value_id, store.vals[value_id].generation);
}

Op Mod::loop(Op before, std::span<const std::string> names,
             std::span<const Val> sources, std::span<const Val> carried) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message) {
    detail::add_diag(store.diags, std::move(message), before.loc());
    return Op{};
  };
  if (!before.valid() || before.store_ != &store || names.empty() ||
      names.size() != sources.size())
    return reject("loop requires an insertion point and one name per source");
  std::unordered_set<std::string> unique;
  for (const std::string& name : names)
    if (!valid_binding(name) || !unique.insert(name).second)
      return reject("loop variable names must be valid and distinct");
  std::vector<Val> inputs(sources.begin(), sources.end());
  inputs.insert(inputs.end(), carried.begin(), carried.end());
  for (Val value : inputs)
    if (!value.valid() || value.store_ != &store ||
        !detail::dominates(store, value.id_, before.id_))
      return reject("loop inputs must dominate the insertion point");
  const std::uint32_t parent = store.ops[before.id_].data.block;
  auto& parent_ops = store.blocks[parent].data.ops;
  const auto position = std::find(parent_ops.begin(), parent_ops.end(),
                                  before.id_);
  if (position == parent_ops.end())
    return reject("loop insertion point is not in its block");
  for (Val value : carried) {
    if (value.name().empty())
      return reject("loop-carried values must be named local bindings");
    const Op definition = value.def();
    if (!definition && !carried_arg(store, value.id_))
      return reject("loop-carried values must be mutable local bindings");
    if (definition) {
      detail::OpData& def = store.ops[definition.id_].data;
      if ((def.kind == Op::Kind::call || def.kind == Op::Kind::constant) &&
          def.form == detail::Form::let)
        def.form = detail::Form::var;
    }
  }

  detail::OpData data;
  data.kind = Op::Kind::loop;
  data.block = parent;
  data.iter_names.assign(names.begin(), names.end());
  data.carried_count = carried.size();
  data.loc = before.loc();
  for (Val value : inputs)
    data.args.push_back(value.id_);
  const auto op_id = static_cast<std::uint32_t>(store.ops.size());
  store.ops.push_back({std::move(data), 1, true});
  parent_ops.insert(position, op_id);

  for (Val value : carried) {
    detail::ValData result;
    result.name = std::string(value.name());
    result.type = value.type();
    result.def = op_id;
    result.index = store.ops[op_id].data.outs.size();
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(result), 1, true});
    store.ops[op_id].data.outs.push_back(id);
  }

  detail::BlkData body;
  body.fn = store.blocks[parent].data.fn;
  body.parent_op = op_id;
  const auto block_id = static_cast<std::uint32_t>(store.blocks.size());
  store.blocks.push_back({std::move(body), 1, true});
  store.fns[store.blocks[parent].data.fn].data.blocks.push_back(block_id);
  store.ops[op_id].data.blocks.push_back(block_id);
  for (const std::string& name : names) {
    detail::ValData arg;
    arg.kind = detail::ValKind::block_arg;
    arg.name = name;
    arg.type = Ty("index");
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(arg), 1, true});
    store.blocks[block_id].data.args.push_back(id);
  }
  std::vector<std::uint32_t> yielded;
  for (Val value : carried) {
    detail::ValData arg;
    arg.kind = detail::ValKind::block_arg;
    arg.name = std::string(value.name());
    arg.type = value.type();
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(arg), 1, true});
    store.blocks[block_id].data.args.push_back(id);
    yielded.push_back(id);
  }
  detail::OpData yield;
  yield.kind = Op::Kind::yield;
  yield.block = block_id;
  yield.args = std::move(yielded);
  yield.loc = before.loc();
  const auto yield_id = static_cast<std::uint32_t>(store.ops.size());
  store.ops.push_back({std::move(yield), 1, true});
  store.blocks[block_id].data.ops.push_back(yield_id);
  detail::rebuild_uses(store);
  touch(store);
  return Op(&store, op_id, store.ops[op_id].generation);
}

Op Mod::branch(Op before, Val condition, std::span<const Val> carried) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message) {
    detail::add_diag(store.diags, std::move(message), before.loc());
    return Op{};
  };
  if (!before.valid() || before.store_ != &store || !condition.valid() ||
      condition.store_ != &store)
    return reject("branch requires an insertion point and condition");
  if (condition.type().text() != "_" && condition.type().text() != "bool")
    return reject("branch condition must have type bool");
  std::vector<Val> inputs{condition};
  inputs.insert(inputs.end(), carried.begin(), carried.end());
  for (Val value : inputs)
    if (!value.valid() || value.store_ != &store ||
        !detail::dominates(store, value.id_, before.id_))
      return reject("branch inputs must dominate the insertion point");
  const std::uint32_t parent = store.ops[before.id_].data.block;
  auto& parent_ops = store.blocks[parent].data.ops;
  const auto position = std::find(parent_ops.begin(), parent_ops.end(),
                                  before.id_);
  if (position == parent_ops.end())
    return reject("branch insertion point is not in its block");
  for (Val value : carried) {
    if (value.name().empty())
      return reject("branch-carried values must be named local bindings");
    const Op definition = value.def();
    if (!definition && !carried_arg(store, value.id_))
      return reject("branch-carried values must be mutable local bindings");
    if (definition) {
      detail::OpData& def = store.ops[definition.id_].data;
      if ((def.kind == Op::Kind::call || def.kind == Op::Kind::constant) &&
          def.form == detail::Form::let)
        def.form = detail::Form::var;
    }
  }

  detail::OpData data;
  data.kind = Op::Kind::branch;
  data.block = parent;
  data.carried_count = carried.size();
  data.loc = before.loc();
  for (Val value : inputs)
    data.args.push_back(value.id_);
  const auto op_id = static_cast<std::uint32_t>(store.ops.size());
  store.ops.push_back({std::move(data), 1, true});
  parent_ops.insert(position, op_id);
  for (Val value : carried) {
    detail::ValData result;
    result.name = std::string(value.name());
    result.type = value.type();
    result.def = op_id;
    result.index = store.ops[op_id].data.outs.size();
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(result), 1, true});
    store.ops[op_id].data.outs.push_back(id);
  }

  const std::uint32_t fn = store.blocks[parent].data.fn;
  for (std::size_t arm = 0; arm < 2; ++arm) {
    detail::BlkData body;
    body.fn = fn;
    body.parent_op = op_id;
    const auto block_id = static_cast<std::uint32_t>(store.blocks.size());
    store.blocks.push_back({std::move(body), 1, true});
    store.fns[fn].data.blocks.push_back(block_id);
    store.ops[op_id].data.blocks.push_back(block_id);
    std::vector<std::uint32_t> yielded;
    for (Val value : carried) {
      detail::ValData arg;
      arg.kind = detail::ValKind::block_arg;
      arg.name = std::string(value.name());
      arg.type = value.type();
      const auto id = static_cast<std::uint32_t>(store.vals.size());
      store.vals.push_back({std::move(arg), 1, true});
      store.blocks[block_id].data.args.push_back(id);
      yielded.push_back(id);
    }
    detail::OpData yield;
    yield.kind = Op::Kind::yield;
    yield.block = block_id;
    yield.args = std::move(yielded);
    yield.loc = before.loc();
    const auto yield_id = static_cast<std::uint32_t>(store.ops.size());
    store.ops.push_back({std::move(yield), 1, true});
    store.blocks[block_id].data.ops.push_back(yield_id);
  }
  detail::rebuild_uses(store);
  touch(store);
  return Op(&store, op_id, store.ops[op_id].generation);
}

Op Mod::clone(Op source, Op before) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return Op{};
  };
  if (!source.valid() || !before.valid() || source.store_ != &store ||
      before.store_ != &store)
    return reject("clone requires live operations in this module");
  if (source.kind() == Op::Kind::ret || source.kind() == Op::Kind::yield)
    return reject("clone does not duplicate block terminators", source.loc());

  std::unordered_set<std::uint32_t> subtree_ops;
  std::unordered_set<std::uint32_t> subtree_values;
  const auto collect = [&](const auto& self, Op op) -> void {
    subtree_ops.insert(op.id_);
    for (Val value : op.outs())
      subtree_values.insert(value.id_);
    for (Blk block : op.blocks()) {
      for (Val value : block.args())
        subtree_values.insert(value.id_);
      for (Op child : block.ops())
        self(self, child);
    }
  };
  collect(collect, source);
  if (before != source && subtree_ops.contains(before.id_))
    return reject("clone insertion point cannot be inside the source",
                  before.loc());
  for (const std::uint32_t id : subtree_ops) {
    for (const std::uint32_t arg : store.ops[id].data.args) {
      if (!subtree_values.contains(arg) &&
          !detail::dominates(store, arg, before.id_))
        return reject("clone operand must dominate the insertion point",
                      before.loc());
    }
  }

  const std::uint32_t destination = store.ops[before.id_].data.block;
  const std::uint32_t fn = store.blocks[destination].data.fn;
  const auto insertion = std::find(store.blocks[destination].data.ops.begin(),
                                   store.blocks[destination].data.ops.end(),
                                   before.id_);
  if (insertion == store.blocks[destination].data.ops.end())
    return reject("clone insertion point is not in its block", before.loc());
  std::unordered_map<std::uint32_t, std::uint32_t> values;
  const auto copy_op = [&](const auto& self, std::uint32_t old_id,
                           std::uint32_t block) -> std::uint32_t {
    const detail::OpData old = store.ops[old_id].data;
    detail::OpData next = old;
    next.block = block;
    next.args.clear();
    next.outs.clear();
    next.blocks.clear();
    for (const std::uint32_t arg : old.args) {
      const auto mapped = values.find(arg);
      next.args.push_back(mapped == values.end() ? arg : mapped->second);
    }
    const auto next_id = static_cast<std::uint32_t>(store.ops.size());
    store.ops.push_back({std::move(next), 1, true});
    store.blocks[block].data.ops.push_back(next_id);

    for (const std::uint32_t old_value : old.outs) {
      detail::ValData value = store.vals[old_value].data;
      value.def = next_id;
      value.index = store.ops[next_id].data.outs.size();
      value.users.clear();
      const auto value_id = static_cast<std::uint32_t>(store.vals.size());
      store.vals.push_back({std::move(value), 1, true});
      store.ops[next_id].data.outs.push_back(value_id);
      values.emplace(old_value, value_id);
    }

    for (const std::uint32_t old_block : old.blocks) {
      detail::BlkData body;
      body.fn = fn;
      body.parent_op = next_id;
      const auto body_id = static_cast<std::uint32_t>(store.blocks.size());
      store.blocks.push_back({std::move(body), 1, true});
      store.fns[fn].data.blocks.push_back(body_id);
      store.ops[next_id].data.blocks.push_back(body_id);
      const std::vector<std::uint32_t> old_args =
          store.blocks[old_block].data.args;
      const std::vector<std::uint32_t> old_ops =
          store.blocks[old_block].data.ops;
      for (const std::uint32_t old_arg : old_args) {
        detail::ValData value = store.vals[old_arg].data;
        value.users.clear();
        const auto value_id = static_cast<std::uint32_t>(store.vals.size());
        store.vals.push_back({std::move(value), 1, true});
        store.blocks[body_id].data.args.push_back(value_id);
        values.emplace(old_arg, value_id);
      }
      for (const std::uint32_t child : old_ops)
        self(self, child, body_id);
    }
    return next_id;
  };

  const std::uint32_t cloned_id = copy_op(copy_op, source.id_, destination);
  auto& order = store.blocks[destination].data.ops;
  order.pop_back();
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end())
    return reject("clone insertion point is not in its block", before.loc());
  order.insert(position, cloned_id);
  detail::rebuild_uses(store);
  touch(store);
  return Op(&store, cloned_id, store.ops[cloned_id].generation);
}

bool Mod::move(Op op, Op before) {
  auto& store = impl_->store;
  if (!op.valid() || !before.valid() || op.store_ != &store ||
      before.store_ != &store) {
    detail::add_diag(store.diags,
                     "move requires live operations in this module");
    return false;
  }
  if (op == before)
    return true;
  if (op.block() != before.block()) {
    detail::add_diag(store.diags,
                     "move currently requires one destination block",
                     before.loc());
    return false;
  }
  if (op.kind() == Op::Kind::ret || op.kind() == Op::Kind::yield ||
      before.kind() == Op::Kind::yield) {
    detail::add_diag(store.diags, "move cannot reorder a block terminator",
                     op.loc());
    return false;
  }
  auto& order = store.blocks[op.block().id_].data.ops;
  const std::vector<std::uint32_t> old_order = order;
  order.erase(std::remove(order.begin(), order.end(), op.id_), order.end());
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end()) {
    order = old_order;
    detail::add_diag(store.diags, "move insertion point is not in its block",
                     before.loc());
    return false;
  }
  order.insert(position, op.id_);
  for (std::uint32_t id = 0; id < store.ops.size(); ++id) {
    if (!store.ops[id].live)
      continue;
    for (const std::uint32_t arg : store.ops[id].data.args) {
      if (!detail::dominates(store, arg, id)) {
        order = old_order;
        detail::add_diag(store.diags,
                         "move would violate value dominance",
                         store.ops[id].data.loc);
        return false;
      }
    }
  }
  touch(store);
  return true;
}

bool Mod::args(Op op, std::span<const Val> values) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message) {
    detail::add_diag(store.diags, std::move(message), op.loc());
    return false;
  };
  if (!op.valid() || op.store_ != &store)
    return reject("args requires a live operation in this module");
  for (Val value : values)
    if (!value.valid() || value.store_ != &store ||
        !detail::dominates(store, value.id_, op.id_))
      return reject("operation arguments must dominate their use");

  const detail::OpData& data = store.ops[op.id_].data;
  std::size_t expected = values.size();
  if (data.kind == Op::Kind::constant)
    expected = 0;
  else if (data.kind == Op::Kind::loop)
    expected = data.iter_names.size() + data.carried_count;
  else if (data.kind == Op::Kind::branch)
    expected = 1 + data.carried_count;
  else if (data.kind == Op::Kind::ret) {
    const std::uint32_t block = data.block;
    const std::uint32_t fn = store.blocks[block].data.fn;
    expected = store.fns[fn].data.returns.size();
  } else if (data.kind == Op::Kind::yield) {
    const std::uint32_t block = data.block;
    const std::uint32_t parent = store.blocks[block].data.parent_op;
    expected = parent == detail::none ? 0 : store.ops[parent].data.carried_count;
  }
  if (values.size() != expected)
    return reject("operation argument count would break its structure");
  if (data.kind == Op::Kind::branch && !values.empty() &&
      values.front().type().text() != "_" &&
      values.front().type().text() != "bool")
    return reject("branch condition must have type bool");

  const auto compatible = [](const Ty& left, const Ty& right) {
    return left.text() == "_" || right.text() == "_" || left == right;
  };
  if (data.kind == Op::Kind::ret) {
    const std::uint32_t fn = store.blocks[data.block].data.fn;
    const std::vector<Ty>& returns = store.fns[fn].data.returns;
    for (std::size_t index = 0; index < values.size(); ++index)
      if (!compatible(values[index].type(), returns[index]))
        return reject("return argument type does not match its function");
  } else if (data.kind == Op::Kind::yield) {
    const std::uint32_t parent = store.blocks[data.block].data.parent_op;
    if (parent != detail::none) {
      const std::vector<std::uint32_t>& outs = store.ops[parent].data.outs;
      for (std::size_t index = 0; index < values.size(); ++index)
        if (!compatible(values[index].type(),
                        store.vals[outs[index]].data.type))
          return reject("yield argument type does not match its parent");
    }
  }

  std::vector<std::uint32_t> next;
  next.reserve(values.size());
  for (Val value : values)
    next.push_back(value.id_);
  if (next == data.args)
    return true;
  store.ops[op.id_].data.args = std::move(next);
  detail::rebuild_uses(store);
  touch(store);
  return true;
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
  if (op.kind() == Op::Kind::ret || op.kind() == Op::Kind::yield) {
    detail::add_diag(store.diags, "cannot erase a block terminator", op.loc());
    return false;
  }

  std::unordered_set<std::uint32_t> ops;
  std::unordered_set<std::uint32_t> blocks;
  std::unordered_set<std::uint32_t> values;
  const auto collect = [&](const auto& self, std::uint32_t id) -> void {
    ops.insert(id);
    for (const std::uint32_t value : store.ops[id].data.outs)
      values.insert(value);
    for (const std::uint32_t block : store.ops[id].data.blocks) {
      blocks.insert(block);
      for (const std::uint32_t value : store.blocks[block].data.args)
        values.insert(value);
      for (const std::uint32_t child : store.blocks[block].data.ops)
        self(self, child);
    }
  };
  collect(collect, op.id_);
  for (const std::uint32_t value : values) {
    for (const std::uint32_t user : store.vals[value].data.users) {
      if (!ops.contains(user)) {
        detail::add_diag(store.diags,
                         "cannot erase an operation with live external users",
                         store.ops[op.id_].data.loc);
        return false;
      }
    }
  }

  const std::uint32_t parent = store.ops[op.id_].data.block;
  if (parent != detail::none) {
    auto& order = store.blocks[parent].data.ops;
    const auto found = std::find(order.begin(), order.end(), op.id_);
    if (found == order.end()) {
      detail::add_diag(store.diags,
                       "erase operation is not in its parent block", op.loc());
      return false;
    }
    order.erase(found);
  }
  for (const std::uint32_t block : blocks) {
    const std::uint32_t fn = store.blocks[block].data.fn;
    if (fn < store.fns.size()) {
      auto& owned = store.fns[fn].data.blocks;
      owned.erase(std::remove(owned.begin(), owned.end(), block), owned.end());
    }
    store.blocks[block].live = false;
    ++store.blocks[block].generation;
  }
  for (const std::uint32_t value : values) {
    store.vals[value].live = false;
    ++store.vals[value].generation;
  }
  for (const std::uint32_t id : ops) {
    store.ops[id].live = false;
    ++store.ops[id].generation;
  }
  detail::rebuild_uses(store);
  touch(store);
  return true;
}

bool Mod::rename(Val value, std::string name) {
  auto& store = impl_->store;
  if (!value.valid() || value.store_ != &store || !valid_binding(name)) {
    detail::add_diag(store.diags,
                     "rename requires a live value and valid binding name");
    return false;
  }

  std::unordered_set<std::uint32_t> family{value.id_};
  bool expanded = true;
  while (expanded) {
    expanded = false;
    const auto connect = [&](std::span<const std::uint32_t> ids) {
      const bool related = std::any_of(ids.begin(), ids.end(),
                                       [&](std::uint32_t id) {
                                         return family.contains(id);
                                       });
      if (!related)
        return;
      for (const std::uint32_t id : ids)
        if (id < store.vals.size() && store.vals[id].live)
          expanded = family.insert(id).second || expanded;
    };
    for (const auto& slot : store.ops) {
      if (!slot.live || (slot.data.kind != Op::Kind::loop &&
                         slot.data.kind != Op::Kind::branch))
        continue;
      const detail::OpData& op = slot.data;
      const std::size_t offset =
          op.kind == Op::Kind::loop ? op.iter_names.size() : 1;
      if (op.args.size() < offset + op.carried_count ||
          op.outs.size() < op.carried_count)
        continue;
      for (std::size_t index = 0; index < op.carried_count; ++index) {
        std::vector<std::uint32_t> ids{op.args[offset + index],
                                       op.outs[index]};
        for (const std::uint32_t block : op.blocks) {
          if (block >= store.blocks.size() || !store.blocks[block].live)
            continue;
          const detail::BlkData& body = store.blocks[block].data;
          const std::size_t arg =
              op.kind == Op::Kind::loop ? offset + index : index;
          if (arg >= body.args.size())
            continue;
          ids.push_back(body.args[arg]);
          if (!body.ops.empty()) {
            const detail::OpData& end = store.ops[body.ops.back()].data;
            if (end.kind == Op::Kind::yield && index < end.args.size())
              ids.push_back(end.args[index]);
          }
        }
        connect(ids);
      }
    }
  }

  const auto conflicts = [&](std::span<const std::uint32_t> ids) {
    const bool owns = std::any_of(ids.begin(), ids.end(), [&](std::uint32_t id) {
      return family.contains(id);
    });
    return owns && std::any_of(ids.begin(), ids.end(), [&](std::uint32_t id) {
             return !family.contains(id) && id < store.vals.size() &&
                    store.vals[id].live && store.vals[id].data.name == name;
           });
  };
  for (const auto& slot : store.fns) {
    if (!slot.live)
      continue;
    std::vector<std::uint32_t> bindings = slot.data.generic_vals;
    bindings.insert(bindings.end(), slot.data.params.begin(),
                    slot.data.params.end());
    if (conflicts(bindings)) {
      detail::add_diag(store.diags,
                       "rename would duplicate a function binding");
      return false;
    }
  }
  for (const auto& slot : store.blocks)
    if (slot.live && conflicts(slot.data.args)) {
      detail::add_diag(store.diags, "rename would duplicate a block binding");
      return false;
    }

  bool changed = false;
  for (const std::uint32_t id : family) {
    detail::ValData& data = store.vals[id].data;
    changed = data.name != name || changed;
    data.name = name;
    if (data.def == detail::none)
      continue;
    detail::OpData& op = store.ops[data.def].data;
    if ((op.kind == Op::Kind::call || op.kind == Op::Kind::constant) &&
        op.form == detail::Form::hidden)
      op.form = detail::Form::let;
  }
  for (const auto& block_slot : store.blocks) {
    if (!block_slot.live || block_slot.data.parent_op == detail::none ||
        block_slot.data.parent_op >= store.ops.size() ||
        !store.ops[block_slot.data.parent_op].live)
      continue;
    detail::OpData& parent = store.ops[block_slot.data.parent_op].data;
    if (parent.kind != Op::Kind::loop)
      continue;
    const auto& args = block_slot.data.args;
    for (std::size_t index = 0;
         index < parent.iter_names.size() && index < args.size(); ++index) {
      if (!family.contains(args[index]))
        continue;
      changed = parent.iter_names[index] != name || changed;
      parent.iter_names[index] = name;
    }
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
