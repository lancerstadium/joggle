#include "value.h"

#include "detail.h"

#include <algorithm>

namespace joggle::detail {

ValFamilies::ValFamilies(const Store& store)
    : store_(store), parents_(store.vals.size()), ranks_(store.vals.size()) {
  for (std::uint32_t id = 0; id < parents_.size(); ++id)
    parents_[id] = id;
  for (const auto& slot : store.ops) {
    if (!slot.live || (slot.data.kind != Op::Kind::loop &&
                       slot.data.kind != Op::Kind::branch))
      continue;
    const OpData& op = slot.data;
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
        const BlkData& body = store.blks[blk].data;
        const std::size_t arg =
            op.kind == Op::Kind::loop ? offset + index : index;
        if (arg < body.args.size())
          join(seed, body.args[arg]);
        if (!body.ops.empty()) {
          const OpData& end = store.ops[body.ops.back()].data;
          if (end.kind == Op::Kind::yield && index < end.args.size())
            join(seed, end.args[index]);
        }
      }
    }
  }
}

std::uint32_t ValFamilies::root(std::uint32_t id) {
  while (parents_[id] != id) {
    parents_[id] = parents_[parents_[id]];
    id = parents_[id];
  }
  return id;
}

std::unordered_set<std::uint32_t>
ValFamilies::members(std::uint32_t seed) {
  std::unordered_set<std::uint32_t> out;
  const std::uint32_t group = root(seed);
  for (std::uint32_t id = 0; id < store_.vals.size(); ++id)
    if (store_.vals[id].live && root(id) == group)
      out.insert(id);
  return out;
}

void ValFamilies::join(std::uint32_t left, std::uint32_t right) {
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

std::unordered_set<std::uint32_t> family(const Store& store,
                                         std::uint32_t seed) {
  ValFamilies families(store);
  return families.members(seed);
}

bool printable_value(const Store& store,
                     const std::unordered_set<std::uint32_t>& values) {
  for (const std::uint32_t id : values) {
    if (id >= store.vals.size() || !store.vals[id].live)
      continue;
    const ValData& value = store.vals[id].data;
    if (value.kind == ValKind::generic || value.kind == ValKind::param)
      return true;
    if (value.kind != ValKind::result || value.name.empty() ||
        value.def == none || value.def >= store.ops.size() ||
        !store.ops[value.def].live)
      continue;
    const Op::Form form = store.ops[value.def].data.form;
    if (form == Op::Form::let || form == Op::Form::var)
      return true;
  }
  return false;
}

}  // namespace joggle::detail
