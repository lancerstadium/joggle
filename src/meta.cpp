#include "detail.h"
#include "value.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace joggle {

bool Mod::set(Fn fn, std::string key, Attr value) {
  auto& store = impl_->store;
  if (!fn.valid() || fn.store_ != &store ||
      !detail::valid_qualified_name(key)) {
    detail::add_diag(store.diags,
                     "set requires a live function and valid metadata key");
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
  if (!item.valid() || item.store_ != &store ||
      !detail::valid_qualified_name(key)) {
    detail::add_diag(store.diags,
                     "set requires a live value and valid metadata key");
    return false;
  }
  const std::unordered_set<std::uint32_t> related =
      detail::family(store, item.id_);
  if (!detail::printable_value(store, related)) {
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
  if (items.empty() || !detail::valid_qualified_name(key))
    return reject("set requires values and a valid metadata key");
  for (Val item : items)
    if (!item.valid() || item.store_ != &store)
      return reject("set requires live values in this module");

  detail::ValFamilies families(store);

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
  if (!op.valid() || op.store_ != &store ||
      !detail::valid_qualified_name(key)) {
    detail::add_diag(store.diags,
                     "set requires a live operation and valid metadata key");
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
  if (!fn.valid() || fn.store_ != &store ||
      !detail::valid_qualified_name(key)) {
    detail::add_diag(store.diags,
                     "unset requires a live function and valid metadata key");
    return false;
  }
  if (!store.fns[fn.id_].data.meta.erase(std::string(key)))
    return false;
  touch(store);
  return true;
}

bool Mod::unset(Val item, std::string_view key) {
  auto& store = impl_->store;
  if (!item.valid() || item.store_ != &store ||
      !detail::valid_qualified_name(key)) {
    detail::add_diag(store.diags,
                     "unset requires a live value and valid metadata key");
    return false;
  }
  const std::unordered_set<std::uint32_t> related =
      detail::family(store, item.id_);
  if (!detail::printable_value(store, related)) {
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
  if (!op.valid() || op.store_ != &store ||
      !detail::valid_qualified_name(key)) {
    detail::add_diag(store.diags,
                     "unset requires a live operation and valid metadata key");
    return false;
  }
  if (!store.ops[op.id_].data.meta.erase(std::string(key)))
    return false;
  touch(store);
  return true;
}


}  // namespace joggle
