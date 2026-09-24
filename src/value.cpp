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
  // Follow only control-flow ties incident to this value. Building the global
  // union-find for each rename/metadata edit turns local expansion quadratic.
  std::unordered_set<std::uint32_t> out;
  std::vector<std::uint32_t> pending;
  std::unordered_set<std::uint64_t> groups;
  const auto add = [&](std::uint32_t id) {
    if (id < store.vals.size() && store.vals[id].live && out.insert(id).second)
      pending.push_back(id);
  };
  const auto group = [&](std::uint32_t op_id, std::size_t index) {
    if (op_id >= store.ops.size() || !store.ops[op_id].live)
      return;
    const OpData& op = store.ops[op_id].data;
    if (op.kind != Op::Kind::loop && op.kind != Op::Kind::branch)
      return;
    const std::size_t offset = op.kind == Op::Kind::loop ? op.iter_names.size() : 1;
    if (index >= op.carried_count || op.args.size() < offset + op.carried_count ||
        op.outs.size() < op.carried_count ||
        !groups.insert((std::uint64_t(op_id) << 32) | index).second)
      return;
    add(op.args[offset + index]);
    add(op.outs[index]);
    for (const auto blk : op.blks) {
      if (blk >= store.blks.size() || !store.blks[blk].live)
        continue;
      const BlkData& body = store.blks[blk].data;
      const auto arg = op.kind == Op::Kind::loop ? offset + index : index;
      if (arg < body.args.size())
        add(body.args[arg]);
      if (!body.ops.empty()) {
        const OpData& end = store.ops[body.ops.back()].data;
        if (end.kind == Op::Kind::yield && index < end.args.size())
          add(end.args[index]);
      }
    }
  };
  add(seed);
  for (std::size_t next = 0; next < pending.size(); ++next) {
    const auto id = pending[next];
    const ValData& value = store.vals[id].data;
    if (value.def < store.ops.size() && store.ops[value.def].live) {
      const auto& op = store.ops[value.def].data;
      for (std::size_t i = 0; i < op.outs.size(); ++i)
        if (op.outs[i] == id)
          group(value.def, i);
    }
    if (value.kind == ValKind::blk_arg && value.blk < store.blks.size() &&
        store.blks[value.blk].live) {
      // Block ownership survives renames and is remapped when cloning. Only
      // inspect this argument's block, never all blocks in its function.
      const auto& body = store.blks[value.blk].data;
      if (body.parent_op < store.ops.size() && store.ops[body.parent_op].live) {
        const auto& op = store.ops[body.parent_op].data;
        const auto offset = op.kind == Op::Kind::loop ? op.iter_names.size() : 0;
        for (std::size_t i = offset; i < body.args.size(); ++i)
          if (body.args[i] == id)
            group(body.parent_op, i - offset);
      }
    }
    for (const auto user : value.users) {
      if (user >= store.ops.size() || !store.ops[user].live)
        continue;
      const auto& op = store.ops[user].data;
      if (op.kind == Op::Kind::loop || op.kind == Op::Kind::branch) {
        const auto offset = op.kind == Op::Kind::loop ? op.iter_names.size() : 1;
        for (std::size_t i = offset; i < op.args.size(); ++i)
          if (op.args[i] == id)
            group(user, i - offset);
      } else if (op.kind == Op::Kind::yield && op.blk < store.blks.size() &&
                 store.blks[op.blk].live) {
        const auto& body = store.blks[op.blk].data;
        if (!body.ops.empty() && body.ops.back() == user)
          for (std::size_t i = 0; i < op.args.size(); ++i)
            if (op.args[i] == id)
              group(body.parent_op, i);
      }
    }
  }
  return out;
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
