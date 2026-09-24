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
    for (std::uint32_t blk = 0; blk < store.blks.size(); ++blk)
      for (const auto id : store.blks[blk].data.args) {
        store.vals[id].data.kind = ValKind::blk_arg;
        store.vals[id].data.blk = blk;
      }
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

bool long_carried_family() {
  using namespace joggle::detail;
  constexpr std::uint32_t count = 96;
  Store store;
  store.vals.resize(3 * count + 2);
  for (auto& value : store.vals) {
    value.live = true;
    value.data.fn = 0;
  }
  store.fns.resize(1);
  store.fns[0].live = true;
  store.blks.resize(count);
  store.ops.resize(2 * count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const auto iterator = 3 * i + 2, argument = iterator + 1, result = iterator + 2;
    auto& loop = store.ops[2 * i];
    loop.live = true;
    loop.data.kind = joggle::Op::Kind::loop;
    loop.data.iter_names = {"i"};
    loop.data.carried_count = 1;
    loop.data.args = {1, i ? result - 3 : 0};
    loop.data.outs = {result};
    loop.data.blks = {i};
    auto& body = store.blks[i];
    body.live = true;
    body.data.fn = 0;
    body.data.parent_op = 2 * i;
    body.data.args = {iterator, argument};
    body.data.ops = {2 * i + 1};
    store.fns[0].data.blks.push_back(i);
    auto& yield = store.ops[2 * i + 1];
    yield.live = true;
    yield.data.kind = joggle::Op::Kind::yield;
    yield.data.blk = i;
    yield.data.args = {argument};
    store.vals[iterator].data.kind = ValKind::blk_arg;
    store.vals[argument].data.kind = ValKind::blk_arg;
    store.vals[iterator].data.blk = i;
    store.vals[argument].data.blk = i;
    store.vals[result].data.def = 2 * i;
  }
  rebuild_uses(store);
  ValFamilies reference(store);
  for (std::uint32_t id = 0; id < store.vals.size(); ++id)
    if (family(store, id) != reference.members(id)) return false;
  return family(store, 0).size() == 2 * count + 1;
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
  CHECK(joggle::detail::sha256("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(joggle::detail::sha256("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(joggle::detail::sha256(
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(joggle::detail::sha256(std::string(1000000, 'a')) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  CHECK(joggle::detail::fingerprint(joggle::Attr(std::int64_t{1})) !=
        joggle::detail::fingerprint(joggle::Attr("1")));
  // Independently generated with hashlib over the versioned tagged encoding.
  CHECK(joggle::detail::fingerprint(joggle::Attr("abc")) ==
        "sha256:a1c608d48467928f0d79aaf81d743b9b24196edc87959d8755e316595d1b9f24");
  CHECK(joggle::detail::fingerprint(joggle::Attr(0.0)) !=
        joggle::detail::fingerprint(joggle::Attr(-0.0)));
  CHECK(local_families());
  CHECK(long_carried_family());
  CHECK(argc == 2);
  joggle::Env env;
  env.path(argv[1]);
  CHECK(env.load("base"));

  // Audit immediately after construction: verify() rebuilds uses and would
  // otherwise hide missing or duplicated incremental edges.
  joggle::Mod control;
  CHECK(joggle::parse(env,
      "mod control_edges\nuse base\n"
      "fn main(flag: bool) -> i32 {\n"
      "  var state = i32(0)\n  for k in 0..4 {}\n  return state\n}\n",
      control, "control-edges.jog"));
  const auto control_fn = control.find_fn("main");
  joggle::Val span, state;
  for (auto op : control_fn.ops())
    if (op.kind() == joggle::Op::Kind::loop) span = op.args().front();
  for (auto value : control_fn.vals()) {
    if (value.name() == "state") state = value;
  }
  CHECK(span && state && audit(control));
  const std::vector<std::string> iterators{"i", "j"};
  const std::vector<joggle::Val> ranges{span, span};
  const std::vector<joggle::Val> carried{state};
  const auto loop = control.loop(control_fn.body().ops().back(),
                                 iterators, ranges, carried);
  CHECK(loop && audit(control));
  const auto range_users = span.users();
  CHECK(std::count(range_users.begin(), range_users.end(), loop) == 2);
  const auto body = loop.blks().front();
  const std::vector<joggle::Val> nested_carried{body.args().back()};
  const auto branch = control.branch(body.ops().back(),
                                     control_fn.params().front(), nested_carried);
  CHECK(branch && audit(control));
  CHECK(branch.blks().size() == 2);
  for (auto arm : branch.blks()) {
    const auto argument = arm.args().front();
    const auto users = argument.users();
    CHECK(users.size() == 1 && users.front() == arm.ops().back());
  }
  CHECK(control.verify(env) && audit(control));

  // Erasure updates only surviving parent orders and each owner's block list.
  // Overlapping roots and duplicate roots must still remove each subtree once.
  for (int variant = 0; variant < 3; ++variant) {
    joggle::Mod dead;
    CHECK(joggle::parse(env,
        "mod dead_controls\nuse base\n"
        "fn main(x: i32) -> i32 {\n"
        "  for i in 0..4 { for j in 0..3 { let unused = x + x } }\n"
        "  for k in 0..2 { let unused = x + x }\n  return x\n}\n"
        "fn other(x: i32) -> i32 { for k in 0..2 {} return x }\n",
        dead, "dead-controls.jog"));
    const auto fn = dead.find_fn("main");
    const auto other = dead.find_fn("other");
    const auto other_blocks = other.blks();
    const auto other_ops = other.ops();
    std::vector<joggle::Op> outer;
    for (const auto op : fn.body().ops())
      if (op.kind() == joggle::Op::Kind::loop) outer.push_back(op);
    CHECK(outer.size() == 2);
    joggle::Op inner;
    for (const auto op : outer[0].blks()[0].ops())
      if (op.kind() == joggle::Op::Kind::loop) inner = op;
    CHECK(inner && audit(dead));
    const auto removed_blocks = outer[0].blks();
    if (variant == 0) {
      CHECK(dead.erase(outer[0]));
      CHECK(outer[1] && fn.blks().size() == 2);
    } else {
      const std::vector<joggle::Op> roots = variant == 1
          ? std::vector<joggle::Op>{outer[0], inner, outer[1], outer[0]}
          : std::vector<joggle::Op>{inner, outer[1], outer[0]};
      CHECK(dead.erase(roots));
      CHECK(!outer[1] && fn.blks().size() == 1);
    }
    CHECK(!outer[0] && !inner && !removed_blocks[0]);
    CHECK(other.blks() == other_blocks && other.ops() == other_ops);
    CHECK(audit(dead));
    CHECK(dead.verify(env));
  }

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
