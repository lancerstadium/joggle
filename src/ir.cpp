#include "detail.h"

#include <algorithm>
#include <array>
#include <charconv>
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

bool valid_module(std::string_view text) {
  if (text.empty())
    return false;
  std::size_t begin = 0;
  while (begin < text.size()) {
    const std::size_t end = text.find('.', begin);
    const std::string_view part = text.substr(
        begin, end == std::string_view::npos ? text.size() - begin
                                             : end - begin);
    if (part.empty() ||
        (!std::isalpha(static_cast<unsigned char>(part.front())) &&
         part.front() != '_') ||
        std::any_of(part.begin() + 1, part.end(), [](char ch) {
          return !std::isalnum(static_cast<unsigned char>(ch)) && ch != '_';
        }))
      return false;
    if (end == std::string_view::npos)
      return true;
    begin = end + 1;
  }
  return false;
}

bool valid_callee(std::string_view text) {
  constexpr std::string_view prefix = "operator ";
  if (text.starts_with(prefix)) {
    const std::string_view spelling = text.substr(prefix.size());
    static constexpr std::string_view operators[] = {
        "||", "&&", "==", "!=", "<",  "<=", ">",  ">=", "|",
        "^",  "&",  "<<", ">>", "+",  "-",  "*",  "/",  "%",
        "!",  "~",  "..", "[]", "[]="};
    return std::find(std::begin(operators), std::end(operators), spelling) !=
           std::end(operators);
  }
  const Ty applied{std::string(text)};
  if (!applied.valid())
    return false;
  return valid_module(applied.name());
}

std::optional<std::size_t>
generic_index(const std::vector<std::string>& generics, std::string_view name) {
  const auto found = std::find(generics.begin(), generics.end(), name);
  return found == generics.end()
             ? std::nullopt
             : std::optional<std::size_t>(
                   static_cast<std::size_t>(found - generics.begin()));
}

bool carried_arg(const detail::Store& store, std::uint32_t value) {
  for (const auto& slot : store.blks) {
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

class ValFamilies {
public:
  explicit ValFamilies(const detail::Store& store)
      : store_(store), parents_(store.vals.size()), ranks_(store.vals.size()) {
    for (std::uint32_t id = 0; id < parents_.size(); ++id)
      parents_[id] = id;
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
        const std::uint32_t seed = op.args[offset + index];
        join(seed, op.outs[index]);
        for (const std::uint32_t blk : op.blks) {
          if (blk >= store.blks.size() || !store.blks[blk].live)
            continue;
          const detail::BlkData& body = store.blks[blk].data;
          const std::size_t arg =
              op.kind == Op::Kind::loop ? offset + index : index;
          if (arg < body.args.size())
            join(seed, body.args[arg]);
          if (!body.ops.empty()) {
            const detail::OpData& end = store.ops[body.ops.back()].data;
            if (end.kind == Op::Kind::yield && index < end.args.size())
              join(seed, end.args[index]);
          }
        }
      }
    }
  }

  std::uint32_t root(std::uint32_t id) {
    while (parents_[id] != id) {
      parents_[id] = parents_[parents_[id]];
      id = parents_[id];
    }
    return id;
  }

  std::unordered_set<std::uint32_t> members(std::uint32_t seed) {
    std::unordered_set<std::uint32_t> out;
    const std::uint32_t group = root(seed);
    for (std::uint32_t id = 0; id < store_.vals.size(); ++id)
      if (store_.vals[id].live && root(id) == group)
        out.insert(id);
    return out;
  }

private:
  void join(std::uint32_t left, std::uint32_t right) {
    if (left >= store_.vals.size() || right >= store_.vals.size() ||
        !store_.vals[left].live || !store_.vals[right].live)
      return;
    left = root(left);
    right = root(right);
    if (left == right)
      return;
    if (ranks_[left] < ranks_[right])
      std::swap(left, right);
    parents_[right] = left;
    if (ranks_[left] == ranks_[right])
      ++ranks_[left];
  }

  const detail::Store& store_;
  std::vector<std::uint32_t> parents_;
  std::vector<std::uint8_t> ranks_;
};

std::unordered_set<std::uint32_t> family(const detail::Store& store,
                                         std::uint32_t seed) {
  ValFamilies families(store);
  return families.members(seed);
}

bool printable_value(const detail::Store& store,
                     const std::unordered_set<std::uint32_t>& values) {
  for (const std::uint32_t id : values) {
    if (id >= store.vals.size() || !store.vals[id].live)
      continue;
    const detail::ValData& value = store.vals[id].data;
    if (value.kind == detail::ValKind::generic ||
        value.kind == detail::ValKind::param)
      return true;
    if (value.kind != detail::ValKind::result || value.name.empty() ||
        value.def == detail::none || value.def >= store.ops.size() ||
        !store.ops[value.def].live)
      continue;
    const Op::Form form = store.ops[value.def].data.form;
    if (form == Op::Form::let || form == Op::Form::var)
      return true;
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

using Bindings = std::unordered_map<std::string, Ty>;

Ty substitute(const Ty& type, const Bindings& bindings) {
  if (type.args().empty()) {
    const auto found = bindings.find(std::string(type.name()));
    return found == bindings.end() ? type : found->second;
  }
  std::vector<Ty> args;
  args.reserve(type.args().size());
  for (const Ty& arg : type.args())
    args.push_back(substitute(arg, bindings));
  return Ty(std::string(type.name()), args);
}

std::optional<std::int64_t> integer(const Ty& value) {
  const std::string_view text = value.text();
  std::int64_t result = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), result);
  return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size()
             ? std::optional<std::int64_t>(result)
             : std::nullopt;
}

bool concrete_term(const Ty& value) {
  if (!value.valid() || value.name() == "_")
    return false;
  return std::all_of(value.args().begin(), value.args().end(), concrete_term);
}

bool sized_integer_term(std::string_view name) {
  return name.size() > 1 && (name.front() == 'i' || name.front() == 'u') &&
         std::all_of(name.begin() + 1, name.end(), [](char ch) {
           return std::isdigit(static_cast<unsigned char>(ch));
         });
}

Ty generic_kind(const Ty& value) {
  if (integer(value))
    return Ty("int");
  if (value.text() == "true" || value.text() == "false")
    return Ty("bool");
  if (value.name() != "[]")
    return Ty("Ty");
  if (value.args().empty())
    return Ty("list<_>");
  Ty element = generic_kind(value.args().front());
  for (std::size_t index = 1; index < value.args().size(); ++index)
    if (generic_kind(value.args()[index]) != element)
      element = Ty("_");
  const std::array<Ty, 1> args{element};
  return Ty("list", args);
}

bool same_signature(const detail::Store& store,
                    const detail::FnData& candidate,
                    std::span<const Ty> params,
                    const std::vector<std::string>& generics) {
  if (candidate.generic_vals.size() != generics.size() ||
      candidate.params.size() != params.size())
    return false;
  std::vector<std::string> candidate_generics;
  candidate_generics.reserve(candidate.generic_vals.size());
  for (const std::uint32_t generic : candidate.generic_vals)
    candidate_generics.push_back(store.vals[generic].data.name);
  for (std::size_t index = 0; index < params.size(); ++index)
    if (!detail::same_type_pattern(
            store.vals[candidate.params[index]].data.type,
            candidate_generics, params[index], generics))
      return false;
  return true;
}

}  // namespace

bool detail::literal_matches(const Attr& value, const Ty& type) {
  const std::string_view name = type.name();
  if (name == "_" || name == "Attr" || name == "meta")
    return true;
  if (name == "nil")
    return value.empty();
  if (name == "bool")
    return value.boolean().has_value();
  if (name == "int" || name == "index" ||
      (name.size() > 1 && (name.front() == 'i' || name.front() == 'u') &&
       std::all_of(name.begin() + 1, name.end(), [](char ch) {
         return std::isdigit(static_cast<unsigned char>(ch));
       })))
    return value.integer().has_value();
  if (name == "f16" || name == "f32" || name == "f64")
    return value.real().has_value();
  if (name == "str")
    return value.string().has_value();
  if (name == "bytes")
    return value.bytes() != nullptr;
  if (name == "dict")
    return value.dict() != nullptr;
  if (name == "list") {
    const Attr::List* items = value.list();
    if (!items || type.args().size() > 1)
      return false;
    if (type.args().empty())
      return true;
    return std::all_of(items->begin(), items->end(), [&](const Attr& item) {
      return literal_matches(item, type.args().front());
    });
  }
  if (name == "range" || name == "Ty" || name == "Mod" || name == "Fn" ||
      name == "Blk" || name == "Op" || name == "Val")
    return false;
  return true;
}

bool detail::same_type_pattern(
    const Ty& left, const std::vector<std::string>& left_generics,
    const Ty& right, const std::vector<std::string>& right_generics) {
  const auto left_generic = generic_index(left_generics, left.name());
  const auto right_generic = generic_index(right_generics, right.name());
  if (left.args().empty() && right.args().empty() &&
      (left_generic || right_generic))
    return left_generic == right_generic;
  if (left.name() != right.name() || left.args().size() != right.args().size())
    return false;
  for (std::size_t index = 0; index < left.args().size(); ++index)
    if (!same_type_pattern(left.args()[index], left_generics,
                           right.args()[index], right_generics))
      return false;
  return true;
}

void detail::touch(Store& store) {
  if (store.revision != std::numeric_limits<std::uint64_t>::max())
    ++store.revision;
  store.queries.clear();
}

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

Ty::Ty(std::string name, std::span<const Ty> args) {
  const std::string_view constructor = trim(name);
  if ((constructor != "[]" &&
       (!valid_atom(constructor) || args.empty())) ||
      std::any_of(args.begin(), args.end(),
                  [](const Ty& arg) { return !arg.valid(); }))
    return;
  name_ = std::string(constructor);
  args_.assign(args.begin(), args.end());
  text_ = name_ == "[]" ? "[" : name_ + '<';
  for (std::size_t index = 0; index < args_.size(); ++index) {
    if (index)
      text_ += ", ";
    text_ += args_[index].text();
  }
  text_ += name_ == "[]" ? ']' : '>';
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

Mod::Mod() : impl_(std::make_unique<Impl>()) {}
Mod::~Mod() = default;
Mod::Mod(Mod&&) noexcept = default;
Mod& Mod::operator=(Mod&&) noexcept = default;

std::string_view Mod::name() const noexcept { return impl_->store.name; }
std::vector<std::string> Mod::uses() const { return impl_->store.uses; }

bool Mod::use(const Env& env, std::string module) {
  auto& store = impl_->store;
  if (!valid_module(module) || module == store.name) {
    detail::add_diag(store.diags,
                     "use requires a valid, different module name");
    return false;
  }
  if (!env.loaded(module)) {
    detail::add_diag(store.diags,
                     "use requires the dependency module to be loaded");
    return false;
  }
  if (env.reaches(module, store.name)) {
    detail::add_diag(store.diags,
                     "use would create a module dependency cycle");
    return false;
  }
  if (std::find(store.uses.begin(), store.uses.end(), module) !=
      store.uses.end())
    return true;
  store.uses.push_back(std::move(module));
  touch(store);
  return true;
}

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

std::vector<Val> Mod::vals() const {
  std::vector<Val> out;
  for (Fn fn : fns()) {
    std::vector<Val> nested = fn.vals();
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
  if (!before.valid() || before.store_ != &store || !valid_callee(callee) ||
      std::any_of(types.begin(), types.end(),
                  [](const Ty& type) { return !type.valid(); })) {
    detail::add_diag(store.diags,
                     "call requires a live insertion point, valid callee, and "
                     "valid result types");
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

  const std::uint32_t blk = store.ops[before.id_].data.blk;
  auto& order = store.blks[blk].data.ops;
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end()) {
    detail::add_diag(store.diags, "call insertion point is not in its Blk",
                     before.loc());
    return {};
  }

  const auto op_id = static_cast<std::uint32_t>(store.ops.size());
  detail::OpData op;
  op.kind = Op::Kind::call;
  op.blk = blk;
  op.callee = std::move(callee);
  op.form = types.empty() ? Op::Form::expr : Op::Form::hidden;
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
    value.type_annotation = types.size() > 1 || types[index].text() != "_";
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
  if (!detail::literal_matches(literal, type)) {
    detail::add_diag(store.diags,
                     "constant literal does not match result type '" +
                         std::string(type.text()) + "'",
                     before.loc());
    return {};
  }
  const std::uint32_t blk = store.ops[before.id_].data.blk;
  auto& order = store.blks[blk].data.ops;
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end()) {
    detail::add_diag(store.diags,
                     "constant insertion point is not in its Blk",
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
  op.blk = blk;
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
  const std::uint32_t parent = store.ops[before.id_].data.blk;
  auto& parent_ops = store.blks[parent].data.ops;
  const auto position = std::find(parent_ops.begin(), parent_ops.end(),
                                  before.id_);
  if (position == parent_ops.end())
    return reject("loop insertion point is not in its Blk");
  for (Val value : carried) {
    if (value.name().empty())
      return reject("loop-carried values must be named local bindings");
    const Op definition = value.def();
    if (!definition && !carried_arg(store, value.id_))
      return reject("loop-carried values must be mutable local bindings");
  }
  for (Val value : carried) {
    const Op definition = value.def();
    if (definition) {
      detail::OpData& def = store.ops[definition.id_].data;
      if ((def.kind == Op::Kind::call || def.kind == Op::Kind::constant) &&
          def.form == Op::Form::let)
        def.form = Op::Form::var;
    }
  }

  detail::OpData data;
  data.kind = Op::Kind::loop;
  data.blk = parent;
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
    result.meta = value.meta();
    result.def = op_id;
    result.index = store.ops[op_id].data.outs.size();
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(result), 1, true});
    store.ops[op_id].data.outs.push_back(id);
  }

  detail::BlkData body;
  body.fn = store.blks[parent].data.fn;
  body.parent_op = op_id;
  const auto blk_id = static_cast<std::uint32_t>(store.blks.size());
  store.blks.push_back({std::move(body), 1, true});
  store.fns[store.blks[parent].data.fn].data.blks.push_back(blk_id);
  store.ops[op_id].data.blks.push_back(blk_id);
  for (const std::string& name : names) {
    detail::ValData arg;
    arg.kind = detail::ValKind::blk_arg;
    arg.name = name;
    arg.type = Ty("index");
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(arg), 1, true});
    store.blks[blk_id].data.args.push_back(id);
  }
  std::vector<std::uint32_t> yielded;
  for (Val value : carried) {
    detail::ValData arg;
    arg.kind = detail::ValKind::blk_arg;
    arg.name = std::string(value.name());
    arg.type = value.type();
    arg.meta = value.meta();
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(arg), 1, true});
    store.blks[blk_id].data.args.push_back(id);
    yielded.push_back(id);
  }
  detail::OpData yield;
  yield.kind = Op::Kind::yield;
  yield.blk = blk_id;
  yield.args = std::move(yielded);
  yield.loc = before.loc();
  const auto yield_id = static_cast<std::uint32_t>(store.ops.size());
  store.ops.push_back({std::move(yield), 1, true});
  store.blks[blk_id].data.ops.push_back(yield_id);
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
  const std::uint32_t parent = store.ops[before.id_].data.blk;
  auto& parent_ops = store.blks[parent].data.ops;
  const auto position = std::find(parent_ops.begin(), parent_ops.end(),
                                  before.id_);
  if (position == parent_ops.end())
    return reject("branch insertion point is not in its Blk");
  for (Val value : carried) {
    if (value.name().empty())
      return reject("branch-carried values must be named local bindings");
    const Op definition = value.def();
    if (!definition && !carried_arg(store, value.id_))
      return reject("branch-carried values must be mutable local bindings");
  }
  for (Val value : carried) {
    const Op definition = value.def();
    if (definition) {
      detail::OpData& def = store.ops[definition.id_].data;
      if ((def.kind == Op::Kind::call || def.kind == Op::Kind::constant) &&
          def.form == Op::Form::let)
        def.form = Op::Form::var;
    }
  }

  detail::OpData data;
  data.kind = Op::Kind::branch;
  data.blk = parent;
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
    result.meta = value.meta();
    result.def = op_id;
    result.index = store.ops[op_id].data.outs.size();
    const auto id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(result), 1, true});
    store.ops[op_id].data.outs.push_back(id);
  }

  const std::uint32_t fn = store.blks[parent].data.fn;
  for (std::size_t arm = 0; arm < 2; ++arm) {
    detail::BlkData body;
    body.fn = fn;
    body.parent_op = op_id;
    const auto blk_id = static_cast<std::uint32_t>(store.blks.size());
    store.blks.push_back({std::move(body), 1, true});
    store.fns[fn].data.blks.push_back(blk_id);
    store.ops[op_id].data.blks.push_back(blk_id);
    std::vector<std::uint32_t> yielded;
    for (Val value : carried) {
      detail::ValData arg;
      arg.kind = detail::ValKind::blk_arg;
      arg.name = std::string(value.name());
      arg.type = value.type();
      arg.meta = value.meta();
      const auto id = static_cast<std::uint32_t>(store.vals.size());
      store.vals.push_back({std::move(arg), 1, true});
      store.blks[blk_id].data.args.push_back(id);
      yielded.push_back(id);
    }
    detail::OpData yield;
    yield.kind = Op::Kind::yield;
    yield.blk = blk_id;
    yield.args = std::move(yielded);
    yield.loc = before.loc();
    const auto yield_id = static_cast<std::uint32_t>(store.ops.size());
    store.ops.push_back({std::move(yield), 1, true});
    store.blks[blk_id].data.ops.push_back(yield_id);
  }
  detail::rebuild_uses(store);
  touch(store);
  return Op(&store, op_id, store.ops[op_id].generation);
}

Op Mod::clone(Op source, Op before) {
  return clone(source, before, std::span<const Val>{},
               std::span<const Val>{});
}

Op Mod::clone(Op source, Op before, std::span<const Val> old_values,
              std::span<const Val> new_values) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return Op{};
  };
  if (!source.valid() || !before.valid() || source.store_ != &store ||
      before.store_ != &store)
    return reject("clone requires live operations in this module");
  if (source.kind() == Op::Kind::ret || source.kind() == Op::Kind::yield)
    return reject("clone does not duplicate Blk terminators", source.loc());
  if (old_values.size() != new_values.size())
    return reject("clone requires one new value per old value", source.loc());

  std::unordered_set<std::uint32_t> subtree_ops;
  std::unordered_set<std::uint32_t> subtree_values;
  const auto collect = [&](const auto& self, Op op) -> void {
    subtree_ops.insert(op.id_);
    for (Val value : op.outs())
      subtree_values.insert(value.id_);
    for (Blk blk : op.blks()) {
      for (Val value : blk.args())
        subtree_values.insert(value.id_);
      for (Op child : blk.ops())
        self(self, child);
    }
  };
  collect(collect, source);
  if (before != source && subtree_ops.contains(before.id_))
    return reject("clone insertion point cannot be inside the source",
                  before.loc());

  std::unordered_map<std::uint32_t, std::uint32_t> substitutions;
  substitutions.reserve(old_values.size());
  for (std::size_t index = 0; index < old_values.size(); ++index) {
    const Val old_value = old_values[index];
    const Val new_value = new_values[index];
    if (!old_value.valid() || !new_value.valid() ||
        old_value.store_ != &store || new_value.store_ != &store)
      return reject("clone substitutions require live values in this module",
                    source.loc());
    if (subtree_values.contains(old_value.id_))
      return reject("clone cannot substitute a value defined by the source",
                    source.loc());
    if (old_value.type() != new_value.type())
      return reject("clone substitutions require identical value types",
                    source.loc());
    if (old_value == new_value)
      continue;
    const auto [found, inserted] =
        substitutions.emplace(old_value.id_, new_value.id_);
    if (!inserted && found->second != new_value.id_)
      return reject("clone has conflicting substitutions for one value",
                    source.loc());
  }

  std::unordered_set<std::uint32_t> external_values;
  for (const std::uint32_t id : subtree_ops) {
    for (const std::uint32_t arg : store.ops[id].data.args) {
      if (subtree_values.contains(arg))
        continue;
      external_values.insert(arg);
      const auto mapped = substitutions.find(arg);
      const std::uint32_t value =
          mapped == substitutions.end() ? arg : mapped->second;
      if (!detail::dominates(store, value, before.id_))
        return reject("clone operand must dominate the insertion point",
                      before.loc());
    }
  }
  for (const auto& substitution : substitutions)
    if (!external_values.contains(substitution.first))
      return reject("clone can only substitute values captured by the source",
                    source.loc());

  const std::uint32_t destination = store.ops[before.id_].data.blk;
  const std::uint32_t fn = store.blks[destination].data.fn;
  const auto insertion = std::find(store.blks[destination].data.ops.begin(),
                                   store.blks[destination].data.ops.end(),
                                   before.id_);
  if (insertion == store.blks[destination].data.ops.end())
    return reject("clone insertion point is not in its Blk", before.loc());
  std::unordered_set<std::string> used_names;
  for (const auto& slot : store.vals)
    if (slot.live && !slot.data.name.empty())
      used_names.insert(slot.data.name);
  std::unordered_map<std::string, std::string> copied_names;
  const auto copy_name = [&](std::string_view source_name) {
    if (source_name.empty())
      return std::string{};
    const auto found = copied_names.find(std::string(source_name));
    if (found != copied_names.end())
      return found->second;
    std::string candidate(source_name);
    for (std::size_t suffix = 1; used_names.contains(candidate); ++suffix)
      candidate = std::string(source_name) + '_' + std::to_string(suffix);
    used_names.insert(candidate);
    copied_names.emplace(std::string(source_name), candidate);
    return candidate;
  };
  std::unordered_map<std::uint32_t, std::uint32_t> values = substitutions;
  const auto copy_op = [&](const auto& self, std::uint32_t old_id,
                           std::uint32_t blk) -> std::uint32_t {
    const detail::OpData old = store.ops[old_id].data;
    detail::OpData next = old;
    next.blk = blk;
    next.args.clear();
    next.outs.clear();
    next.blks.clear();
    for (const std::uint32_t arg : old.args) {
      const auto mapped = values.find(arg);
      next.args.push_back(mapped == values.end() ? arg : mapped->second);
    }
    const auto next_id = static_cast<std::uint32_t>(store.ops.size());
    store.ops.push_back({std::move(next), 1, true});
    store.blks[blk].data.ops.push_back(next_id);

    for (const std::uint32_t old_value : old.outs) {
      detail::ValData value = store.vals[old_value].data;
      if (old_id == source.id_ &&
          (old.form == Op::Form::let || old.form == Op::Form::var))
        value.name = copy_name(value.name);
      value.def = next_id;
      value.index = store.ops[next_id].data.outs.size();
      value.users.clear();
      const auto value_id = static_cast<std::uint32_t>(store.vals.size());
      store.vals.push_back({std::move(value), 1, true});
      store.ops[next_id].data.outs.push_back(value_id);
      values.emplace(old_value, value_id);
    }

    for (const std::uint32_t old_blk : old.blks) {
      detail::BlkData body;
      body.fn = fn;
      body.parent_op = next_id;
      const auto body_id = static_cast<std::uint32_t>(store.blks.size());
      store.blks.push_back({std::move(body), 1, true});
      store.fns[fn].data.blks.push_back(body_id);
      store.ops[next_id].data.blks.push_back(body_id);
      const std::vector<std::uint32_t> old_args =
          store.blks[old_blk].data.args;
      const std::vector<std::uint32_t> old_ops =
          store.blks[old_blk].data.ops;
      for (const std::uint32_t old_arg : old_args) {
        detail::ValData value = store.vals[old_arg].data;
        value.users.clear();
        const auto value_id = static_cast<std::uint32_t>(store.vals.size());
        store.vals.push_back({std::move(value), 1, true});
        store.blks[body_id].data.args.push_back(value_id);
        values.emplace(old_arg, value_id);
      }
      for (const std::uint32_t child : old_ops)
        self(self, child, body_id);
    }
    return next_id;
  };

  const std::uint32_t cloned_id = copy_op(copy_op, source.id_, destination);
  auto& order = store.blks[destination].data.ops;
  order.pop_back();
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end())
    return reject("clone insertion point is not in its Blk", before.loc());
  order.insert(position, cloned_id);
  detail::rebuild_uses(store);
  touch(store);
  return Op(&store, cloned_id, store.ops[cloned_id].generation);
}

void Mod::infer(const Env& env, std::uint32_t id) {
  auto& store = impl_->store;
  if (id >= store.ops.size() || store.ops[id].data.kind != Op::Kind::call)
    return;
  const Op op(&store, id, store.ops[id].generation);
  std::vector<Ty> returns;
  if (!env.resolve(*this, op, op.callee(), op.args(), &returns) ||
      returns.size() != op.outs().size())
    return;
  const std::vector<Val> outputs = op.outs();
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    Ty& type = store.vals[outputs[index].id_].data.type;
    if (type.text() == "_" && returns[index].text() != "_")
      type = returns[index];
  }
}

Fn Mod::clone(const Env& env, Fn source_fn, std::string name,
              std::span<const Ty> generic_args) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return Fn{};
  };
  if (!source_fn.valid())
    return reject("function clone requires a live source function");
  if (!valid_module(store.name))
    return reject("function clone requires a named destination module",
                  source_fn.loc());
  if (!valid_binding(name))
    return reject("function clone requires a valid local function name",
                  source_fn.loc());
  if (source_fn.external() || !source_fn.body())
    return reject("function clone requires a source body", source_fn.loc());
  if (source_fn.store_ != &store && source_fn.module() == store.name)
    return reject("function clone cannot merge distinct modules with one name",
                  source_fn.loc());

  const detail::Store& initial_source = *source_fn.store_;
  const detail::FnData& initial_fn = initial_source.fns[source_fn.id_].data;
  std::vector<std::string> source_generics;
  source_generics.reserve(initial_fn.generic_vals.size());
  for (const std::uint32_t id : initial_fn.generic_vals)
    source_generics.push_back(initial_source.vals[id].data.name);
  const bool specialized = !generic_args.empty();
  if (specialized && generic_args.size() != initial_fn.generic_vals.size())
    return reject("function clone generic argument count does not match",
                  source_fn.loc());
  Bindings bindings;
  if (specialized)
    for (std::size_t index = 0; index < generic_args.size(); ++index)
      bindings.emplace(source_generics[index], generic_args[index]);
  const std::vector<std::string> target_generics =
      specialized ? std::vector<std::string>{} : source_generics;
  std::vector<Ty> target_params;
  target_params.reserve(initial_fn.params.size());
  for (const std::uint32_t id : initial_fn.params)
    target_params.push_back(
        substitute(initial_source.vals[id].data.type, bindings));
  std::vector<Ty> target_returns;
  target_returns.reserve(initial_fn.returns.size());
  for (const Ty& type : initial_fn.returns)
    target_returns.push_back(substitute(type, bindings));
  if (specialized) {
    if (!std::all_of(generic_args.begin(), generic_args.end(), concrete_term))
      return reject("function clone requires concrete generic arguments",
                    source_fn.loc());
    const std::array candidates{source_fn};
    std::vector<Ty> resolved_returns;
    std::vector<Ty> resolved_generics;
    if (!detail::resolve_overload(candidates, target_params, generic_args,
                                  &resolved_returns, nullptr, {},
                                  &resolved_generics, target_returns) ||
        resolved_returns != target_returns ||
        resolved_generics !=
            std::vector<Ty>(generic_args.begin(), generic_args.end()))
      return reject("function clone generic arguments do not match the source "
                    "function",
                    source_fn.loc());
  }
  const auto existing = store.symbols.find(name);
  if (existing != store.symbols.end()) {
    for (const std::uint32_t id : existing->second) {
      if (id >= store.fns.size() || !store.fns[id].live)
        continue;
      const detail::FnData& candidate = store.fns[id].data;
      if (same_signature(store, candidate, target_params, target_generics))
        return reject("function clone would duplicate signature '" + name +
                          "'",
                      source_fn.loc());
    }
  }

  detail::Store backup = store;
  const detail::Store* source =
      source_fn.store_ == &store ? &backup : source_fn.store_;
  const detail::FnData& old_fn = source->fns[source_fn.id_].data;
  const Fn source_context(const_cast<detail::Store*>(source), source_fn.id_,
                          source->fns[source_fn.id_].generation);
  const auto rollback = [&](std::string message, Loc loc = {}) {
    store = backup;
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return Fn{};
  };

  if (source != &backup) {
    const std::string symbol = std::string(source_fn.module()) + "." +
                               std::string(source_fn.name());
    const std::vector<Fn> visible = env.resolve_fns(*this, symbol);
    if (std::find(visible.begin(), visible.end(), source_fn) == visible.end() &&
        !use(env, std::string(source_fn.module())))
      return rollback("function clone could not make its source module visible",
                      source_fn.loc());
  }

  detail::FnData next_fn = old_fn;
  next_fn.name = std::move(name);
  next_fn.returns = target_returns;
  next_fn.generic_vals.clear();
  next_fn.params.clear();
  next_fn.blks.clear();
  const auto next_fn_id = static_cast<std::uint32_t>(store.fns.size());
  store.fns.push_back({std::move(next_fn), 1, true});
  store.symbols[store.fns[next_fn_id].data.name].push_back(next_fn_id);

  std::unordered_map<std::uint32_t, std::uint32_t> values;
  const auto copy_value = [&](std::uint32_t old_id) {
    detail::ValData value = source->vals[old_id].data;
    value.type = substitute(value.type, bindings);
    value.users.clear();
    const auto next_id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(value), 1, true});
    values.emplace(old_id, next_id);
    return next_id;
  };
  if (!specialized)
    for (const std::uint32_t id : old_fn.generic_vals)
      store.fns[next_fn_id].data.generic_vals.push_back(copy_value(id));
  for (const std::uint32_t id : old_fn.params)
    store.fns[next_fn_id].data.params.push_back(copy_value(id));

  const std::uint32_t old_body = old_fn.blks.front();
  detail::BlkData body;
  body.fn = next_fn_id;
  const auto body_id = static_cast<std::uint32_t>(store.blks.size());
  store.blks.push_back({std::move(body), 1, true});
  store.fns[next_fn_id].data.blks.push_back(body_id);
  for (const std::uint32_t old_arg : source->blks[old_body].data.args)
    store.blks[body_id].data.args.push_back(copy_value(old_arg));

  const auto constant = [&](Attr literal, Ty type) {
    detail::OpData op;
    op.kind = Op::Kind::constant;
    op.blk = body_id;
    op.literal = std::move(literal);
    op.loc = old_fn.loc;
    const auto op_id = static_cast<std::uint32_t>(store.ops.size());
    store.ops.push_back({std::move(op), 1, true});
    store.blks[body_id].data.ops.push_back(op_id);
    detail::ValData result;
    result.type = std::move(type);
    result.def = op_id;
    const auto result_id = static_cast<std::uint32_t>(store.vals.size());
    store.vals.push_back({std::move(result), 1, true});
    store.ops[op_id].data.outs.push_back(result_id);
    return result_id;
  };
  const auto materialize = [&](const auto& self, const Ty& expected,
                               const Ty& value) -> std::optional<std::uint32_t> {
    Ty type = expected.name() == "_" ? generic_kind(value) : expected;
    if (type.name() == "int" || type.name() == "index" ||
        sized_integer_term(type.name())) {
      const auto number = integer(value);
      if (!number)
        return std::nullopt;
      return constant(Attr(*number), std::move(type));
    }
    if (type.name() == "bool") {
      if (value.text() != "true" && value.text() != "false")
        return std::nullopt;
      return constant(Attr(value.text() == "true"), std::move(type));
    }
    if (type.name() == "list" && type.args().size() == 1 &&
        value.name() == "[]") {
      std::vector<std::uint32_t> items;
      items.reserve(value.args().size());
      for (const Ty& item : value.args()) {
        const auto materialized = self(self, type.args().front(), item);
        if (!materialized)
          return std::nullopt;
        items.push_back(*materialized);
      }
      detail::OpData op;
      op.kind = Op::Kind::call;
      op.blk = body_id;
      op.callee = "base.list";
      op.args = std::move(items);
      op.loc = old_fn.loc;
      const auto op_id = static_cast<std::uint32_t>(store.ops.size());
      store.ops.push_back({std::move(op), 1, true});
      store.blks[body_id].data.ops.push_back(op_id);
      detail::ValData result;
      result.type = type;
      result.def = op_id;
      const auto result_id = static_cast<std::uint32_t>(store.vals.size());
      store.vals.push_back({std::move(result), 1, true});
      store.ops[op_id].data.outs.push_back(result_id);
      return result_id;
    }
    return std::nullopt;
  };
  if (specialized) {
    for (std::size_t index = 0; index < old_fn.generic_vals.size(); ++index) {
      const std::uint32_t old_id = old_fn.generic_vals[index];
      if (source->vals[old_id].data.users.empty())
        continue;
      const Ty expected =
          substitute(source->vals[old_id].data.type, bindings);
      const auto value =
          materialize(materialize, expected, generic_args[index]);
      if (!value)
        return rollback("function clone cannot materialize generic argument '" +
                            std::string(generic_args[index].text()) + "'",
                        old_fn.loc);
      values.emplace(old_id, *value);
    }
  }

  bool failed = false;
  const auto copy_op = [&](const auto& self, std::uint32_t old_id,
                           std::uint32_t blk) -> void {
    const detail::OpData old = source->ops[old_id].data;
    detail::OpData next = old;
    next.blk = blk;
    next.args.clear();
    next.outs.clear();
    next.blks.clear();
    for (const std::uint32_t arg : old.args) {
      const auto mapped = values.find(arg);
      if (mapped == values.end()) {
        failed = true;
        return;
      }
      next.args.push_back(mapped->second);
    }
    if (old.kind == Op::Kind::call) {
      const Ty applied(old.callee);
      if (applied.valid()) {
        const Ty concrete = substitute(applied, bindings);
        next.callee = std::string(concrete.text());
        const Op old_op(const_cast<detail::Store*>(source), old_id,
                        source->ops[old_id].generation);
        const std::vector<Fn> candidates =
            env.resolve_fns(source_context, applied.name());
        const Fn resolved = env.match(old_op, candidates);
        if (resolved) {
          std::string callee;
          if (resolved.store_ == source && resolved.id_ == source_fn.id_) {
            callee = store.name + "." + store.fns[next_fn_id].data.name;
          } else {
            bool ambiguous = false;
            const std::vector<Fn> visible =
                env.resolve_fns(*this, applied.name());
            const Fn destination = env.match(old_op, visible, &ambiguous);
            if (!ambiguous && destination == resolved)
              callee = std::string(applied.name());
            else
              callee = std::string(resolved.module()) + "." +
                       std::string(resolved.name());
          }
          if (!(specialized && resolved.store_ == source &&
                resolved.id_ == source_fn.id_) &&
              !concrete.args().empty()) {
            callee += '<';
            for (std::size_t index = 0; index < concrete.args().size();
                 ++index) {
              if (index)
                callee += ", ";
              callee += concrete.args()[index].text();
            }
            callee += '>';
          }
          next.callee = std::move(callee);
        }
      }
    }
    const auto next_id = static_cast<std::uint32_t>(store.ops.size());
    store.ops.push_back({std::move(next), 1, true});
    store.blks[blk].data.ops.push_back(next_id);
    for (const std::uint32_t old_value : old.outs) {
      detail::ValData value = source->vals[old_value].data;
      value.type = substitute(value.type, bindings);
      value.def = next_id;
      value.index = store.ops[next_id].data.outs.size();
      value.users.clear();
      const auto value_id = static_cast<std::uint32_t>(store.vals.size());
      store.vals.push_back({std::move(value), 1, true});
      store.ops[next_id].data.outs.push_back(value_id);
      values.emplace(old_value, value_id);
    }
    infer(env, next_id);
    for (const std::uint32_t old_blk : old.blks) {
      detail::BlkData body;
      body.fn = next_fn_id;
      body.parent_op = next_id;
      const auto body_id = static_cast<std::uint32_t>(store.blks.size());
      store.blks.push_back({std::move(body), 1, true});
      store.fns[next_fn_id].data.blks.push_back(body_id);
      store.ops[next_id].data.blks.push_back(body_id);
      for (const std::uint32_t old_arg : source->blks[old_blk].data.args) {
        detail::ValData value = source->vals[old_arg].data;
        value.type = substitute(value.type, bindings);
        value.users.clear();
        const auto value_id = static_cast<std::uint32_t>(store.vals.size());
        store.vals.push_back({std::move(value), 1, true});
        store.blks[body_id].data.args.push_back(value_id);
        values.emplace(old_arg, value_id);
      }
      for (const std::uint32_t child : source->blks[old_blk].data.ops) {
        self(self, child, body_id);
        if (failed)
          return;
      }
    }
  };

  for (const std::uint32_t op : source->blks[old_body].data.ops) {
    copy_op(copy_op, op, body_id);
    if (failed)
      return rollback("function clone encountered an unmapped value",
                      source->ops[op].data.loc);
  }
  detail::rebuild_uses(store);
  store.revision = backup.revision;
  touch(store);
  return Fn(&store, next_fn_id, store.fns[next_fn_id].generation);
}

bool Mod::expand(const Env& env, Op call, Fn callee,
                 std::string_view semantic) {
  auto& store = impl_->store;
  if (!call.valid() || call.store_ != &store ||
      call.kind() != Op::Kind::call) {
    detail::add_diag(store.diags,
                     "expand requires a live call in this module");
    return false;
  }
  if (!callee.valid() || callee.external() || !callee.body()) {
    detail::add_diag(store.diags,
                     "expand requires a function with a body", call.loc());
    return false;
  }
  if (!call.meta().empty()) {
    detail::add_diag(store.diags,
                     "expand requires call metadata to be handled explicitly",
                     call.loc());
    return false;
  }

  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return false;
  };
  const std::uint64_t before = store.revision;
  const Ty applied{std::string(call.callee())};
  const std::string_view symbol =
      applied.args().empty() ? call.callee() : applied.name();
  const std::string qualified =
      std::string(callee.module()) + "." + std::string(callee.name());
  if (symbol != callee.name() && symbol != qualified &&
      semantic != callee.name())
    return reject("expanded function does not match the call", call.loc());

  std::vector<Ty> arguments;
  for (Val value : call.args())
    arguments.push_back(value.type());
  std::vector<Ty> expected_returns;
  for (Val value : call.outs())
    expected_returns.push_back(value.type());
  const std::vector<Ty> explicit_arguments =
      applied.args().empty() ? std::vector<Ty>{} : applied.args();
  const std::vector<Fn> candidates{callee};
  const std::vector<Val> context = call.blk().fn().generics();
  std::vector<Ty> result_types;
  std::vector<Ty> generic_values;
  if (!detail::resolve_overload(candidates, arguments, explicit_arguments,
                                &result_types, nullptr, context,
                                &generic_values, expected_returns))
    return reject("call arguments do not match the expanded function",
                  call.loc());
  const std::vector<Val> params = callee.params();
  const std::vector<Val> generics = callee.generics();
  const std::vector<Val> call_args = call.args();
  const std::vector<Val> call_outs = call.outs();
  if (params.size() != call_args.size() ||
      result_types.size() != call_outs.size() ||
      generics.size() != generic_values.size())
    return reject("expanded function signature is inconsistent", call.loc());

  Bindings bindings;
  std::unordered_map<std::uint32_t, std::uint32_t> values;
  for (std::size_t index = 0; index < params.size(); ++index)
    values.emplace(params[index].id_, call_args[index].id_);
  for (std::size_t index = 0; index < generics.size(); ++index)
    bindings.emplace(std::string(generics[index].name()),
                     generic_values[index]);

  for (std::size_t index = 0; index < generics.size(); ++index) {
    const Val generic = generics[index];
    if (generic.users().empty())
      continue;
    const Ty value = generic_values[index];
    bool mapped = false;
    for (Val outer : context) {
      if (value.args().empty() && value.name() == outer.name()) {
        values.emplace(generic.id_, outer.id_);
        mapped = true;
        break;
      }
    }
    if (mapped)
      continue;
    if (generic.type().name() == "int") {
      const auto number = integer(value);
      if (!number)
        return reject("cannot materialize an expanded integer parameter",
                      call.loc());
      const Val constant = this->constant(call, Attr(*number), Ty("int"));
      if (!constant)
        return reject("cannot materialize an expanded integer parameter",
                      call.loc());
      values.emplace(generic.id_, constant.id_);
      continue;
    }
    if (generic.type().name() == "list" && value.name() == "[]") {
      std::vector<Val> items;
      for (const Ty& item : value.args()) {
        bool mapped_item = false;
        for (Val outer : context) {
          if (item.args().empty() && item.name() == outer.name()) {
            items.push_back(outer);
            mapped_item = true;
            break;
          }
        }
        if (mapped_item)
          continue;
        const auto number = integer(item);
        if (!number)
          return reject("cannot materialize an expanded list parameter",
                        call.loc());
        const Val constant = this->constant(call, Attr(*number), Ty("int"));
        if (!constant)
          return reject("cannot materialize an expanded list parameter",
                        call.loc());
        items.push_back(constant);
      }
      const Val list = this->call(call, "base.list", items, generic.type());
      if (!list)
        return reject("cannot materialize an expanded list parameter",
                      call.loc());
      values.emplace(generic.id_, list.id_);
      continue;
    }
    return reject("expanded generic parameter is not representable as a value",
                  call.loc());
  }

  const detail::Store& source = *callee.store_;
  const std::uint32_t destination = call.blk().id_;
  const std::uint32_t owner = store.blks[destination].data.fn;
  std::unordered_set<std::string> used_names;
  for (const auto& slot : store.vals)
    if (slot.live && !slot.data.name.empty())
      used_names.insert(slot.data.name);
  std::unordered_map<std::string, std::string> copied_names;
  const auto copy_name = [&](std::string_view source_name) {
    if (source_name.empty())
      return std::string{};
    const auto found = copied_names.find(std::string(source_name));
    if (found != copied_names.end())
      return found->second;
    std::string candidate(source_name);
    for (std::size_t suffix = 1; used_names.contains(candidate); ++suffix)
      candidate = std::string(source_name) + '_' + std::to_string(suffix);
    used_names.insert(candidate);
    copied_names.emplace(std::string(source_name), candidate);
    return candidate;
  };

  bool failed = false;
  const auto copy_op = [&](const auto& self, std::uint32_t old_id,
                           std::uint32_t blk) -> std::uint32_t {
    const detail::OpData old = source.ops[old_id].data;
    detail::OpData next = old;
    next.blk = blk;
    next.args.clear();
    next.outs.clear();
    next.blks.clear();
    if (next.kind == Op::Kind::call) {
      const Ty spelling(next.callee);
      if (spelling.valid())
        next.callee = std::string(substitute(spelling, bindings).text());
    }
    for (const std::uint32_t arg : old.args) {
      const auto mapped = values.find(arg);
      if (mapped == values.end()) {
        failed = true;
        return detail::none;
      }
      next.args.push_back(mapped->second);
    }
    const auto next_id = static_cast<std::uint32_t>(store.ops.size());
    store.ops.push_back({std::move(next), 1, true});
    store.blks[blk].data.ops.push_back(next_id);

    for (const std::uint32_t old_value : old.outs) {
      detail::ValData value = source.vals[old_value].data;
      value.name = copy_name(value.name);
      value.type = substitute(value.type, bindings);
      value.def = next_id;
      value.index = store.ops[next_id].data.outs.size();
      value.users.clear();
      const auto value_id = static_cast<std::uint32_t>(store.vals.size());
      store.vals.push_back({std::move(value), 1, true});
      store.ops[next_id].data.outs.push_back(value_id);
      values.emplace(old_value, value_id);
    }
    infer(env, next_id);

    for (const std::uint32_t old_blk : old.blks) {
      detail::BlkData body;
      body.fn = owner;
      body.parent_op = next_id;
      const auto body_id = static_cast<std::uint32_t>(store.blks.size());
      store.blks.push_back({std::move(body), 1, true});
      store.fns[owner].data.blks.push_back(body_id);
      store.ops[next_id].data.blks.push_back(body_id);
      const std::vector<std::uint32_t> old_args =
          source.blks[old_blk].data.args;
      const std::vector<std::uint32_t> old_ops =
          source.blks[old_blk].data.ops;
      for (std::size_t index = 0; index < old_args.size(); ++index) {
        const std::uint32_t old_arg = old_args[index];
        detail::ValData value = source.vals[old_arg].data;
        value.name = copy_name(value.name);
        value.type = substitute(value.type, bindings);
        value.users.clear();
        const auto value_id = static_cast<std::uint32_t>(store.vals.size());
        store.vals.push_back({std::move(value), 1, true});
        store.blks[body_id].data.args.push_back(value_id);
        values.emplace(old_arg, value_id);
        if (store.ops[next_id].data.kind == Op::Kind::loop &&
            index < store.ops[next_id].data.iter_names.size())
          store.ops[next_id].data.iter_names[index] =
              store.vals[value_id].data.name;
      }
      for (const std::uint32_t child : old_ops) {
        self(self, child, body_id);
        if (failed)
          return detail::none;
      }
    }
    return next_id;
  };

  const std::vector<Op> body = callee.body().ops();
  if (body.empty() || body.back().kind() != Op::Kind::ret)
    return reject("expanded function has no final return", callee.loc());
  std::vector<std::uint32_t> roots;
  for (std::size_t index = 0; index + 1 < body.size(); ++index) {
    if (body[index].kind() == Op::Kind::ret)
      return reject("expanded function has an early return", body[index].loc());
    roots.push_back(copy_op(copy_op, body[index].id_, destination));
    if (failed)
      return reject("expanded function contains an unmapped value",
                    body[index].loc());
  }

  const std::vector<Val> returned = body.back().args();
  if (returned.size() != call_outs.size())
    return reject("expanded return count does not match the call", call.loc());
  std::vector<std::uint32_t> replacements;
  replacements.reserve(returned.size());
  for (std::size_t index = 0; index < returned.size(); ++index) {
    const auto mapped = values.find(returned[index].id_);
    if (mapped == values.end())
      return reject("expanded return value is not available", call.loc());
    const Ty& old_type = store.vals[call_outs[index].id_].data.type;
    const Ty& new_type = store.vals[mapped->second].data.type;
    if (old_type.text() != "_" && new_type.text() != "_" &&
        old_type != new_type)
      return reject("expanded return type does not match the call", call.loc());
    replacements.push_back(mapped->second);
  }

  for (std::size_t index = 0; index < call_outs.size(); ++index) {
    const Attr::Dict& boundary =
        store.vals[call_outs[index].id_].data.meta;
    const std::unordered_set<std::uint32_t> related =
        family(store, replacements[index]);
    for (const auto& [key, value] : boundary) {
      for (const std::uint32_t id : related) {
        const auto found = store.vals[id].data.meta.find(key);
        if (found != store.vals[id].data.meta.end() &&
            found->second != value)
          return reject("expanded result metadata conflicts with function body",
                        call.loc());
      }
      for (const std::uint32_t id : related)
        store.vals[id].data.meta[key] = value;
    }
  }

  auto& order = store.blks[destination].data.ops;
  for (const std::uint32_t root : roots)
    order.erase(std::remove(order.begin(), order.end(), root), order.end());
  const auto position = std::find(order.begin(), order.end(), call.id_);
  if (position == order.end())
    return reject("expanded call is not in its Blk", call.loc());
  order.insert(position, roots.begin(), roots.end());
  for (auto& slot : store.ops) {
    if (!slot.live)
      continue;
    for (std::uint32_t& arg : slot.data.args) {
      for (std::size_t index = 0; index < call_outs.size(); ++index)
        if (arg == call_outs[index].id_)
          arg = replacements[index];
    }
  }
  detail::rebuild_uses(store);
  for (std::size_t index = 0; index < call_outs.size(); ++index) {
    const std::string name = store.vals[call_outs[index].id_].data.name;
    if (name.empty())
      continue;
    Val replacement(&store, replacements[index],
                    store.vals[replacements[index]].generation);
    if (!rename(replacement, name))
      return reject("expanded result cannot preserve its binding",
                    call.loc());
  }
  order.erase(std::remove(order.begin(), order.end(), call.id_), order.end());
  for (Val output : call_outs) {
    store.vals[output.id_].live = false;
    ++store.vals[output.id_].generation;
  }
  store.ops[call.id_].live = false;
  ++store.ops[call.id_].generation;
  detail::rebuild_uses(store);
  store.revision = before;
  touch(store);
  return true;
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
  if (op.blk() != before.blk()) {
    detail::add_diag(store.diags,
                     "move currently requires one destination Blk",
                     before.loc());
    return false;
  }
  if (op.kind() == Op::Kind::ret || op.kind() == Op::Kind::yield ||
      before.kind() == Op::Kind::yield) {
    detail::add_diag(store.diags, "move cannot reorder a Blk terminator",
                     op.loc());
    return false;
  }
  auto& order = store.blks[op.blk().id_].data.ops;
  const std::vector<std::uint32_t> old_order = order;
  order.erase(std::remove(order.begin(), order.end(), op.id_), order.end());
  const auto position = std::find(order.begin(), order.end(), before.id_);
  if (position == order.end()) {
    order = old_order;
    detail::add_diag(store.diags, "move insertion point is not in its Blk",
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

bool Mod::args(const Env& env, Op op, std::span<const Val> values) {
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
  std::size_t expected = data.args.size();
  if (data.kind == Op::Kind::constant)
    expected = 0;
  else if (data.kind == Op::Kind::loop)
    expected = data.iter_names.size() + data.carried_count;
  else if (data.kind == Op::Kind::branch)
    expected = 1 + data.carried_count;
  else if (data.kind == Op::Kind::ret) {
    const std::uint32_t blk = data.blk;
    const std::uint32_t fn = store.blks[blk].data.fn;
    expected = store.fns[fn].data.returns.size();
  } else if (data.kind == Op::Kind::yield) {
    const std::uint32_t blk = data.blk;
    const std::uint32_t parent = store.blks[blk].data.parent_op;
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
  if (data.kind == Op::Kind::call) {
    std::vector<Ty> returns;
    const Fn current = env.resolve(*this, op);
    const Fn next = env.resolve(*this, op, op.callee(), values, &returns);
    if (current && !next)
      return reject("call arguments do not match its resolved function");
    if (next) {
      const std::vector<Val> outputs = op.outs();
      if (returns.size() != outputs.size())
        return reject("call result count does not match its resolved function");
      for (std::size_t index = 0; index < returns.size(); ++index)
        if (!compatible(outputs[index].type(), returns[index]))
          return reject(
              "call result types do not match its resolved function");
    }
  } else if (data.kind == Op::Kind::loop ||
             data.kind == Op::Kind::branch) {
    for (std::size_t index = 0; index < values.size(); ++index)
      if (!compatible(values[index].type(),
                      store.vals[data.args[index]].data.type))
        return reject("structured operation argument types do not match");
  } else if (data.kind == Op::Kind::ret) {
    const std::uint32_t fn = store.blks[data.blk].data.fn;
    const std::vector<Ty>& returns = store.fns[fn].data.returns;
    for (std::size_t index = 0; index < values.size(); ++index)
      if (!compatible(values[index].type(), returns[index]))
        return reject("return argument type does not match its function");
  } else if (data.kind == Op::Kind::yield) {
    const std::uint32_t parent = store.blks[data.blk].data.parent_op;
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

bool Mod::fuse(const Env& env, std::span<const Op> ops, std::string callee) {
  auto& store = impl_->store;
  auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return false;
  };
  if (ops.size() < 2 || !valid_callee(callee))
    return reject("fuse requires at least two calls and a valid callee");

  const Blk blk = ops.front().blk();
  if (!blk || blk.store_ != &store)
    return reject("fuse requires live calls in this module");
  const std::vector<Op> order = blk.ops();
  std::unordered_set<std::uint32_t> selected;
  std::size_t previous = 0;
  for (std::size_t index = 0; index < ops.size(); ++index) {
    const Op op = ops[index];
    if (!op.valid() || op.store_ != &store || op.blk() != blk ||
        op.kind() != Op::Kind::call || !selected.insert(op.id_).second)
      return reject("fuse requires distinct calls in one Blk", op.loc());
    const auto position = std::find(order.begin(), order.end(), op);
    if (position == order.end())
      return reject("fuse call is not in its Blk", op.loc());
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
  if (old_form != Op::Form::hidden && old_form != Op::Form::let &&
      old_form != Op::Form::var)
    return reject("fuse live-out must be an expression value",
                  output.def().loc());
  if (external_uses > 1 && old_form == Op::Form::hidden)
    return reject("an unnamed fuse live-out cannot have multiple users",
                  output.def().loc());

  detail::Store backup = store;
  const auto rollback = [&]() {
    std::vector<Diag> diags = store.diags;
    store = backup;
    store.diags = std::move(diags);
    return false;
  };
  const std::string target = callee;
  Val fused = call(ops.back(), std::move(callee), inputs, output.type());
  if (!fused)
    return rollback();
  const Ty applied{target};
  const std::string_view symbol =
      applied.args().empty() ? std::string_view(target) : applied.name();
  const std::vector<Fn> declarations = env.resolve_fns(*this, symbol);
  if (!declarations.empty()) {
    std::vector<Ty> returns;
    const Fn resolved =
        env.resolve(*this, fused.def(), target, inputs, &returns);
    const Ty actual = output.type();
    const bool result_matches =
        returns.size() == 1 &&
        (actual.text() == "_" || returns.front().text() == "_" ||
         actual == returns.front());
    if (!resolved || !result_matches) {
      detail::add_diag(store.diags,
                       "fuse target does not accept the region signature",
                       ops.front().loc());
      return rollback();
    }
  }
  store.vals[fused.id_].data.name = std::string(output.name());
  store.vals[fused.id_].data.meta = output.meta();
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

bool Mod::replace(Op op, Attr value) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return false;
  };
  if (!op.valid() || op.store_ != &store)
    return reject(
        "constant replacement requires a live operation in this module");
  auto& data = store.ops[op.id_].data;
  if ((data.kind != Op::Kind::call && data.kind != Op::Kind::constant) ||
      !data.blks.empty() || data.outs.size() != 1 ||
      data.form == Op::Form::index_assign)
    return reject("constant replacement requires a single-result expression",
                  data.loc);
  const std::uint32_t output = data.outs.front();
  if (output >= store.vals.size() || !store.vals[output].live)
    return reject("constant replacement has no live result", data.loc);
  const Ty& type = store.vals[output].data.type;
  if (!detail::literal_matches(value, type))
    return reject("constant literal does not match result type '" +
                      std::string(type.text()) + "'",
                  data.loc);
  if (data.kind == Op::Kind::constant && data.literal == value &&
      data.callee.empty() && data.args.empty())
    return false;

  data.kind = Op::Kind::constant;
  data.callee.clear();
  data.args.clear();
  data.blks.clear();
  data.iter_names.clear();
  data.carried_count = 0;
  data.logic = detail::Logic::none;
  data.literal = std::move(value);
  detail::rebuild_uses(store);
  detail::touch(store);
  return true;
}

bool Mod::replace(Val old_value, Val new_value) {
  const std::array old_values{old_value};
  const std::array new_values{new_value};
  return replace(old_values, new_values);
}

bool Mod::replace(std::span<const Val> old_values,
                  std::span<const Val> new_values) {
  auto& store = impl_->store;
  if (old_values.size() != new_values.size()) {
    detail::add_diag(store.diags,
                     "replace requires one new value per old value");
    return false;
  }
  if (old_values.empty())
    return false;
  std::unordered_map<std::uint32_t, std::uint32_t> replacements;
  replacements.reserve(old_values.size());
  for (std::size_t index = 0; index < old_values.size(); ++index) {
    const Val old_value = old_values[index];
    const Val new_value = new_values[index];
    if (!old_value.valid() || !new_value.valid() ||
        old_value.store_ != &store || new_value.store_ != &store) {
      detail::add_diag(store.diags,
                       "replace requires live values in this module");
      return false;
    }
    if (old_value.type() != new_value.type()) {
      detail::add_diag(store.diags,
                       "replacement values have different types");
      return false;
    }
    if (old_value == new_value)
      continue;
    const auto [found, inserted] =
        replacements.emplace(old_value.id_, new_value.id_);
    if (!inserted && found->second != new_value.id_) {
      detail::add_diag(store.diags,
                       "replace assigns conflicting new values");
      return false;
    }
  }
  if (replacements.empty())
    return true;

  for (auto& [old_value, new_value] : replacements) {
    std::unordered_set<std::uint32_t> path{old_value};
    auto found = replacements.find(new_value);
    while (found != replacements.end()) {
      if (!path.insert(new_value).second) {
        detail::add_diag(store.diags,
                         "replacement values form a cycle");
        return false;
      }
      new_value = found->second;
      found = replacements.find(new_value);
    }
  }
  for (const auto& [old_value, new_value] : replacements) {
    for (const std::uint32_t user : store.vals[old_value].data.users) {
      if (!detail::dominates(store, new_value, user)) {
        detail::add_diag(
            store.diags,
            "replacement value must dominate every selected use",
            store.ops[user].data.loc);
        return false;
      }
    }
  }
  bool changed = false;
  for (auto& entry : store.ops) {
    if (!entry.live)
      continue;
    for (std::uint32_t& arg : entry.data.args) {
      const auto found = replacements.find(arg);
      if (found != replacements.end() && arg != found->second) {
        arg = found->second;
        changed = true;
      }
    }
  }
  detail::rebuild_uses(store);
  if (changed)
    detail::touch(store);
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
  if (old_type != new_type) {
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
    detail::touch(store);
  }
  return true;
}

bool Mod::erase(Op op) {
  const std::array ops{op};
  return erase(ops);
}

bool Mod::erase(std::span<const Op> roots) {
  auto& store = impl_->store;
  if (roots.empty())
    return false;
  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return false;
  };
  for (Op op : roots) {
    if (!op.valid() || op.store_ != &store)
      return reject("erase requires live operations in this module");
    if (op.kind() == Op::Kind::ret || op.kind() == Op::Kind::yield)
      return reject("cannot erase a Blk terminator", op.loc());
    const std::uint32_t parent = store.ops[op.id_].data.blk;
    if (parent == detail::none || parent >= store.blks.size() ||
        !store.blks[parent].live ||
        std::find(store.blks[parent].data.ops.begin(),
                  store.blks[parent].data.ops.end(), op.id_) ==
            store.blks[parent].data.ops.end())
      return reject("erase operation is not in its parent Blk", op.loc());
  }

  std::unordered_set<std::uint32_t> ops;
  std::unordered_set<std::uint32_t> blks;
  std::unordered_set<std::uint32_t> values;
  const auto collect = [&](const auto& self, std::uint32_t id) -> void {
    if (!ops.insert(id).second)
      return;
    for (const std::uint32_t value : store.ops[id].data.outs)
      values.insert(value);
    for (const std::uint32_t blk : store.ops[id].data.blks) {
      blks.insert(blk);
      for (const std::uint32_t value : store.blks[blk].data.args)
        values.insert(value);
      for (const std::uint32_t child : store.blks[blk].data.ops)
        self(self, child);
    }
  };
  for (Op op : roots)
    collect(collect, op.id_);
  for (const std::uint32_t value : values) {
    for (const std::uint32_t user : store.vals[value].data.users) {
      if (!ops.contains(user))
        return reject("cannot erase an operation with live external users",
                      store.ops[store.vals[value].data.def].data.loc);
    }
  }
  for (auto& slot : store.blks) {
    if (!slot.live)
      continue;
    auto& order = slot.data.ops;
    order.erase(std::remove_if(order.begin(), order.end(),
                               [&](std::uint32_t id) {
                                 return ops.contains(id);
                               }),
                order.end());
  }
  for (const std::uint32_t blk : blks) {
    const std::uint32_t fn = store.blks[blk].data.fn;
    if (fn < store.fns.size()) {
      auto& owned = store.fns[fn].data.blks;
      owned.erase(std::remove(owned.begin(), owned.end(), blk), owned.end());
    }
    store.blks[blk].live = false;
    ++store.blks[blk].generation;
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
  detail::touch(store);
  return true;
}

bool Mod::erase(const Env& env, Fn fn) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return false;
  };
  if (!fn.valid() || fn.store_ != &store)
    return reject("erase requires a live function in this module");

  for (std::uint32_t id = 0; id < store.ops.size(); ++id) {
    if (!store.ops[id].live || store.ops[id].data.kind != Op::Kind::call)
      continue;
    const std::uint32_t blk = store.ops[id].data.blk;
    if (blk >= store.blks.size() || !store.blks[blk].live ||
        store.blks[blk].data.fn == fn.id_)
      continue;
    const Op call(&store, id, store.ops[id].generation);
    if (env.resolve(*this, call) == fn)
      return reject("cannot erase a function with live callers", call.loc());
  }

  const detail::FnData& data = store.fns[fn.id_].data;
  std::unordered_set<std::uint32_t> ops;
  std::unordered_set<std::uint32_t> values(data.generic_vals.begin(),
                                           data.generic_vals.end());
  values.insert(data.params.begin(), data.params.end());
  for (const std::uint32_t blk : data.blks) {
    if (blk >= store.blks.size() || !store.blks[blk].live)
      return reject("function owns an invalid Blk", data.loc);
    values.insert(store.blks[blk].data.args.begin(),
                  store.blks[blk].data.args.end());
    for (const std::uint32_t op : store.blks[blk].data.ops) {
      if (op >= store.ops.size() || !store.ops[op].live)
        return reject("function owns an invalid operation", data.loc);
      ops.insert(op);
      values.insert(store.ops[op].data.outs.begin(),
                    store.ops[op].data.outs.end());
    }
  }
  for (const std::uint32_t value : values) {
    if (value >= store.vals.size() || !store.vals[value].live)
      return reject("function owns an invalid value", data.loc);
    for (const std::uint32_t user : store.vals[value].data.users)
      if (!ops.contains(user))
        return reject("cannot erase a function with externally used values",
                      data.loc);
  }

  auto symbol = store.symbols.find(data.name);
  if (symbol == store.symbols.end())
    return reject("function is absent from its symbol table", data.loc);
  auto& overloads = symbol->second;
  const auto found = std::find(overloads.begin(), overloads.end(), fn.id_);
  if (found == overloads.end())
    return reject("function is absent from its overload set", data.loc);
  overloads.erase(found);
  if (overloads.empty())
    store.symbols.erase(symbol);

  for (const std::uint32_t blk : data.blks) {
    store.blks[blk].live = false;
    ++store.blks[blk].generation;
  }
  for (const std::uint32_t op : ops) {
    store.ops[op].live = false;
    ++store.ops[op].generation;
  }
  for (const std::uint32_t value : values) {
    store.vals[value].live = false;
    ++store.vals[value].generation;
  }
  store.fns[fn.id_].live = false;
  ++store.fns[fn.id_].generation;
  detail::rebuild_uses(store);
  touch(store);
  return true;
}

bool Mod::type(Val value, Ty next) {
  return type(std::span<const Val>(&value, 1),
              std::span<const Ty>(&next, 1));
}

bool Mod::type(std::span<const Val> values, std::span<const Ty> types) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message) {
    detail::add_diag(store.diags, std::move(message));
    return false;
  };
  if (values.size() != types.size())
    return reject("type requires one structural type per value");
  if (values.empty())
    return reject("type requires at least one value");
  for (Val value : values)
    if (!value.valid() || value.store_ != &store)
      return reject("type requires live values in this module");
  for (const Ty& type : types)
    if (!type.valid())
      return reject("type requires valid structural types");

  ValFamilies families(store);
  std::unordered_map<std::uint32_t, Ty> assignments;
  assignments.reserve(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    const std::uint32_t group = families.root(values[index].id_);
    const auto [found, inserted] = assignments.emplace(group, types[index]);
    if (!inserted && found->second != types[index])
      return reject("type assigns conflicting types to one value family");
  }

  bool changed = false;
  for (std::uint32_t id = 0; id < store.vals.size(); ++id) {
    if (!store.vals[id].live)
      continue;
    const auto assigned = assignments.find(families.root(id));
    if (assigned == assignments.end())
      continue;
    detail::ValData& data = store.vals[id].data;
    changed = data.type != assigned->second || changed;
    data.type = assigned->second;
    if (data.kind == detail::ValKind::result) {
      changed = !data.type_annotation || changed;
      data.type_annotation = true;
    }
  }
  if (changed)
    touch(store);
  return true;
}

bool Mod::returns(Fn fn, std::span<const Ty> types) {
  auto& store = impl_->store;
  if (!fn.valid() || fn.store_ != &store ||
      std::any_of(types.begin(), types.end(),
                  [](const Ty& type) { return !type.valid(); })) {
    detail::add_diag(store.diags,
                     "returns requires a live function and valid types");
    return false;
  }
  const std::vector<Ty> next(types.begin(), types.end());
  if (store.fns[fn.id_].data.returns == next)
    return true;
  store.fns[fn.id_].data.returns = next;
  touch(store);
  return true;
}

bool Mod::generics(const Env& env, Op call, std::span<const Ty> types) {
  auto& store = impl_->store;
  if (!call.valid() || call.store_ != &store ||
      call.kind() != Op::Kind::call ||
      std::any_of(types.begin(), types.end(),
                  [](const Ty& type) { return !type.valid(); })) {
    detail::add_diag(store.diags,
                     "generics requires a live call and valid types");
    return false;
  }
  const std::string current(call.callee());
  const Ty applied{current};
  if (!applied.valid()) {
    detail::add_diag(store.diags,
                     "call callee is not a structural type application",
                     call.loc());
    return false;
  }
  std::string next(applied.args().empty() ? applied.text() : applied.name());
  if (!types.empty()) {
    next += '<';
    for (std::size_t index = 0; index < types.size(); ++index) {
      if (index)
        next += ", ";
      next += types[index].text();
    }
    next += '>';
  }
  return rename(env, call, std::move(next));
}

bool Mod::rename(const Env& env, Fn fn, std::string name) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message, Loc loc = {}) {
    detail::add_diag(store.diags, std::move(message), std::move(loc));
    return false;
  };
  if (!fn.valid() || fn.store_ != &store || !valid_binding(name))
    return reject("rename requires a live function and valid local name");
  const detail::FnData& current = store.fns[fn.id_].data;
  if (current.name == name)
    return true;

  std::vector<std::string> current_generics;
  current_generics.reserve(current.generic_vals.size());
  for (const std::uint32_t generic : current.generic_vals)
    current_generics.push_back(store.vals[generic].data.name);
  std::vector<Ty> current_params;
  current_params.reserve(current.params.size());
  for (const std::uint32_t param : current.params)
    current_params.push_back(store.vals[param].data.type);
  const auto existing = store.symbols.find(name);
  if (existing != store.symbols.end()) {
    for (const std::uint32_t id : existing->second) {
      if (id >= store.fns.size() || !store.fns[id].live || id == fn.id_)
        continue;
      const detail::FnData& candidate = store.fns[id].data;
      if (same_signature(store, candidate, current_params, current_generics))
        return reject("function rename would duplicate signature '" + name +
                          "'",
                      current.loc);
    }
  }

  std::vector<std::uint32_t> calls;
  for (std::uint32_t id = 0; id < store.ops.size(); ++id) {
    if (!store.ops[id].live || store.ops[id].data.kind != Op::Kind::call)
      continue;
    const Op call(&store, id, store.ops[id].generation);
    if (env.resolve(*this, call) == fn)
      calls.push_back(id);
  }

  detail::Store backup = store;
  auto old_symbol = store.symbols.find(current.name);
  if (old_symbol == store.symbols.end())
    return reject("function is absent from its symbol table", current.loc);
  auto& old_overloads = old_symbol->second;
  const auto old = std::find(old_overloads.begin(), old_overloads.end(), fn.id_);
  if (old == old_overloads.end())
    return reject("function is absent from its overload set", current.loc);
  old_overloads.erase(old);
  if (old_overloads.empty())
    store.symbols.erase(old_symbol);
  store.fns[fn.id_].data.name = name;
  store.symbols[name].push_back(fn.id_);

  const std::string qualified = store.name + "." + name;
  for (const std::uint32_t id : calls) {
    const Ty applied(store.ops[id].data.callee);
    std::string suffix;
    if (applied.valid() && !applied.args().empty()) {
      suffix += '<';
      for (std::size_t index = 0; index < applied.args().size(); ++index) {
        if (index)
          suffix += ", ";
        suffix += applied.args()[index].text();
      }
      suffix += '>';
    }
    store.ops[id].data.callee = name + suffix;
    const Op call(&store, id, store.ops[id].generation);
    if (env.resolve(*this, call) != fn)
      store.ops[id].data.callee = qualified + suffix;
  }
  for (const std::uint32_t id : calls) {
    const Op call(&store, id, store.ops[id].generation);
    if (env.resolve(*this, call) == fn)
      continue;
    const Loc loc = call.loc();
    store = std::move(backup);
    return reject("function rename could not preserve a call", loc);
  }
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

  const std::unordered_set<std::uint32_t> related = family(store, value.id_);

  const auto conflicts = [&](std::span<const std::uint32_t> ids) {
    const bool owns = std::any_of(ids.begin(), ids.end(), [&](std::uint32_t id) {
      return related.contains(id);
    });
    return owns && std::any_of(ids.begin(), ids.end(), [&](std::uint32_t id) {
             return !related.contains(id) && id < store.vals.size() &&
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
  for (const auto& slot : store.blks)
    if (slot.live && conflicts(slot.data.args)) {
      detail::add_diag(store.diags, "rename would duplicate a Blk binding");
      return false;
    }

  bool changed = false;
  for (const std::uint32_t id : related) {
    detail::ValData& data = store.vals[id].data;
    changed = data.name != name || changed;
    data.name = name;
    if (data.def == detail::none)
      continue;
    detail::OpData& op = store.ops[data.def].data;
    if ((op.kind == Op::Kind::call || op.kind == Op::Kind::constant) &&
        op.form == Op::Form::hidden)
      op.form = Op::Form::let;
  }
  for (const auto& blk_slot : store.blks) {
    if (!blk_slot.live || blk_slot.data.parent_op == detail::none ||
        blk_slot.data.parent_op >= store.ops.size() ||
        !store.ops[blk_slot.data.parent_op].live)
      continue;
    detail::OpData& parent = store.ops[blk_slot.data.parent_op].data;
    if (parent.kind != Op::Kind::loop)
      continue;
    const auto& args = blk_slot.data.args;
    for (std::size_t index = 0;
         index < parent.iter_names.size() && index < args.size(); ++index) {
      if (!related.contains(args[index]))
        continue;
      changed = parent.iter_names[index] != name || changed;
      parent.iter_names[index] = name;
    }
  }
  if (changed)
    touch(store);
  return true;
}

bool Mod::rename(const Env& env, Op call, std::string callee) {
  auto& store = impl_->store;
  if (!call.valid() || call.store_ != &store || call.kind() != Op::Kind::call ||
      !valid_callee(callee)) {
    detail::add_diag(store.diags,
                     "rename requires a live call and valid callee");
    return false;
  }
  if (store.ops[call.id_].data.callee == callee)
    return true;

  const Ty applied{callee};
  const std::string_view symbol =
      applied.args().empty() ? std::string_view(callee) : applied.name();
  const std::vector<Fn> declarations = env.resolve_fns(*this, symbol);
  if (!declarations.empty()) {
    const std::vector<Val> arguments = call.args();
    std::vector<Ty> returns;
    const Fn resolved =
        env.resolve(*this, call, callee, arguments, &returns);
    const std::vector<Val> outputs = call.outs();
    bool compatible = resolved && returns.size() == outputs.size();
    for (std::size_t index = 0; compatible && index < returns.size(); ++index) {
      const Ty actual = outputs[index].type();
      compatible = actual.text() == "_" || returns[index].text() == "_" ||
                   actual == returns[index];
    }
    if (!compatible) {
      detail::add_diag(store.diags,
                       "rename target does not accept the call signature",
                       call.loc());
      return false;
    }
  }
  store.ops[call.id_].data.callee = std::move(callee);
  touch(store);
  return true;
}

bool Mod::retarget(const Env& env, Op call, std::string callee,
                   std::span<const Val> args) {
  auto& store = impl_->store;
  if (!call.valid() || call.store_ != &store ||
      call.kind() != Op::Kind::call || !valid_callee(callee)) {
    detail::add_diag(store.diags,
                     "retarget requires a live call and valid callee");
    return false;
  }
  for (Val value : args)
    if (!value.valid() || value.store_ != &store ||
        !detail::dominates(store, value.id_, call.id_)) {
      detail::add_diag(store.diags,
                       "retarget arguments must dominate their call",
                       call.loc());
      return false;
  }
  std::vector<Ty> returns;
  if (!env.resolve(*this, call, callee, args, &returns)) {
    detail::add_diag(store.diags,
                     "retarget target does not accept the call signature",
                     call.loc());
    return false;
  }
  const std::vector<Val> outputs = call.outs();
  if (returns.size() != outputs.size()) {
    detail::add_diag(store.diags,
                     "retarget result count does not match the call",
                     call.loc());
    return false;
  }
  for (std::size_t index = 0; index < returns.size(); ++index) {
    const Ty type = outputs[index].type();
    if (type.text() != "_" && returns[index].text() != "_" &&
        type != returns[index]) {
      detail::add_diag(store.diags,
                       "retarget result types do not match the call",
                       call.loc());
      return false;
    }
  }

  std::vector<std::uint32_t> values;
  values.reserve(args.size());
  for (Val value : args)
    values.push_back(value.id_);
  detail::OpData& op = store.ops[call.id_].data;
  if (op.callee == callee && op.args == values)
    return true;
  const bool changed_args = op.args != values;
  op.callee = std::move(callee);
  op.args = std::move(values);
  if (changed_args)
    detail::rebuild_uses(store);
  touch(store);
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

bool Mod::set(Val item, std::string key, Attr value) {
  auto& store = impl_->store;
  if (!item.valid() || item.store_ != &store || key.empty()) {
    detail::add_diag(store.diags,
                     "set requires a live value and non-empty key");
    return false;
  }
  const std::unordered_set<std::uint32_t> related = family(store, item.id_);
  if (!printable_value(store, related)) {
    detail::add_diag(store.diags,
                     "cannot annotate a value without a source binding");
    return false;
  }
  bool changed = false;
  for (const std::uint32_t id : related) {
    Attr::Dict& meta = store.vals[id].data.meta;
    const auto found = meta.find(key);
    if (found != meta.end() && found->second == value)
      continue;
    meta[key] = value;
    changed = true;
  }
  if (changed)
    touch(store);
  return true;
}

bool Mod::set(std::span<const Val> items, std::string key,
              std::span<const Attr> values) {
  auto& store = impl_->store;
  const auto reject = [&](std::string message) {
    detail::add_diag(store.diags, std::move(message));
    return false;
  };
  if (items.size() != values.size())
    return reject("set requires one metadata value per IR value");
  if (items.empty() || key.empty())
    return reject("set requires values and a non-empty key");
  for (Val item : items)
    if (!item.valid() || item.store_ != &store)
      return reject("set requires live values in this module");

  ValFamilies families(store);

  std::vector<bool> printable(store.vals.size());
  for (std::uint32_t id = 0; id < store.vals.size(); ++id) {
    if (!store.vals[id].live)
      continue;
    const detail::ValData& value = store.vals[id].data;
    bool source = value.kind == detail::ValKind::generic ||
                  value.kind == detail::ValKind::param;
    if (value.kind == detail::ValKind::result && !value.name.empty() &&
        value.def != detail::none && value.def < store.ops.size() &&
        store.ops[value.def].live) {
      const Op::Form form = store.ops[value.def].data.form;
      source = source || form == Op::Form::let ||
               form == Op::Form::var;
    }
    if (source)
      printable[families.root(id)] = true;
  }

  std::unordered_map<std::uint32_t, Attr> assignments;
  assignments.reserve(items.size());
  for (std::size_t index = 0; index < items.size(); ++index) {
    const std::uint32_t group = families.root(items[index].id_);
    if (!printable[group])
      return reject("cannot annotate a value without a source binding");
    const auto [found, inserted] = assignments.emplace(group, values[index]);
    if (!inserted && found->second != values[index])
      return reject("set assigns conflicting metadata to one value family");
  }

  bool changed = false;
  for (std::uint32_t id = 0; id < store.vals.size(); ++id) {
    if (!store.vals[id].live)
      continue;
    const auto assigned = assignments.find(families.root(id));
    if (assigned == assignments.end())
      continue;
    Attr::Dict& meta = store.vals[id].data.meta;
    const auto found = meta.find(key);
    if (found == meta.end() || found->second != assigned->second) {
      meta[key] = assigned->second;
      changed = true;
    }
  }
  if (changed)
    touch(store);
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
      data.form == Op::Form::hidden) {
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

bool Mod::unset(Val item, std::string_view key) {
  auto& store = impl_->store;
  if (!item.valid() || item.store_ != &store || key.empty()) {
    detail::add_diag(store.diags,
                     "unset requires a live value and non-empty key");
    return false;
  }
  const std::unordered_set<std::uint32_t> related = family(store, item.id_);
  if (!printable_value(store, related)) {
    detail::add_diag(store.diags,
                     "cannot annotate a value without a source binding");
    return false;
  }
  bool changed = false;
  for (const std::uint32_t id : related)
    changed = store.vals[id].data.meta.erase(std::string(key)) || changed;
  if (!changed)
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
