#include "joggle/joggle.h"
#include "../src/detail.h"
#include "../src/value.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,    \
                   #expression);                                               \
      return 1;                                                                \
    }                                                                          \
  } while (false)

namespace {

// Compare local family discovery with the independent whole-store union-find
// across nested control flow, shared yields, dead controls, and unused values.
bool local_families() {
  using namespace joggle::detail;
  for (int variant = 0; variant != 4; ++variant) {
    Store store;
    store.vals.resize(18);
    for (auto& slot : store.vals) {
      slot.live = true;
      slot.data.fn = 0;
    }
    store.fns.resize(1);
    store.fns[0].live = true;
    store.fns[0].data.blks = {0, 1, 2};
    store.blks.resize(3);
    for (auto& slot : store.blks) {
      slot.live = true;
      slot.data.fn = 0;
    }
    store.ops.resize(5);
    for (auto& slot : store.ops) slot.live = true;
    auto& loop = store.ops[0].data;
    loop.kind = joggle::Op::Kind::loop;
    loop.iter_names = {"i", "j"};
    loop.carried_count = 2;
    loop.args = {0, 1, 2, 3};
    loop.outs = {4, 5};
    loop.blks = {0};
    auto& branch = store.ops[1].data;
    branch.kind = joggle::Op::Kind::branch;
    branch.carried_count = 2;
    branch.args = {16, 8, 9};
    branch.outs = {14, 15};
    branch.blks = {1, 2};
    for (const auto op : {2, 3, 4})
      store.ops[op].data.kind = joggle::Op::Kind::yield;
    store.blks[0].data.parent_op = 0;
    store.blks[0].data.args = {6, 7, 8, 9};
    store.blks[0].data.ops = {1, 3};
    store.blks[1].data.parent_op = 1;
    store.blks[1].data.args = {10, 11};
    store.blks[1].data.ops = {2};
    store.blks[2].data.parent_op = 1;
    store.blks[2].data.args = {12, 13};
    store.blks[2].data.ops = {4};
    store.ops[2].data.blk = 1;
    store.ops[2].data.args = {10, 11};
    store.ops[3].data.blk = 0;
    store.ops[3].data.args = {14, 15};
    store.ops[4].data.blk = 2;
    store.ops[4].data.args = {12, 13};
    for (const auto id : {4, 5}) store.vals[id].data.def = 0;
    for (const auto id : {14, 15}) store.vals[id].data.def = 1;
    for (int id = 6; id <= 13; ++id)
      store.vals[id].data.kind = ValKind::blk_arg;
    if (variant == 1) store.ops[2].data.args = {11, 11};
    if (variant == 2) store.ops[4].data.args = {2, 13};
    if (variant == 3) store.ops[1].live = false;
    rebuild_uses(store);
    ValFamilies reference(store);
    for (std::uint32_t id = 0; id < store.vals.size(); ++id)
      if (family(store, id) != reference.members(id))
        return false;
  }
  return true;
}

class Random {
public:
  explicit Random(std::uint64_t state) : state_(state) {}

  std::uint64_t next() {
    state_ ^= state_ >> 12;
    state_ ^= state_ << 25;
    state_ ^= state_ >> 27;
    return state_ * 0x2545f4914f6cdd1dULL;
  }

  std::size_t index(std::size_t bound) {
    return bound == 0 ? 0 : static_cast<std::size_t>(next() % bound);
  }

private:
  std::uint64_t state_;
};

bool contains(std::span<const joggle::Val> values, joggle::Val value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

bool contains(std::span<const joggle::Op> ops, joggle::Op op) {
  return std::find(ops.begin(), ops.end(), op) != ops.end();
}

bool audit(const joggle::Mod& mod) {
  for (joggle::Fn fn : mod.fns()) {
    for (joggle::Blk blk : fn.blks()) {
      for (joggle::Op op : blk.ops()) {
        if (op.blk() != blk)
          return false;
        for (joggle::Val arg : op.args())
          if (!contains(arg.users(), op))
            return false;
        for (joggle::Val out : op.outs())
          if (out.def() != op)
            return false;
      }
    }
    for (joggle::Val value : fn.vals()) {
      if (joggle::Op def = value.def(); def && !contains(def.outs(), value))
        return false;
      for (joggle::Op user : value.users())
        if (!contains(user.args(), value))
          return false;
      std::size_t expected_uses = 0;
      for (joggle::Op op : fn.ops()) {
        const auto args = op.args();
        expected_uses += std::count(args.begin(), args.end(), value);
      }
      if (value.users().size() != expected_uses)
        return false;
    }
  }
  return true;
}

bool roundtrip(joggle::Env& env, const joggle::Mod& mod) {
  const std::string source = joggle::print(mod);
  joggle::Mod parsed;
  return joggle::parse(env, source, parsed, "edit-roundtrip.jog") &&
         parsed.verify(env) && joggle::structurally_equal(mod, parsed) &&
         joggle::print(parsed) == source;
}

std::vector<joggle::Op> editable_ops(const joggle::Mod& mod) {
  std::vector<joggle::Op> out;
  for (joggle::Op op : mod.ops())
    if (op.kind() == joggle::Op::Kind::call ||
        op.kind() == joggle::Op::Kind::constant)
      out.push_back(op);
  return out;
}

bool edit(joggle::Env& env, joggle::Mod& mod, Random& random,
          std::size_t sequence) {
  const std::string before = joggle::print(mod);
  const std::uint64_t revision = mod.revision();
  bool attempted = true;
  bool changed = false;
  joggle::Op erased;
  std::vector<joggle::Op> ops = editable_ops(mod);

  switch (random.index(8)) {
  case 0: {
    if (ops.empty()) {
      attempted = false;
      break;
    }
    changed = static_cast<bool>(mod.constant(
        ops[random.index(ops.size())],
        joggle::Attr(static_cast<std::int64_t>(sequence)),
        joggle::Ty("i32")));
    break;
  }
  case 1: {
    if (ops.empty()) {
      attempted = false;
      break;
    }
    const joggle::Op source = ops[random.index(ops.size())];
    const std::vector<joggle::Op> body = source.blk().ops();
    if (body.empty()) {
      attempted = false;
      break;
    }
    // Compare indexed clone naming against an independent live-value scan
    // after arbitrary preceding edits, including rename and erase.
    std::string expected_name;
    const auto outputs = source.outs();
    if ((source.form() == joggle::Op::Form::let ||
         source.form() == joggle::Op::Form::var) &&
        outputs.size() == 1 && !outputs[0].name().empty()) {
      const std::string stem(outputs[0].name());
      expected_name = stem;
      const auto values = mod.vals();
      for (std::size_t suffix = 1;
           std::any_of(values.begin(), values.end(), [&](joggle::Val value) {
             return value.name() == expected_name;
           }); ++suffix)
        expected_name = stem + '_' + std::to_string(suffix);
    }
    const auto copy = mod.clone(source, body[random.index(body.size())]);
    changed = static_cast<bool>(copy);
    if (copy && !expected_name.empty() && copy.outs()[0].name() != expected_name)
      return false;
    break;
  }
  case 2: {
    if (ops.size() < 2) {
      attempted = false;
      break;
    }
    const joggle::Op source = ops[random.index(ops.size())];
    std::vector<joggle::Op> destinations;
    for (joggle::Op candidate : source.blk().ops())
      if (candidate != source)
        destinations.push_back(candidate);
    if (destinations.empty()) {
      attempted = false;
      break;
    }
    changed = mod.move(source, destinations[random.index(destinations.size())]);
    break;
  }
  case 3: {
    std::vector<joggle::Op> users;
    for (joggle::Op op : mod.ops())
      if (!op.args().empty())
        users.push_back(op);
    if (users.empty()) {
      attempted = false;
      break;
    }
    const joggle::Op user = users[random.index(users.size())];
    const std::vector<joggle::Val> args = user.args();
    const joggle::Val old_value = args[random.index(args.size())];
    std::vector<joggle::Val> replacements;
    for (joggle::Val value : user.blk().fn().vals())
      if (value != old_value && value.type() == old_value.type())
        replacements.push_back(value);
    if (replacements.empty()) {
      attempted = false;
      break;
    }
    changed = mod.replace(
        old_value, replacements[random.index(replacements.size())], user);
    break;
  }
  case 4: {
    std::vector<joggle::Op> unused;
    for (joggle::Op op : ops) {
      bool used = false;
      for (joggle::Val out : op.outs())
        used = used || !out.users().empty();
      if (!used)
        unused.push_back(op);
    }
    if (unused.empty()) {
      attempted = false;
      break;
    }
    erased = unused[random.index(unused.size())];
    changed = mod.erase(erased);
    break;
  }
  case 5: {
    std::vector<joggle::Op> constants;
    for (joggle::Op op : ops)
      if (op.kind() == joggle::Op::Kind::constant &&
          !op.outs().empty() && op.outs().front().type() == joggle::Ty("i32"))
        constants.push_back(op);
    if (constants.empty()) {
      attempted = false;
      break;
    }
    changed = mod.replace(constants[random.index(constants.size())],
                          joggle::Attr(static_cast<std::int64_t>(sequence + 1)));
    break;
  }
  case 6: {
    std::vector<joggle::Val> values;
    for (joggle::Op op : ops) {
      const std::vector<joggle::Val> outputs = op.outs();
      values.insert(values.end(), outputs.begin(), outputs.end());
    }
    if (values.empty()) {
      attempted = false;
      break;
    }
    changed = mod.rename(values[random.index(values.size())],
                         "value_" + std::to_string(sequence));
    break;
  }
  default: {
    if (ops.empty()) {
      attempted = false;
      break;
    }
    const joggle::Op op = ops[random.index(ops.size())];
    if (op.meta("probe"))
      changed = mod.unset(op, "probe");
    else
      changed = mod.set(op, "probe",
                        joggle::Attr(static_cast<std::int64_t>(sequence)));
    break;
  }
  }

  if (!attempted)
    return true;
  if (!changed) {
    const bool stable = mod.revision() == revision && joggle::print(mod) == before;
    mod.clear_diags();
    return stable;
  }
  if (mod.revision() <= revision || (erased && erased.valid()) ||
      !audit(mod) || !mod.verify(env) || !roundtrip(env, mod)) {
    mod.print_diags(stderr);
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  CHECK(local_families());
  CHECK(argc == 2);
  joggle::Env env;
  env.path(argv[1]);
  CHECK(env.load("base"));

  // Rejected typed calls must roll back appended arenas, use edges, and
  // revisions without disturbing pre-existing handles or subsequent calls.
  joggle::Mod calls;
  CHECK(joggle::parse(env,
      "mod call_rollback\nuse base\n"
      "fn pair(x: i32, y: i32) -> i32 { return x + y }\n"
      "fn main(x: i32) -> i32 { return x }\n", calls, "calls.jog"));
  CHECK(calls.verify(env));
  const auto entry = calls.find_fn("main");
  const auto target = calls.find_fn("pair");
  const auto terminator = entry.body().ops().back();
  const auto input = entry.params().front();
  const std::vector<joggle::Val> repeated{input, input};
  for (int attempt = 0; attempt < 12; ++attempt) {
    const auto text = joggle::print(calls);
    const auto revision = calls.revision();
    const auto fn_revision = entry.revision();
    const auto target_revision = target.revision();
    const auto imports = calls.uses();
    const auto users = input.users();
    const auto ops = calls.ops();
    const auto vals = calls.vals();
    if (attempt % 3 == 0) {
      CHECK(!calls.call(env, terminator, target, repeated, joggle::Ty("f32")));
    } else if (attempt % 3 == 1) {
      const std::vector<joggle::Val> incomplete{input};
      CHECK(!calls.call(env, terminator, target, incomplete, joggle::Ty("i32")));
    } else {
      const std::vector<joggle::Ty> outputs{joggle::Ty("i32"), joggle::Ty("i32")};
      CHECK(!calls.call(env, terminator, target, repeated, outputs));
    }
    CHECK(!calls.diags().empty());
    CHECK(joggle::print(calls) == text && calls.revision() == revision);
    CHECK(entry.revision() == fn_revision && input.users() == users);
    CHECK(target.revision() == target_revision && calls.uses() == imports);
    CHECK(calls.ops() == ops && calls.vals() == vals);
    calls.clear_diags();
    CHECK(calls.verify(env) && audit(calls));
    const auto result = calls.call(env, terminator, target, repeated, joggle::Ty("i32"));
    CHECK(result && env.resolve(calls, result.def()) == target);
    CHECK(calls.verify(env) && audit(calls));
  }

  constexpr std::string_view source =
      "mod edit_robustness\n"
      "use base\n"
      "fn main(x: i32) -> i32 {\n"
      "  let a: i32 = x + 1\n"
      "  let b: i32 = a * 2\n"
      "  let c: i32 = b + 3\n"
      "  return c\n"
      "}\n";
  Random random(0x45646974526f6275ULL);
  std::size_t sequence = 0;
  for (std::size_t round = 0; round < 20; ++round) {
    joggle::Mod mod;
    CHECK(joggle::parse(env, source, mod, "edit-seed.jog"));
    CHECK(mod.verify(env) && audit(mod));
    for (std::size_t step = 0; step < 100; ++step)
      CHECK(edit(env, mod, random, sequence++));
  }
  return 0;
}
